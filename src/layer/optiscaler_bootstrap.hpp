#pragma once

#include <windows.h>
#include <psapi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

// Consumer of the existing OptiScaler V2 identity, not a producer API export.
// Based on the preserved V232 bootstrap. Autonomous and the embedded layer have
// different loader names: adapt only our own nextInfo entry at instance creation.
namespace xrfg::optiscaler_bootstrap {
inline constexpr char embedded_name[] = "XR_APILAYER_XRFrameBridge_diagnostic";
inline constexpr char autonomous_name[] = "XR_APILAYER_OFXRAutonomous";
struct Identity {
    std::uint32_t struct_size{sizeof(Identity)};
    std::uint32_t api_version{2};
    std::uint64_t magic{0x4F4658524F505432ULL};
    std::uint64_t capabilities{};
};
using Provider = int (*)(Identity*);
using Activation = int (*)(int);
struct Exports {
    HMODULE module{};
    Activation activate{};
    PFN_xrNegotiateLoaderApiLayerInterface negotiate{};
};
inline std::mutex mutex;
inline bool decided{};
inline HMODULE retained_owner{}; // Retained for every returned callback's lifetime.
inline XrNegotiateApiLayerRequest delegated_request{};
inline std::atomic<PFN_xrCreateApiLayerInstance> delegated_create{};

inline FARPROC find_export(HMODULE module, const char* name) noexcept {
    __try { return GetProcAddress(module, name); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
inline bool owns(HMODULE expected, FARPROC address) noexcept {
    HMODULE actual{};
    return address && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &actual) && actual == expected;
}
inline bool query(Provider provider, Identity* identity) noexcept {
    __try { return provider(identity) != 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool activate(Activation function, int value) noexcept {
    __try { return function(value) != 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline XrResult negotiate_host(PFN_xrNegotiateLoaderApiLayerInterface function,
    const XrNegotiateLoaderInfo* loader, XrNegotiateApiLayerRequest* request) noexcept {
    __try { return function(loader, embedded_name, request); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return XR_ERROR_INITIALIZATION_FAILED; }
}
inline bool enumerate(HMODULE* modules, DWORD capacity, DWORD* bytes) noexcept {
    __try { return EnumProcessModules(GetCurrentProcess(), modules, capacity, bytes) != FALSE; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline std::optional<Exports> discover() noexcept {
    std::array<HMODULE, 1024> modules{};
    DWORD bytes{};
    if (!enumerate(modules.data(), sizeof(modules), &bytes) || bytes > sizeof(modules))
        return std::nullopt;
    std::optional<Exports> selected;
    for (std::size_t i = 0; i < bytes / sizeof(HMODULE); ++i) {
        const auto provider = reinterpret_cast<Provider>(find_export(modules[i], "OFXR_OptiScalerProviderV2"));
        HMODULE retained{};
        if (!provider || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(provider), &retained)) continue;
        const auto activation = reinterpret_cast<Activation>(find_export(retained, "OFXR_SetEmbeddedLayerActiveV1"));
        const auto negotiation = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
            find_export(retained, "xrNegotiateLoaderApiLayerInterface"));
        Identity identity{};
        const bool compatible = retained == modules[i] &&
            owns(retained, reinterpret_cast<FARPROC>(activation)) &&
            owns(retained, reinterpret_cast<FARPROC>(negotiation)) && query(provider, &identity) &&
            identity.struct_size >= sizeof(Identity) && identity.api_version == 2 &&
            identity.magic == 0x4F4658524F505432ULL && (identity.capabilities & 1ULL);
        if (!compatible) { FreeLibrary(retained); continue; }
        if (selected) { // Ambiguous owners must not receive a partial activation.
            FreeLibrary(retained); FreeLibrary(selected->module);
            return std::nullopt;
        }
        selected = Exports{retained, activation, negotiation};
    }
    return selected;
}

inline XRAPI_ATTR XrResult XRAPI_CALL create_instance(
    const XrInstanceCreateInfo* create_info, const XrApiLayerCreateInfo* layer_info, XrInstance* instance) {
    const auto create = delegated_create.load(std::memory_order_acquire);
    if (!create || !create_info || !layer_info || !instance ||
        layer_info->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
        layer_info->structVersion < XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
        layer_info->structSize < sizeof(XrApiLayerCreateInfo) || !layer_info->nextInfo ||
        layer_info->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
        layer_info->nextInfo->structVersion < XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
        layer_info->nextInfo->structSize < sizeof(XrApiLayerNextInfo) ||
        std::strncmp(layer_info->nextInfo->layerName, autonomous_name, XR_MAX_API_LAYER_NAME_SIZE) != 0 ||
        !layer_info->nextInfo->nextGetInstanceProcAddr || !layer_info->nextInfo->nextCreateApiLayerInstance)
        return XR_ERROR_INITIALIZATION_FAILED;
    auto next = *layer_info->nextInfo;
    std::memset(next.layerName, 0, sizeof(next.layerName));
    std::memcpy(next.layerName, embedded_name, sizeof(embedded_name));
    auto adapted = *layer_info;
    adapted.nextInfo = &next;
    // The embedded owner alone advances nextInfo and creates the XR instance.
    return create(create_info, &adapted, instance);
}

inline bool negotiate(const XrNegotiateLoaderInfo* loader, XrNegotiateApiLayerRequest* request) {
    std::scoped_lock lock(mutex);
    if (!decided) {
        decided = true; // Stable ownership across multiple instances in this process.
        if (auto host = discover()) {
            auto candidate = *request;
            const bool activated = activate(host->activate, 1);
            const auto result = activated ? negotiate_host(host->negotiate, loader, &candidate)
                                          : XR_ERROR_INITIALIZATION_FAILED;
            if (activated && XR_SUCCEEDED(result) &&
                candidate.layerInterfaceVersion == XR_CURRENT_LOADER_API_LAYER_VERSION &&
                candidate.layerApiVersion == XR_MAKE_VERSION(1, 0, 0) &&
                owns(host->module, reinterpret_cast<FARPROC>(candidate.getInstanceProcAddr)) &&
                owns(host->module, reinterpret_cast<FARPROC>(candidate.createApiLayerInstance))) {
                retained_owner = host->module;
                delegated_create.store(candidate.createApiLayerInstance, std::memory_order_release);
                candidate.createApiLayerInstance = create_instance;
                delegated_request = candidate;
                OutputDebugStringA("[OFXR V567] OptiScaler embedded owner; controls: OptiScaler in-game menu\n");
            } else {
                (void)activate(host->activate, 0);
                FreeLibrary(host->module);
            }
        }
    }
    if (!retained_owner) return false;
    *request = delegated_request;
    return true;
}
} // namespace xrfg::optiscaler_bootstrap
