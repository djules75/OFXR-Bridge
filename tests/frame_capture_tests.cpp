#include "xrfg/frame_capture.hpp"
#include <wrl/client.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <unordered_map>
#include <vector>
using Microsoft::WRL::ComPtr;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"device");
    auto root=std::filesystem::current_path()/ ("capture-test-"+std::to_string(GetCurrentProcessId()));
    xrfg::FrameCapture capture(device.Get(),context.Get(),root);
    check(!capture.start(false),"logging disabled accepted capture");
    check(!std::filesystem::exists(root),"disabled diagnostic created files");
    check(capture.start(true,4),"shader/capture init; inspect trace.jsonl for HRESULT");
    check(!capture.start(true,4),"overlapping run accepted");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=96;desc.Height=80;desc.MipLevels=1;desc.ArraySize=2;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> image;
    check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&image)),"source");
    std::vector<unsigned> pixels(96*80);
    std::vector<std::uint64_t> ids;
    for(unsigned frame=0;frame<4;++frame){
        for(unsigned eye=0;eye<2;++eye){
            std::fill(pixels.begin(),pixels.end(),eye?0xff407020u:0xff106040u);
            if(frame==2 && eye==0)pixels[9*96+7]^=0x00000001u; // NOT a thumbnail sample
            context->UpdateSubresource(image.Get(),eye,nullptr,pixels.data(),96*4,0);
        }
        auto id=capture.source(123,image.Get(),0,frame+100,frame+200);check(id!=0,"sample rejected");ids.push_back(id);
        ComPtr<ID3D11ComputeShader> bound;context->CSGetShader(&bound,nullptr,nullptr);check(!bound,"CS state leaked");
        std::fill(pixels.begin(),pixels.end(),0xffffffffu);
        for(unsigned eye=0;eye<2;++eye)context->UpdateSubresource(image.Get(),eye,nullptr,pixels.data(),96*4,0);
        for(int i=0;i<10;++i){capture.tick(true);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    capture.tick(false);check(!capture.active(),"disabled capture continues");
    check(capture.source(123,image.Get(),0,500,600)==0,"disabled source copied");capture.shutdown();
    std::unordered_map<std::string,std::string> hashes;
    unsigned samples=0;bool closed=false;
    for(const auto& d:std::filesystem::directory_iterator(root)){
        std::ifstream trace(d.path()/"trace.jsonl");std::string line;
        while(std::getline(trace,line)){
            if(line.find("\"kind\":\"end\"")!=std::string::npos){closed=true;check(line.find("\"gpu_drops\":0")!=std::string::npos,"GPU drops");}
            if(line.find("\"kind\":\"sample\"")==std::string::npos)continue;
            auto field=[&](const char* key){std::smatch m;check(std::regex_search(line,m,std::regex(std::string("\"")+key+"\":\"?([A-Za-z0-9.-]+)")),"missing field");return m[1].str();};
            hashes[field("capture")+":"+field("slice")]=field("hash");++samples;
            check(std::stoull(field("completed_at_read"))>=std::stoull(field("diagnostic_fence")),"GPU read before completed fence");
            std::ifstream ppm(d.path()/field("thumbnail"),std::ios::binary);std::string magic;unsigned w,h,max;
            ppm>>magic>>w>>h>>max;ppm.get();char rgb[3]{};ppm.read(rgb,3);
            check(magic=="P6"&&w==6&&h==5&&max==255,"thumbnail size/header");
            check(static_cast<unsigned char>(rgb[0])==(field("slice")=="0"?0x40:0x20),"source poisoned or eyes crossed");
        }
    }
    check(samples==8&&closed,"missing readback or end record");
    auto hash=[&](unsigned f,unsigned eye){return hashes.at(std::to_string(ids[f])+":"+std::to_string(eye));};
    check(hash(0,0)==hash(1,0)&&hash(0,0)==hash(3,0),"identical inputs disagree");
    check(hash(0,0)!=hash(2,0),"full-image fingerprint missed off-thumbnail pixel");
    check(hash(0,1)==hash(2,1)&&hash(0,0)!=hash(0,1),"eye identity lost");
    std::cout<<"GPU fingerprints: exact repeat, changed unsampled pixel, eye isolation, private pre-release copy, fence-ready readback, logging gate and CS restoration passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
