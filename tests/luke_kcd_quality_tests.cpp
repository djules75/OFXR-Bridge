// Compile the shipping implementation so the same camera builder, depth/motion
// shaders, provider ABI and submission path are checked together.
#include <Windows.h>
// Observe the actual shipping provider call, then delegate the unchanged ABI
// to the real bundled/game export. This remains confined to this test TU.
FARPROC WINAPI quality_get_proc_address(HMODULE module,LPCSTR name);
#define GetProcAddress quality_get_proc_address
#include "../src/d3d12/luke_afw.cpp"
#undef GetProcAddress
#include "xrfg/native_ingress/KcdCameraPacket.h"
#include <bit>
#include <d3d12sdklayers.h>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <map>
#include <sstream>

namespace quality {
using Microsoft::WRL::ComPtr;
using xrfg::DlssMotionVectorFrame;
constexpr unsigned width=512,height=512;
bool consumer_probe{};
float consumer_probe_gain=1.F;
constexpr double near_plane=.1,baseline=.064,plane_z=-3;
constexpr double relative_xr_yaw=.025;
struct Scene {
    const char* name;
    unsigned color_width,color_height,guide_width,guide_height;
    float scale_x,scale_y,jitter_x,jitter_y;
    double span_x,span_y;
};
constexpr Scene scenes[]{
    {"full",width,height,width,height,1,1,0,0,1,1},
    {"odd_raster_jitter",411,422,256,256,-256,256,.125F,-.25F,411./512.,422./512.}};
struct ProviderReceipt {
    xrfg::EvaluateFrameWarpFn original{};
    unsigned calls{},expected_width{},expected_height{},expected_shading_rate{9};
    float expected_gain_x{},expected_gain_y{},gain_x{},gain_y{};
    bool valid{true};
} provider;
std::vector<std::filesystem::path> private_modules;
std::filesystem::path original_provider_file;
std::vector<char> bytes(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void __stdcall observe_evaluate(xrfg::FrameWarpEvaluateParams& params) {
    ++provider.calls;
    provider.gain_x=params.motion_scale[0];provider.gain_y=params.motion_scale[1];
    provider.valid&=std::bit_cast<std::uint32_t>(params.motion_scale[0])==
            std::bit_cast<std::uint32_t>(provider.expected_gain_x)&&
        std::bit_cast<std::uint32_t>(params.motion_scale[1])==
            std::bit_cast<std::uint32_t>(provider.expected_gain_y)&&
        params.shading_rate==(params.mode==xrfg::CombinedWarping ? provider.expected_shading_rate : 9u)&&
        params.motion_vectors_type==xrfg::Normal&&params.input_eye_frame_buffer&&
        params.input_eye_frame_buffer->motion_vectors.texture;
    if(params.input_eye_frame_buffer&&params.input_eye_frame_buffer->motion_vectors.texture) {
        const auto motion=params.input_eye_frame_buffer->motion_vectors.texture->GetDesc();
        provider.valid&=motion.Width==provider.expected_width&&motion.Height==provider.expected_height;
    }
    if(!provider.valid)std::cerr<<"Provider mismatch mode="<<params.mode<<" shading="<<params.shading_rate<<" expected="<<provider.expected_shading_rate<<" gain="<<params.motion_scale[0]<<','<<params.motion_scale[1]<<" expected_gain="<<provider.expected_gain_x<<','<<provider.expected_gain_y<<" mvtype="<<params.motion_vectors_type<<" crop="<<provider.expected_width<<'x'<<provider.expected_height<<'\n';
    if(consumer_probe) {
        params.debug=true;
        params.motion_scale[0]*=consumer_probe_gain;
        params.motion_scale[1]*=consumer_probe_gain;
    }
    provider.original(params);
}
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void check(HRESULT value,const char* message) {if(FAILED(value))std::cerr<<"HRESULT="<<std::hex<<unsigned(value)<<std::dec<<'\n';require(SUCCEEDED(value),message);}
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER result{};result.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    result.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return result;
}
struct Camera {double angle{},x{},z{},center_x{},center_y{};
    kcd_camera::Camera packet;std::array<float,16> inverse_view{};};
Camera camera(unsigned n,const Scene& scene) {
    Camera result{};result.angle=.012*double(n);
    const float c=float(std::cos(result.angle)),s=float(std::sin(result.angle));
    const float side=n&1?-.5F:.5F;
    result.x=double(side)*baseline*c;result.z=-double(side)*baseline*s;
    using Matrix=std::array<float,16>;
    Matrix inverse{c,0,s,float(result.x),0,1,0,0,-s,0,c,float(result.z),0,0,0,1};
    Matrix view{c,0,-s,float(-c*result.x+s*result.z),0,1,0,0,s,0,c,float(-s*result.x-c*result.z),0,0,0,1};
    Matrix projection{};projection[0]=projection[5]=1;projection[11]=float(near_plane);projection[14]=-1;
    projection[2]=-2*scene.jitter_x/float(scene.guide_width);
    projection[6]=2*scene.jitter_y/float(scene.guide_height);
    result.center_x=projection[2];result.center_y=projection[6];
    Matrix vp{};
    for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)for(unsigned k=0;k<4;++k)
        vp[r*4+col]+=projection[r*4+k]*view[k*4+col];
    auto sky=vp;sky[3]=sky[7]=sky[15]=0;
    std::array<unsigned char,kcd_camera::packet_bytes> bytes{};
    std::memcpy(bytes.data(),sky.data(),64);std::memcpy(bytes.data()+80,vp.data(),64);
    std::memcpy(bytes.data()+880,view.data(),64);std::memcpy(bytes.data()+944,inverse.data(),64);
    require(kcd_camera::decode(bytes.data(),bytes.size(),width,height,result.packet),"rotated KCD packet rejected");
    require(std::abs(result.packet.projection[8]-result.center_x)<1e-6&&
        std::abs(result.packet.projection[9]-result.center_y)<1e-6,"decoded KCD raster jitter lost the original projection center");
    for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)result.inverse_view[r*4+col]=inverse[col*4+r];
    return result;
}
// Independent Euclidean ray/plane intersection and camera projection. Neither
// the shipping camera matrices nor its native-motion correction are the oracle.
std::array<double,3> world(const Camera& camera,unsigned x,unsigned y,
    unsigned pixels_x,unsigned pixels_y,double span_x,double span_y,bool guide=false) {
    const double nx=(2*(double(x)+.5)/pixels_x-1)*span_x+
            (guide?camera.center_x:0),
        ny=(1-2*(double(y)+.5)/pixels_y)*span_y+
            (guide?camera.center_y:0);
    const double c=std::cos(camera.angle),s=std::sin(camera.angle);
    const double dx=c*nx-s,dz=-s*nx-c,t=(plane_z-camera.z)/dz;
    return {camera.x+t*dx,t*ny,plane_z};
}
std::array<double,3> project(const Camera& camera,const std::array<double,3>& point) {
    const double dx=point[0]-camera.x,dz=point[2]-camera.z;
    const double c=std::cos(camera.angle),s=std::sin(camera.angle);
    const double vx=c*dx-s*dz,vz=s*dx+c*dz;
    return {.5+.5*(vx/-vz-camera.center_x),
        .5-.5*(point[1]/-vz-camera.center_y),near_plane/-vz};
}
std::array<unsigned char,4> color(const std::array<double,3>& point) {
    if(consumer_probe) {const auto g=static_cast<unsigned char>(80+60*std::sin(point[0]*8)*std::cos(point[1]*7));return {g,g,g,255};}
    return {static_cast<unsigned char>(20+100*(1+std::sin(point[0]*8))),
        static_cast<unsigned char>(40+80*(1+std::sin(point[1]*7))),
        static_cast<unsigned char>(40+80*(1+std::sin((point[0]+point[1])*6))),255};
}
Camera peer(const Camera& source,unsigned n) {
    auto result=source;const double direction=n&1?1:-1;
    result.x+=direction*baseline*std::cos(source.angle);
    result.z-=direction*baseline*std::sin(source.angle);
    // The physical left/right XR eyes retain -half/+half yaw across calls.
    // Right-handed +Y turns view back toward +X, so the peer angle changes
    // by destination minus source: +yaw from left, -yaw from right.
    result.angle+=direction*relative_xr_yaw;return result;
}
}

FARPROC WINAPI quality_get_proc_address(HMODULE module,LPCSTR name) {
    const auto actual=::GetProcAddress(module,name);
    if(actual&&name&&!std::strcmp(name,"EvaluateFrameWarp")) {
        wchar_t path[32768]{};
        if(GetModuleFileNameW(module,path,32768) && std::filesystem::path(path).extension()==L".tmp") {
            quality::require(quality::bytes(path)==quality::bytes(quality::original_provider_file),
                "isolated provider is not byte-identical to the selected bundled SDK");
            quality::private_modules.emplace_back(path);
        }
        quality::provider.original=reinterpret_cast<xrfg::EvaluateFrameWarpFn>(actual);
        return reinterpret_cast<FARPROC>(&quality::observe_evaluate);
    }
    return actual;
}

#include "luke_capture_replay.inc"

int main(int argc,char** argv) {try {
    if(argc==4 && std::filesystem::is_directory(argv[2]))return replay::run(argv[1],argv[2],argv[3]);
    using namespace quality;
    require(argc>=2 && argc<=4,"provider path argument missing");
    const bool interleaved=argc==4 && std::string_view(argv[3])=="interleaved";
    consumer_probe=GetEnvironmentVariableW(L"XRFG_TEST_AFW_CONSUMER_PROBE",nullptr,0)!=0;
    if(GetEnvironmentVariableW(L"XRFG_TEST_AFW_CONSUMER_HALF",nullptr,0))consumer_probe_gain=.5F;
    original_provider_file=argv[1];
    const int shading=argc>=3 ? std::stoi(argv[2]) : 0;
    const auto diagnostic_root=std::filesystem::temp_directory_path()/
        ("ofxr-afw-timing-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    if(shading==9) {
        std::filesystem::create_directories(diagnostic_root);
        {std::ofstream ini(diagnostic_root/"ofxr_bridge.ini");ini<<"[diagnostics]\nlogging_enabled=1\nmax_file_mb=32\n";}
        xrfg::bridge_flight_logger().initialize(diagnostic_root);
    }
    constexpr unsigned upstream_rates[]{9,10,12,17,18,20,33,34,36};
    for(int i=0;i<9;++i)require(xrfg::embedded::afw_shading_rate(i)==upstream_rates[i],"beta6 non-D3D12 shading encoding");
    require(shading>=0&&shading<=9,"shading index outside ABI contract");
    const auto config=xrfg::luke_game_camera_config_from_ini(xrfg::luke_host_game_directory()/L"RealVR.ini");
    const bool indiana=config.axes==xrfg::LukeProducerCameraAxes::native_view_rows;
    require(config.world_scale==1,"isolated KCD WorldScale fixture missing");
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"hardware device");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
    check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"hardware queue");
    ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"completion fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"completion event");UINT64 tick{};
    const auto wait=[&] {check(queue->Signal(fence.Get(),++tick),"queue receipt");
        check(fence->SetEventOnCompletion(tick,event),"receipt notification");
        require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"hardware queue timeout");};
    const auto texture=[&](DXGI_FORMAT format,D3D12_RESOURCE_STATES state,
        unsigned texture_width,unsigned texture_height) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=texture_width;desc.Height=texture_height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=format;
        ComPtr<ID3D12Resource> result;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,
            nullptr,IID_PPV_ARGS(&result)),"fixture texture");return result;};
    const auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE kind) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=kind;D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            kind==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&result)),"fixture buffer");return result;};
    bool all_camera_bits_match=true,all_mode_pixels_match=true;
    for(const auto& scene:scenes) for(unsigned span:{1u,2u}) for(int mode:{1,2,3}) {
        if(consumer_probe && (std::string_view(scene.name)!="full" || span!=2 || mode!=3))continue;
        xrfg::LukeAfwSynthesizer synth;check(synth.initialize(device.Get(),queue.Get(),std::filesystem::path(argv[1])),"full AFW provider initialization");
        std::vector<std::shared_ptr<DlssMotionVectorFrame>> frames;
        std::vector<ComPtr<ID3D12Resource>> colors;
        xrfg::LukeCameraOrigin origin;require(origin.update(1,1,{0,0,0}).has_value(),"oracle camera origin");
        double error_sum{};std::uint64_t samples{},green{},pixels{};double basis_error{};bool basis_bits_match=true;
        for(unsigned n=1;n<=18;++n) {
            const auto current=camera(n,scene),previous=camera(n>=span?n-span:0,scene);
            auto frame=std::make_shared<DlssMotionVectorFrame>();frame->stream=1;frame->epoch=1;
            frame->raw_motion_native_span=span;
            frame->serial=frame->publication=frame->camera_frame_token=frame->native_production_sequence=n;
            frame->previous_serial=n>=3?n-2:0;
            if(interleaved) {
                frame->stream=1+(n&1);frame->epoch=n&1?7:3;
                frame->serial=(n+1)/2;frame->native_production_sequence=0;
                frame->previous_serial=n>=3?(n-1)/2:0;
                frame->luke_interleaved_history=true;
            }
            frame->camera_data_valid=frame->camera_matrices_valid=frame->camera_frame_token_valid=true;
            frame->camera_position=current.packet.position;frame->camera_right=current.packet.right;
            frame->camera_up=current.packet.up;frame->camera_forward=current.packet.forward;
            if(indiana)for(float& value:frame->camera_forward)value=-value;
            frame->camera_view_to_clip=current.packet.projection;frame->camera_clip_to_view=current.packet.inverse_projection;
            frame->camera_near=current.packet.near_plane;frame->camera_far=current.packet.far_plane;
            frame->camera_fov_vertical=current.packet.fov;frame->depth_inverted=frame->depth_infinite=true;
            frame->motion_width=frame->depth_width=scene.guide_width;frame->output_width=width;
            frame->motion_height=frame->depth_height=scene.guide_height;frame->output_height=height;
            frame->scale_x=scene.scale_x;frame->scale_y=scene.scale_y;
            frame->jitter_x=scene.jitter_x;frame->jitter_y=scene.jitter_y;
            frame->calibrated_game_eye_baseline=float(baseline);frame->producer_queue=queue;
            frame->verified_producer_device=frame->motion_snapshot_device=frame->depth_snapshot_device=device;
            frame->motion_retention=std::make_shared<xrfg::DlssSnapshotRetention>();
            frame->depth_retention=std::make_shared<xrfg::DlssSnapshotRetention>();
            frame->motion_vectors=texture(DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST,scene.guide_width,scene.guide_height);
            frame->depth=texture(DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COPY_DEST,scene.guide_width,scene.guide_height);
            auto source=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST,scene.color_width,scene.color_height);
            auto destination=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COMMON,scene.color_width,scene.color_height);
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"upload allocator");
            check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"upload list");
            std::array<ComPtr<ID3D12Resource>,3> uploads;
            const std::array targets{source.Get(),frame->motion_vectors.Get(),frame->depth.Get()};
            for(unsigned index=0;index<3;++index) {
                const auto desc=targets[index]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};
                device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
                uploads[index]=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);void* data{};
                check(uploads[index]->Map(0,nullptr,&data),"original plane upload");
                for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x) {
                    const auto point=index==0?world(current,x,y,scene.color_width,scene.color_height,scene.span_x,scene.span_y):
                        world(current,x,y,scene.guide_width,scene.guide_height,1,1,true);
                    const auto prior=project(previous,point),now=project(current,point);
                    auto* pixel=static_cast<char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*(index==1?8:4);
                    if(index==0){const auto rgba=color(point);std::memcpy(pixel,rgba.data(),4);}
                    else if(index==1){const float mv[]{float((prior[0]-now[0])*scene.guide_width/scene.scale_x),
                        float((prior[1]-now[1])*scene.guide_height/scene.scale_y)};std::memcpy(pixel,mv,8);}
                    else {const float depth=float(now[2]);std::memcpy(pixel,&depth,4);}
                }
                uploads[index]->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION from{},to{};
                from.pResource=uploads[index].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
                to.pResource=targets[index];to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
                auto barrier=transition(targets[index],D3D12_RESOURCE_STATE_COPY_DEST,
                    index==0?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                list->ResourceBarrier(1,&barrier);
            }
            check(list->Close(),"original upload close");ID3D12CommandList* recorded[]{list.Get()};queue->ExecuteCommandLists(1,recorded);wait();
            frame->producer_fence=fence;frame->producer_fence_device=device;frame->producer_fence_value=tick;
            if(n>=2)frame->previous_opposite_guide=frames[n-2];
            if(n>=3){frame->previous_same_guide=frames[n-3];frame->previous_same_native_camera=xrfg::dlss_native_camera_history(*frames[n-3]);}
            frames.push_back(frame);colors.push_back(source);
            if(n<(interleaved?4u:3u))continue;
            const unsigned eye=n&1?0:1;
            xrfg::D3D12ReprojectionView source_view{},destination_view{};
            // The first odd-raster beta5 Combined probe had MAE6.7346 and
            // every source used identity while every destination used +yaw: the same
            // eye then changed pose when it became the next real source. Keep
            // each physical eye's pose persistent for temporal provider input.
            const double source_xr_yaw=(eye?1:-1)*relative_xr_yaw*.5;
            source_view.pose.orientation.y=float(std::sin(source_xr_yaw*.5));
            destination_view.pose.orientation.y=-source_view.pose.orientation.y;
            source_view.pose.orientation.w=destination_view.pose.orientation.w=float(std::cos(source_xr_yaw*.5));
            source_view.pose.position.x=eye?float(baseline*.5):float(-baseline*.5);
            destination_view.pose.position.x=-source_view.pose.position.x;
            source_view.fov=destination_view.fov={float(-std::atan(scene.span_x)),float(std::atan(scene.span_x)),
                float(std::atan(scene.span_y)),float(-std::atan(scene.span_y))};
            xrfg::CameraData sdk_camera{};require(xrfg::camera_data(source_view,destination_view,*frame,eye,
                1,float(baseline),1,origin,config.axes,sdk_camera),"actual shipping camera builder");
            for(unsigned i=0;i<16;++i)basis_error=std::max(basis_error,double(std::abs(sdk_camera.source_view_to_world.values[i]-current.inverse_view[i])));
            basis_bits_match&=!std::memcmp(sdk_camera.source_view_to_world.values,current.inverse_view.data(),sizeof(current.inverse_view));
            xrfg::embedded::AfwSettings settings{};settings.mode=mode;settings.depth_dilate_mode=0;settings.shading_rate=shading==9 ? (mode==3 ? std::array{0,4,8,0}[(n-3)/4%4] : 0) : shading;
            provider.expected_shading_rate=upstream_rates[mode==3 ? settings.shading_rate : 0];
            provider.calls=0;provider.valid=true;
            provider.expected_width=(scene.guide_width*scene.color_width+width-1)/width;
            provider.expected_height=(scene.guide_height*scene.color_height+height-1)/height;
            // Independent accepted ABI receipt, using the original native
            // raster units, not the padded integer crop or the new helper.
            provider.expected_gain_x=scene.scale_x*float(width)/float(scene.guide_width);
            provider.expected_gain_y=scene.scale_y*float(height)/float(scene.guide_height);
            check(synth.synthesize(source.Get(),destination.Get(),*frame,source_view,destination_view,eye,float(baseline),settings),"actual full rotated AFW synthesis");
            wait();
            require(xrfg::embedded::active_afw_shading_rate.load(std::memory_order_relaxed)==settings.shading_rate,
                "active shading status does not follow the selected AFW quality");
            ComPtr<ID3D12InfoQueue> stage_info;
            if(SUCCEEDED(device.As(&stage_info)))for(UINT64 k=0;k<stage_info->GetNumStoredMessages();++k) {
                SIZE_T bytes{};stage_info->GetMessage(k,nullptr,&bytes);std::vector<char> storage(bytes);
                auto* m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());stage_info->GetMessage(k,m,&bytes);
                if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<"Error frame scene="<<scene.name<<" span="<<span<<" mode="<<mode<<" n="<<n<<" "<<m->pDescription<<'\n';require(false,"first GPU error");}
            }
            require(provider.calls>=1&&provider.calls<=2&&provider.valid,"actual PDAFW call changed the accepted raster motion gain/Normal crop or shading contract");
            if(n<9)continue;
            check(allocator->Reset(),"readback allocator");check(list->Reset(allocator.Get(),nullptr),"readback list");
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};const auto desc=destination->GetDesc();
            device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);auto readback=buffer(bytes,D3D12_HEAP_TYPE_READBACK);
            auto before=transition(destination.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&before);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=destination.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
            list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(before.Transition.StateBefore,before.Transition.StateAfter);list->ResourceBarrier(1,&before);
            check(list->Close(),"generated peer readback close");queue->ExecuteCommandLists(1,recorded);wait();
            void* data{};check(readback->Map(0,nullptr,&data),"generated peer bytes");const auto expected_camera=peer(current,n);
            for(unsigned y=scene.color_height/4;y<3*scene.color_height/4;y+=2)
                for(unsigned x=scene.color_width/4;x<3*scene.color_width/4;x+=2) {
                const auto expected=color(world(expected_camera,x,y,scene.color_width,scene.color_height,scene.span_x,scene.span_y));
                const auto* pixel=static_cast<unsigned char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
                if(consumer_probe) {++pixels;if(pixel[1]>pixel[0]+50 && pixel[1]>pixel[2]+50)++green;}
                for(unsigned channel=0;channel<3;++channel){error_sum+=std::abs(int(pixel[channel])-int(expected[channel]));++samples;}
            }
            readback->Unmap(0,nullptr);
        }
        const double mae=error_sum/double(samples);
        std::cout<<"KCD rotated plane scene="<<scene.name<<" native_span="<<span<<" mode="<<mode<<" RGB_MAE="<<mae
            <<" inverseView_max_error="<<basis_error<<" bit_exact="<<basis_bits_match
            <<" actual_provider_gain="<<provider.gain_x<<','<<provider.gain_y
            <<" shading_rate="<<provider.expected_shading_rate
            <<" actual_motion_crop="<<provider.expected_width<<'x'<<provider.expected_height<<'\n';
        all_camera_bits_match&=basis_bits_match&&basis_error<1e-6;
        if(consumer_probe)std::cout<<"CONSUMER_PROBE gain="<<consumer_probe_gain<<" green="<<double(green)/double(pixels)<<'\n';
        all_mode_pixels_match&=consumer_probe || mae<6;
    }
    wait();ComPtr<ID3D12InfoQueue> info;
    if(SUCCEEDED(device.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T bytes{};info->GetMessage(i,nullptr,&bytes);std::vector<char> storage(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(info->GetMessage(i,message,&bytes),"GPU validation message");
        if(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)
            std::cerr<<message->pDescription<<'\n';
        require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR&&message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"full KCD AFW GPU validation error");
    }
    CloseHandle(event);
    if(shading==9) {
        require(private_modules.size()==12,"live quality transitions did not create fresh isolated provider state");
        for(const auto& file:private_modules)require(!std::filesystem::exists(file),
            "retired isolated AFW provider left its private file loaded or on disk");
        const auto log=xrfg::bridge_flight_logger().log_path();
        xrfg::bridge_flight_logger().shutdown();
        std::ifstream input(log);std::string line;
        using Key=std::pair<std::uint64_t,std::uint64_t>;
        std::map<Key,std::array<std::uint64_t,5>> stamps;
        std::map<std::uint64_t,unsigned> rates;
        bool clock_frequency=false;
        while(std::getline(input,line)) {
            require(line.find("op=gpu_timing_error")==std::string::npos,"AFW timestamp validation error");
            std::map<std::string,std::string> fields;std::istringstream tokens(line);std::string token;
            while(tokens>>token){const auto at=token.find('=');if(at!=std::string::npos)fields[token.substr(0,at)]=token.substr(at+1);}
            if(fields["op"]=="gpu_afw") {
                const auto point=std::stoul(fields["result"]);require(point<5,"AFW GPU point outside ABI");
                stamps[{std::stoull(fields["a"]),std::stoull(fields["b"])}][point]=std::stoull(fields["c"]);
            } else if(fields["op"]=="luke_afw_stage" && fields["a"]=="1100")
                rates[std::stoull(fields["b"])]=std::stoul(fields["result"]);
            else if(fields["op"]=="luke_afw_stage" && fields["a"]=="1102")
                clock_frequency|=std::stoull(fields["b"])>0;
        }
        std::array<unsigned,9> measured{};
        for(const auto& [key,v]:stamps) {
            if(!v[0] || !v[1] || !v[2] || !v[3] || !v[4])continue; // last pending submission needs no extra drain
            require(v[0]<=v[1] && v[1]<=v[2] && v[2]<=v[3],"AFW GPU stages out of order");
            require(rates.contains(key.second),"AFW GPU sample lost quality metadata");
            ++measured.at(rates.at(key.second));
        }
        require(clock_frequency && measured[0] && measured[4] && measured[8],"missing measured Full/Half/Quarter GPU stages or QPC frequency");
        std::cout<<"Actual AFW GPU timestamps: Full="<<measured[0]<<" Half="<<measured[4]<<" Quarter="<<measured[8]
            <<" ordered conversion/warp/output stages; live provider replacement preserves retired receipts PASS\n";
        std::error_code error;std::filesystem::remove_all(diagnostic_root,error);
    }
    require(all_camera_bits_match,"KCD plugin camera reflects decoded inverseView instead of preserving its actual view basis");
    require(all_mode_pixels_match,"full PDAFW rotated static-world reconstruction diverges from independent same-time peer oracle");
    std::cout<<(interleaved?"Interleaved AER full quality:":indiana?"Indiana full quality:":"KCD full quality:")<<" actual decoded camera, depth/Normal-motion shaders and PDAFW three modes match independent rotated-plane peer PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
