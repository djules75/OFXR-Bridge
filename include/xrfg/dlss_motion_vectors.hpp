#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <memory>

namespace xrfg {

// The camera a DLSS evaluation rendered (V515), from NVIDIA Streamline's
// per-viewport constants: the tangents of its image's edges - left and
// right where the image's x runs from its left to its right edge, up and
// down at its top and bottom row - and where the camera sits. MSFS 2024's
// foveated rendering evaluates DLSS once per view, four views for two eyes,
// none of them the size of an eye image; this is what places each in the
// eye it belongs to.
struct DlssViewGeometry {
    bool known{};
    std::uint32_t viewport{};
    float tan_left{};
    float tan_right{};
    float tan_up{};
    float tan_down{};
    std::array<float, 3> position{};
    std::array<float, 3> right{};
};

// Four tangents in one flight-log word (V515): left, right, up, down from
// the low bits, each a signed 16-bit count of 4096ths.
[[nodiscard]] inline std::uint64_t pack_view_tangents(
    float left, float right, float up, float down) noexcept {
    const auto field = [](float value) -> std::uint64_t {
        float scaled = value * 4096.0F;
        if (!(scaled > -32768.0F)) scaled = -32768.0F;  // and NaN
        if (scaled > 32767.0F) scaled = 32767.0F;
        return static_cast<std::uint64_t>(static_cast<std::uint16_t>(
            static_cast<std::int16_t>(scaled)));
    };
    return field(left) | (field(right) << 16U) | (field(up) << 32U) | (field(down) << 48U);
}

struct DlssMotionVectorPublication {
    std::uint64_t stream{};
    ID3D12Resource* output{};
    ID3D12Resource* motion_vectors{};
    ID3D12CommandQueue* producer_queue{};
    std::uint32_t output_x{};
    std::uint32_t output_y{};
    std::uint32_t output_width{};
    std::uint32_t output_height{};
    std::uint32_t motion_x{};
    std::uint32_t motion_y{};
    std::uint32_t motion_width{};
    std::uint32_t motion_height{};
    float scale_x{1.0F};
    float scale_y{1.0F};
    float jitter_x{};
    float jitter_y{};
    D3D12_RESOURCE_STATES resource_state{
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    bool jittered{};
    bool reset{};
    ID3D12GraphicsCommandList* producer_command_list{};
    // Optional frame-generation guides. The established DLSS-vector route
    // remains valid when these fields are absent.
    ID3D12Resource* depth{};
    std::uint32_t depth_x{};
    std::uint32_t depth_y{};
    std::uint32_t depth_width{};
    std::uint32_t depth_height{};
    D3D12_RESOURCE_STATES depth_resource_state{
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    float frame_time_delta_ms{16.6667F};
    float camera_near{0.1F};
    float camera_far{1000.0F};
    bool depth_inverted{};
    bool depth_infinite{};
    // A cooperating producer may supply the native D3D12 device after it has
    // unwrapped and identity-checked every object in the publication.
    ID3D12Device* verified_producer_device{};
    // V515: the evaluation's camera, where the producer knows it.
    DlssViewGeometry view{};
};

struct DlssMotionVectorFrame {
    std::uint64_t stream{};
    std::uint64_t epoch{};
    std::uint64_t serial{};
    std::uint64_t previous_serial{};
    std::uint64_t publication{};
    Microsoft::WRL::ComPtr<ID3D12Resource> output;
    Microsoft::WRL::ComPtr<ID3D12Resource> motion_vectors;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> producer_queue;
    std::uint32_t output_x{};
    std::uint32_t output_y{};
    std::uint32_t output_width{};
    std::uint32_t output_height{};
    std::uint32_t output_slice{};
    std::uint32_t motion_x{};
    std::uint32_t motion_y{};
    std::uint32_t motion_width{};
    std::uint32_t motion_height{};
    std::uint32_t motion_slice{};
    float scale_x{1.0F};
    float scale_y{1.0F};
    float jitter_x{};
    float jitter_y{};
    float previous_jitter_x{};
    float previous_jitter_y{};
    D3D12_RESOURCE_STATES resource_state{
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    bool jittered{};
    bool reset{};
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    std::uint32_t depth_x{};
    std::uint32_t depth_y{};
    std::uint32_t depth_width{};
    std::uint32_t depth_height{};
    D3D12_RESOURCE_STATES depth_resource_state{
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    float frame_time_delta_ms{16.6667F};
    float camera_near{0.1F};
    float camera_far{1000.0F};
    bool depth_inverted{};
    bool depth_infinite{};
    DlssViewGeometry view{};
    // V515: a guide placed by its camera rather than pixel for pixel. The
    // output rectangle is then the eye's image rectangle the guide covers,
    // and placed_motion_rect (x, y, width, height in vector texels, not
    // whole) the part of the vectors that lands on it: MSFS 2024's foveated
    // periphery spreads 892 render columns over a 5092-pixel eye.
    bool placed{};
    std::array<float, 4> placed_motion_rect{};
};

constexpr std::uint32_t kDlssMotionVectorEyeCount = 2;

// An eye's view in its XR image (V515): the tangents of its field of view
// (XrFovf) and its image rectangle.
struct DlssEyeView {
    float tan_left{};
    float tan_right{};
    float tan_up{};
    float tan_down{};
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct DlssMotionVectorSet {
    std::array<std::shared_ptr<const DlssMotionVectorFrame>,
               kDlssMotionVectorEyeCount>
        eyes;
    std::uint32_t eye_count{};
};

enum class DlssMotionVectorStatus : std::uint32_t {
    disabled,
    waiting_for_dlss,
    output_not_direct,
    queue_mismatch,
    invalid_input,
    temporal_mismatch,
    used,
};

struct DlssMotionVectorStatistics {
    std::uint64_t published{};
    std::uint64_t depth_publications{};
    std::uint64_t invalid_publications{};
    std::uint64_t snapshot_copies{};
    std::uint64_t snapshot_failures{};
    std::uint64_t resolve_calls{};
    std::uint64_t resolve_missing_streams{};
    std::uint64_t resolve_stale_pairs{};
    std::uint64_t resolve_queue_mismatches{};
    std::uint64_t matched{};
    std::uint64_t temporal_rejections{};
    std::uint64_t invalid_rejections{};
    std::uint64_t used{};
    // Pairs composed by the hybrid: vectors and optical flow both (V497).
    std::uint64_t hybrid_used{};
    // V515: eye pairs joined by their cameras, and attempts that found
    // cameras but no guide covering both eyes.
    std::uint64_t view_matched{};
    std::uint64_t view_unmatched{};
    std::uint64_t last_used_publication{};
    DlssMotionVectorStatus status{DlssMotionVectorStatus::waiting_for_dlss};
};

void publish_dlss_motion_vectors(
    const DlssMotionVectorPublication& publication) noexcept;
// Whether two ID3D12Device interfaces are the same device underneath. An
// interposer such as NVIDIA Streamline hands the game a proxy device and
// proxy queues while resources report the real device, so pointer identity
// says "different" for one and the same GPU device. Identical pointers are
// the same; otherwise, on one adapter, a private-data value set through one
// interface and read back through the other proves shared backing. Verified
// pairs are remembered; the check never guesses.
bool same_d3d12_device_backing(ID3D12Device* left, ID3D12Device* right) noexcept;
void configure_dlss_motion_vector_tracking(bool enabled) noexcept;
// Call only after all queue work that may reference published snapshots has
// completed. A live disable intentionally retains them until this safe point.
void retire_disabled_dlss_motion_vector_resources() noexcept;
void retire_dlss_motion_vector_stream(std::uint64_t stream) noexcept;
// One DLSS evaluation for both eyes side by side, each eye then in a
// swapchain of its own at that swapchain's origin (V513, No Man's Sky): the
// guide eye `eye` (0 left, 1 right) reads is the evaluation's half, placed at
// the view's origin - its output the view's size, its vector and depth
// rectangles the evaluation's matching halves. Null unless the evaluation
// spans exactly two views of view_width x view_height side by side.
std::shared_ptr<const DlssMotionVectorFrame> split_both_eyes_guide(
    const DlssMotionVectorFrame& frame,
    std::uint32_t view_width,
    std::uint32_t view_height,
    std::uint32_t eye) noexcept;
// The guides for an XR image. With an array image both eyes come as a
// stereo set. With a one-slice image (a swapchain per eye) and its eye (0 or
// 1; -1 unknown), the set holds that eye's own evaluation: chosen by the
// publication watermark at the eye's colour release (released_at, with the
// other eye's other_released_at), or without one by the order the stereo
// path gives two - two streams, or one stream alternating eyes (V497, from
// 0.3's R15).
std::shared_ptr<const DlssMotionVectorSet> resolve_dlss_motion_vectors(
    ID3D12Resource* output,
    ID3D12CommandQueue* consumer_queue,
    int eye = -1,
    std::uint64_t released_at = 0,
    std::uint64_t other_released_at = 0) noexcept;
// A guide placed into an eye's view by its camera (V515): the copy whose
// output rectangle is the view's rectangle and whose placed_motion_rect is
// the part of its vectors (and depth) that lands there. Null unless the
// guide's camera covers the view, to within a hundredth of its field.
std::shared_ptr<const DlssMotionVectorFrame> place_guide_in_view(
    const DlssMotionVectorFrame& frame,
    const DlssEyeView& view) noexcept;
// Both eyes' guides from evaluations whose cameras are known (V515), for a
// game whose DLSS outputs are views of their own rather than its eye images
// - MSFS 2024's foveated rendering: per eye a periphery over the whole field
// at a fraction of its pixels, and an inset. Each eye takes the recent
// stream whose camera covers its field and fits it best (the cameras'
// positions decide between two that fit alike), placed scaled; the
// evaluation is the stream's newest by the eye's release watermark. Both
// eyes or neither, from two streams.
std::array<std::shared_ptr<const DlssMotionVectorSet>, kDlssMotionVectorEyeCount>
resolve_view_placed_dlss_guides(
    ID3D12CommandQueue* consumer_queue,
    const std::array<DlssEyeView, kDlssMotionVectorEyeCount>& eyes,
    const std::array<std::uint64_t, kDlssMotionVectorEyeCount>& released_at) noexcept;
// The process-monotonic publication count: a release watermark.
std::uint64_t dlss_motion_vector_publications() noexcept;
void report_dlss_motion_vector_status(DlssMotionVectorStatus status) noexcept;
void report_dlss_motion_vector_use() noexcept;
// A pair the hybrid composition used: counted as used, and as hybrid.
void report_dlss_hybrid_use() noexcept;
DlssMotionVectorStatistics dlss_motion_vector_statistics() noexcept;

}  // namespace xrfg
