// A host exe whose Agility SDK export names a folder that is not there, as
// Beat Saber's does: every D3D12CreateDevice in the process then fails with
// D3D12_ERROR_INVALID_REDIST, including the D3D11 bridge's own device. The
// bridge falls back to the system's D3D12 through a device factory.
#include "xrfg/d3d11_d3d12_interop.hpp"
#include "xrfg/d3d12_compat.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include "xrfg/d3d12_frame_synthesizer.hpp"
#include "xrfg/d3d12_history.hpp"

using Microsoft::WRL::ComPtr;

#ifdef XRFG_BROKEN_AGILITY
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 715;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\no-such-agility-sdk\\";
}
#endif

int main() {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return 1;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) == S_OK; ++index) {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) break;
    }
    if (!adapter) return 1;

    // The scenario: the host's configuration refuses a plain device.
    ComPtr<ID3D12Device> plain;
    const HRESULT plain_result = D3D12CreateDevice(
        adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(plain.GetAddressOf()));
    std::printf("D3D12CreateDevice: 0x%08lx\n", static_cast<unsigned long>(plain_result));
    #ifdef XRFG_BROKEN_AGILITY
    if (plain_result != static_cast<HRESULT>(0x887E0003L)) {
        std::printf("the host configuration did not reproduce the invalid redist\n");
        return 1;
    }

    #else
    if (FAILED(plain_result)) return 1;
    #endif

    // The bridge's device, on a D3D11 device of the same adapter.
    ComPtr<ID3D11Device> d3d11;
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, d3d11.GetAddressOf(), nullptr, nullptr))) {
        return 1;
    }
    ComPtr<ID3D12Device> bridge_device;
    ComPtr<ID3D12CommandQueue> bridge_queue;
    const HRESULT bridge_result = xrfg::create_d3d12_device_for_d3d11(
        d3d11.Get(), bridge_device.GetAddressOf(), bridge_queue.GetAddressOf());
    std::printf("create_d3d12_device_for_d3d11: 0x%08lx\n", static_cast<unsigned long>(bridge_result));
    if (FAILED(bridge_result) || !bridge_device || !bridge_queue) {
#ifdef XRFG_BROKEN_AGILITY
        ComPtr<ID3D12SDKConfiguration1> configuration;
        HRESULT capability=D3D12GetInterface(CLSID_D3D12SDKConfiguration,
            IID_PPV_ARGS(&configuration));
        if(SUCCEEDED(capability)) {
            HMODULE core=LoadLibraryExW(L"D3D12Core.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            const auto* version=core?reinterpret_cast<const UINT*>(GetProcAddress(core,"D3D12SDKVersion")):nullptr;
            const UINT system_version=version?*version:0;
            if(core)FreeLibrary(core);
            ComPtr<ID3D12DeviceFactory> probe;
            capability=system_version?configuration->CreateDeviceFactory(system_version,"",IID_PPV_ARGS(&probe)):E_NOINTERFACE;
        }
        if ((capability==E_NOINTERFACE || capability==E_NOTIMPL) &&
            (bridge_result==xrfg::kD3D12InvalidRedist || bridge_result==capability) &&
            !bridge_device && !bridge_queue) {
            std::printf("System device factory unavailable:0x%08lx; no partial device/queue and no host SDK override. Recovery GPU branch not exercised on this OS.\n",
                static_cast<unsigned long>(capability));
            ComPtr<ID3D12Device> unchanged;
            if(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&unchanged))!=plain_result) return 1;
            if(xrfg::global_d3d12_functions_usable())return 1;
            return 0;
        }
#endif
        std::printf("the bridge found no device under an invalid redist\n");
        return 1;
    }
    const LUID actual = bridge_device->GetAdapterLuid();
    DXGI_ADAPTER_DESC1 expected{}; adapter->GetDesc1(&expected);
    if (actual.HighPart != expected.AdapterLuid.HighPart || actual.LowPart != expected.AdapterLuid.LowPart) return 1;
    // Root signatures are refused the same way, and serialize on the device.
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues = 4;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 1;
    description.pParameters = &parameter;
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    const HRESULT serialize_result = xrfg::serialize_root_signature(
        bridge_device.Get(), description, blob.GetAddressOf(), error.GetAddressOf());
    ComPtr<ID3D12RootSignature> root_signature;
    if (FAILED(serialize_result) || !blob ||
        FAILED(bridge_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                  IID_PPV_ARGS(root_signature.GetAddressOf())))) {
        std::printf("no root signature under an invalid redist: 0x%08lx\n",
                    static_cast<unsigned long>(serialize_result));
        return 1;
    }
    if (xrfg::global_d3d12_functions_usable() ==
#ifdef XRFG_BROKEN_AGILITY
        true
#else
        false
#endif
    ) {
        std::printf("the global D3D12 functions were reported usable\n");
        return 1;
    }
    // Execute and read back real GPU work on the recovered same-adapter device.
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=td.Height=64;td.DepthOrArraySize=td.MipLevels=1;td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> color,synthetic;
    if(FAILED(bridge_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&color))) ||
       FAILED(bridge_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&synthetic)))) return 1;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> rtv;if(FAILED(bridge_device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return 1;
    bridge_device->CreateRenderTargetView(color.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size{};
    bridge_device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&size);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;
    bd.Height=bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES readheap{};readheap.Type=D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;if(FAILED(bridge_device->CreateCommittedResource(&readheap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return 1;
    ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
    if(FAILED(bridge_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))) ||
       FAILED(bridge_device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))))return 1;
    const float rgba[]{.5f,.25f,0.f,1.f};list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(),rgba,0,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={color.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(FAILED(list->Close()))return 1;ID3D12CommandList* lists[]{list.Get()};bridge_queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;if(FAILED(bridge_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) || FAILED(bridge_queue->Signal(fence.Get(),1)))return 1;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 1;
    if(FAILED(fence->SetEventOnCompletion(1,event))) {CloseHandle(event);return 1;}
    const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);if(wait!=WAIT_OBJECT_0)return 1;
    void* data{};D3D12_RANGE range{0,4};if(FAILED(readback->Map(0,&range,&data)))return 1;
    const auto* bytes=static_cast<const unsigned char*>(data);
    const bool pixels=bytes[0]>=127 && bytes[0]<=128 && bytes[1]==64 && bytes[2]==0 && bytes[3]==255;
    readback->Unmap(0,nullptr);if(!pixels)return 1;
    auto history=std::make_shared<xrfg::D3D12SwapchainHistory>();ID3D12Resource* inputs[]{color.Get()};ID3D12Resource* outputs[]{synthetic.Get()};
    if(FAILED(history->initialize(bridge_device.Get(),bridge_queue.Get(),inputs,D3D12_RESOURCE_STATE_RENDER_TARGET)))return 1;
    xrfg::D3D12FrameSynthesizer synthesis;
    const HRESULT optical=synthesis.initialize(bridge_device.Get(),bridge_queue.Get(),history,inputs,outputs,td.Format,D3D12_RESOURCE_STATE_RENDER_TARGET);
#ifdef XRFG_BROKEN_AGILITY
    if(optical!=xrfg::kD3D12InvalidRedist) {std::printf("FidelityFX was not precisely refused:0x%08lx\n",static_cast<unsigned long>(optical));return 1;}
#else
    if(FAILED(optical))return 1;
#endif
    std::printf("real GPU readback and precise FidelityFX backend guard passed\n");
    std::printf("invalid redist tests passed\n");
    return 0;
}
