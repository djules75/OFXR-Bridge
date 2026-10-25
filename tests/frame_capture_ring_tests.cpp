#include "xrfg/frame_capture.hpp"
#include "xrfg/diagnostic_ring_sink.hpp"
#include <wrl/client.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"GPU device");
    const auto root=std::filesystem::current_path()/("ring-test-"+std::to_string(GetCurrentProcessId()));
    xrfg::FrameCapture capture(device.Get(),context.Get(),root);
    check(!capture.ring_active() && !xrfg::diagnostic_ring_enabled,"capture defaults off");
    check(!capture.save_ring() && !std::filesystem::exists(root),"disabled save created files");
    check(capture.start_ring(),"arm ring");check(!capture.start_ring(),"overlapping ring accepted");
    D3D11_TEXTURE2D_DESC d{};d.Width=96;d.Height=80;d.MipLevels=1;d.ArraySize=2;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> image;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&image)),"texture");
    std::vector<unsigned> pixels(d.Width*d.Height);
    for(unsigned frame=0;frame<32;++frame) {
        for(unsigned eye=0;eye<2;++eye) {
            std::fill(pixels.begin(),pixels.end(),eye?0xff407020u:0xff106040u);
            context->UpdateSubresource(image.Get(),eye,nullptr,pixels.data(),d.Width*4,0);
        }
        check(capture.source(123,image.Get(),frame,100+frame,200+frame)!=0,"ring source rejected");
        xrfg::diagnostic_ring_record("{\"kind\":\"test_pose\",\"qpc\":"+std::to_string(xrfg::FrameCapture::qpc())+",\"serial\":"+std::to_string(100+frame)+"}");
        std::fill(pixels.begin(),pixels.end(),0xffffffffu);
        for(unsigned eye=0;eye<2;++eye)context->UpdateSubresource(image.Get(),eye,nullptr,pixels.data(),d.Width*4,0);
        capture.tick(true);std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    check(capture.save_ring(),"first snapshot");
    check(capture.ring_active(),"save disarmed ring");
    for(unsigned eye=0;eye<2;++eye) {
        std::fill(pixels.begin(),pixels.end(),eye?0xff407020u:0xff106040u);
        context->UpdateSubresource(image.Get(),eye,nullptr,pixels.data(),d.Width*4,0);
    }
    check(capture.source(123,image.Get(),99,999,999)!=0,"post-save source rejected");
    check(capture.save_ring(),"repeated snapshot");
    for(unsigned n=0;n<30;++n){capture.tick(true);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    capture.stop_ring();check(!capture.ring_active() && !xrfg::diagnostic_ring_enabled,"stop did not disarm");
    check(!capture.source(123,image.Get(),1,9999,9999),"disabled GPU capture");
    capture.tick(true);capture.shutdown();
    unsigned runs=0;
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        std::ifstream trace(entry.path()/"trace.jsonl");std::string line;std::int64_t start{},end{};
        bool closed=false,late=false,early=false;unsigned samples{},poses{};
        const auto number=[](const std::string& s,const char* name){std::smatch m;check(std::regex_search(s,m,std::regex(std::string("\"")+name+"\":([0-9]+)")),"numeric field missing");return std::stoll(m[1]);};
        while(std::getline(trace,line)) {
            if(line.find("\"kind\":\"begin\"")!=std::string::npos) {
                start=number(line,"window_start_qpc");end=number(line,"window_end_qpc");
                check(end-start==number(line,"qpc_frequency"),"window not exactly one second");
                check(number(line,"gpu_drops")==0 && number(line,"writer_drops")==0,"capture dropped samples");
            }
            if(line.find("\"kind\":\"end\"")!=std::string::npos)closed=true;
            if(line.find("\"kind\":\"test_pose\"")!=std::string::npos){++poses;const auto t=number(line,"qpc");check(t>=start&&t<=end,"pose outside previous-second window");}
            if(line.find("\"kind\":\"sample\"")==std::string::npos)continue;
            ++samples;const auto t=number(line,"submit_qpc");check(t>=start&&t<=end,"image outside previous-second window");
            const auto serial=number(line,"serial");late|=serial==999;early|=serial==100;
            check(number(line,"completed_at_read")>=number(line,"diagnostic_fence"),"mapped before fence completed");
            std::smatch m;check(std::regex_search(line,m,std::regex("\"thumbnail\":\"([^\"]+)\"")),"image missing");
            std::ifstream ppm(entry.path()/m[1].str(),std::ios::binary);std::string magic;unsigned w{},h{},max{};ppm>>magic>>w>>h>>max;ppm.get();char rgb[3]{};ppm.read(rgb,3);
            check(magic=="P6"&&w==12&&h==10&&max==255,"rolling thumbnail grid");
            check(static_cast<unsigned char>(rgb[0])==(number(line,"slice")==0?0x40:0x20),"source overwritten/eyes crossed");
        }
        check(closed && samples>=40 && poses>=20 && !early,"one-second snapshot incomplete");
        if(runs==0)check(!late,"future image leaked into first snapshot");else check(late,"second press lost new image");
        ++runs;
    }
    check(runs==2,"repeated save not independent");
    std::cout<<"Rolling GPU capture: prior second, retained private copy, eye separation, async fence/map, two independent snapshots, keeps recording, default/off gating passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
