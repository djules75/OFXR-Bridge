#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "xrfg/afw_provider_device.hpp"
using Microsoft::WRL::ComPtr;
struct DeviceParams {void* d11{};void* context{};ID3D12Device* device{};ID3D12CommandQueue* queue{};void* adapter{};};
struct InitParams {int width{},height{};DXGI_FORMAT eye{DXGI_FORMAT_R8G8B8A8_UNORM},back{DXGI_FORMAT_R8G8B8A8_UNORM};};
struct Texture {int type{};ID3D12Resource* texture{};int srv{},uav{};UINT64 sh{},uh{},rh{};D3D12_RESOURCE_STATES state{};};
struct Buffers {Texture textures[6];};
static_assert(sizeof(Texture)==56 && sizeof(Buffers)==336);
using DeviceFn=void*(__stdcall*)(DeviceParams);
using InitFn=Buffers(__stdcall*)(InitParams);
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void ok(HRESULT hr){if(FAILED(hr))throw std::runtime_error("GPU operation failed");}
int main(int argc,char**argv){try{
 if(argc!=2)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
 ComPtr<ID3D12Device> device;ok(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC desc{};ok(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
 ComPtr<IDXGIFactory4> factory;ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
 ComPtr<IDXGIAdapter3> adapter;ok(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
 ComPtr<ID3D12Fence> fence;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{ok(queue->Signal(fence.Get(),++serial));ok(fence->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");};
 auto sample=[&](int step,const char* phase){DXGI_QUERY_VIDEO_MEMORY_INFO info{};ok(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info));std::printf("%d,%s,%llu,%llu\n",step,phase,info.CurrentUsage,info.Budget);};
 sample(0,"baseline");DXGI_QUERY_VIDEO_MEMORY_INFO baseline{};ok(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&baseline));
 // A normally released factory result must not be released again; a foreign
 // AddRef remains valid after the single provider factory reference retires.
 for(bool foreign:{false,true}) {
  xrfg::AfwProviderAllocations observed;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=16;
  image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Resource* resource{};ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
  ok(observed.observe(resource));
  if(foreign){resource->AddRef();check(observed.retire()==1,"factory reference not retired");check(resource->GetDesc().Width==16,"foreign reference destroyed");check(observed.retire()==0,"factory reference retired twice");resource->Release();}
  else {resource->Release();check(observed.retire()==0,"already destroyed resource released twice");}
 }

 for(int i=1;i<=6;++i){
  const auto file=std::filesystem::current_path()/("full-retire-provider-"+std::to_string(i)+".dll");
  if(!CopyFileW(std::filesystem::path(argv[1]).c_str(),file.c_str(),TRUE))throw std::runtime_error("isolated copy failed");
  auto mod=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!mod)throw std::runtime_error("load failed");
  auto initDevice=reinterpret_cast<DeviceFn>(GetProcAddress(mod,"InitDevice"));auto init=reinterpret_cast<InitFn>(GetProcAddress(mod,"InitFrameWarp"));if(!initDevice||!init)throw std::runtime_error("ABI missing");
  xrfg::AfwProviderAllocations allocations;xrfg::AfwProviderDevice facade(device.Get(),allocations);
  if(!initDevice({nullptr,nullptr,&facade,queue.Get(),nullptr}))throw std::runtime_error("provider initialization failed");
  const auto buffers=init({2048,2048});if(!buffers.textures[0].texture)throw std::runtime_error("framebuffers missing");
  wait();sample(i,"initialized");
  if(!FreeLibrary(mod))throw std::runtime_error("unload failed");
  wait();check(allocations.retire()>17,"provider child allocations were not captured");Sleep(100);sample(i,"unloaded");
  DXGI_QUERY_VIDEO_MEMORY_INFO memory{};ok(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory));
  check(memory.CurrentUsage<=baseline.CurrentUsage+8*1024*1024,"AFW provider reset retained more than8MiB after completed GPU work and module unload");
  check(allocations.retire()==0,"provider factory retirement is not idempotent");
  if(!DeleteFileW(file.c_str()))throw std::runtime_error("owned copy deletion failed");
 }
 CloseHandle(event);std::puts("AFW6isolated2048x2048resets:memorybaseline, factory ownership and safe prior destruction PASS");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
