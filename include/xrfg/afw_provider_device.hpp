#pragma once
#include "xrfg/afw_provider_allocations.hpp"
namespace xrfg {
// Private provider device facade. Only factory results are observed; commands,
// settings and resource identities remain those of the real D3D12 device.
class AfwProviderDevice final : public ID3D12Device {
    ID3D12Device* actual_;
    AfwProviderAllocations& allocations_;
    std::atomic<ULONG> references_{1};
    HRESULT capture(HRESULT result,void** output) noexcept {
        if(FAILED(result)||!output||!*output)return result;
        ID3D12Object* object{};
        auto* created=reinterpret_cast<IUnknown*>(*output);
        if(FAILED(created->QueryInterface(IID_PPV_ARGS(&object))))return result;
        const auto observed=allocations_.observe(object);object->Release();
        if(FAILED(observed)){created->Release();*output=nullptr;return observed;}
        return result;
    }
public:
    AfwProviderDevice(ID3D12Device* actual,AfwProviderAllocations& allocations) noexcept
        :actual_(actual),allocations_(allocations) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output) override {
        if(!output)return E_POINTER;*output=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(ID3D12Object)||id==__uuidof(ID3D12Device)) {
            *output=static_cast<ID3D12Device*>(this);AddRef();return S_OK;
        }
        return actual_->QueryInterface(id,output);
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {return --references_;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return actual_->GetPrivateData(guid,pDataSize,pData);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return actual_->SetPrivateData(guid,DataSize,pData);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return actual_->SetPrivateDataInterface(guid,pData);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override {return actual_->SetName(Name);}
    UINT STDMETHODCALLTYPE GetNodeCount(void) override {return actual_->GetNodeCount();}
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) override {return capture(actual_->CreateCommandQueue(pDesc,riid,ppCommandQueue),ppCommandQueue);}
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) override {return capture(actual_->CreateCommandAllocator(type,riid,ppCommandAllocator),ppCommandAllocator);}
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override {return capture(actual_->CreateGraphicsPipelineState(pDesc,riid,ppPipelineState),ppPipelineState);}
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override {return capture(actual_->CreateComputePipelineState(pDesc,riid,ppPipelineState),ppPipelineState);}
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) override {return capture(actual_->CreateCommandList(nodeMask,type,pCommandAllocator,pInitialState,riid,ppCommandList),ppCommandList);}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override {return actual_->CheckFeatureSupport(Feature,pFeatureSupportData,FeatureSupportDataSize);}
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) override {return capture(actual_->CreateDescriptorHeap(pDescriptorHeapDesc,riid,ppvHeap),ppvHeap);}
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {return actual_->GetDescriptorHandleIncrementSize(DescriptorHeapType);}
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void **ppvRootSignature) override {return capture(actual_->CreateRootSignature(nodeMask,pBlobWithRootSignature,blobLengthInBytes,riid,ppvRootSignature),ppvRootSignature);}
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateConstantBufferView(pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateShaderResourceView(pResource,pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource *pResource, ID3D12Resource *pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateUnorderedAccessView(pResource,pCounterResource,pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateRenderTargetView(pResource,pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateDepthStencilView(pResource,pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {return actual_->CreateSampler(pDesc,DestDescriptor);}
    void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts, const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts, const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override {return actual_->CopyDescriptors(NumDestDescriptorRanges,pDestDescriptorRangeStarts,pDestDescriptorRangeSizes,NumSrcDescriptorRanges,pSrcDescriptorRangeStarts,pSrcDescriptorRangeSizes,DescriptorHeapsType);}
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override {return actual_->CopyDescriptorsSimple(NumDescriptors,DestDescriptorRangeStart,SrcDescriptorRangeStart,DescriptorHeapsType);}
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) override {return actual_->GetResourceAllocationInfo(visibleMask,numResourceDescs,pResourceDescs);}
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT nodeMask, D3D12_HEAP_TYPE heapType) override {return actual_->GetCustomHeapProperties(nodeMask,heapType);}
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource) override {return capture(actual_->CreateCommittedResource(pHeapProperties,HeapFlags,pDesc,InitialResourceState,pOptimizedClearValue,riidResource,ppvResource),ppvResource);}
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override {return capture(actual_->CreateHeap(pDesc,riid,ppvHeap),ppvHeap);}
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) override {return capture(actual_->CreatePlacedResource(pHeap,HeapOffset,pDesc,InitialState,pOptimizedClearValue,riid,ppvResource),ppvResource);}
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) override {return capture(actual_->CreateReservedResource(pDesc,InitialState,pOptimizedClearValue,riid,ppvResource),ppvResource);}
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes, DWORD Access, LPCWSTR Name, HANDLE *pHandle) override {return actual_->CreateSharedHandle(pObject,pAttributes,Access,Name,pHandle);}
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void **ppvObj) override {return capture(actual_->OpenSharedHandle(NTHandle,riid,ppvObj),ppvObj);}
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, /* [annotation][out] */ HANDLE *pNTHandle) override {return actual_->OpenSharedHandleByName(Name,Access,pNTHandle);}
    HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable *const *ppObjects) override {return actual_->MakeResident(NumObjects,ppObjects);}
    HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable *const *ppObjects) override {return actual_->Evict(NumObjects,ppObjects);}
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) override {return capture(actual_->CreateFence(InitialValue,Flags,riid,ppFence),ppFence);}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void) override {return actual_->GetDeviceRemovedReason();}
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override {return actual_->GetCopyableFootprints(pResourceDesc,FirstSubresource,NumSubresources,BaseOffset,pLayouts,pNumRows,pRowSizeInBytes,pTotalBytes);}
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override {return capture(actual_->CreateQueryHeap(pDesc,riid,ppvHeap),ppvHeap);}
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override {return actual_->SetStablePowerState(Enable);}
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature, REFIID riid, void **ppvCommandSignature) override {return capture(actual_->CreateCommandSignature(pDesc,pRootSignature,riid,ppvCommandSignature),ppvCommandSignature);}
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO *pPackedMipDesc, D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) override {return actual_->GetResourceTiling(pTiledResource,pNumTilesForEntireResource,pPackedMipDesc,pStandardTileShapeForNonPackedMips,pNumSubresourceTilings,FirstSubresourceTilingToGet,pSubresourceTilingsForNonPackedMips);}
    LUID STDMETHODCALLTYPE GetAdapterLuid(void) override {return actual_->GetAdapterLuid();}
};
} // namespace xrfg
