#pragma once
#include <windows.h>
#include <algorithm>

namespace xrfg {
// REAL VR owns NGX dispatch exports and may call cached original trampolines.
// A basename lookup of NVIDIA libraries cannot observe those calls. Require
// the actual REAL VR marker and its own executable Create/Evaluate/Release
// exports; this is a generic dispatch, never evidence of an SR/RR feature.
inline bool luke_dispatch_module_valid(HMODULE module) noexcept {
    if(!module || !GetProcAddress(module,"RealVRVersion"))return false;
    for(const auto* name:{"NVSDK_NGX_D3D12_CreateFeature",
        "NVSDK_NGX_D3D12_EvaluateFeature","NVSDK_NGX_D3D12_ReleaseFeature"}) {
        const auto address=GetProcAddress(module,name);HMODULE owner{};
        if(!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),&owner) || owner!=module)return false;
    }
    return true;
}
inline HMODULE luke_dispatch_module() noexcept {
    for(const auto* name:{L"dxgi.dll",L"RealVR64.dll"}) {
        const auto module=GetModuleHandleW(name);
        if(luke_dispatch_module_valid(module))return module;
    }
    using Enum=BOOL(WINAPI*)(HANDLE,HMODULE*,DWORD,LPDWORD);
    const auto enumerate=reinterpret_cast<Enum>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32EnumProcessModules"));
    HMODULE modules[1024]{};DWORD bytes{};
    if(!enumerate || !enumerate(GetCurrentProcess(),modules,sizeof(modules),&bytes))return nullptr;
    for(unsigned i=0;i<std::min<DWORD>(bytes/sizeof(HMODULE),1024);++i)
        if(luke_dispatch_module_valid(modules[i]))return modules[i];
    return nullptr;
}
} // namespace xrfg
