#pragma once

// Adapted from elliotttate/OFXR-Bridge 0c994d780a601f4f833cca323652aebbf5f61baa.
// Private bridge devices only: never change the host's global SDK selection.
#include <d3d12.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstring>
#include <initializer_list>

namespace xrfg {
inline constexpr HRESULT kD3D12InvalidRedist = static_cast<HRESULT>(0x887E0003L);

inline HRESULT create_d3d12_device_on_adapter(IDXGIAdapter* adapter,
                                             ID3D12Device** device) noexcept {
    if (!device) return E_POINTER;
    *device = nullptr;
    HRESULT result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device));
    if (result != kD3D12InvalidRedist) return result;
    HMODULE core = LoadLibraryExW(L"D3D12Core.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!core) return kD3D12InvalidRedist;
    const auto* version = reinterpret_cast<const UINT*>(GetProcAddress(core, "D3D12SDKVersion"));
    const UINT system_version = version ? *version : 0;
    FreeLibrary(core);
    if (!system_version) return kD3D12InvalidRedist;
    Microsoft::WRL::ComPtr<ID3D12SDKConfiguration1> configuration;
    result = D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&configuration));
    if (FAILED(result)) return kD3D12InvalidRedist;
    wchar_t directory[MAX_PATH]{};
    char path[MAX_PATH * 2]{};
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH || !WideCharToMultiByte(CP_UTF8, 0, directory, -1,
            path, static_cast<int>(sizeof(path)) - 2, nullptr, nullptr)) return E_FAIL;
    strcat_s(path, "\\");
    for (const char* candidate : {"", static_cast<const char*>(path)}) {
        Microsoft::WRL::ComPtr<ID3D12DeviceFactory> factory;
        result = configuration->CreateDeviceFactory(system_version, candidate, IID_PPV_ARGS(&factory));
        if (FAILED(result)) continue;
        result = factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device));
        if (SUCCEEDED(result)) return result;
    }
    return result;
}

inline bool global_d3d12_functions_usable() noexcept {
    static const bool usable = [] {
        D3D12_ROOT_SIGNATURE_DESC description{};
        Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
        return D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
            &blob, &error) != kD3D12InvalidRedist;
    }();
    return usable;
}

inline HRESULT serialize_root_signature(ID3D12Device* device,
    const D3D12_ROOT_SIGNATURE_DESC& description, ID3DBlob** blob, ID3DBlob** error) noexcept {
    const HRESULT result = D3D12SerializeRootSignature(&description,
        D3D_ROOT_SIGNATURE_VERSION_1, blob, error);
    if (result != kD3D12InvalidRedist || !device) return result;
    Microsoft::WRL::ComPtr<ID3D12DeviceConfiguration> configuration;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&configuration)))) return result;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    versioned.Desc_1_0 = description;
    return configuration->SerializeVersionedRootSignature(&versioned, blob, error);
}
} // namespace xrfg
