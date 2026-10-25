#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <string>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cmath>
#include <string_view>
#include "xrfg/luke_afw_continuity.hpp"
using Microsoft::WRL::ComPtr;
struct Matrix4x4 { float values[16]{}; };

struct DeviceParams {
    ID3D11Device* d3d11_device{};
    ID3D11DeviceContext* d3d11_context{};
    ID3D12Device* d3d12_device{};
    ID3D12CommandQueue* d3d12_queue{};
    IDXGIAdapter* dxgi_adapter{};
};

struct FrameWarpInitParams {
    int hmd_width{};
    int hmd_height{};
    DXGI_FORMAT eye_format{DXGI_FORMAT_R8G8B8A8_UNORM};
    DXGI_FORMAT backbuffer_format{DXGI_FORMAT_R8G8B8A8_UNORM};
};

enum EyeIndex { EyeLeft = 0, EyeRight = 1 };
enum FrameWarpMode { None = 0, AlternateEyeWarping = 1, PreviousFrameWarping = 2, CombinedWarping = 3 };
enum ImageType { Image = 0, Depth = 1 };
enum MotionVectorType { Normal = 0, FromOtherEye = 1, ObjectOnly = 2 };

struct TextureDesc {
    ImageType type{Image};
    ID3D12Resource* texture{};
    int srv_position{-1};
    int uav_position{-1};
    D3D12_GPU_DESCRIPTOR_HANDLE shader_resource_view{};
    D3D12_GPU_DESCRIPTOR_HANDLE unordered_access_view{};
    union {
        D3D12_CPU_DESCRIPTOR_HANDLE render_target_view{};
        D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view;
    };
    D3D12_RESOURCE_STATES initial_state{D3D12_RESOURCE_STATE_COMMON};
};

struct FrameBufferDesc { TextureDesc color; TextureDesc depth; TextureDesc motion_vectors; };
struct EyeFrameBuffers { FrameBufferDesc eye_frame_buffers[2]; };

struct CameraData {
    Matrix4x4 destination_world_to_view;
    Matrix4x4 destination_view_to_world;
    Matrix4x4 destination_view_to_clip;
    Matrix4x4 destination_clip_to_view;
    Matrix4x4 source_world_to_view;
    Matrix4x4 source_view_to_world;
    Matrix4x4 source_view_to_clip;
    Matrix4x4 source_clip_to_view;
    Matrix4x4 camera_world_to_view;
    Matrix4x4 camera_view_to_world;
    Matrix4x4 camera_view_to_clip;
    Matrix4x4 camera_clip_to_view;
};

struct FrameWarpEvaluateParams {
    void* command_list{};
    FrameBufferDesc* input_eye_frame_buffer{};
    FrameBufferDesc* output_eye_frame_buffer{};
    TextureDesc* ui_color_alpha{};
    float ui_scale[2]{1.0F, 1.0F};
    float ui_position[3]{0.0F, 0.0F, -1.0F};
    float motion_scale[2]{};
    FrameWarpMode mode{};
    EyeIndex eye_index{};
    CameraData* camera_data{};
    bool clear_before_warping{};
    float ignore_motion_threshold{2.5F};
    bool is_hudless_color{true};
    MotionVectorType motion_vectors_type{Normal};
    bool debug{};
    bool is_foveated{};
    RECT foveated_area{};
    TextureDesc* ue_velocity_buffer{};
    bool use_uint64{};
    std::uint32_t shading_rate{9}; // beta6 ShadingRate_1X1
    float reserved[12]{};
};

static_assert(sizeof(TextureDesc) == 56);
static_assert(sizeof(FrameBufferDesc) == 168);
static_assert(sizeof(EyeFrameBuffers) == 336);
static_assert(sizeof(CameraData) == 768);
static_assert(sizeof(FrameWarpEvaluateParams) == 184);
static_assert(offsetof(FrameWarpEvaluateParams, shading_rate) == 132);
static_assert(offsetof(FrameWarpEvaluateParams, reserved) == 136);

struct RendererApi;
using InitDeviceFn = RendererApi* (__stdcall*)(DeviceParams);
using InitFrameWarpFn = EyeFrameBuffers (__stdcall*)(FrameWarpInitParams);
using EvaluateFrameWarpFn = void (__stdcall*)(FrameWarpEvaluateParams&);
using SetupTextureDescFn = void (__fastcall*)(RendererApi*, TextureDesc&);
using CreateTextureFn = bool (__fastcall*)(RendererApi*, int, int, DXGI_FORMAT,
    D3D12_RESOURCE_STATES, TextureDesc&, bool);
using CopyTextureFn = void (__fastcall*)(RendererApi*, ID3D12GraphicsCommandList*,
    TextureDesc&, TextureDesc&, D3D12_BOX, UINT, UINT);

constexpr std::size_t kSetupTextureDescSlot = 14;
constexpr std::size_t kCreateTextureSlot = 16;
constexpr std::size_t kCopyTextureSlot = 20;


void check(HRESULT h) {if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h));}
void require(bool b) {if(!b)throw std::runtime_error("provider check");}
D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
 D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};return v;
}
void identity(Matrix4x4& m) {m={};m.values[0]=m.values[5]=m.values[10]=m.values[15]=1;}
int main(int argc,char** argv) {try {
 require(argc==2 || argc==4);
 if(argc==2) {
  char executable[32768]{};require(GetModuleFileNameA(nullptr,executable,sizeof(executable))!=0);
  DWORD clear_control{};
  for(int clear=0;clear<2;++clear)for(int fixed=0;fixed<2;++fixed) {
   std::string command="\""+std::string(executable)+"\" \""+argv[1]+"\" "+std::to_string(clear)+" "+std::to_string(fixed);
   STARTUPINFOA start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;
   start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);start.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);start.hStdError=GetStdHandle(STD_ERROR_HANDLE);
   PROCESS_INFORMATION process{};require(CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process));
   require(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0);
   DWORD result{};require(GetExitCodeProcess(process.hProcess,&result));CloseHandle(process.hThread);CloseHandle(process.hProcess);
   if(clear){require((result&0xf0000000U)==0x60000000U);if(!fixed)clear_control=result;else require(result==clear_control);}
   else require(result==0);
  }
  std::cout<<"Isolated real-provider retained-color recovery and unchanged Clear readback controls PASS\n";return 0;
 }
 auto module=LoadLibraryA(argv[1]);require(module);
 auto init=reinterpret_cast<InitDeviceFn>(GetProcAddress(module,"InitDevice"));
 auto buffers=reinterpret_cast<InitFrameWarpFn>(GetProcAddress(module,"InitFrameWarp"));
 auto evaluate=reinterpret_cast<EvaluateFrameWarpFn>(GetProcAddress(module,"EvaluateFrameWarp"));require(init&&buffers&&evaluate);
 ComPtr<ID3D12Device> device;check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
 DeviceParams dp{};dp.d3d12_device=device.Get();dp.d3d12_queue=queue.Get();auto renderer=init(dp);require(renderer);
 auto** table=*reinterpret_cast<void***>(renderer);auto create=reinterpret_cast<CreateTextureFn>(table[kCreateTextureSlot]);
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
 check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
 check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));check(list->Close());
 ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 value=0;
 auto buffer=[&](D3D12_HEAP_TYPE type,UINT64 n) {D3D12_HEAP_PROPERTIES h{};h.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=n;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;};
 constexpr int W=512,H=512;auto upload=buffer(D3D12_HEAP_TYPE_UPLOAD,W*H*4);auto readback=buffer(D3D12_HEAP_TYPE_READBACK,W*H*4);
 TextureDesc depth{},motion{};depth.type=Depth;require(create(renderer,W,H,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COMMON,depth,false));
 require(create(renderer,W,H,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_COMMON,motion,false));
 auto write=[&](TextureDesc t,bool color,unsigned eye,bool is_depth) {void* p{};check(upload->Map(0,nullptr,&p));
  for(int y=0;y<H;++y)for(int x=0;x<W;++x) {auto offset=(y*W+x)*4;if(color){auto* b=static_cast<unsigned char*>(p)+offset;const bool white=x>=(eye?231:257)&&x<(eye?235:261);b[0]=b[1]=b[2]=white?255:0;b[3]=255;}else {float v=is_depth?.5F:0.F;std::memcpy(static_cast<char*>(p)+offset,&v,4);}}
  upload->Unmap(0,nullptr);auto b=barrier(t.texture,t.initial_state,D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&b);
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=t.texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=upload.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint.Footprint={t.texture->GetDesc().Format,W,H,1,W*4};list->CopyTextureRegion(&dst,0,0,0,&source,nullptr);
  std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
  check(list->Close());ID3D12CommandList* ls[]{list.Get()};queue->ExecuteCommandLists(1,ls);check(queue->Signal(fence.Get(),++value));check(fence->SetEventOnCompletion(value,event));require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));};
 check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));write(depth,false,0,true);write(motion,false,0,false);check(list->Close());
 for(bool clear:{false,true}) {if(clear!=(std::string_view(argv[2])=="1"))continue;
 std::vector<double> old_result,new_result;
 for(bool fixed:{false,true}) {if(fixed!=(std::string_view(argv[3])=="1"))continue;FrameWarpInitParams bp{};bp.hmd_width=W;bp.hmd_height=H;auto eyes=buffers(bp);UINT64 pub=0,last=0;unsigned previous=2;std::array<UINT64,2> previous_color{};
  for(unsigned eye:{0U,1U,0U,0U,1U,0U}) {++pub;if(pub==4)++pub;check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));auto input=eyes.eye_frame_buffers[eye];input.depth=depth;input.motion_vectors=motion;write(input.color,true,eye,false);
   CameraData camera{};identity(camera.source_view_to_world);identity(camera.source_world_to_view);identity(camera.destination_view_to_world);identity(camera.destination_world_to_view);
   const float sx=eye?.01F:-.01F;camera.source_view_to_world.values[12]=sx;camera.source_world_to_view.values[12]=-sx;camera.destination_view_to_world.values[12]=-sx;camera.destination_world_to_view.values[12]=sx;
   for(auto* p:{&camera.source_view_to_clip,&camera.destination_view_to_clip}) {p->values[0]=p->values[5]=1;p->values[11]=-1;p->values[14]=.1F;}
   for(auto* p:{&camera.source_clip_to_view,&camera.destination_clip_to_view}) {p->values[0]=p->values[5]=1;p->values[11]=10;p->values[14]=-1;}
   identity(camera.camera_world_to_view);identity(camera.camera_view_to_world);identity(camera.camera_view_to_clip);identity(camera.camera_clip_to_view);
   FrameWarpEvaluateParams ep{};ep.command_list=list.Get();ep.input_eye_frame_buffer=&input;ep.eye_index=static_cast<EyeIndex>(eye);ep.mode=static_cast<FrameWarpMode>(fixed?xrfg::luke_afw_effective_mode(3,pub,last,eye,previous,previous_color[eye],clear):(clear?(!last||last+1!=pub||eye==previous?1:3):3));ep.camera_data=&camera;ep.motion_scale[0]=ep.motion_scale[1]=1;ep.is_hudless_color=true;ep.clear_before_warping=clear;evaluate(ep);require(ep.output_eye_frame_buffer&&ep.output_eye_frame_buffer->color.texture);
   auto t=ep.output_eye_frame_buffer->color;
   std::cout<<"same_input="<<(t.texture==input.color.texture)<<" same_peer="<<(t.texture==eyes.eye_frame_buffers[1-eye].color.texture)<<" ";
   auto b=barrier(t.texture,t.initial_state,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
   D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,W,H,1,W*4};D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=t.texture;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&source,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
   check(list->Close());ID3D12CommandList* ls[]{list.Get()};queue->ExecuteCommandLists(1,ls);check(queue->Signal(fence.Get(),++value));check(fence->SetEventOnCompletion(value,event));require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
   void* p{};check(readback->Map(0,nullptr,&p));double sum=0,weight=0;for(int y=128;y<384;++y)for(int x=0;x<W;++x) {auto v=static_cast<unsigned char*>(p)[(y*W+x)*4];sum+=x*v;weight+=v;}readback->Unmap(0,nullptr);double center=weight?sum/weight:-1;(fixed?new_result:old_result).push_back(center);std::cout<<fixed<<" eye="<<eye<<" pub="<<pub<<" mode="<<ep.mode<<" center="<<center<<" native="<<(eye?232.5:258.5)<<'\n';last=pub;previous=eye;previous_color[eye]=pub;
  }
 }
 // Clear mode is deliberately unchanged. Compare isolated readback receipts
 // with the exact V633 opposite-only policy, including its existing empty
 // transition output; do not turn that inherited limitation into a new claim.
 if(clear) {
  const auto& values=new_result.empty()?old_result:new_result;require(values.size()==6);
  std::uint32_t receipt=2166136261U;
  for(double center:values){receipt^=static_cast<std::uint32_t>(std::lround((center+2)*100));receipt*=16777619U;}
  std::cout<<"Clear mode readback receipt="<<receipt<<" (V633 compatibility, not a Clear-mode repair)\n";
  return static_cast<int>(0x60000000U|(receipt&0x0fffffffU));
 }
 if(!old_result.empty()){require(old_result.size()==6);require(std::abs(old_result[3]-258.5)<1.0);}
 if(!new_result.empty()){require(new_result.size()==6); // wrong eye at the exact repeated-source transition
 require(std::abs(new_result[3]-232.5)<=1.1); // current depth-warped opposite eye
 require(std::abs(new_result[4]-258.5)<1.0); // the other eye must also re-prime
 require(std::abs(new_result[5]-232.5)<1.0);} // normal temporal path resumes after both eyes
 std::cout<<"Real provider: repeated-eye 26px stereo collapse reproduced, corrected and resumed, clear="<<clear<<'\n';
 }
 CloseHandle(event);return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
