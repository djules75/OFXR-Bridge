#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace xrfg {

// Where the compositor really is, which OpenXR does not report at all.
//
// Everything this layer records is written before xrEndFrame hands the frame
// over, so a session can submit 90 frames a second, space them correctly, aim
// them at 90 distinct slots and still put 45 in front of the user with every
// record reading healthy. SteamVR exposes the truth through OpenVR, and this is
// the one place that reads it.
//
// Fails open and permanently. Not SteamVR, openvr_api.dll missing, an interface
// refused, the counters describing a different process - any of them and every
// accessor returns nothing for the life of the session, leaving each caller
// with whatever it did before. Nothing here can fail a frame.
//
// Thread-safe: the overlay reads it from the application thread and the
// presenter from its own.
class SteamVrDelivery {
public:
    explicit SteamVrDelivery(bool steamvr_runtime) noexcept;
    ~SteamVrDelivery();
    SteamVrDelivery(const SteamVrDelivery&) = delete;
    SteamVrDelivery& operator=(const SteamVrDelivery&) = delete;

    // Distinct images the headset received per second, over a rolling window of
    // about a second. Empty until the first window closes. Attaches lazily on
    // the first call of any accessor, once.
    [[nodiscard]] std::optional<float> delivered_fps(std::int64_t now_ns) noexcept;

    // When the display last scanned out, converted into the layer's own clock,
    // plus the compositor's frame counter. This is the thing the presenter's
    // grid has never had: a relationship between its steady_clock schedule and
    // the display's. Empty when the runtime has no vsync times to give, which
    // it is explicitly allowed to do - callers must keep working without it.
    struct VsyncAnchor {
        std::chrono::steady_clock::time_point at{};
        std::uint64_t frame_counter{};
        // What the read cost. It happens on the paced presenter thread, so
        // this is the number that says whether it belongs there.
        std::chrono::microseconds cost{};
    };
    [[nodiscard]] std::optional<VsyncAnchor> vsync_anchor() noexcept;

    // Whether the compositor's most recent frame went out on the vsync it was
    // predicted for. mispresented > 0 is "it landed somewhere else", which is
    // per-frame attribution for a loss that submission counts cannot see.
    struct FramePresentation {
        std::uint32_t frame_index{};
        std::uint32_t mispresented{};
        std::uint32_t presents{};
        std::uint32_t dropped{};
        // Compositor frames between this one and the last reported. Non-zero
        // means the caller is sampling slower than the display, so some frames
        // were never examined - the attribution has holes rather than being
        // wrong.
        std::uint32_t skipped{};
        // The compositor's own account of what it did with this frame:
        // 0x001 reprojected for a CPU reason, 0x002 for a GPU reason, 0x004
        // async, 0x008 motion, and 0xF0 how many frames ahead it is predicting.
        //
        // Not a rejection reason - the API has none - but it is the only field
        // in which the compositor says why it compensated rather than showing
        // what it was handed. Read from the same struct everything else here
        // comes from, and discarded until now.
        std::uint32_t reprojection_flags{};
        // The OR of the flags across every compositor frame since the last
        // report, so a reason that appears on a frame this caller did not land
        // on is still seen. Sampling the newest settled frame alone misses
        // about half of them at this cadence.
        std::uint32_t reprojection_flags_window{};
        // What the compositor attributes to rendering this frame, in
        // microseconds: the application's total and the compositor's own. If it
        // claims a GPU reason while the machine has headroom, these say what it
        // is counting.
        std::uint32_t total_render_gpu_us{};
        std::uint32_t compositor_render_gpu_us{};
        // Margin in the units the compositor actually works in: whole scanouts.
        //
        // The millisecond fields beside these are all offsets from *this
        // frame's* SystemTimeInSeconds, so when a submission is attributed to a
        // different frame the origin moves with it and a margin computed from
        // them reads as a jump rather than a slide. These are counts, and a
        // count means the same thing on every machine.
        //
        // ready_vsyncs is how many scanouts ahead the frame was available;
        // vsyncs_to_first_view how many passed before it was first shown.
        std::uint32_t ready_vsyncs{};
        std::uint32_t vsyncs_to_first_view{};
    };
    [[nodiscard]] std::optional<FramePresentation> last_presentation() noexcept;

    // How long the compositor says is left in the frame it is currently
    // assembling. Sampled at the moment of submission it is that submission's
    // own margin, with no per-frame origin to get wrong. Empty when the
    // compositor is unavailable.
    //
    // "Due to running start, this value may roll over to the next frame before
    // ever reaching 0.0" - so a small value is close to the deadline, and a
    // large one may be either early or just past it.
    [[nodiscard]] std::optional<std::chrono::nanoseconds>
    frame_time_remaining() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xrfg
