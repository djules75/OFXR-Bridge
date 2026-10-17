#include "../src/layer/optiscaler_bootstrap.hpp"
#include <cstdlib>

namespace {
bool active{};
bool mode(const char* expected) {
    char value[64]{};
    GetEnvironmentVariableA("OFXR_BOOTSTRAP_TEST_MODE", value, sizeof(value));
    return std::strcmp(value, expected) == 0;
}
XrResult XRAPI_CALL gipa(XrInstance, const char*, PFN_xrVoidFunction*) {
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* layers, XrInstance* instance) {
    if (!layers || !layers->nextInfo ||
        std::strcmp(layers->nextInfo->layerName, xrfg::optiscaler_bootstrap::embedded_name) != 0)
        return XR_ERROR_INITIALIZATION_FAILED;
    auto downstream = *layers;
    downstream.nextInfo = layers->nextInfo->next;
    return layers->nextInfo->nextCreateApiLayerInstance(info, &downstream, instance);
}
}
extern "C" __declspec(dllexport) int OFXR_OptiScalerProviderV2(xrfg::optiscaler_bootstrap::Identity* identity) {
    *identity = {};
    identity->capabilities = mode("no-capability") ? 0 : 1;
    if (mode("incompatible")) identity->magic = 0;
    if (mode("bad-size")) identity->struct_size = 0;
    if (mode("bad-version")) identity->api_version = 1;
    if (mode("identity-seh")) RaiseException(0xE0421001, 0, 0, nullptr);
    return 1;
}
extern "C" __declspec(dllexport) int OFXR_SetEmbeddedLayerActiveV1(int value) {
    active = value != 0;
    if (mode("activation-seh") && value) RaiseException(0xE0421003, 0, 0, nullptr);
    return mode("activation-failure") && value ? 0 : 1;
}
extern "C" __declspec(dllexport) int OFXR_GetEmbeddedLayerActiveV1() { return active; }
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo*, const char* name, XrNegotiateApiLayerRequest* request) {
    if (std::strcmp(name, xrfg::optiscaler_bootstrap::embedded_name)) return XR_ERROR_INITIALIZATION_FAILED;
    if (mode("failure")) return XR_ERROR_INITIALIZATION_FAILED;
    if (mode("seh")) RaiseException(0xE0421002, 0, 0, nullptr);
    request->layerInterfaceVersion = mode("bad-interface") ? 2 : 1;
    request->layerApiVersion = XR_MAKE_VERSION(1, 0, 0);
    request->getInstanceProcAddr = gipa;
    request->createApiLayerInstance = mode("missing-callback") ? nullptr : create;
    if (mode("foreign-callback")) request->createApiLayerInstance =
        reinterpret_cast<PFN_xrCreateApiLayerInstance>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
    return XR_SUCCESS;
}
