#pragma once

#include <memory>
#include <functional>
#include <cstdint>
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xrfg {
// Downstream functions are the REAL runtime, never the layer intercepts.
struct PrivateRuntimeDispatch {
    XrInstance instance{};
    XrSession session{};
    PFN_xrWaitFrame wait{};
    PFN_xrBeginFrame begin{};
    PFN_xrEndFrame end{};
    PFN_xrCreateSwapchain create{};
    PFN_xrDestroySwapchain destroy{};
    PFN_xrEnumerateSwapchainImages enumerate{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait_image{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrConvertTimeToWin32PerformanceCounterKHR to_qpc{};
};

// SteamVR's runtime binding sees only the consumer. All application/generator
// images are private, with leases (not copies) transferred at virtual EndFrame.
// Native D3D11 and D3D12 are supported; existing Vulkan/D3D11 translation binds
// D3D12 below this endpoint and therefore uses the identical D3D12 transport.
class PrivateOpenXrRuntime final {
public:
    struct Options {
        PrivateRuntimeDispatch real;
        ID3D12Device* device12{};
        ID3D12CommandQueue* queue12{}; // the actual runtime binding queue
        ID3D11Device* device11{};
        ID3D11DeviceContext* context11{};
        std::size_t capacity{3};
        std::function<std::int64_t()> physical_period;
        // stage: 1 accepted, 2 accepted by real runtime, -1 failed; sequence,
        // QPC ns and accumulated phase displacement (not a latency meter).
        // Stage3 instead carries submitted and raw predicted XR displayTime.
        std::function<void(int,std::uint64_t,std::int64_t,std::int64_t)> trace;
    };
    explicit PrivateOpenXrRuntime(Options);
    ~PrivateOpenXrRuntime();
    PrivateOpenXrRuntime(const PrivateOpenXrRuntime&)=delete;
    PrivateOpenXrRuntime& operator=(const PrivateOpenXrRuntime&)=delete;
    HRESULT initialize();
    XrResult start(); // after successful real xrBeginSession
    void stop(); // wakes blocked producers, joins consumer, retires GPU reads
    XrResult drain();
    XrResult wait_frame(const XrFrameWaitInfo*, XrFrameState*);
    XrResult predict_frame(XrFrameState*); // upper virtual application clock
    XrResult begin_frame(const XrFrameBeginInfo*);
    XrResult end_frame(const XrFrameEndInfo*);
    XrResult create_swapchain(const XrSwapchainCreateInfo*, XrSwapchain*,
        D3D12_RESOURCE_STATES image_state=D3D12_RESOURCE_STATE_RENDER_TARGET,
        std::uint32_t image_count=3);
    XrResult destroy_swapchain(XrSwapchain);
    XrResult enumerate_images(XrSwapchain,std::uint32_t,std::uint32_t*,XrSwapchainImageBaseHeader*);
    XrResult acquire_image(XrSwapchain,const XrSwapchainImageAcquireInfo*,std::uint32_t*);
    XrResult wait_image(XrSwapchain,const XrSwapchainImageWaitInfo*);
    XrResult release_image(XrSwapchain,const XrSwapchainImageReleaseInfo*);
    [[nodiscard]] bool owns(XrSwapchain) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xrfg
