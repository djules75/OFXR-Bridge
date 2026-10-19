// A stand-in nvngx_dlss.dll for xrfg_ngx_guide_capture_tests: exports the
// three D3D12 NGX entry points the capture detours, with bodies large enough
// to detour and distinct enough that the linker cannot fold them into one.
// It never touches the GPU; the test checks that the originals ran and that
// the capture published what the parameters carried. Built beside the test,
// never beside the tray.
// The parameter interface and the SDK types only: nvsdk_ngx.h declares the
// entry points with the SDK's own linkage, which this module defines itself.
#include <d3d11.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_params.h>

#include <atomic>
#include <cstdint>

namespace {

struct FakeHandle {
    unsigned id;
    NVSDK_NGX_Feature feature;
};

std::atomic<unsigned> g_next_handle{1};
std::atomic<unsigned> g_creations{0};
std::atomic<unsigned> g_evaluations{0};
std::atomic<unsigned> g_c_evaluations{0};
std::atomic<unsigned> g_releases{0};
std::atomic<unsigned> g_d3d11_evaluations{0};
std::atomic<unsigned> g_vulkan_evaluations{0};
std::atomic<std::uintptr_t> g_last_list{0};

}  // namespace

extern "C" {

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList* list,
    NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    if (handle == nullptr || parameters == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    unsigned width = 0;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Width, &width)) || width == 0) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    auto* fake = new FakeHandle{g_next_handle.fetch_add(1), feature};
    *handle = reinterpret_cast<NVSDK_NGX_Handle*>(fake);
    g_creations.fetch_add(1);
    g_last_list.store(reinterpret_cast<std::uintptr_t>(list));
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D12_EvaluateFeature(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    if (handle == nullptr || parameters == nullptr || list == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    ID3D12Resource* output = nullptr;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Output, &output)) ||
        output == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    if (callback != nullptr) {
        bool cancel = false;
        callback(1.0F, cancel);
    }
    g_evaluations.fetch_add(1);
    g_last_list.store(reinterpret_cast<std::uintptr_t>(list));
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D12_EvaluateFeature_C(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    if (handle == nullptr || parameters == nullptr || list == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    if (callback != nullptr) {
        bool cancel = false;
        callback(0.5F, &cancel);
    }
    g_c_evaluations.fetch_add(1);
    g_last_list.store(reinterpret_cast<std::uintptr_t>(list) + 1U);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* handle) {
    if (handle == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    delete reinterpret_cast<FakeHandle*>(handle);
    g_releases.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

// The D3D11 entry points (V512): the same stand-ins on a device context.
__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D11_CreateFeature(
    ID3D11DeviceContext* context,
    NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    if (handle == nullptr || parameters == nullptr || context == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    unsigned width = 0;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Width, &width)) || width == 0) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    auto* fake = new FakeHandle{g_next_handle.fetch_add(1) + 1000U, feature};
    *handle = reinterpret_cast<NVSDK_NGX_Handle*>(fake);
    g_creations.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D11_EvaluateFeature(
    ID3D11DeviceContext* context,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    if (handle == nullptr || parameters == nullptr || context == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    ID3D11Resource* output = nullptr;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Output, &output)) ||
        output == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    if (callback != nullptr) {
        bool cancel = false;
        callback(1.0F, cancel);
    }
    g_d3d11_evaluations.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_D3D11_ReleaseFeature(NVSDK_NGX_Handle* handle) {
    if (handle == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    delete reinterpret_cast<FakeHandle*>(handle);
    g_releases.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

// The Vulkan entry points (V513).
__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_VULKAN_CreateFeature(
    VkCommandBuffer buffer,
    NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    if (handle == nullptr || parameters == nullptr || buffer == VK_NULL_HANDLE) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    unsigned width = 0;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Width, &width)) || width == 0) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    auto* fake = new FakeHandle{g_next_handle.fetch_add(1) + 2000U, feature};
    *handle = reinterpret_cast<NVSDK_NGX_Handle*>(fake);
    g_creations.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_VULKAN_EvaluateFeature(
    VkCommandBuffer buffer,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    if (handle == nullptr || parameters == nullptr || buffer == VK_NULL_HANDLE) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    void* output = nullptr;
    if (!NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_Output, &output)) ||
        output == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    if (callback != nullptr) {
        bool cancel = false;
        callback(1.0F, cancel);
    }
    g_vulkan_evaluations.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) __declspec(noinline) NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_VULKAN_ReleaseFeature(NVSDK_NGX_Handle* handle) {
    if (handle == nullptr) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    delete reinterpret_cast<FakeHandle*>(handle);
    g_releases.fetch_add(1);
    return NVSDK_NGX_Result_Success;
}

// For the test: what the originals saw.
__declspec(dllexport) unsigned fake_ngx_creations() { return g_creations.load(); }
__declspec(dllexport) unsigned fake_ngx_evaluations() { return g_evaluations.load(); }
__declspec(dllexport) unsigned fake_ngx_c_evaluations() { return g_c_evaluations.load(); }
__declspec(dllexport) unsigned fake_ngx_releases() { return g_releases.load(); }
__declspec(dllexport) unsigned fake_ngx_d3d11_evaluations() { return g_d3d11_evaluations.load(); }
__declspec(dllexport) unsigned fake_ngx_vulkan_evaluations() { return g_vulkan_evaluations.load(); }

}  // extern "C"
