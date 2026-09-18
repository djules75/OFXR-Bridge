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
    // The D3D11 interop's hold on the application's immediate context. Two
    // threads reach that context - the layer on the application's, and the
    // runtime on the presenter's whenever it reads a submitted image - and
    // per-call protection is not enough for a four-call sequence, so capture
    // and publish take the device section across the whole of theirs.
    //
    // result=0 is the one-off arming record: a says whether the section was
    // obtained at all, b the protection state the application had before the
    // layer turned it on. Without a=1 the hold is not there and a clean run
    // proves nothing.
    //
    // result=1 is a sequence that had to wait for the section, with a the
    // microseconds it blocked. Each one is a collision that used to run
    // concurrently.
    d3d11_context_section,
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
