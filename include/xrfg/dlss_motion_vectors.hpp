#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <memory>

namespace xrfg {

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
};

constexpr std::uint32_t kDlssMotionVectorEyeCount = 2;

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
// The process-monotonic publication count: a release watermark.
std::uint64_t dlss_motion_vector_publications() noexcept;
void report_dlss_motion_vector_status(DlssMotionVectorStatus status) noexcept;
void report_dlss_motion_vector_use() noexcept;
// A pair the hybrid composition used: counted as used, and as hybrid.
void report_dlss_hybrid_use() noexcept;
DlssMotionVectorStatistics dlss_motion_vector_statistics() noexcept;

}  // namespace xrfg
