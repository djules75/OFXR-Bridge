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
    // second x1000, b= repeats among them (a frame occupying more than one
    // scanout), c= how many distinct compositor frames the window counted.
    //
    // Counted from the per-frame stream, not from GetCumulativeStats. The
    // cumulative reprojected counter includes routine reprojection, so where
    // the compositor predicts ahead - MSFS 2024, flags 0x024 - it marks nearly
    // every frame and presents minus reprojected collapses to zero through a
    // healthy session.
    steamvr_delivery,
    // The vsync phase lock, once every few submissions while the rate is
    // right. result= the signed error against the held offset in ns, a= the
    // correction applied in ns (it always opposes the error, so its sign is
    // known), b= the offset being held, c= what the vsync read cost in
    // microseconds - it runs on the paced thread, so that is the number that
    // says whether it belongs there. An error that does not settle toward
    // zero means the grid is being dragged rather than held.
    presenter_vsync_lock,
    // Per submission: did the compositor put a settled frame on the vsync it
    // was predicted for. result= mispresented, a= our submission serial,
    // b= the compositor's frame index, c= presents in the low byte and frames
    // skipped since the last record above it. This is the attribution no
    // submission count can give. Read a frame that has actually been presented
    // - frames-ago zero is still in flight and reports zero for everything.
    presenter_frame_presented,
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
