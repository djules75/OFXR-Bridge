#pragma once

#include <d3d11.h>
#include <d3d12.h>

#include "xrfg/vulkan_d3d12_interop.hpp"

#include <array>
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
    // D3D11 (V512): exports detoured, evaluations published, and the copy
    // into the shared textures failing or the context not the session's.
    std::uint64_t d3d11_modules_hooked{};
    std::uint64_t d3d11_publications{};
    std::uint64_t d3d11_transport_failures{};
    // V524: a D3D12 game in a bridged D3D11 session (R.E.A.L. VR's Cyberpunk
    // 2077): its D3D12 evaluations published, and that transport failing.
    std::uint64_t d3d12_bridged_publications{};
    std::uint64_t d3d12_bridged_transport_failures{};
    // Vulkan (V513): the same, and the copies dropped because the game never
    // submitted them.
    std::uint64_t vulkan_modules_hooked{};
    std::uint64_t vulkan_publications{};
    std::uint64_t vulkan_transport_failures{};
    std::uint64_t vulkan_dropped{};
    // The stage the last Vulkan start failed at (824 reason 7), 0 none.
    std::uint64_t vulkan_start_stage{};
    // V514: the Vulkan capture runs without the queue layer, publishing at
    // the bridged release.
    bool vulkan_untracked{};
    // V515: sl.interposer.dll's slSetConstants and slEvaluateFeature are
    // detoured, so publications made inside Streamline's DLSS evaluation
    // carry their viewport's camera; and how many did.
    bool streamline_hooked{};
    std::uint64_t publications_with_camera{};
    bool active{};
    bool d3d11_active{};
    bool vulkan_active{};
};

// Start (enabled) or pause capture for the session whose queue this is. The
// queue is the one the snapshots are published for, so the resolver accepts
// them: the game's own queue, as bound to the OpenXR session. Starts the
// module monitor on the first enable. Call outside the loader lock.
void configure_ngx_guide_capture(ID3D12CommandQueue* queue, bool enabled) noexcept;
// Pause capture if it is running for this queue. The hooks stay.
void stop_ngx_guide_capture(ID3D12CommandQueue* queue) noexcept;
// A bridged D3D11 session (V512): the game runs DLSS on its D3D11 device,
// the runtime and the synthesis on the layer's D3D12 device and queue. The
// D3D11 exports are detoured as the D3D12 ones are; at each evaluation the
// vectors (and the depth, where the driver shares its format) are copied
// into textures shared with that D3D12 device, ordered by shared fences on
// the GPU, and published on its queue. Pause by enabled = false. A game that
// renders in D3D12 behind a D3D11 session (V524, R.E.A.L. VR) is captured too:
// its D3D12 evaluations copy into textures shared with that D3D12 device, and
// their snapshot copies wait for the release, as the D3D11 ones do.
void configure_ngx_guide_capture_d3d11(
    ID3D11Device* game_device,
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    bool enabled) noexcept;
void stop_ngx_guide_capture_d3d11(ID3D12CommandQueue* queue) noexcept;
// A bridged Vulkan session (V513): the game runs DLSS on its Vulkan device.
// At each evaluation the vectors and depth are copied, inside the game's own
// command buffer, into textures shared with the layer's D3D12 device; the
// Vulkan queue layer signals a shared timeline right after the game submits
// that buffer, and the registry's snapshot copy waits on it.
void configure_ngx_guide_capture_vulkan(
    const VulkanSessionBinding& binding,
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    bool enabled) noexcept;
void stop_ngx_guide_capture_vulkan(ID3D12CommandQueue* queue) noexcept;
// Publishes the copies whose command buffer the game has submitted since,
// with their snapshot copies. A copy the game never submits is dropped when
// its slot comes round again. end_of_frame is kept for the call sites.
void flush_ngx_guide_capture_vulkan(bool end_of_frame) noexcept;
// At each image release of the bridged session, after the bridge's own wait
// for the game's context: submits the snapshot copies the evaluations since
// the last release recorded, so they never wait on the game mid-frame.
void flush_ngx_guide_capture_d3d11() noexcept;
// V524: the eyes of one DLSS stream rendered for both eyes in turn
// (configure_dlss_alternating_eyes; Luke Ross's R.E.A.L. VR). While enabled,
// each bridged D3D12 evaluation also copies 16 patches of its finished DLSS
// output into a four-deep history; at each release anchor_ngx_alternating_eyes
// measures the two eye images against the last four evaluations (record 851)
// and, until the stream is anchored, votes on it: five agreeing votes anchor
// the left eye to its evaluations' R.E.A.L. VR handle (V540, eye_tag), for
// the session, or without the handles to their serial parity, for the epoch.
void configure_ngx_alternating_eye_anchor(bool enabled) noexcept;
struct NgxEyeImage {
    // The layer's D3D12 image the eye was released in (a bridged session's
    // shared texture, simultaneous access), and the eye's rectangle on it.
    ID3D12Resource* image{};
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};
// At xrEndFrame's mapping, both eyes released with the same watermark, on
// the queue the bridged release flushed to. Reads a comparison queued at an
// earlier call once the GPU has run it, and queues the next one.
void anchor_ngx_alternating_eyes(
    const std::array<NgxEyeImage, 2>& eyes, std::uint64_t released_at) noexcept;
// Look for the NGX modules now rather than on the monitor's next tick, so a
// test need not wait for it.
void poll_ngx_guide_capture() noexcept;
NgxGuideCaptureStatistics ngx_guide_capture_statistics() noexcept;
// V521: how far an input rectangle starting at base (with its output at
// output_base, extents in their own texels) sits off its eye's render
// region, in render texels; 0 when on it or too far to be an offset.
long dlss_input_offset_within_region(
    unsigned base, unsigned output_base, unsigned extent, unsigned output_extent) noexcept;
// V551: the render and output sizes of a super-resolution feature registered
// from its evaluation, from the parameter object's Width/Height and
// OutWidth/OutHeight. An output smaller than the input on both axes is not an
// upscale, so the pair is the other way round - No Man's Sky's evaluation
// carries Width x Height = 6144 x 3264 (its side-by-side output) and
// OutWidth x OutHeight = 3564 x 1893 (its render size) - and is swapped.
struct LateFeatureSizes {
    unsigned width{}, height{}, out_width{}, out_height{};
    bool swapped{};
};
[[nodiscard]] LateFeatureSizes late_feature_sizes(
    unsigned width, unsigned height, unsigned out_width, unsigned out_height) noexcept;

}  // namespace xrfg
