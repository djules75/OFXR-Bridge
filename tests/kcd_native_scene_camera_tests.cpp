#define NOMINMAX
#define XRFG_NATIVE_SCENE_CAMERA_TESTS
#include "xrfg/native_ingress/LukeNativeSceneCamera.h"
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "fixtures/kcd_camera_packets.hpp"
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void check(HRESULT value,const char* message){require(SUCCEEDED(value),message);}
class CachedCameraResource:public ID3D12Resource {
    ComPtr<ID3D12Resource> native_;D3D12_GPU_VIRTUAL_ADDRESS cached_{};volatile LONG refs_{1};
public:
    virtual ~CachedCameraResource()=default;
    explicit CachedCameraResource(ID3D12Resource* resource):native_(resource),cached_(resource->GetGPUVirtualAddress()){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;*output=nullptr;
        if(iid==xrfg::d3d_native::streamline_native_guid)return native_->QueryInterface(__uuidof(ID3D12Resource),output);
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(ID3D12Object)&&iid!=__uuidof(ID3D12DeviceChild)&&
            iid!=__uuidof(ID3D12Pageable)&&iid!=__uuidof(ID3D12Resource))return E_NOINTERFACE;
        *output=static_cast<ID3D12Resource*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{const auto left=InterlockedDecrement(&refs_);if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid,UINT* size,void* data) override{return native_->GetPrivateData(guid,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid,UINT size,const void* data) override{return native_->SetPrivateData(guid,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,const IUnknown* data) override{return native_->SetPrivateDataInterface(guid,data);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override{return native_->SetName(name);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** device) override{return native_->GetDevice(iid,device);}
    HRESULT STDMETHODCALLTYPE Map(UINT subresource,const D3D12_RANGE* range,void** data) override{return native_->Map(subresource,range,data);}
    void STDMETHODCALLTYPE Unmap(UINT subresource,const D3D12_RANGE* range) override{native_->Unmap(subresource,range);}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override{return native_->GetDesc();}
    __declspec(noinline) D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override{return cached_;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT subresource,const D3D12_BOX* box,const void* data,UINT row,UINT depth) override{
        return native_->WriteToSubresource(subresource,box,data,row,depth);}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void* data,UINT row,UINT depth,UINT subresource,const D3D12_BOX* box) override{
        return native_->ReadFromSubresource(data,row,depth,subresource,box);}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS* flags) override{return native_->GetHeapProperties(heap,flags);}
};
class AlternateCameraResource final:public CachedCameraResource {
public:
    explicit AlternateCameraResource(ID3D12Resource* resource):CachedCameraResource(resource){}
    __declspec(noinline) D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override {
        return CachedCameraResource::GetGPUVirtualAddress();
    }
};
class CameraDeviceAlias final : public ID3D12Device {
public:
    explicit CameraDeviceAlias(ID3D12Device* native, bool exposes_native = false,
        bool exposes_public_video=false,ID3D12Device* video_owner=nullptr)
        : native_(native),video_owner_(video_owner?video_owner:native),
          exposes_native_(exposes_native),exposes_public_video_(exposes_public_video) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (exposes_native_ && iid == xrfg::d3d_native::streamline_native_guid)
            return native_->QueryInterface(__uuidof(ID3D12Device), object);
        if(exposes_public_video_ && iid==__uuidof(ID3D12VideoDevice2)) {
            ++public_video_queries;
            return video_owner_->QueryInterface(iid,object);
        }
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3D12Object) && iid != __uuidof(ID3D12Device))
            return E_NOINTERFACE;
        *object = static_cast<ID3D12Device*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto left = InterlockedDecrement(&references_); if (!left) delete this;
        return static_cast<ULONG>(left);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override { return native_->GetPrivateData(guid, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override { return native_->SetPrivateData(guid, size, data); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override { return native_->SetPrivateDataInterface(guid, data); }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override { return native_->SetName(name); }
    UINT STDMETHODCALLTYPE GetNodeCount() override { return native_->GetNodeCount(); }
    unsigned intercepted_queues{};
    unsigned public_video_queries{};
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* d, REFIID i, void** p) override { ++intercepted_queues;return native_->CreateCommandQueue(d, i, p); }
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE t, REFIID i, void** p) override { return native_->CreateCommandAllocator(t, i, p); }
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID i, void** p) override { return native_->CreateGraphicsPipelineState(d, i, p); }
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, REFIID i, void** p) override { return native_->CreateComputePipelineState(d, i, p); }
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT n, D3D12_COMMAND_LIST_TYPE t, ID3D12CommandAllocator* a, ID3D12PipelineState* s, REFIID i, void** p) override { return native_->CreateCommandList(n, t, a, s, i, p); }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE f, void* d, UINT n) override { return native_->CheckFeatureSupport(f, d, n); }
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* d, REFIID i, void** p) override { return native_->CreateDescriptorHeap(d, i, p); }
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE t) override { return native_->GetDescriptorHandleIncrementSize(t); }
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT n, const void* d, SIZE_T s, REFIID i, void** p) override { return native_->CreateRootSignature(n, d, s, i, p); }
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateConstantBufferView(d, h); }
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateShaderResourceView(r, d, h); }
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* r, ID3D12Resource* c, const D3D12_UNORDERED_ACCESS_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateUnorderedAccessView(r, c, d, h); }
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* r, const D3D12_RENDER_TARGET_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateRenderTargetView(r, d, h); }
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* r, const D3D12_DEPTH_STENCIL_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateDepthStencilView(r, d, h); }
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override { native_->CreateSampler(d, h); }
    void STDMETHODCALLTYPE CopyDescriptors(UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE* d, const UINT* ds, UINT ns, const D3D12_CPU_DESCRIPTOR_HANDLE* s, const UINT* ss, D3D12_DESCRIPTOR_HEAP_TYPE t) override { native_->CopyDescriptors(nd, d, ds, ns, s, ss, t); }
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT n, D3D12_CPU_DESCRIPTOR_HANDLE d, D3D12_CPU_DESCRIPTOR_HANDLE s, D3D12_DESCRIPTOR_HEAP_TYPE t) override { native_->CopyDescriptorsSimple(n, d, s, t); }
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT m, UINT n, const D3D12_RESOURCE_DESC* d) override { return native_->GetResourceAllocationInfo(m, n, d); }
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT n, D3D12_HEAP_TYPE t) override { return native_->GetCustomHeapProperties(n, t); }
    __declspec(noinline) HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* h, D3D12_HEAP_FLAGS f, const D3D12_RESOURCE_DESC* d, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* c, REFIID i, void** p) override {
        if(!d||d->Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||!p)return native_->CreateCommittedResource(h,f,d,s,c,i,p);
        ComPtr<ID3D12Resource> resource;const auto result=native_->CreateCommittedResource(h,f,d,s,c,IID_PPV_ARGS(resource.GetAddressOf()));
        if(FAILED(result))return result;auto* proxy=new CachedCameraResource(resource.Get());
        const auto queried=proxy->QueryInterface(i,p);proxy->Release();return queried;
    }
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* d, REFIID i, void** p) override { return native_->CreateHeap(d, i, p); }
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* h, UINT64 o, const D3D12_RESOURCE_DESC* d, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* c, REFIID i, void** p) override { return native_->CreatePlacedResource(h, o, d, s, c, i, p); }
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* d, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* c, REFIID i, void** p) override { return native_->CreateReservedResource(d, s, c, i, p); }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* o, const SECURITY_ATTRIBUTES* a, DWORD d, LPCWSTR n, HANDLE* h) override { return native_->CreateSharedHandle(o, a, d, n, h); }
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE h, REFIID i, void** p) override { return native_->OpenSharedHandle(h, i, p); }
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR n, DWORD d, HANDLE* h) override { return native_->OpenSharedHandleByName(n, d, h); }
    HRESULT STDMETHODCALLTYPE MakeResident(UINT n, ID3D12Pageable* const* p) override { return native_->MakeResident(n, p); }
    HRESULT STDMETHODCALLTYPE Evict(UINT n, ID3D12Pageable* const* p) override { return native_->Evict(n, p); }
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 v, D3D12_FENCE_FLAGS f, REFIID i, void** p) override { return native_->CreateFence(v, f, i, p); }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return native_->GetDeviceRemovedReason(); }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* d, UINT f, UINT n, UINT64 b, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* l, UINT* r, UINT64* s, UINT64* t) override { native_->GetCopyableFootprints(d, f, n, b, l, r, s, t); }
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* d, REFIID i, void** p) override { return native_->CreateQueryHeap(d, i, p); }
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL e) override { return native_->SetStablePowerState(e); }
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* d, ID3D12RootSignature* s, REFIID i, void** p) override { return native_->CreateCommandSignature(d, s, i, p); }
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* r, UINT* n, D3D12_PACKED_MIP_INFO* m, D3D12_TILE_SHAPE* s, UINT* t, UINT f, D3D12_SUBRESOURCE_TILING* d) override { native_->GetResourceTiling(r, n, m, s, t, f, d); }
    LUID STDMETHODCALLTYPE GetAdapterLuid() override { return native_->GetAdapterLuid(); }
private:
    ComPtr<ID3D12Device> native_;
    ComPtr<ID3D12Device> video_owner_;
    bool exposes_native_{};
    bool exposes_public_video_{};
    volatile LONG references_{1};
};

int main(){try{
    std::array<unsigned char,kcd_camera::packet_bytes> first{},second{};kcd_camera::Camera expected{},other_camera{};
    first=kcd_fixture::packet(0);
    require(kcd_camera::decode(first.data(),first.size(),1280,720,expected),"fixture camera");
    bool changed=false;
    for(unsigned index=1;index<kcd_fixture::count;++index) {
        second=kcd_fixture::packet(index);
        if(kcd_camera::decode(second.data(),second.size(),1280,720,other_camera)&&!kcd_camera::equivalent(expected,other_camera)){changed=true;break;}
    }
    require(changed,"fixture has no changing camera");
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> device;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");
    check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<CameraDeviceAlias> reported;reported.Attach(new CameraDeviceAlias(device.Get(),true));
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=2048;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> source;check(reported->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&source)),"upload");
    void* cpu=nullptr;D3D12_RANGE empty{};check(source->Map(0,&empty,&cpu),"pre-hook persistent Map");
    std::memcpy(cpu,first.data(),first.size());std::memcpy(static_cast<unsigned char*>(cpu)+1024,second.data(),second.size());
    const auto unknown=source->GetGPUVirtualAddress();
    // A failed last entry leaves address/Map trampolines dormant. An unrelated
    // proxy family then shares and activates Map; its callback must keep the
    // original assigned to the reserved first index, not a rebound function.
    auto** source_table=*reinterpret_cast<void***>(source.Get());
    luke_scene_camera::detail::denied_entry=source_table[2];luke_scene_camera::detail::denied_entry_count=1;
    require(!luke_scene_camera::detail::install_resource_methods(source.Get()),"partial resource install did not fail");
    const auto parked_address=luke_scene_camera::detail::resource_methods[0].address.load();
    IUnknown* borrowed=nullptr;require(xrfg::d3d_native::get_borrowed(source.Get(),&borrowed),"proxy native contract");
    ComPtr<ID3D12Resource> native_source;check(borrowed->QueryInterface(IID_PPV_ARGS(native_source.GetAddressOf())),"native resource");
    ComPtr<ID3D12Resource> alternate;alternate.Attach(new AlternateCameraResource(native_source.Get()));
    require(luke_scene_camera::detail::install_resource_methods(alternate.Get())&&
        luke_scene_camera::detail::resource_methods[0].address.load()==parked_address,
        "independent method family rebound a dormant original");
    void* family_map=nullptr;check(alternate->Map(0,&empty,&family_map),"shared-entry original Map");
    require(family_map==cpu&&source->GetGPUVirtualAddress()==unknown&&alternate->GetGPUVirtualAddress()==unknown,
        "partial/independent family altered original Map or cached addresses");alternate->Unmap(0,&empty);
    alternate.Reset();native_source.Reset();
    require(luke_scene_camera::install(reported.Get())&&luke_scene_camera::install(reported.Get())&&
        luke_scene_camera::detail::resource_family_count.load()>=2,"native/proxy method family install");
    require(luke_scene_camera::detail::resource_methods[0].active.load()&&
        luke_scene_camera::detail::resource_methods[0].address.load()==parked_address,
        "failed resource family was not retried with its immutable originals");
    ComPtr<ID3D12CommandAllocator> allocator,other_allocator;ComPtr<ID3D12GraphicsCommandList> list,other;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&other_allocator)),"other allocator");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,other_allocator.Get(),nullptr,IID_PPV_ARGS(&other)),"other list");
    D3D12_ROOT_PARAMETER params[3]{};for(unsigned i=0;i<3;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[i].Descriptor.ShaderRegister=i;}
    D3D12_ROOT_SIGNATURE_DESC sd{};sd.NumParameters=3;sd.pParameters=params;
    ComPtr<ID3DBlob> serialized,errors;ComPtr<ID3D12RootSignature> signature,different;
    check(D3D12SerializeRootSignature(&sd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"serialize");
    check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)),"root signature");
    params[0].Descriptor.RegisterSpace=1;check(D3D12SerializeRootSignature(&sd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"other serialize");
    check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&different)),"other signature");
    list->SetGraphicsRootSignature(signature.Get());list->SetGraphicsRootConstantBufferView(1,unknown);
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"unknown range guessed");
    // The application cached this VA and first Map before hook installation.
    // A later successful Map is itself an exact live-resource receipt: it must
    // recover the range without any subsequent game GetGPUVirtualAddress call.
    const auto prior_maps=luke_scene_camera::detail::observed_maps;
    void* remapped=nullptr;check(source->Map(0,&empty,&remapped),"cached-address successful Map");
    require(remapped==cpu,"Map observer altered returned mapped memory");source->Unmap(0,&empty);
    list->SetGraphicsRootConstantBufferView(1,unknown);
    const auto recovered=luke_scene_camera::take(list.Get(),1280,720);
    require(recovered.valid&&recovered.reads==1&&recovered.unknown_ranges==0&&
        luke_scene_camera::detail::observed_maps>prior_maps,"cached pre-hook VA was not recovered by successful Map");
    // A successful original proxy creation and its native allocation are both
    // observed before the caller asks either object for its address or maps it.
    const auto prior_creations=luke_scene_camera::detail::observed_creations;
    ComPtr<ID3D12Resource> created;
    check(reported->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
        IID_PPV_ARGS(&created)),"observed proxy buffer creation");
    require(luke_scene_camera::detail::resources.contains(created.Get())&&
        luke_scene_camera::detail::observed_creations>=prior_creations+2,"actual native/proxy creation receipts missing");
    const auto created_address=luke_scene_camera::detail::resources.at(created.Get()).gpu;
    void* created_bytes=nullptr;check(created->Map(0,&empty,&created_bytes),"created buffer Map");
    std::memcpy(created_bytes,first.data(),first.size());created->Unmap(0,&empty);
    list->SetGraphicsRootConstantBufferView(1,created_address);
    require(luke_scene_camera::take(list.Get(),1280,720).valid,"created camera resource was not exact");
    const auto created_id=created.Get();created.Reset();
    require(!luke_scene_camera::detail::resources.contains(created_id)&&
        !luke_scene_camera::take(list.Get(),1280,720).valid,"final proxy/native release retained its camera range");
    const auto gpu=source->GetGPUVirtualAddress();list->SetGraphicsRootConstantBufferView(1,gpu);
    require(!luke_scene_camera::take(other.Get(),1280,720).valid,"foreign list borrowed camera");
    // Modify mapped bytes after binding: read at production, not at old binding.
    std::memcpy(cpu,second.data(),second.size());
    auto saved=luke_scene_camera::take(list.Get(),1280,720);
    require(saved.valid&&saved.reads==1&&saved.candidates==1&&saved.graphics&&saved.slot==1,"current camera missing");
    require(kcd_camera::equivalent(saved.camera,other_camera)&&!kcd_camera::equivalent(saved.camera,expected),"binding-time camera reused");
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"preceding production reused");
    std::memcpy(cpu,first.data(),first.size());require(kcd_camera::equivalent(saved.camera,other_camera),"borrowed bytes escaped");
    list->SetGraphicsRootConstantBufferView(1,gpu);list->SetGraphicsRootSignature(different.Get());
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"signature change retained stale camera");
    list->SetGraphicsRootSignature(signature.Get());list->SetGraphicsRootConstantBufferView(1,gpu);list->SetGraphicsRootConstantBufferView(2,gpu+1024);
    const auto ambiguous=luke_scene_camera::take(list.Get(),1280,720);
    require(!ambiguous.valid&&ambiguous.candidates==2&&(ambiguous.failure&128),"ambiguous camera selected");
    list->SetGraphicsRootConstantBufferView(1,gpu+1792);
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"short range guessed");
    auto default_heap=heap;default_heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> resident;
    check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resident)),"DEFAULT");
    list->SetGraphicsRootConstantBufferView(1,resident->GetGPUVirtualAddress());const auto no_cpu=luke_scene_camera::take(list.Get(),1280,720);
    require(!no_cpu.valid&&(no_cpu.failure&16)&&FAILED(no_cpu.error),"non-CPU buffer synthesized camera");
    require(device->GetDeviceRemovedReason()==S_OK,"Map failure removed device");
    // A real dispatch still consumes the original root address/data after take.
    D3D12_ROOT_PARAMETER compute_params[2]{};compute_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    compute_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;sd.NumParameters=2;sd.pParameters=compute_params;
    check(D3D12SerializeRootSignature(&sd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"compute serialize");
    ComPtr<ID3D12RootSignature> compute_signature;check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&compute_signature)),"compute root");
    const char shader[]="cbuffer C:register(b0){uint4 c;} RWStructuredBuffer<uint> o:register(u0);[numthreads(1,1,1)]void main(){o[0]=c.x;}";
    ComPtr<ID3DBlob> compiled;check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&compiled,&errors),"shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=compute_signature.Get();pd.CS={compiled->GetBufferPointer(),compiled->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)),"pipeline");
    desc.Width=256;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> output,readback;
    check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)),"output");
    auto readback_heap=heap;readback_heap.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    check(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
    list->SetPipelineState(pipeline.Get());list->SetComputeRootSignature(compute_signature.Get());
    list->SetComputeRootUnorderedAccessView(0,output->GetGPUVirtualAddress());list->SetComputeRootConstantBufferView(1,gpu);
    require(luke_scene_camera::take(list.Get(),1280,720).valid,"compute scene camera");list->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=output.Get();
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1,&barrier);list->CopyBufferRegion(readback.Get(),0,output.Get(),0,4);check(list->Close(),"Close");
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    ID3D12CommandList* submitted[]{list.Get()};queue->ExecuteCommandLists(1,submitted);check(queue->Signal(fence.Get(),1),"signal");
    HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(done!=nullptr,"event");check(fence->SetEventOnCompletion(1,done),"notify");
    const auto waited=WaitForSingleObject(done,10000);CloseHandle(done);require(waited==WAIT_OBJECT_0,"GPU timeout");
    void* actual=nullptr;D3D12_RANGE read_range{0,4};check(readback->Map(0,&read_range,&actual),"readback Map");
    std::uint32_t wanted{};std::memcpy(&wanted,first.data(),4);require(*static_cast<std::uint32_t*>(actual)==wanted,"camera capture changed GPU constants");readback->Unmap(0,&empty);
    check(allocator->Reset(),"allocator Reset");check(list->Reset(allocator.Get(),nullptr),"Reset");
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"Reset retained camera");
    list->SetGraphicsRootSignature(signature.Get());list->SetGraphicsRootConstantBufferView(1,gpu);source->Unmap(0,&empty);source.Reset();
    require(!luke_scene_camera::take(list.Get(),1280,720).valid,"released resource retained camera");
    check(list->Close(),"final Close");check(other->Close(),"other Close");auto* list_id=list.Get();list.Reset();
    require(!luke_scene_camera::detail::roots.contains(list_id),"released list retained context");
    require(luke_scene_camera::uninstall(),"detach");require(!luke_scene_camera::take(nullptr,1280,720).valid,"inactive camera admitted");
    std::cout<<"Current scene camera: real GPU consumer, cached pre-hook VA recovered by successful Map, native/proxy creation originals and lifetime, exact-list/once/signature/ambiguity/bounds/Map/Reset/Release/detach PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
