#pragma once

#include <d3d12.h>

#include <cstdint>

// The game's own DLSS motion vectors, taken from its NGX calls (V497).
//
// The layer is loaded in the game's process. Once a D3D12 session asks for
// the game's vectors, a monitor looks every 50 ms for the NGX modules in the
// process - nvngx_dlss.dll (super resolution), nvngx_dlssd.dll (ray
// reconstruction) and _nvngx.dll (the driver's core, which the game's calls
// reach first) - and detours their CreateFeature, EvaluateFeature and
// ReleaseFeature exports in place (safetyhook). At every evaluation of a
// super-resolution or ray-reconstruction feature the game has already filled
// in its parameter object: the output, the motion vectors, their rectangles,
// scale and jitter. The capture reads those and publishes them through
// publish_dlss_motion_vectors, exactly as an OFXR-aware OptiScaler would,
// recording the vectors' snapshot copy into the game's own command list
// ahead of DLSS's work on it. Streamline titles arrive the same way: the
// interposer ends in the same exports. The vectors-only and hybrid
// compositions then pair the snapshots by stream, one stream per feature
// handle, so a game with one feature per eye publishes a stereo pair.
//
// Nested calls - the core forwarding to the feature module - are captured
// once, at the outermost hooked call, which is the one that holds the
// game's handle and parameters. Hooks are never removed: the trampolines
// and the hooked modules are pinned for the life of the process, so a late
// call from the game can never land on freed code. Disabling only stops the
// capture; the registry of published snapshots is the one the provider ABI
// uses, with its retirement rules.
namespace xrfg {

struct NgxGuideCaptureStatistics {
    // Modules whose exports are detoured: 0-3.
    std::uint64_t modules_hooked{};
    std::uint64_t modules_failed{};
    // Super-resolution and ray-reconstruction features the game created.
    std::uint64_t features_created{};
    std::uint64_t features_released{};
    // Evaluations of those features seen while capture was active.
    std::uint64_t evaluations{};
    // Of which were published; the rest lacked an output or a motion-vector
    // resource, or were nested in another hooked call.
    std::uint64_t publications{};
    std::uint64_t skipped_unknown_feature{};
    std::uint64_t skipped_missing_resources{};
    std::uint64_t skipped_nested{};
    // The motion vectors, the command list and the session's queue did not
    // prove to share one device.
    std::uint64_t skipped_device_mismatch{};
    bool active{};
};

// Start (enabled) or pause capture for the session whose queue this is. The
// queue is the one the snapshots are published for, so the resolver accepts
// them: the game's own queue, as bound to the OpenXR session. Starts the
// module monitor on the first enable. Call outside the loader lock.
void configure_ngx_guide_capture(ID3D12CommandQueue* queue, bool enabled) noexcept;
// Pause capture if it is running for this queue. The hooks stay.
void stop_ngx_guide_capture(ID3D12CommandQueue* queue) noexcept;
// Look for the NGX modules now rather than on the monitor's next tick, so a
// test need not wait for it.
void poll_ngx_guide_capture() noexcept;
NgxGuideCaptureStatistics ngx_guide_capture_statistics() noexcept;

}  // namespace xrfg
