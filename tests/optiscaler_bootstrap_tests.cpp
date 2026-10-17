#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <array>
#include <atomic>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
HMODULE owner(PFN_xrVoidFunction function) {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(function), &module);
    return module;
}
unsigned creates{};
const XrApiLayerNextInfo* expected_next{};
XrResult XRAPI_CALL runtime_create(const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* layers, XrInstance*) {
    require(info && layers && layers->nextInfo == expected_next, "chain advanced exactly once");
    ++creates;
    return XR_ERROR_RUNTIME_FAILURE; // Deliberate runtime refusal, not an adapter failure.
}
XrResult XRAPI_CALL runtime_gipa(XrInstance, const char*, PFN_xrVoidFunction* function) {
    if (function) *function = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
XrNegotiateApiLayerRequest request() {
    XrNegotiateApiLayerRequest r{};
    r.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    r.structVersion = XR_API_LAYER_INFO_STRUCT_VERSION; r.structSize = sizeof(r);
    return r;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 4, "layer host mode required");
        const std::string mode = argv[3];
        SetEnvironmentVariableA("OFXR_BOOTSTRAP_TEST_MODE", mode.c_str());
        const HMODULE host = LoadLibraryA(argv[2]);
        const HMODULE layer = LoadLibraryA(argv[1]);
        require(host && layer, "load actual DLLs");
        auto active = reinterpret_cast<int(*)()>(GetProcAddress(host, "OFXR_GetEmbeddedLayerActiveV1"));
        auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
            GetProcAddress(layer, "xrNegotiateLoaderApiLayerInterface"));
        require(active && negotiate, "exports exist");
        XrNegotiateLoaderInfo loader{};
        loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
        loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION; loader.structSize = sizeof(loader);
        loader.minInterfaceVersion = 1; loader.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        loader.minApiVersion = XR_MAKE_VERSION(1,0,0); loader.maxApiVersion = XR_CURRENT_API_VERSION;
        auto r = request();
        require(XR_FAILED(negotiate(&loader, "wrong-name", &r)), "invalid name refused before host");
        require(active() == 0, "invalid negotiation cannot activate host");
        if (mode == "excluded") {
            require(negotiate(&loader, "XR_APILAYER_OFXRAutonomous", &r) == XR_ERROR_INITIALIZATION_FAILED &&
                active() == 0 && !r.getInstanceProcAddr && !r.createApiLayerInstance,
                "exclusion wins over compatible embedded owner");
            std::cout << "OptiScaler bootstrap PASS: exclusion before activation\n";
            return 0;
        }
        require(negotiate(&loader, "XR_APILAYER_OFXRAutonomous", &r) == XR_SUCCESS, "valid negotiation");
        const bool delegated = mode == "success" || mode == "actual";
        require(active() == int(delegated), "host activation and rollback");
        require(owner(reinterpret_cast<PFN_xrVoidFunction>(r.getInstanceProcAddr)) == (delegated ? host : layer), "dispatch owner");
        std::array<std::thread, 8> callers;
        std::atomic_bool consistent{true};
        for (auto& t : callers) t = std::thread([&] {
            auto another = request();
            if (negotiate(&loader, "XR_APILAYER_OFXRAutonomous", &another) != XR_SUCCESS ||
                another.getInstanceProcAddr != r.getInstanceProcAddr ||
                another.createApiLayerInstance != r.createApiLayerInstance) consistent = false;
        });
        for (auto& t : callers) t.join();
        require(consistent, "concurrent negotiations retain one owner");
        if (delegated) {
            XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
            XrApiLayerNextInfo terminal{};
            expected_next = &terminal;
            XrApiLayerNextInfo next{};
            next.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
            next.structVersion = XR_API_LAYER_NEXT_INFO_STRUCT_VERSION; next.structSize = sizeof(next);
            std::strcpy(next.layerName, "XR_APILAYER_OFXRAutonomous");
            next.nextGetInstanceProcAddr = runtime_gipa; next.nextCreateApiLayerInstance = runtime_create;
            next.next = &terminal;
            XrApiLayerCreateInfo chain{};
            chain.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;
            chain.structVersion = XR_API_LAYER_CREATE_INFO_STRUCT_VERSION; chain.structSize = sizeof(chain);
            chain.nextInfo = &next;
            XrInstance instance{};
            require(r.createApiLayerInstance(&info, &chain, &instance) == XR_ERROR_RUNTIME_FAILURE, "real host reached downstream creation");
            require(creates == 1 && chain.nextInfo == &next && next.next == &terminal &&
                !std::strcmp(next.layerName, "XR_APILAYER_OFXRAutonomous"), "input chain unchanged");
            next.structSize = 0;
            require(r.createApiLayerInstance(&info, &chain, &instance) == XR_ERROR_INITIALIZATION_FAILED && creates == 1, "malformed chain refused");
            require(r.createApiLayerInstance(&info, nullptr, &instance) == XR_ERROR_INITIALIZATION_FAILED, "null chain refused");
            require(r.createApiLayerInstance(nullptr, &chain, &instance) == XR_ERROR_INITIALIZATION_FAILED, "null create refused");
            // Drop the caller's host reference: bootstrap must retain callbacks.
            FreeLibrary(host);
            PFN_xrVoidFunction ignored{};
            require(r.getInstanceProcAddr(XR_NULL_HANDLE, "xrUnknownBootstrapTest", &ignored) == XR_ERROR_FUNCTION_UNSUPPORTED,
                "retained host dispatch remains callable");
        }
        std::cout << "OptiScaler bootstrap PASS: " << mode << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
