#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace xrfg {

enum class BridgeFlightOperation : std::uint32_t {
    logger,
    negotiation,
    instance_create,
    instance_destroy,
    session_create,
    session_destroy,
    session_begin,
    session_end,
    application_wait_frame,
    application_begin_frame,
    application_end_frame,
    application_swapchain_acquire,
    application_swapchain_wait,
    application_swapchain_release,
    private_swapchain_acquire,
    private_swapchain_wait,
    private_swapchain_release,
    synthesis_initialize,
    synthesis_prime,
    synthesis_pair,
    downstream_first_end_frame,
    internal_wait_frame,
    internal_begin_frame,
    internal_end_frame,
    continuity_reset,
    gpu_drain,
    swapchain_create,
    swapchain_eligibility,
    projection_mapping,
    generation_prepare,
    session_binding,
    swapchain_image,
    d3d11_capture,
    d3d11_publish,
    runtime_identity,
    presenter_submission,
    presenter_transition,
    nvidia_gpu_stages,
    nvidia_gpu_total,
    // How long the presenter held itself back before starting a runtime frame
    // cycle, and how far it was from its own schedule when it did. Without
    // this the pace and the loop's other costs are indistinguishable inside
    // one gap between records.
    presenter_pace,
    synthesis_frame_start_wait,
    embedded_configuration,
    // When the GPU actually began and finished a pair's synthesis, on the
    // same timeline as every other record. Duration alone cannot say whether
    // the pixels existed when the presenter handed the frame over; only the
    // finish time against that submission can.
    synthesis_gpu_span,
    // The application asked for more frames than the presenter produced and
    // the virtual clock was held to the runtime's timeline instead of
    // stepping past it. a is how far the step overshot, in nanoseconds.
    virtual_clock_clamp,
    // A runtime session state transition, exactly as the application receives
    // it. result is the new XrSessionState; a is the session, b the event's
    // own time. A runtime that stops asking for frames says so here and
    // nowhere else the layer can see.
    session_state,
    // The once-per-pair hold releasing the application. result is the serial
    // surplus the release discarded - the presenter had run that many frames
    // beyond the two this hold is for, so the application had already missed
    // its slot and the hold throttled nothing. a is how long it waited in
    // microseconds, b the presenter's frame serial, c the serial the
    // application had been served to. Zero surplus is the hold working; a
    // surplus is the application running free, gated only by whatever it
    // blocks on next.
    presenter_pair_release,
    // How hard the runtime-entry gate is actually working on a D3D11 session.
    // result is the number of entries in this window that had to wait, a the
    // mean wait and b the longest, both nanoseconds, c the longest any single
    // entry held the gate. Zero contention across a session means the two
    // threads were never inside the runtime together, so the gate is not what
    // is protecting it - the earlier device-section attempt reported exactly
    // that and it was read as a clean result rather than as a refutation.
    runtime_entry_section,
    // Once per session: how the compositor interface was obtained, or why
    // it was not. result 0 with a=1 borrowed the process's existing OpenVR
    // context, a=2 opened a background one of its own; negative results are
    // the step that refused.
    steamvr_delivery_attach,
    // Once per closed window: a= distinct frames the headset received per
    // second x1000, b= the share of counted frames that carried nothing new
    // x1000, c= how many distinct compositor frames the window counted.
    steamvr_delivery,
    // Six times a second while a pair is being submitted: result 300, a= the
    // pair bias in ns, b= the synthetic's downstream call mean, c= the arrival
    // gap mean. The bias is judged by b falling toward the real frame's own
    // call cost - not by c, which shrinks by construction when the bias works.
    //
    // The vsync phase lock, once every few submissions while the rate is
    // right. result= the signed error against the held offset in ns, a= the
    // correction applied in ns (it always opposes the error, so its sign is
    // known), b= the offset being held, c= what the vsync read cost in
    // microseconds - it runs on the paced thread, so that is the number that
    // says whether it belongs there. An error that does not settle toward
    // zero means the grid is being dragged rather than held.
    presenter_vsync_lock,
    // Per submission: did the compositor put a settled frame on the vsync it
    // was predicted for, and what did it say it was doing. result= mispresented,
    // a= our submission serial. This is the attribution no submission count can
    // give. Read a frame that has actually been presented - frames-ago zero is
    // still in flight and reports zero for everything.
    //
    //   b  bits  0-31  the compositor's frame index
    //         bits 32-47  that frame's m_nReprojectionFlags
    //         bits 48-63  the OR of the flags across the whole window
    //   c  bits  0-7   presents
    //         bits  8-15  frames skipped since the record above it
    //         bits 16-39  m_flTotalRenderGpuMs in microseconds
    //         bits 40-63  m_flCompositorRenderGpuMs in microseconds
    //
    // Flags are 0x001 reprojected for a CPU reason, 0x002 for a GPU reason,
    // 0x004 async, 0x008 motion, 0xF0 frames predicted ahead. The windowed OR
    // is there because this is read once per submission while the window holds
    // several frames, so a reason raised on a frame we did not land on would
    // otherwise go unseen.
    presenter_frame_presented,
    // How the application negotiates Vulkan with the runtime, recorded before
    // the layer supports Vulkan so the interop can be designed against what
    // real titles do. result is a selector:
    //   1  xrCreateInstance: a= graphics extensions enabled, bit 0
    //      XR_KHR_vulkan_enable, 1 XR_KHR_vulkan_enable2, 2 D3D11, 3 D3D12,
    //      4 OpenGL
    //   2  xrGetVulkanInstanceExtensionsKHR returned its list: a= names,
    //      b= interop extension bits (below), c= string length
    //   3  xrGetVulkanDeviceExtensionsKHR, the same fields
    //   4  xrCreateVulkanInstanceKHR: a= extensions enabled, b= bits,
    //      c= XrResult in the high half, VkResult in the low
    //   5  xrCreateVulkanDeviceKHR: a= extensions enabled, b= bits,
    //      c= XrResult in the high half, VkResult in the low
    //   6  one queue family that device created: a= family, b= queue count
    //   7  xrGetVulkanGraphicsDevice(2)KHR: a= 1 or 2
    //   8  xrGetVulkanGraphicsRequirements(2)KHR: a= 1 or 2, b= minimum and
    //      c= maximum Vulkan version supported
    //   9  a Vulkan session: a= the VkDevice, b= queue family, c= queue index
    //  10  the layer appended to a list: a= 2 instance or 3 device list,
    //      b= the interop extension bits it added
    //  11  the session's device, asked which interop commands it exposes
    //  12  what the physical device supports; see
    //      probe_vulkan_interop_support for both
    // Interop extension bits: 0 VK_KHR_external_memory_win32,
    // 1 VK_KHR_external_semaphore_win32, 2 VK_KHR_external_memory,
    // 3 VK_KHR_external_semaphore, 4 VK_KHR_timeline_semaphore,
    // 5 VK_KHR_dedicated_allocation, 6 VK_KHR_get_memory_requirements2,
    // 7 VK_KHR_win32_keyed_mutex, 8 VK_KHR_external_memory_capabilities,
    // 9 VK_KHR_external_semaphore_capabilities,
    // 10 VK_KHR_get_physical_device_properties2.
    vulkan_negotiation,
    // The Vulkan interop's fence traffic, one record per step, so the two
    // APIs' progress can be read against each other. a= the shared fence's
    // completed value when the step ran, b= the value the step signals,
    // c= the value it waits for (0: none). result: 1 capture submitted on
    // the Vulkan queue, 2 the D3D12 history copy signalled, 3 the D3D12
    // queue told to wait for the last publish, 4 publish submitted on the
    // Vulkan queue.
    vulkan_interop,
};

struct BridgeFlightToken {
    std::uint64_t sequence{};
    std::int64_t start_counter{};
};

class BridgeFlightLogger final {
public:
    BridgeFlightLogger() noexcept;
    ~BridgeFlightLogger();

    BridgeFlightLogger(const BridgeFlightLogger&) = delete;
    BridgeFlightLogger& operator=(const BridgeFlightLogger&) = delete;

    void initialize(const std::filesystem::path& module_directory) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::filesystem::path log_path() const;

    // Places a QueryPerformanceCounter value on this log's own timeline,
    // so a GPU timestamp calibrated to QPC can be compared directly with
    // the ms column of every other record.
    [[nodiscard]] std::int64_t microseconds_for_counter(
        std::int64_t counter) const noexcept;

    [[nodiscard]] BridgeFlightToken begin(
        BridgeFlightOperation operation,
        std::uint64_t a = 0,
        std::uint64_t b = 0,
        std::uint64_t c = 0) noexcept;

    void end(
        BridgeFlightToken token,
        BridgeFlightOperation operation,
        std::int64_t result,
        std::uint64_t a = 0,
        std::uint64_t b = 0,
        std::uint64_t c = 0) noexcept;

    void event(
        BridgeFlightOperation operation,
        std::int64_t result = 0,
        std::uint64_t a = 0,
        std::uint64_t b = 0,
        std::uint64_t c = 0) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] BridgeFlightLogger& bridge_flight_logger() noexcept;
void initialize_bridge_flight_logger() noexcept;

} // namespace xrfg
