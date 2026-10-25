#pragma once
#include <d3d12.h>

namespace DlssNr {
inline DXGI_FORMAT NrTypedGuideFormat(DXGI_FORMAT f) noexcept {
    switch (f) {
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
inline bool NrGuideShapeMatches(const D3D12_RESOURCE_DESC& snapshot,
                               const D3D12_RESOURCE_DESC& source) noexcept {
    return snapshot.Dimension == source.Dimension && snapshot.Width == source.Width &&
        snapshot.Height == source.Height && snapshot.DepthOrArraySize == source.DepthOrArraySize &&
        snapshot.MipLevels == source.MipLevels && snapshot.Format == NrTypedGuideFormat(source.Format) &&
        snapshot.SampleDesc.Count == source.SampleDesc.Count &&
        snapshot.SampleDesc.Quality == source.SampleDesc.Quality;
}
// Called only after this NR handle's exact recording/submission has retired.
// Both typed and typeless inputs become private, per-handle GPU snapshots.
// Copy and state restoration are on the caller's original command list.
template<class Transition, class Retire>
inline HRESULT SnapshotNrGuide(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                               ID3D12Resource* source, ID3D12Resource** snapshot,
                               Transition transition, Retire retire) {
    if (!device || !commands || !source || !snapshot) return E_POINTER;
    auto desc = source->GetDesc();
    if (*snapshot && !NrGuideShapeMatches((*snapshot)->GetDesc(), desc)) retire(*snapshot);
    if (!*snapshot) {
        desc.Format = NrTypedGuideFormat(desc.Format);
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(snapshot));
        if (FAILED(result)) return result;
    }
    transition(commands, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_SOURCE, L"guide-source-copy-begin");
    commands->CopyResource(*snapshot, source);
    transition(commands, source, D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"guide-source-copy-end");
    transition(commands, *snapshot, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"guide-clone-ready");
    return S_OK;
}
}
