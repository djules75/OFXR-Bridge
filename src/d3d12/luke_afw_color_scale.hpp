#pragma once
#include "luke_afw_color_scale_shader.hpp"

namespace xrfg {
// Two descriptor pairs remain immutable until the existing AFW allocator fence
// retires. All images are private; no XR swapchain, wait or readback is added.
class AfwColorScale {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> expanded_;
    UINT increment_{};
    DXGI_FORMAT format_{};
public:
    ID3D12Resource* expanded() const noexcept {return expanded_.Get();}
    HRESULT prepare(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format) {
        if(expanded_)return S_OK;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
        ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable={2,ranges};
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants={0,0,4};
        D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=2;signature.pParameters=params;
        Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
        HRESULT result=D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
        if(FAILED(result))return result;
        result=device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_));
        if(FAILED(result))return result;
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root_.Get();
        ps.CS={g_xrfg_luke_afw_color_scale_shader,sizeof(g_xrfg_luke_afw_color_scale_shader)};
        result=device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline_));
        if(FAILED(result))return result;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=4;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        result=device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap_));if(FAILED(result))return result;
        increment_=device->GetDescriptorHandleIncrementSize(hd.Type);format_=format;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=width;rd.Height=height;
        rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Format=format;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&expanded_));
    }
    void record(ID3D12Device* device,ID3D12GraphicsCommandList* list,
        ID3D12Resource* source,D3D12_RESOURCE_STATES source_state,UINT sw,UINT sh,
        ID3D12Resource* destination,D3D12_RESOURCE_STATES destination_state,UINT dw,UINT dh,UINT pair) {
        auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(pair)*2*increment_;
        auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(pair)*2*increment_;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format_;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(source,&srv,cpu);cpu.ptr+=increment_;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format_;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(destination,nullptr,&uav,cpu);
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        barriers[0].Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        barriers[1].Transition={destination,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,destination_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        list->ResourceBarrier(2,barriers);
        ID3D12DescriptorHeap* heaps[]{heap_.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());
        list->SetComputeRootDescriptorTable(0,gpu);const UINT sizes[]{sw,sh,dw,dh};list->SetComputeRoot32BitConstants(1,4,sizes,0);
        list->Dispatch((dw+7)/8,(dh+7)/8,1);
        for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        list->ResourceBarrier(2,barriers);
    }
};
}
