#include "xrfg/frame_capture.hpp"
#include "xrfg/diagnostic_ring_sink.hpp"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace xrfg {
using Microsoft::WRL::ComPtr;
namespace {
// Every mip-zero pixel contributes, including alpha. Hash decoded float bits,
// not a thumbnail. Two spatially salted reductions per 16x16 tile; CPU folds
// the ordered tile hashes into a 128-bit fingerprint. Probabilistic equality,
// not a proof of engine-frame identity. Thumbnail is separate and approximate.
constexpr char shader_source[] = R"(
#ifndef CAPTURE_TILE
#define CAPTURE_TILE 16
#endif
Texture2DArray<float4> src : register(t0);
RWStructuredBuffer<uint4> result : register(u0);
groupshared uint2 hashes[CAPTURE_TILE*CAPTURE_TILE];
groupshared uint sample;
uint mix(uint x) { x ^= x>>16; x *= 0x7feb352d; x ^= x>>15; x *= 0x846ca68b; return x^(x>>16); }
[numthreads(CAPTURE_TILE,CAPTURE_TILE,1)]
void main(uint3 p:SV_DispatchThreadID,uint3 g:SV_GroupID,uint i:SV_GroupIndex) {
 uint w,h,n; src.GetDimensions(w,h,n);
 uint2 v=0;
 if(p.x<w && p.y<h) {
  float4 color=src.Load(int4(p,0)); uint4 b=asuint(color);
  uint position=p.x+p.y*w+p.z*w*h;
  uint a=mix(b.x ^ mix(b.y+0x9e3779b9) ^ mix(b.z+0x85ebca6b) ^ mix(b.w+position));
  v=uint2(a,mix(a+position+0xc2b2ae35));
  if(i==0) { uint3 c=(uint3)round(saturate(color.rgb)*255); sample=c.x|(c.y<<8)|(c.z<<16); }
 }
 hashes[i]=v; GroupMemoryBarrierWithGroupSync();
 for(uint stride=CAPTURE_TILE*CAPTURE_TILE/2;stride;stride>>=1) {
  if(i<stride) { hashes[i].x+=hashes[i+stride].x; hashes[i].y^=hashes[i+stride].y; }
  GroupMemoryBarrierWithGroupSync();
 }
 if(i==0) { uint nx=(w+CAPTURE_TILE-1)/CAPTURE_TILE, ny=(h+CAPTURE_TILE-1)/CAPTURE_TILE; result[g.x+g.y*nx+g.z*nx*ny]=uint4(hashes[0],sample,0); }
})";
DXGI_FORMAT srv_format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R32G32B32A32_FLOAT: return f;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
std::uint64_t texture_bytes(const D3D11_TEXTURE2D_DESC& d) {
    const auto f=srv_format(d.Format);
    const unsigned bpp=f==DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 :
        (f==DXGI_FORMAT_R16G16B16A16_FLOAT || f==DXGI_FORMAT_R16G16B16A16_UNORM ? 8 : 4);
    return static_cast<std::uint64_t>(d.Width)*d.Height*d.ArraySize*bpp;
}
}

struct FrameCapture::Impl {
    struct Slot {
        ComPtr<ID3D11Buffer> readback;
        ComPtr<ID3D11CommandList> commands;
        std::uint64_t fence{}, id{}, serial{}, history_fence{};
        std::uint32_t index{};
        std::int64_t submitted{};
    };
    struct Chain {
        D3D11_TEXTURE2D_DESC desc{};
        ComPtr<ID3D11Texture2D> snapshot;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11Buffer> output;
        ComPtr<ID3D11UnorderedAccessView> uav;
        std::array<Slot,4> slots;
        unsigned nx{},ny{},words{};
    };
    struct Packet {
        enum Kind { begin, line, pixels, end, ring_begin, snapshot, ring_end } kind{line};
        std::filesystem::path path;
        std::string text;
        std::vector<std::uint32_t> data;
        std::vector<unsigned char> rgb;
        std::array<std::string,2> hashes;
        std::uint64_t id{},chain{},serial{},fence{},completed{},history_fence{};
        std::uint32_t index{},nx{},ny{},slices{};
        std::int64_t submitted{},ready{};
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext4> context4;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Fence> fence;
    std::filesystem::path root;
    std::unordered_map<std::uint64_t,Chain> chains;
    std::atomic<bool> enabled{false};
    std::atomic<bool> run_open{false};
    std::atomic<bool> rolling{false};
    unsigned tile{16};
    std::int64_t frequency{};
    std::int64_t ring_armed_qpc{};
    struct SaveRequest {std::filesystem::path path;std::int64_t cutoff{};std::uint64_t fence{};};
    std::deque<SaveRequest> saves;
    std::uint64_t save_drops{};
    bool stopped{};
    std::unordered_set<std::uint64_t> refused;
    unsigned run{};
    std::uint64_t next_fence{}, next_id{}, gpu_drops{};
    std::atomic<std::uint64_t> writer_drops{};
    std::chrono::steady_clock::time_point deadline;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Packet> packets;
    std::size_t queued_bytes{};
    bool stop_writer{};
    std::thread writer;

    void enqueue(Packet p, bool mandatory=false) {
        std::scoped_lock lock(mutex);
        const auto bytes=p.data.size()*4+p.text.size();
        if(!mandatory && (queued_bytes+bytes>(rolling?128ULL:32ULL)*1024*1024 || packets.size()>4096)) { ++writer_drops; return; }
        queued_bytes+=bytes; packets.push_back(std::move(p)); wake.notify_one();
    }
    void event(std::string text) { Packet p; p.text=std::move(text);p.submitted=FrameCapture::qpc();enqueue(std::move(p)); }
    void active_event(std::string text) {
        // Order the active check with packet insertion: a director event
        // racing the end marker must not land after that file was closed.
        std::scoped_lock lock(mutex);
        if(!enabled)return;
        if(queued_bytes+text.size()>(rolling?128ULL:32ULL)*1024*1024 || packets.size()>4096){++writer_drops;return;}
        Packet p;p.text=std::move(text);p.submitted=FrameCapture::qpc();queued_bytes+=p.text.size();packets.push_back(std::move(p));wake.notify_one();
    }
    void write_main() noexcept {
        try {
            std::ofstream log;
            std::filesystem::path directory;
            std::uint64_t thumbnail_bytes=0;
            bool rolling_writer=false;
            std::deque<Packet> ring;
            std::size_t ring_bytes{};
            std::uint64_t capacity_evictions{};
            const auto size=[](const Packet& p){return p.rgb.size()+p.data.size()*4+p.text.size()+sizeof(Packet);};
            const auto compact=[](Packet& p) {
                if(p.kind!=Packet::pixels)return;
                const auto count=p.nx*p.ny;
                p.rgb.resize(static_cast<std::size_t>(count)*p.slices*3);
                for(unsigned slice=0;slice<p.slices;++slice) {
                    std::uint64_t h1=1469598103934665603ULL,h2=0x9e3779b97f4a7c15ULL;
                    const auto* data=p.data.data()+slice*count*4;
                    for(unsigned t=0;t<count;++t) {
                        for(unsigned k=0;k<2;++k) {
                            h1=(h1^data[t*4+k])*1099511628211ULL;
                            h2^=static_cast<std::uint64_t>(data[t*4+k])+0x9e3779b97f4a7c15ULL+(h2<<6)+(h2>>2);
                        }
                        const auto color=data[t*4+2];auto* rgb=p.rgb.data()+(slice*count+t)*3;
                        rgb[0]=static_cast<unsigned char>(color);rgb[1]=static_cast<unsigned char>(color>>8);rgb[2]=static_cast<unsigned char>(color>>16);
                    }
                    std::ostringstream hash;hash<<std::hex<<std::setfill('0')<<std::setw(16)<<h1<<std::setw(16)<<h2;
                    p.hashes[slice]=hash.str();
                }
                std::vector<std::uint32_t>().swap(p.data);
            };
            const auto emit=[&](const Packet& p) {
                if(p.kind==Packet::line){log<<p.text<<'\n';return;}
                for(unsigned slice=0;slice<p.slices;++slice) {
                    std::uint64_t h1=1469598103934665603ULL,h2=0x9e3779b97f4a7c15ULL;
                    const auto count=p.nx*p.ny;
                    const auto* data=p.data.empty()?nullptr:p.data.data()+slice*count*4;
                    if(data)for(unsigned t=0;t<count;++t)for(unsigned k=0;k<2;++k) {
                        h1=(h1^data[t*4+k])*1099511628211ULL;
                        h2^=static_cast<std::uint64_t>(data[t*4+k])+0x9e3779b97f4a7c15ULL+(h2<<6)+(h2>>2);
                    }
                    std::ostringstream hash;hash<<std::hex<<std::setfill('0')<<std::setw(16)<<h1<<std::setw(16)<<h2;
                    std::string file,tiles;
                    if(thumbnail_bytes+count*11<256*1024*1024) {
                        file="sample-"+std::to_string(p.id)+"-slice-"+std::to_string(slice)+".ppm";
                        std::ofstream image(directory/file,std::ios::binary);
                        image<<"P6\n"<<p.nx<<' '<<p.ny<<"\n255\n";
                        if(data) {
                            tiles="sample-"+std::to_string(p.id)+"-slice-"+std::to_string(slice)+".tiles";
                            std::ofstream tile_file(directory/tiles,std::ios::binary);
                            for(unsigned t=0;t<count;++t) {const auto c=data[t*4+2];char rgb[3]={char(c),char(c>>8),char(c>>16)};image.write(rgb,3);tile_file.write(reinterpret_cast<const char*>(data+t*4),8);}
                            if(!tile_file)tiles.clear();
                        } else image.write(reinterpret_cast<const char*>(p.rgb.data()+slice*count*3),count*3);
                        if(!image)file.clear();
                        thumbnail_bytes+=count*(data?11:3);
                    }
                    log<<"{\"kind\":\"sample\",\"capture\":"<<p.id<<",\"swapchain\":\""<<p.chain
                       <<"\",\"slice\":"<<slice<<",\"serial\":"<<p.serial<<",\"image_index\":"<<p.index
                       <<",\"submit_qpc\":"<<p.submitted<<",\"ready_observed_qpc\":"<<p.ready
                       <<",\"diagnostic_fence\":"<<p.fence<<",\"completed_at_read\":"<<p.completed
                       <<",\"history_fence\":"<<p.history_fence<<",\"hash\":\""<<(data?hash.str():p.hashes[slice])
                       <<"\",\"thumbnail\":\""<<file<<"\",\"tiles\":\""<<tiles<<"\",\"tiles_x\":"<<p.nx<<",\"tiles_y\":"<<p.ny<<"}\n";
                }
            };
            for(;;) {
                Packet p;
                { std::unique_lock lock(mutex); wake.wait(lock,[&]{return stop_writer||!packets.empty();});
                  if(packets.empty()) break;
                  p=std::move(packets.front()); packets.pop_front(); queued_bytes-=p.data.size()*4+p.text.size(); }
                if(p.kind==Packet::ring_begin){rolling_writer=true;ring.clear();ring_bytes=0;capacity_evictions=0;continue;}
                if(p.kind==Packet::ring_end){rolling_writer=false;ring.clear();ring_bytes=0;continue;}
                if(p.kind==Packet::snapshot) {
                    std::filesystem::create_directories(p.path);directory=p.path;thumbnail_bytes=0;
                    log.close();log.open(directory/L"trace.jsonl",std::ios::binary);log<<p.text<<'\n';
                    std::size_t rows{};
                    for(const auto& saved:ring)if(saved.submitted>=p.submitted-frequency && saved.submitted<=p.submitted){emit(saved);++rows;}
                    log<<"{\"kind\":\"end\",\"rows\":"<<rows<<",\"capacity_evictions\":"<<capacity_evictions<<"}\n";
                    log.flush();log.close();MessageBeep(MB_OK);continue;
                }
                if(rolling_writer && (p.kind==Packet::line || p.kind==Packet::pixels)) {
                    compact(p);ring_bytes+=size(p);
                    const auto position=std::upper_bound(ring.begin(),ring.end(),p.submitted,[](std::int64_t t,const Packet& other){return t<other.submitted;});
                    ring.insert(position,std::move(p));
                    const auto expired=FrameCapture::qpc()-frequency*2;
                    while(!ring.empty() && (ring.front().submitted<expired || ring_bytes>256ULL*1024*1024)) {
                        if(ring_bytes>256ULL*1024*1024)++capacity_evictions;
                        ring_bytes-=size(ring.front());ring.pop_front();
                    }
                    continue;
                }
                if(p.kind==Packet::begin) {
                    directory=p.path; thumbnail_bytes=0;
                    log.close(); log.open(directory/L"trace.jsonl",std::ios::binary);
                    log<<p.text<<'\n'; log.flush(); continue;
                }
                if(p.kind==Packet::end) { log<<p.text<<'\n';log.flush();log.close();continue; }
                emit(p);
                log.flush();
            }
        } catch(...) { ++writer_drops; }
    }
    HRESULT initialize() {
        const unsigned requested_tile=rolling?8:16;
        if(shader && tile==requested_tile) return S_OK;
        shader.Reset();tile=requested_tile;
        ComPtr<ID3D11Device5> device5;
        HRESULT r=device.As(&device5);
        if(SUCCEEDED(r)) r=context.As(&context4);
        if(SUCCEEDED(r)) r=device5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));
        ComPtr<ID3DBlob> code,error;
        D3D_SHADER_MACRO macros[]={{"CAPTURE_TILE",tile==8?"8":"16"},{nullptr,nullptr}};
        if(SUCCEEDED(r)) r=D3DCompile(shader_source,sizeof(shader_source)-1,nullptr,macros,nullptr,
            "main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(SUCCEEDED(r)) r=device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader);
        return r;
    }
    HRESULT create_chain(Chain& c,ID3D11Texture2D* source) {
        source->GetDesc(&c.desc);
        auto format=srv_format(c.desc.Format);
        if(format==DXGI_FORMAT_UNKNOWN || c.desc.SampleDesc.Count!=1 || c.desc.ArraySize>2) return E_NOTIMPL;
        auto d=c.desc; d.MipLevels=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;
        d.Usage=D3D11_USAGE_DEFAULT;d.MiscFlags=0;
        HRESULT r=device->CreateTexture2D(&d,nullptr,&c.snapshot);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MipLevels=1;sd.Texture2DArray.ArraySize=d.ArraySize;
        if(SUCCEEDED(r)) r=device->CreateShaderResourceView(c.snapshot.Get(),&sd,&c.srv);
        c.nx=(d.Width+tile-1)/tile;c.ny=(d.Height+tile-1)/tile;c.words=c.nx*c.ny*d.ArraySize*4;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=c.words*4;bd.Usage=D3D11_USAGE_DEFAULT;
        bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
        if(SUCCEEDED(r)) r=device->CreateBuffer(&bd,nullptr,&c.output);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=c.words/4;
        if(SUCCEEDED(r)) r=device->CreateUnorderedAccessView(c.output.Get(),&ud,&c.uav);
        ComPtr<ID3D11DeviceContext> deferred;
        if(SUCCEEDED(r)) r=device->CreateDeferredContext(0,&deferred);
        for(auto& slot:c.slots) {
            if(FAILED(r)) break;
            bd.BindFlags=0;bd.MiscFlags=0;bd.StructureByteStride=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            r=device->CreateBuffer(&bd,nullptr,&slot.readback);
            if(FAILED(r)) break;
            auto* srv=c.srv.Get();auto* uav=c.uav.Get();
            deferred->CSSetShader(shader.Get(),nullptr,0);deferred->CSSetShaderResources(0,1,&srv);deferred->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
            deferred->Dispatch(c.nx,c.ny,d.ArraySize);
            deferred->CopyResource(slot.readback.Get(),c.output.Get());
            r=deferred->FinishCommandList(FALSE,&slot.commands);
        }
        return r;
    }
    void collect() {
        const auto completed=fence ? fence->GetCompletedValue() : 0;
        for(auto& [handle,c]:chains) for(auto& s:c.slots) {
            if(!s.fence) continue;
            if(completed==UINT64_MAX) { ++gpu_drops;s.fence=0;continue; }
            if(completed<s.fence) continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT r=context->Map(s.readback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(r==DXGI_ERROR_WAS_STILL_DRAWING) continue;
            if(FAILED(r)) { ++gpu_drops;s.fence=0;continue; }
            Packet p;p.kind=Packet::pixels;p.id=s.id;p.chain=handle;p.serial=s.serial;p.history_fence=s.history_fence;
            p.index=s.index;p.fence=s.fence;p.completed=completed;p.submitted=s.submitted;p.ready=FrameCapture::qpc();
            p.nx=c.nx;p.ny=c.ny;p.slices=c.desc.ArraySize;
            try { p.data.assign(static_cast<std::uint32_t*>(mapped.pData),static_cast<std::uint32_t*>(mapped.pData)+c.words); }
            catch(...) { context->Unmap(s.readback.Get(),0);throw; }
            context->Unmap(s.readback.Get(),0);s.fence=0;enqueue(std::move(p));
        }
        bool pending=false;
        for(auto& [handle,c]:chains)for(auto& s:c.slots)pending|=s.fence!=0;
        while(!saves.empty()) {
            const auto& request=saves.front();
            bool waiting=false;
            for(const auto& [handle,c]:chains)for(const auto& s:c.slots)
                waiting|=s.fence && s.fence<=request.fence;
            if(waiting)break;
            Packet p;p.kind=Packet::snapshot;p.path=request.path;p.submitted=request.cutoff;
            p.text="{\"kind\":\"begin\",\"version\":482,\"mode\":\"rolling_previous_second\",\"qpc_frequency\":"+std::to_string(frequency)+
                ",\"window_start_qpc\":"+std::to_string(request.cutoff-frequency)+",\"window_end_qpc\":"+std::to_string(request.cutoff)+
                ",\"ring_armed_qpc\":"+std::to_string(ring_armed_qpc)+",\"full_second_available\":"+(request.cutoff-ring_armed_qpc>=frequency?"true":"false")+
                ",\"gpu_drops\":"+std::to_string(gpu_drops)+",\"writer_drops\":"+std::to_string(writer_drops.load())+
                ",\"save_drops\":"+std::to_string(save_drops)+",\"thumbnail_stride\":8,\"hash_scope\":\"all_mip0_decoded_RGBA_pixels_per_array_slice\"}";
            enqueue(std::move(p),true);saves.pop_front();
        }
        if(run_open && !enabled && !pending && saves.empty()) {
            Packet p;p.kind=rolling?Packet::ring_end:Packet::end;p.text="{\"kind\":\"end\",\"gpu_drops\":"+std::to_string(gpu_drops)+
                ",\"writer_drops\":"+std::to_string(writer_drops.load())+"}";
            enqueue(std::move(p),true);run_open=false;rolling=false;chains.clear();MessageBeep(MB_OK);
        }
    }
};

std::int64_t FrameCapture::qpc() noexcept { LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart; }
FrameCapture::FrameCapture(ID3D11Device* device,ID3D11DeviceContext* context,std::filesystem::path root):impl_(std::make_shared<Impl>()) {
    impl_->device=device;impl_->context=context;impl_->root=std::move(root);
}
FrameCapture::~FrameCapture(){shutdown();}
bool FrameCapture::active() const noexcept{return impl_->enabled.load(std::memory_order_relaxed);}
bool FrameCapture::busy() const noexcept{return impl_->run_open.load(std::memory_order_relaxed);}
bool FrameCapture::ring_active() const noexcept{return active() && impl_->rolling.load(std::memory_order_relaxed);}
bool FrameCapture::start_ring() noexcept {
    try {
        auto& p=*impl_;
        if(p.stopped || p.run_open || diagnostic_ring_enabled)return false;
        p.rolling=true;
        if(FAILED(p.initialize())){p.rolling=false;return false;}
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);p.frequency=f.QuadPart;
        p.ring_armed_qpc=qpc();
        p.gpu_drops=0;p.writer_drops=0;p.save_drops=0;p.refused.clear();
        if(!p.writer.joinable())p.writer=std::thread([&p]{p.write_main();});
        Impl::Packet begin;begin.kind=Impl::Packet::ring_begin;p.enqueue(std::move(begin),true);
        p.run_open=true;p.enabled=true;
        {std::scoped_lock lock(diagnostic_ring_sink_mutex);
         diagnostic_ring_sink=[weak=std::weak_ptr<Impl>(impl_)](std::string row){if(const auto owner=weak.lock())if(owner->rolling)owner->active_event(std::move(row));};}
        diagnostic_ring_enabled=true;MessageBeep(MB_ICONASTERISK);return true;
    }catch(...){impl_->enabled=false;impl_->rolling=false;return false;}
}
void FrameCapture::stop_ring() noexcept {
    if(!impl_ || !impl_->rolling)return;
    diagnostic_ring_enabled=false;
    {std::scoped_lock lock(diagnostic_ring_sink_mutex);diagnostic_ring_sink={};}
    impl_->enabled=false;
}
bool FrameCapture::save_ring() noexcept {
    if(!ring_active())return false;
    try {
        auto& p=*impl_;
        if(p.saves.size()>=8){++p.save_drops;return false;}
        const auto cutoff=qpc();SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[160]{};
        swprintf_s(name,L"rolling-%04u%02u%02u-%02u%02u%02u-%03u-pid%lu-%u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),++p.run);
        p.saves.push_back({p.root/name,cutoff,p.next_fence});p.collect();return true;
    }catch(...){++impl_->save_drops;return false;}
}
void FrameCapture::record(std::string json) noexcept {
    try { if(active())impl_->active_event(std::move(json)); }catch(...){++impl_->writer_drops;}
}
bool FrameCapture::start(bool logging_enabled,double seconds) noexcept {
    try {
        auto& p=*impl_;if(!logging_enabled||p.run_open||p.stopped||seconds<=0||seconds>10)return false;
        SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[128]{};
        swprintf_s(name,L"capture-%04u%02u%02u-%02u%02u%02u-pid%lu-%u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),++p.run);
        auto path=p.root/name;std::filesystem::create_directories(path);
        if(!p.writer.joinable())p.writer=std::thread([&p]{p.write_main();});
        p.gpu_drops=0;p.writer_drops=0;p.refused.clear();
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        const HRESULT initialized=p.initialize();
        Impl::Packet begin;begin.kind=Impl::Packet::begin;begin.path=path;
        begin.text="{\"kind\":\"begin\",\"version\":212,\"qpc_frequency\":"+std::to_string(frequency.QuadPart)+
            ",\"qpc\":"+std::to_string(qpc())+",\"seconds\":"+std::to_string(seconds)+
            ",\"initialize_hresult\":"+std::to_string(initialized)+",\"upstream_engine_id\":null,\"hash_scope\":\"all_mip0_decoded_RGBA_pixels_per_array_slice\"}";
        p.enqueue(std::move(begin),true);p.run_open=true;
        p.deadline=std::chrono::steady_clock::now()+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
        p.enabled=SUCCEEDED(initialized);
        if(!p.enabled)p.collect();else MessageBeep(MB_ICONASTERISK);
        return p.enabled;
    }catch(...){impl_->enabled=false;return false;}
}
void FrameCapture::tick(bool logging_enabled) noexcept {
    try {
        auto& p=*impl_;
        if(p.enabled && !p.rolling && (!logging_enabled||std::chrono::steady_clock::now()>=p.deadline))p.enabled=false;
        if(p.run_open)p.collect();
    }catch(...){impl_->enabled=false;++impl_->gpu_drops;}
}
std::uint64_t FrameCapture::source(std::uint64_t handle,ID3D11Texture2D* source,std::uint32_t index,std::uint64_t serial,std::uint64_t history_fence) noexcept {
    if(!active()||!source)return 0;
    try {
        auto& p=*impl_;p.collect();
        if(!p.rolling && std::chrono::steady_clock::now()>=p.deadline){p.enabled=false;p.collect();return 0;}
        if(p.refused.contains(handle)){++p.gpu_drops;return 0;}
        if(!p.chains.contains(handle)) {
            if(p.chains.size()>=4){++p.gpu_drops;return 0;}
            Impl::Chain c;source->GetDesc(&c.desc);
            std::uint64_t bytes=texture_bytes(c.desc);
            for(const auto& [key,chain]:p.chains)bytes+=texture_bytes(chain.desc);
            HRESULT r=bytes<=384ULL*1024*1024 ? p.create_chain(c,source) : E_OUTOFMEMORY;
            p.event("{\"kind\":\"source_info\",\"swapchain\":\""+std::to_string(handle)+"\",\"width\":"+std::to_string(c.desc.Width)+
                ",\"height\":"+std::to_string(c.desc.Height)+",\"format\":"+std::to_string(c.desc.Format)+",\"slices\":"+std::to_string(c.desc.ArraySize)+",\"hresult\":"+std::to_string(r)+"}");
            if(FAILED(r)){++p.gpu_drops;p.refused.insert(handle);return 0;}p.chains.emplace(handle,std::move(c));
        }
        auto& c=p.chains.at(handle);
        auto it=std::find_if(c.slots.begin(),c.slots.end(),[](const auto& s){return !s.fence;});
        if(it==c.slots.end()){++p.gpu_drops;return 0;}
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
        if(d.Width!=c.desc.Width||d.Height!=c.desc.Height||d.Format!=c.desc.Format||d.ArraySize!=c.desc.ArraySize){++p.gpu_drops;return 0;}
        for(unsigned slice=0;slice<d.ArraySize;++slice)
            p.context->CopySubresourceRegion(c.snapshot.Get(),slice,0,0,0,source,D3D11CalcSubresource(0,slice,d.MipLevels),nullptr);
        p.context->ExecuteCommandList(it->commands.Get(),TRUE);
        const auto value=p.next_fence+1;
        HRESULT r=p.context4->Signal(p.fence.Get(),value);p.context->Flush();
        if(FAILED(r)){++p.gpu_drops;p.enabled=false;return 0;}
        p.next_fence=value;it->fence=value;it->id=++p.next_id;it->index=index;it->serial=serial;it->history_fence=history_fence;it->submitted=qpc();
        return it->id;
    }catch(...){++impl_->gpu_drops;return 0;}
}
void FrameCapture::shutdown() noexcept {
    if(!impl_||impl_->stopped)return;
    stop_ring();
    auto& p=*impl_;p.enabled=false;
    try {
        if(p.fence && p.next_fence && p.fence->GetCompletedValue()<p.next_fence) {
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(event){if(SUCCEEDED(p.fence->SetEventOnCompletion(p.next_fence,event)))WaitForSingleObject(event,INFINITE);CloseHandle(event);}
        }
        p.collect();
    }catch(...){}
    {std::scoped_lock lock(p.mutex);p.stop_writer=true;}p.wake.notify_one();
    if(p.writer.joinable())p.writer.join();p.stopped=true;
}
}
