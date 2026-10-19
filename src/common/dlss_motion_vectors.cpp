#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/bridge_flight_logger.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrfg {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint64_t kMaximumStereoPublicationGap = 4;
constexpr std::size_t kSnapshotSlotCount = 4;
constexpr D3D12_RESOURCE_STATES kSnapshotReadState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

struct SnapshotSlot {
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_DESC description{};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
};

struct StreamState {
    std::uint64_t epoch{1};
    std::uint64_t serial{};
    std::uint64_t first_publication{};
    float jitter_x{};
    float jitter_y{};
    std::shared_ptr<const DlssMotionVectorFrame> previous;
    std::shared_ptr<const DlssMotionVectorFrame> latest;
    std::array<SnapshotSlot, kSnapshotSlotCount> snapshots;
    std::size_t next_snapshot{};
    std::array<SnapshotSlot, kSnapshotSlotCount> depth_snapshots;
    std::size_t next_depth_snapshot{};
};

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, StreamState> streams;
    DlssMotionVectorStatistics statistics;
};

Registry& registry() {
    static Registry value;
    return value;
}

std::atomic_bool g_tracking_enabled{false};

ComPtr<IUnknown> identity(IUnknown* object) noexcept {
    ComPtr<IUnknown> value;
    if (object != nullptr) {
        static_cast<void>(object->QueryInterface(IID_PPV_ARGS(value.GetAddressOf())));
    }
    return value;
}

bool same_identity(IUnknown* left, IUnknown* right) noexcept {
    const auto a = identity(left);
    const auto b = identity(right);
    return a && b && a.Get() == b.Get();
}

// Reserved for the short, serialized backing challenge; never frame pixels.
constexpr GUID kBackingChallengeGuid{
    0xf1d70763, 0xc534, 0x4b89, {0xa8, 0x43, 0x64, 0x42, 0x9c, 0x97, 0x18, 0x5b}};

struct BackingChallenge {
    std::uint64_t left{}, right{}, phase{};
    bool operator==(const BackingChallenge&) const = default;
};

// Same backing through two interfaces: a value set through one is read back
// through the other, both ways, then both are cleared again. Only runs when
// neither interface holds a value under the reserved key already.
bool prove_same_backing(ID3D12Device* left, ID3D12Device* right, IUnknown* a, IUnknown* b) noexcept {
    UINT size = 0;
    const HRESULT left_empty = left->GetPrivateData(kBackingChallengeGuid, &size, nullptr);
    size = 0;
    const HRESULT right_empty = right->GetPrivateData(kBackingChallengeGuid, &size, nullptr);
    if (left_empty != DXGI_ERROR_NOT_FOUND || right_empty != DXGI_ERROR_NOT_FOUND) {
        return false;
    }
    BackingChallenge sent{reinterpret_cast<std::uintptr_t>(a), reinterpret_cast<std::uintptr_t>(b), 1};
    BackingChallenge received{};
    bool forward = false, backward = false;
    if (SUCCEEDED(left->SetPrivateData(kBackingChallengeGuid, sizeof(sent), &sent))) {
        size = sizeof(received);
        forward = SUCCEEDED(right->GetPrivateData(kBackingChallengeGuid, &size, &received)) &&
            size == sizeof(sent) && received == sent;
    }
    if (forward) {
        sent.phase = 2;
        received = {};
        if (SUCCEEDED(right->SetPrivateData(kBackingChallengeGuid, sizeof(sent), &sent))) {
            size = sizeof(received);
            backward = SUCCEEDED(left->GetPrivateData(kBackingChallengeGuid, &size, &received)) &&
                size == sizeof(sent) && received == sent;
        }
    }
    static_cast<void>(left->SetPrivateData(kBackingChallengeGuid, 0, nullptr));
    static_cast<void>(right->SetPrivateData(kBackingChallengeGuid, 0, nullptr));
    return forward && backward;
}

struct BackingCache {
    std::mutex mutex;
    struct Entry {
        ComPtr<IUnknown> left, right;
        bool same{};
    };
    std::array<Entry, 8> entries{};
    std::size_t count{};
};

BackingCache& backing_cache() {
    static BackingCache value;
    return value;
}

}  // namespace

bool same_d3d12_device_backing(ID3D12Device* left, ID3D12Device* right) noexcept {
    try {
        if (left == nullptr || right == nullptr) return false;
        const auto a = identity(left);
        const auto b = identity(right);
        if (!a || !b) return false;
        if (a.Get() == b.Get()) return true;
        const LUID al = left->GetAdapterLuid();
        const LUID bl = right->GetAdapterLuid();
        if (al.LowPart != bl.LowPart || al.HighPart != bl.HighPart) return false;
        auto& cache = backing_cache();
        std::scoped_lock lock(cache.mutex);
        for (std::size_t index = 0; index < cache.count; ++index) {
            const auto& entry = cache.entries[index];
            if ((entry.left.Get() == a.Get() && entry.right.Get() == b.Get()) ||
                (entry.left.Get() == b.Get() && entry.right.Get() == a.Get())) {
                return entry.same;
            }
        }
        if (cache.count == cache.entries.size()) return false;  // bounded; never guess
        auto& entry = cache.entries[cache.count++];
        entry.left = a;   // pinned: no pointer reuse behind the cache
        entry.right = b;
        entry.same = prove_same_backing(left, right, a.Get(), b.Get());
        return entry.same;
    } catch (...) {
        return false;
    }
}

namespace {

bool valid(const DlssMotionVectorPublication& publication) noexcept {
    if (publication.stream == 0 || publication.output == nullptr ||
        publication.motion_vectors == nullptr ||
        publication.producer_queue == nullptr ||
        publication.producer_command_list == nullptr || publication.output_width == 0 ||
        publication.output_height == 0 || publication.motion_width == 0 ||
        publication.motion_height == 0 || !std::isfinite(publication.scale_x) ||
        !std::isfinite(publication.scale_y) || !std::isfinite(publication.jitter_x) ||
        !std::isfinite(publication.jitter_y)) {
        return false;
    }
    const auto output = publication.output->GetDesc();
    const auto motion = publication.motion_vectors->GetDesc();
    const bool base_valid = output.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        motion.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        static_cast<std::uint64_t>(publication.output_x) + publication.output_width <=
            output.Width &&
        static_cast<std::uint64_t>(publication.output_y) + publication.output_height <=
            output.Height &&
        static_cast<std::uint64_t>(publication.motion_x) + publication.motion_width <=
            motion.Width &&
        static_cast<std::uint64_t>(publication.motion_y) + publication.motion_height <=
            motion.Height;
    return base_valid;
}

// The optional depth, judged on its own (V505): a depth the layer cannot use
// leaves the vectors published without it rather than refused.
bool valid_depth(const DlssMotionVectorPublication& publication) noexcept {
    if (publication.depth == nullptr) return false;
    const auto depth = publication.depth->GetDesc();
    return depth.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        publication.depth_width != 0 && publication.depth_height != 0 &&
        static_cast<std::uint64_t>(publication.depth_x) + publication.depth_width <=
            depth.Width &&
        static_cast<std::uint64_t>(publication.depth_y) + publication.depth_height <=
            depth.Height &&
        std::isfinite(publication.frame_time_delta_ms) &&
        publication.frame_time_delta_ms > 0.0F &&
        std::isfinite(publication.camera_near) &&
        std::isfinite(publication.camera_far);
}

bool matching_description(
    const D3D12_RESOURCE_DESC& left,
    const D3D12_RESOURCE_DESC& right) noexcept {
    return left.Dimension == right.Dimension && left.Alignment == right.Alignment &&
        left.Width == right.Width && left.Height == right.Height &&
        left.DepthOrArraySize == right.DepthOrArraySize &&
        left.MipLevels == right.MipLevels && left.Format == right.Format &&
        left.SampleDesc.Count == right.SampleDesc.Count &&
        left.SampleDesc.Quality == right.SampleDesc.Quality &&
        left.Layout == right.Layout && left.Flags == right.Flags;
}

D3D12_RESOURCE_BARRIER transition_barrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

HRESULT snapshot_resource(
    std::array<SnapshotSlot, kSnapshotSlotCount>& snapshots,
    std::size_t& next_snapshot,
    ID3D12Resource* source,
    D3D12_RESOURCE_STATES source_state,
    ID3D12GraphicsCommandList* producer_command_list,
    ID3D12CommandQueue* producer_queue,
    ID3D12Device* verified_producer_device,
    ComPtr<ID3D12Resource>* snapshot) noexcept {
    if (source == nullptr || producer_command_list == nullptr ||
        producer_queue == nullptr || snapshot == nullptr) return E_POINTER;
    snapshot->Reset();

    ComPtr<ID3D12Device> source_device;
    ComPtr<ID3D12Device> command_device;
    ComPtr<ID3D12Device> queue_device;
    HRESULT result = source->GetDevice(
        IID_PPV_ARGS(source_device.GetAddressOf()));
    if (FAILED(result)) return result;
    result = producer_command_list->GetDevice(
        IID_PPV_ARGS(command_device.GetAddressOf()));
    if (FAILED(result)) return result;
    result = producer_queue->GetDevice(
        IID_PPV_ARGS(queue_device.GetAddressOf()));
    if (FAILED(result)) return result;
    ComPtr<ID3D12Device> allocation_device;
    if (verified_producer_device != nullptr) {
        // The producer has already unwrapped and identity-checked all related
        // objects. Comparing their reported wrapper devices here would
        // recreate the Streamline false mismatch this field avoids.
        allocation_device = verified_producer_device;
    } else {
        // Streamline's proxies report another device than the resource.
        if (!same_d3d12_device_backing(source_device.Get(), command_device.Get()) ||
            !same_d3d12_device_backing(source_device.Get(), queue_device.Get())) {
            return E_INVALIDARG;
        }
        allocation_device = source_device;
    }

    const D3D12_RESOURCE_DESC description = source->GetDesc();
    SnapshotSlot& slot = snapshots[next_snapshot];
    if (!slot.resource || !matching_description(slot.description, description)) {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        ComPtr<ID3D12Resource> resource;
        result = allocation_device->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(resource.GetAddressOf()));
        if (FAILED(result)) return result;
        slot.resource = std::move(resource);
        slot.description = description;
        slot.state = D3D12_RESOURCE_STATE_COMMON;
    }

    std::array<D3D12_RESOURCE_BARRIER, 2> before{};
    UINT before_count = 0;
    if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        before[before_count++] = transition_barrier(
            source,
            source_state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    if (slot.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        before[before_count++] = transition_barrier(
            slot.resource.Get(), slot.state, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    if (before_count != 0) {
        producer_command_list->ResourceBarrier(before_count, before.data());
    }
    producer_command_list->CopyResource(slot.resource.Get(), source);

    std::array<D3D12_RESOURCE_BARRIER, 2> after{};
    UINT after_count = 0;
    if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        after[after_count++] = transition_barrier(
            source,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            source_state);
    }
    after[after_count++] = transition_barrier(
        slot.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kSnapshotReadState);
    producer_command_list->ResourceBarrier(after_count, after.data());
    slot.state = kSnapshotReadState;
    *snapshot = slot.resource;
    next_snapshot = (next_snapshot + 1) % kSnapshotSlotCount;
    return S_OK;
}

struct Candidate {
    std::uint64_t first_publication{};
    std::shared_ptr<const DlssMotionVectorFrame> frame;
    std::shared_ptr<const DlssMotionVectorFrame> previous;
};

// Where an eye's DLSS inputs sit in their render targets. Both eyes often
// share one double-wide target with the left eye on the left - UEVR's depth,
// for one - so the eye further left is eye 0 whichever evaluates first.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> input_offset(
    const DlssMotionVectorFrame& frame) noexcept {
    return {frame.motion_x, frame.output_x};
}

// 828: the resolver's running totals, every 300 resolves. a matched << 32 |
// stale pairs, b queue mismatches << 32 | missing streams, c used << 32 |
// hybrid pairs.
void record_resolver_statistics(const DlssMotionVectorStatistics& statistics) noexcept {
    if (statistics.resolve_calls % 300 != 0) return;
    try {
        bridge_flight_logger().event(
            BridgeFlightOperation::embedded_configuration, 828,
            (statistics.matched << 32U) | (statistics.resolve_stale_pairs & 0xFFFFFFFFULL),
            (statistics.resolve_queue_mismatches << 32U) |
                (statistics.resolve_missing_streams & 0xFFFFFFFFULL),
            (statistics.used << 32U) | (statistics.hybrid_used & 0xFFFFFFFFULL));
    } catch (...) {
    }
}

}  // namespace

void publish_dlss_motion_vectors(
    const DlssMotionVectorPublication& publication) noexcept {
    try {
        if (!g_tracking_enabled.load(std::memory_order_acquire)) return;
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        if (!valid(publication)) {
            ++state.statistics.invalid_publications;
            state.statistics.status = DlssMotionVectorStatus::invalid_input;
            return;
        }

        auto& stream = state.streams[publication.stream];
        if (publication.reset) {
            ++stream.epoch;
            stream.serial = 0;
            stream.previous.reset();
            stream.latest.reset();
        }

        ComPtr<ID3D12Resource> motion_snapshot;
        if (FAILED(snapshot_resource(stream.snapshots, stream.next_snapshot,
                publication.motion_vectors, publication.resource_state,
                publication.producer_command_list, publication.producer_queue,
                publication.verified_producer_device,
                &motion_snapshot))) {
            ++state.statistics.snapshot_failures;
            state.statistics.status = DlssMotionVectorStatus::invalid_input;
            return;
        }
        ++state.statistics.snapshot_copies;

        ComPtr<ID3D12Resource> depth_snapshot;
        if (valid_depth(publication)) {
            if (FAILED(snapshot_resource(stream.depth_snapshots,
                    stream.next_depth_snapshot, publication.depth,
                    publication.depth_resource_state,
                    publication.producer_command_list,
                    publication.producer_queue,
                    publication.verified_producer_device, &depth_snapshot))) {
                // The vectors go on without it.
                ++state.statistics.snapshot_failures;
                depth_snapshot.Reset();
            } else {
                ++state.statistics.snapshot_copies;
                ++state.statistics.depth_publications;
            }
        }

        auto frame = std::make_shared<DlssMotionVectorFrame>();
        frame->stream = publication.stream;
        frame->epoch = stream.epoch;
        frame->previous_serial = stream.serial;
        frame->serial = ++stream.serial;
        frame->publication = ++state.statistics.published;
        frame->output = publication.output;
        frame->motion_vectors = std::move(motion_snapshot);
        frame->producer_queue = publication.producer_queue;
        frame->output_x = publication.output_x;
        frame->output_y = publication.output_y;
        frame->output_width = publication.output_width;
        frame->output_height = publication.output_height;
        frame->output_slice = 0;
        frame->motion_x = publication.motion_x;
        frame->motion_y = publication.motion_y;
        frame->motion_width = publication.motion_width;
        frame->motion_height = publication.motion_height;
        frame->motion_slice = 0;
        frame->scale_x = publication.scale_x == 0.0F ? 1.0F : publication.scale_x;
        frame->scale_y = publication.scale_y == 0.0F ? 1.0F : publication.scale_y;
        frame->jitter_x = publication.jitter_x;
        frame->jitter_y = publication.jitter_y;
        frame->previous_jitter_x = stream.jitter_x;
        frame->previous_jitter_y = stream.jitter_y;
        frame->resource_state = kSnapshotReadState;
        frame->jittered = publication.jittered;
        frame->reset = publication.reset;
        frame->depth = std::move(depth_snapshot);
        frame->depth_x = publication.depth_x;
        frame->depth_y = publication.depth_y;
        frame->depth_width = publication.depth_width;
        frame->depth_height = publication.depth_height;
        frame->depth_resource_state = kSnapshotReadState;
        frame->frame_time_delta_ms = publication.frame_time_delta_ms;
        frame->camera_near = publication.camera_near;
        frame->camera_far = publication.camera_far;
        frame->depth_inverted = publication.depth_inverted;
        frame->depth_infinite = publication.depth_infinite;
        frame->view = publication.view;
        // V516: the stream's previous camera (a reset above cleared it).
        if (stream.latest) frame->previous_view = stream.latest->view;
        stream.jitter_x = publication.jitter_x;
        stream.jitter_y = publication.jitter_y;
        if (stream.first_publication == 0) {
            stream.first_publication = frame->publication;
        }
        stream.previous = std::move(stream.latest);
        stream.latest = std::move(frame);
        if (state.statistics.used == 0) {
            state.statistics.status = DlssMotionVectorStatus::output_not_direct;
        }
    } catch (...) {
        report_dlss_motion_vector_status(DlssMotionVectorStatus::invalid_input);
    }
}

void configure_dlss_motion_vector_tracking(bool enabled) noexcept {
    const bool previous = g_tracking_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (enabled == previous) return;
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        if (!enabled) {
            // Do not release the private resources here. The desired setting is
            // visible to DLSS before the OpenXR layer reaches xrEndFrame and its
            // GPU-idle fence, so snapshots may still be referenced by submitted
            // synthesis work. The layer retires them explicitly after that fence.
            state.statistics.status = DlssMotionVectorStatus::disabled;
            return;
        }
        // Re-enabling without a layer-side retirement must not expose stale
        // vectors. Preserve the slot resources (and therefore any in-flight GPU
        // lifetime), but start fresh stream epochs and temporal ownership.
        for (auto& [unused_stream_id, stream] : state.streams) {
            static_cast<void>(unused_stream_id);
            ++stream.epoch;
            stream.serial = 0;
            stream.first_publication = 0;
            stream.jitter_x = 0.0F;
            stream.jitter_y = 0.0F;
            stream.previous.reset();
            stream.latest.reset();
        }
        state.statistics = {};
        state.statistics.status = DlssMotionVectorStatus::waiting_for_dlss;
    } catch (...) {
    }
}

void retire_disabled_dlss_motion_vector_resources() noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        if (g_tracking_enabled.load(std::memory_order_acquire)) return;
        state.streams.clear();
        state.statistics = {};
        state.statistics.status = DlssMotionVectorStatus::disabled;
    } catch (...) {
    }
}

void retire_dlss_motion_vector_stream(std::uint64_t stream) noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        state.streams.erase(stream);
    } catch (...) {
    }
}

std::shared_ptr<const DlssMotionVectorFrame> split_both_eyes_guide(
    const DlssMotionVectorFrame& frame,
    std::uint32_t view_width,
    std::uint32_t view_height,
    std::uint32_t eye) noexcept {
    try {
        if (eye > 1 || view_width == 0 || view_height == 0 || frame.output_x != 0 ||
            frame.output_y != 0 || frame.output_width != view_width * 2U ||
            frame.output_height != view_height || frame.motion_width < 2) {
            return {};
        }
        auto half = std::make_shared<DlssMotionVectorFrame>(frame);
        half->output_width = view_width;
        const std::uint32_t motion_half = frame.motion_width / 2U;
        half->motion_x = frame.motion_x + eye * motion_half;
        half->motion_width = motion_half;
        if (frame.depth && frame.depth_width >= 2) {
            const std::uint32_t depth_half = frame.depth_width / 2U;
            half->depth_x = frame.depth_x + eye * depth_half;
            half->depth_width = depth_half;
        }
        return half;
    } catch (...) {
        return {};
    }
}

namespace {

// How far a guide's camera may fall short of an eye's field and still be
// taken to cover it: a hundredth of the field each side.
constexpr double kViewCoverTolerance = 0.01;
// A stream with a camera is recent while its newest evaluation is within
// this many publications of the newest of all: MSFS 2024's foveated
// rendering publishes four a frame, and a stream the game stopped
// evaluating (a menu's, the desktop mirror's) keeps its last frame.
constexpr std::uint64_t kMaximumViewPublicationGap = 8;

[[nodiscard]] double view_fit_error(const DlssViewGeometry& g, const DlssEyeView& eye) noexcept {
    return std::fabs(static_cast<double>(g.tan_left) - eye.tan_left) +
        std::fabs(static_cast<double>(g.tan_right) - eye.tan_right) +
        std::fabs(static_cast<double>(g.tan_up) - eye.tan_up) +
        std::fabs(static_cast<double>(g.tan_down) - eye.tan_down);
}

// Along the two cameras' mean right vector: the smaller is the left eye.
[[nodiscard]] double camera_side(const DlssViewGeometry& g, const std::array<float, 3>& right) noexcept {
    return static_cast<double>(g.position[0]) * right[0] +
        static_cast<double>(g.position[1]) * right[1] +
        static_cast<double>(g.position[2]) * right[2];
}

[[nodiscard]] double camera_distance(const DlssViewGeometry& a, const DlssViewGeometry& b) noexcept {
    const double x = static_cast<double>(a.position[0]) - b.position[0];
    const double y = static_cast<double>(a.position[1]) - b.position[1];
    const double z = static_cast<double>(a.position[2]) - b.position[2];
    return std::sqrt(x * x + y * y + z * z);
}

// An inset's field (V516): inside the eye's, to the cover tolerance, and
// clearly narrower than it - MSFS 2024's is FoveatedRenderingScale of it.
[[nodiscard]] bool inside_eye_field(const DlssViewGeometry& g, const DlssEyeView& eye) noexcept {
    const double eye_width = static_cast<double>(eye.tan_right) - eye.tan_left;
    const double eye_height = static_cast<double>(eye.tan_up) - eye.tan_down;
    const double width = static_cast<double>(g.tan_right) - g.tan_left;
    const double height = static_cast<double>(g.tan_up) - g.tan_down;
    const double slack_x = kViewCoverTolerance * eye_width;
    const double slack_y = kViewCoverTolerance * eye_height;
    return g.known && eye_width > 0.0 && eye_height > 0.0 && width > 0.0 && height > 0.0 &&
        width < 0.9 * eye_width && height < 0.9 * eye_height &&
        g.tan_left >= eye.tan_left - slack_x && g.tan_right <= eye.tan_right + slack_x &&
        g.tan_up <= eye.tan_up + slack_y && g.tan_down >= eye.tan_down - slack_y;
}

// A stream's evaluation for an eye's release: its newest by the watermark.
[[nodiscard]] std::shared_ptr<const DlssMotionVectorFrame> evaluation_by_release(
    const Candidate& candidate, std::uint64_t released_at) noexcept {
    if (released_at == 0 || candidate.frame->publication <= released_at) return candidate.frame;
    return candidate.previous && candidate.previous->epoch == candidate.frame->epoch &&
            candidate.previous->publication <= released_at
        ? candidate.previous : nullptr;
}

}  // namespace

std::array<float, 4> view_rect_in_eye(
    const DlssViewGeometry& view,
    const DlssEyeView& eye) noexcept {
    const double eye_width = static_cast<double>(eye.tan_right) - eye.tan_left;
    const double eye_height = static_cast<double>(eye.tan_up) - eye.tan_down;
    if (!view.known || !(eye_width > 0.0) || !(eye_height > 0.0)) return {};
    const double x = eye.x + (view.tan_left - eye.tan_left) / eye_width * eye.width;
    const double width = (static_cast<double>(view.tan_right) - view.tan_left) / eye_width * eye.width;
    const double y = eye.y + (eye.tan_up - view.tan_up) / eye_height * eye.height;
    const double height = (static_cast<double>(view.tan_up) - view.tan_down) / eye_height * eye.height;
    if (!std::isfinite(x) || !std::isfinite(y) || !(width > 0.0) || !(height > 0.0)) return {};
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
        static_cast<float>(height)};
}

std::shared_ptr<const DlssMotionVectorFrame> place_guide_in_view(
    const DlssMotionVectorFrame& frame,
    const DlssEyeView& view) noexcept {
    try {
        const DlssViewGeometry& g = frame.view;
        const double eye_width = static_cast<double>(view.tan_right) - view.tan_left;
        const double eye_height = static_cast<double>(view.tan_up) - view.tan_down;
        if (!g.known || view.x < 0 || view.y < 0 || view.width == 0 || view.height == 0 ||
            !(eye_width > 0.0) || !(eye_height > 0.0) || frame.motion_width == 0 ||
            frame.motion_height == 0) {
            return {};
        }
        // The guide's edges in the image's pixels.
        const double x0 = view.x, y0 = view.y;
        const double x1 = x0 + view.width, y1 = y0 + view.height;
        const double left = x0 + (g.tan_left - view.tan_left) / eye_width * view.width;
        const double right = x0 + (g.tan_right - view.tan_left) / eye_width * view.width;
        const double top = y0 + (view.tan_up - g.tan_up) / eye_height * view.height;
        const double bottom = y0 + (view.tan_up - g.tan_down) / eye_height * view.height;
        const double slack_x = kViewCoverTolerance * view.width;
        const double slack_y = kViewCoverTolerance * view.height;
        // Written so that a NaN refuses.
        if (!(right - left > 1.0) || !(bottom - top > 1.0) || !(left <= x0 + slack_x) ||
            !(right >= x1 - slack_x) || !(top <= y0 + slack_y) || !(bottom >= y1 - slack_y)) {
            return {};
        }
        // The vectors under the view: texel = origin + (pixel - edge) x
        // texels per pixel, kept inside the vectors where the camera falls
        // short within the tolerance.
        const double per_x = frame.motion_width / (right - left);
        const double per_y = frame.motion_height / (bottom - top);
        const double motion_left = frame.motion_x, motion_top = frame.motion_y;
        const double motion_right = motion_left + frame.motion_width;
        const double motion_bottom = motion_top + frame.motion_height;
        const double mx0 = std::clamp(motion_left + (x0 - left) * per_x, motion_left, motion_right);
        const double mx1 = std::clamp(motion_left + (x1 - left) * per_x, motion_left, motion_right);
        const double my0 = std::clamp(motion_top + (y0 - top) * per_y, motion_top, motion_bottom);
        const double my1 = std::clamp(motion_top + (y1 - top) * per_y, motion_top, motion_bottom);
        if (!(mx1 - mx0 > 0.5) || !(my1 - my0 > 0.5)) return {};
        auto placed = std::make_shared<DlssMotionVectorFrame>(frame);
        placed->output_x = static_cast<std::uint32_t>(view.x);
        placed->output_y = static_cast<std::uint32_t>(view.y);
        placed->output_width = view.width;
        placed->output_height = view.height;
        placed->placed = true;
        placed->placed_motion_rect = {static_cast<float>(mx0), static_cast<float>(my0),
            static_cast<float>(mx1 - mx0), static_cast<float>(my1 - my0)};
        return placed;
    } catch (...) {
        return {};
    }
}

std::array<std::shared_ptr<const DlssMotionVectorSet>, kDlssMotionVectorEyeCount>
resolve_view_placed_dlss_guides(
    ID3D12CommandQueue* consumer_queue,
    const std::array<DlssEyeView, kDlssMotionVectorEyeCount>& eyes,
    const std::array<std::uint64_t, kDlssMotionVectorEyeCount>& released_at) noexcept {
    std::array<std::shared_ptr<const DlssMotionVectorSet>, kDlssMotionVectorEyeCount> none{};
    try {
        if (!g_tracking_enabled.load(std::memory_order_acquire) || consumer_queue == nullptr) {
            return none;
        }
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        std::vector<Candidate> candidates;
        std::uint64_t newest = 0;
        for (const auto& [unused_stream_id, stream] : state.streams) {
            static_cast<void>(unused_stream_id);
            if (!stream.latest || !stream.latest->view.known ||
                !same_identity(stream.latest->producer_queue.Get(), consumer_queue)) continue;
            candidates.push_back({stream.first_publication, stream.latest, stream.previous});
            newest = std::max(newest, stream.latest->publication);
        }
        if (candidates.empty()) return none;
        std::erase_if(candidates, [&](const Candidate& c) {
            return newest - c.frame->publication > kMaximumViewPublicationGap;
        });

        // The streams whose camera covers each eye, and how well each fits.
        constexpr double kNoFit = 1e30;
        std::vector<std::array<double, kDlssMotionVectorEyeCount>> fit(
            candidates.size(), {kNoFit, kNoFit});
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            for (std::uint32_t eye = 0; eye < kDlssMotionVectorEyeCount; ++eye) {
                if (place_guide_in_view(*candidates[index].frame, eyes[eye])) {
                    fit[index][eye] = view_fit_error(candidates[index].frame->view, eyes[eye]);
                }
            }
        }
        // The pair of streams, one an eye, that fits best.
        std::size_t best_left = 0, best_right = 0;
        double best = kNoFit;
        for (std::size_t left = 0; left < candidates.size(); ++left) {
            for (std::size_t right = 0; right < candidates.size(); ++right) {
                if (left == right || fit[left][0] >= kNoFit || fit[right][1] >= kNoFit) continue;
                const double cost = fit[left][0] + fit[right][1];
                if (cost < best) {
                    best = cost;
                    best_left = left;
                    best_right = right;
                }
            }
        }
        if (best >= kNoFit) {
            // 836, the first eight: a the recent streams with a camera |
            // those covering the left eye << 8 | the right << 16, b and c
            // the eyes' fields (pack_view_tangents); 834 has the cameras'.
            if (state.statistics.view_unmatched++ < 8) {
                std::uint64_t covering_left = 0, covering_right = 0;
                for (const auto& f : fit) {
                    covering_left += f[0] < kNoFit ? 1U : 0U;
                    covering_right += f[1] < kNoFit ? 1U : 0U;
                }
                try {
                    bridge_flight_logger().event(
                        BridgeFlightOperation::embedded_configuration, 836,
                        (candidates.size() & 0xFFU) | ((covering_left & 0xFFU) << 8U) |
                            ((covering_right & 0xFFU) << 16U),
                        pack_view_tangents(eyes[0].tan_left, eyes[0].tan_right,
                            eyes[0].tan_up, eyes[0].tan_down),
                        pack_view_tangents(eyes[1].tan_left, eyes[1].tan_right,
                            eyes[1].tan_up, eyes[1].tan_down));
                } catch (...) {
                }
            }
            state.statistics.status = DlssMotionVectorStatus::output_not_direct;
            return none;
        }
        // Two cameras that fit both eyes alike (a symmetric field): the
        // camera further left is the left eye.
        if (fit[best_right][0] < kNoFit && fit[best_left][1] < kNoFit &&
            std::fabs((fit[best_right][0] + fit[best_left][1]) - best) < 1e-4) {
            const auto& a = candidates[best_left].frame->view;
            const auto& b = candidates[best_right].frame->view;
            const std::array<float, 3> right{
                a.right[0] + b.right[0], a.right[1] + b.right[1], a.right[2] + b.right[2]};
            const double side_a = camera_side(a, right), side_b = camera_side(b, right);
            if (!(std::fabs(side_a - side_b) > 1e-6)) {
                ++state.statistics.view_unmatched;
                state.statistics.status = DlssMotionVectorStatus::temporal_mismatch;
                return none;
            }
            if (side_a > side_b) std::swap(best_left, best_right);
        }

        // Each eye's evaluation: its stream's newest by the eye's release.
        std::array<std::shared_ptr<const DlssMotionVectorSet>, kDlssMotionVectorEyeCount> result{};
        const std::array<std::size_t, kDlssMotionVectorEyeCount> chosen_streams{best_left, best_right};
        // V516: the eyes' insets. An eye's inset is the recent stream whose
        // field lies inside the eye's, narrower than it, and whose camera
        // sits with that eye's periphery camera - well within a quarter of
        // the two eyes' spacing, where the other eye's is a whole spacing
        // away. None found, the eye keeps its periphery alone (V515).
        const double eye_spacing = camera_distance(
            candidates[best_left].frame->view, candidates[best_right].frame->view);
        std::uint32_t insets_found = 0;
        for (std::uint32_t eye = 0; eye < kDlssMotionVectorEyeCount; ++eye) {
            const Candidate& candidate = candidates[chosen_streams[eye]];
            const auto chosen = evaluation_by_release(candidate, released_at[eye]);
            auto placed = chosen ? place_guide_in_view(*chosen, eyes[eye]) : nullptr;
            if (!placed) {
                ++state.statistics.resolve_stale_pairs;
                state.statistics.status = DlssMotionVectorStatus::temporal_mismatch;
                return none;
            }
            std::shared_ptr<DlssMotionVectorFrame> inset;
            if (eye_spacing > 1e-6) {
                std::size_t nearest = candidates.size();
                double nearest_distance = 0.25 * eye_spacing;
                for (std::size_t index = 0; index < candidates.size(); ++index) {
                    if (index == best_left || index == best_right ||
                        !inside_eye_field(candidates[index].frame->view, eyes[eye])) continue;
                    const double distance =
                        camera_distance(candidates[index].frame->view, candidate.frame->view);
                    if (distance < nearest_distance) {
                        nearest_distance = distance;
                        nearest = index;
                    }
                }
                const auto evaluation = nearest < candidates.size()
                    ? evaluation_by_release(candidates[nearest], released_at[eye]) : nullptr;
                if (evaluation && evaluation->motion_width != 0 && evaluation->motion_height != 0 &&
                    inside_eye_field(evaluation->view, eyes[eye])) {
                    inset = std::make_shared<DlssMotionVectorFrame>(*evaluation);
                    inset->view_rect = view_rect_in_eye(evaluation->view, eyes[eye]);
                    inset->previous_view_rect_known = evaluation->previous_view.known;
                    inset->previous_view_rect = evaluation->previous_view.known
                        ? view_rect_in_eye(evaluation->previous_view, eyes[eye])
                        : inset->view_rect;
                    if (!(inset->view_rect[2] > 0.0F) || !(inset->previous_view_rect[2] > 0.0F)) {
                        inset.reset();
                    }
                }
            }
            auto set = std::make_shared<DlssMotionVectorSet>();
            set->eye_count = 1;
            if (inset) {
                auto owner = std::make_shared<DlssMotionVectorFrame>(*placed);
                owner->inset = std::move(inset);
                set->eyes[0] = std::move(owner);
                ++insets_found;
            } else {
                set->eyes[0] = std::move(placed);
            }
            result[eye] = std::move(set);
        }
        state.statistics.matched += kDlssMotionVectorEyeCount;
        ++state.statistics.view_matched;
        if (insets_found == kDlssMotionVectorEyeCount) ++state.statistics.inset_matched;
        return result;
    } catch (...) {
        return none;
    }
}

std::uint64_t dlss_motion_vector_publications() noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        return state.statistics.published;
    } catch (...) {
        return 0;
    }
}

std::shared_ptr<const DlssMotionVectorSet> resolve_dlss_motion_vectors(
    ID3D12Resource* output,
    ID3D12CommandQueue* consumer_queue,
    int eye,
    std::uint64_t released_at,
    std::uint64_t other_released_at) noexcept {
    try {
        if (!g_tracking_enabled.load(std::memory_order_acquire)) {
            report_dlss_motion_vector_status(DlssMotionVectorStatus::disabled);
            return {};
        }
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        ++state.statistics.resolve_calls;
        record_resolver_statistics(state.statistics);
        if (output == nullptr || consumer_queue == nullptr) {
            ++state.statistics.invalid_rejections;
            state.statistics.status = DlssMotionVectorStatus::invalid_input;
            return {};
        }

        const auto description = output->GetDesc();
        if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            ++state.statistics.invalid_rejections;
            state.statistics.status = DlssMotionVectorStatus::invalid_input;
            return {};
        }
        std::uint32_t eye_count = std::min<std::uint32_t>(
            std::max<std::uint32_t>(description.DepthOrArraySize, 1U),
            kDlssMotionVectorEyeCount);

        std::vector<Candidate> candidates;
        std::uint32_t streams_with_frames = 0;
        candidates.reserve(state.streams.size());
        for (const auto& [unused_stream_id, stream] : state.streams) {
            static_cast<void>(unused_stream_id);
            if (!stream.latest) continue;
            ++streams_with_frames;
            if (same_identity(stream.latest->producer_queue.Get(), consumer_queue)) {
                candidates.push_back(
                    {stream.first_publication, stream.latest, stream.previous});
            }
        }
        const auto stale = [&]() -> std::shared_ptr<const DlssMotionVectorSet> {
            ++state.statistics.resolve_stale_pairs;
            state.statistics.status = DlssMotionVectorStatus::temporal_mismatch;
            return {};
        };
        // One evaluation for both eyes, on texture arrays with a slice per
        // eye: both eyes take the same publication, each reading its slice,
        // and the eye's previous evaluation is the stream's previous one.
        const auto array_eyes = [](const DlssMotionVectorFrame& frame) {
            if (!frame.output || !frame.motion_vectors) return false;
            return frame.output->GetDesc().DepthOrArraySize >= kDlssMotionVectorEyeCount &&
                frame.motion_vectors->GetDesc().DepthOrArraySize >= kDlssMotionVectorEyeCount;
        };
        const auto one_eye = [&](std::shared_ptr<const DlssMotionVectorFrame> chosen,
                                 bool shared_stream) {
            auto frame = std::make_shared<DlssMotionVectorFrame>(*chosen);
            if (array_eyes(*chosen) && (eye == 0 || eye == 1)) {
                frame->output_slice = static_cast<std::uint32_t>(eye);
                frame->motion_slice = static_cast<std::uint32_t>(eye);
            } else if (shared_stream) {
                // One stream alternating eyes: this eye's previous evaluation
                // is two serials back, which is what a valid pair checks.
                frame->previous_serial = frame->serial > 2 ? frame->serial - 2 : 0;
            }
            auto result = std::make_shared<DlssMotionVectorSet>();
            result->eye_count = 1;
            result->eyes[0] = std::move(frame);
            ++state.statistics.matched;
            return result;
        };

        // An evaluation whose output is this very image belongs to it,
        // whatever else matches its size.
        bool direct_match = false;
        {
            std::vector<Candidate> direct;
            for (const auto& candidate : candidates) {
                if (same_identity(candidate.frame->output.Get(), output)) {
                    direct.push_back(candidate);
                }
            }
            if (!direct.empty()) {
                candidates = std::move(direct);
                direct_match = true;
            }
        }

        // Native Stereo can place two eye viewports side by side in one
        // array-size-one XR image while DLSS keeps one persistent stream per
        // eye. Restrict the candidate set to streams whose published output is
        // exactly one horizontal half of the XR image.
        bool packed_stereo = false;
        if (eye_count == 1 && !direct_match) {
            std::vector<Candidate> halves;
            for (const auto& candidate : candidates) {
                if (candidate.frame->output_width != 0 &&
                    candidate.frame->output_height != 0 &&
                    description.Width ==
                        static_cast<std::uint64_t>(candidate.frame->output_width) * 2U &&
                    description.Height == candidate.frame->output_height) {
                    halves.push_back(candidate);
                }
            }
            if (halves.size() >= kDlssMotionVectorEyeCount) {
                candidates = std::move(halves);
                eye_count = kDlssMotionVectorEyeCount;
                packed_stereo = true;
            }
        }

        if (eye_count == 1 && !direct_match) {
            // Composition/UI swapchains share the scene queue but are not the
            // upscaler output, and a mirror's evaluation is not an eye's.
            // Only an evaluation the size of this image can be its guide.
            std::erase_if(candidates, [&](const Candidate& c) {
                return c.frame->output_x != 0 || c.frame->output_y != 0 ||
                    c.frame->output_width != description.Width ||
                    c.frame->output_height != description.Height;
            });
        }

        // A swapchain per eye, each the size of a DLSS output: every eye's
        // evaluation matches each eye's image by size, and the newest went to
        // both - with the capture at xrEndFrame, after both eyes, always the
        // eye DLSS ran last - so the other eye followed its motion, a double
        // image wherever the two eyes' motion differs. The eye this image is
        // submitted as takes its own evaluation: by its colour's release
        // (released_at, the publication watermark then), or failing that by
        // the order the stereo path gives two - two streams, or the game's
        // one stream alternating eyes.
        if (!direct_match && eye_count == 1 && released_at != 0 &&
            (eye == 0 || eye == 1)) {
            std::uint64_t newest_publication = 0;
            for (const auto& candidate : candidates)
                newest_publication = std::max(newest_publication, candidate.frame->publication);
            std::erase_if(candidates, [&](const Candidate& candidate) {
                return newest_publication - candidate.frame->publication >
                    kMaximumStereoPublicationGap;
            });
            if (candidates.empty()) {
                if (streams_with_frames >= eye_count) {
                    ++state.statistics.resolve_queue_mismatches;
                    state.statistics.status = DlssMotionVectorStatus::queue_mismatch;
                } else {
                    ++state.statistics.resolve_missing_streams;
                    state.statistics.status = state.statistics.published == 0
                        ? DlssMotionVectorStatus::waiting_for_dlss
                        : DlssMotionVectorStatus::output_not_direct;
                }
                return {};
            }
            std::vector<std::shared_ptr<const DlssMotionVectorFrame>> frames;
            for (const auto& candidate : candidates) {
                frames.push_back(candidate.frame);
                if (candidate.previous && candidate.previous->epoch == candidate.frame->epoch)
                    frames.push_back(candidate.previous);
            }
            std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
                return a->publication > b->publication;
            });
            const auto newest_by = [&](std::uint64_t limit) {
                for (const auto& frame : frames)
                    if (frame->publication <= limit) return frame;
                return std::shared_ptr<const DlssMotionVectorFrame>{};
            };
            auto chosen = newest_by(released_at);
            const auto other = other_released_at ? newest_by(other_released_at) : nullptr;
            bool shared_stream = candidates.size() == 1;
            if (chosen && shared_stream && array_eyes(*chosen)) {
                return one_eye(chosen, false);
            }
            if (chosen && other && chosen == other) {
                if (candidates.size() >= 2) {
                    // Both evaluated before either release. Exactly two
                    // active streams keep the input-offset ordering; a
                    // third same-size stream is ambiguous.
                    if (candidates.size() != 2) return stale();
                    std::sort(candidates.begin(), candidates.end(),
                        [](const Candidate& a, const Candidate& b) {
                            const auto ao = input_offset(*a.frame), bo = input_offset(*b.frame);
                            return ao != bo ? ao < bo : a.first_publication < b.first_publication;
                        });
                    const auto& candidate = candidates[static_cast<std::size_t>(eye)];
                    chosen = candidate.frame->publication <= released_at
                        ? candidate.frame : candidate.previous;
                } else {
                    // One shared stream: left first when both eyes were
                    // evaluated before either release.
                    const auto later = std::find(frames.begin(), frames.end(), chosen);
                    const auto earlier = later + 1;
                    chosen = eye == 1 ? *later
                        : earlier != frames.end() ? *earlier : nullptr;
                    if (chosen && eye == 0 && (chosen->stream != other->stream ||
                        chosen->epoch != other->epoch ||
                        chosen->serial + 1 != other->serial)) return stale();
                }
            }
            if (!chosen || frames.empty() || chosen->publication > released_at ||
                frames.front()->publication - chosen->publication > kMaximumStereoPublicationGap)
                return stale();
            if (other && other != chosen)
                shared_stream = other->stream == chosen->stream;
            return one_eye(chosen, shared_stream);
        }

        if (!direct_match && eye_count == 1 && (eye == 0 || eye == 1)) {
            if (candidates.size() >= 2) {
                std::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& a, const Candidate& b) {
                        return a.frame->publication > b.frame->publication;
                    });
                candidates.resize(2);
                const auto newest = candidates[0].frame->publication;
                const auto oldest = candidates[1].frame->publication;
                if (newest < oldest || newest - oldest > kMaximumStereoPublicationGap) {
                    return stale();
                }
                std::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& a, const Candidate& b) {
                        const auto ao = input_offset(*a.frame), bo = input_offset(*b.frame);
                        if (ao != bo) return ao < bo;
                        return a.first_publication < b.first_publication;
                    });
                auto result = std::make_shared<DlssMotionVectorSet>();
                result->eye_count = 1;
                result->eyes[0] = candidates[static_cast<std::size_t>(eye)].frame;
                ++state.statistics.matched;
                return result;
            }
            if (candidates.size() == 1 && streams_with_frames == 1) {
                const auto& current = candidates.front().frame;
                const auto& previous = candidates.front().previous;
                if (array_eyes(*current)) return one_eye(current, false);
                if (!previous && eye == 1) return stale();
                if (previous && current->stream == previous->stream &&
                    current->epoch == previous->epoch &&
                    current->serial == previous->serial + 1 &&
                    current->publication > previous->publication &&
                    current->publication - previous->publication <=
                        kMaximumStereoPublicationGap) {
                    // One stream for both eyes: the odd serial is the left
                    // eye, as for alternating-eye renderers, unless the
                    // inputs say otherwise. Each eye sees every other serial.
                    const auto& odd = (current->serial & 1U) != 0 ? current : previous;
                    const auto& even = (current->serial & 1U) != 0 ? previous : current;
                    const bool swapped = input_offset(*even) < input_offset(*odd);
                    const auto& chosen = (eye == 0) != swapped ? odd : even;
                    // The chosen eye's evaluation of this frame has to be the
                    // newest or the one just before it: a left eye evaluated
                    // with the right still to come is this frame's.
                    if (chosen == current || (current->serial & 1U) == 0) {
                        return one_eye(chosen, true);
                    }
                    return stale();
                }
                // A reset/gap cannot turn a requested eye into whichever
                // evaluation happened last. Only a first left publication
                // may continue through the ordinary path.
                if (previous) return stale();
            }
        }

        // Alternating-eye renderers such as Ghost of Tsushima's AER path reuse
        // one DLSS handle for both eyes. A complete stereo guide set therefore
        // consists of two consecutive publications from that single stream.
        // Even local serials close the stable eye-0/eye-1 pair; odd serials are
        // deliberately left pending until the second eye arrives.
        if (eye_count == 2 && candidates.size() == 1) {
            const auto& current = candidates.front().frame;
            const auto& previous = candidates.front().previous;
            if (previous && current->stream == previous->stream &&
                current->epoch == previous->epoch &&
                current->serial == previous->serial + 1 &&
                (current->serial & 1U) == 0 &&
                current->publication > previous->publication &&
                current->publication - previous->publication <=
                    kMaximumStereoPublicationGap) {
                auto left = std::make_shared<DlssMotionVectorFrame>(*previous);
                auto right = std::make_shared<DlssMotionVectorFrame>(*current);
                // The first of the pair is the left eye unless the inputs
                // say otherwise.
                if (input_offset(*right) < input_offset(*left)) std::swap(left, right);
                left->previous_serial = left->serial > 2 ? left->serial - 2 : 0;
                right->previous_serial = right->serial > 2 ? right->serial - 2 : 0;
                auto result = std::make_shared<DlssMotionVectorSet>();
                result->eye_count = 2;
                result->eyes[0] = std::move(left);
                result->eyes[1] = std::move(right);
                ++state.statistics.matched;
                return result;
            }
        }

        if (candidates.size() < eye_count) {
            if (streams_with_frames >= eye_count) {
                ++state.statistics.resolve_queue_mismatches;
                state.statistics.status = DlssMotionVectorStatus::queue_mismatch;
            } else {
                ++state.statistics.resolve_missing_streams;
                state.statistics.status = state.statistics.published == 0
                    ? DlssMotionVectorStatus::waiting_for_dlss
                    : DlssMotionVectorStatus::output_not_direct;
            }
            return {};
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a,
                                                           const Candidate& b) {
            return a.frame->publication > b.frame->publication;
        });
        candidates.resize(eye_count);
        const auto newest = candidates.front().frame->publication;
        const auto oldest = candidates.back().frame->publication;
        if (newest < oldest || newest - oldest > kMaximumStereoPublicationGap) {
            return stale();
        }

        // Eye order follows the two DLSS evaluation streams rather than the
        // final colour resource, which survives any post-DLSS shader passes:
        // by where each stream's inputs sit, then by first-seen order.
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a,
                                                           const Candidate& b) {
            const auto ao = input_offset(*a.frame), bo = input_offset(*b.frame);
            if (ao != bo) return ao < bo;
            return a.first_publication < b.first_publication;
        });

        auto result = std::make_shared<DlssMotionVectorSet>();
        result->eye_count = eye_count;
        for (std::uint32_t index = 0; index < eye_count; ++index) {
            if (packed_stereo) {
                auto frame = std::make_shared<DlssMotionVectorFrame>(*candidates[index].frame);
                // Each mono producer uses local coordinates. Only the resolved
                // XR join maps colour to its half; native input rects stay.
                frame->output_x = index * frame->output_width;
                result->eyes[index] = std::move(frame);
            } else {
                result->eyes[index] = candidates[index].frame;
            }
        }
        ++state.statistics.matched;
        return result;
    } catch (...) {
        report_dlss_motion_vector_status(DlssMotionVectorStatus::invalid_input);
        return {};
    }
}

void report_dlss_motion_vector_status(DlssMotionVectorStatus status) noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        if (status == DlssMotionVectorStatus::temporal_mismatch) {
            ++state.statistics.temporal_rejections;
        } else if (status == DlssMotionVectorStatus::invalid_input) {
            ++state.statistics.invalid_rejections;
        }
        state.statistics.status = status;
    } catch (...) {
    }
}

void report_dlss_motion_vector_use() noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        ++state.statistics.used;
        state.statistics.last_used_publication = state.statistics.published;
        state.statistics.status = DlssMotionVectorStatus::used;
    } catch (...) {
    }
}

void report_dlss_hybrid_use() noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        ++state.statistics.used;
        ++state.statistics.hybrid_used;
        state.statistics.last_used_publication = state.statistics.published;
        state.statistics.status = DlssMotionVectorStatus::used;
    } catch (...) {
    }
}

DlssMotionVectorStatistics dlss_motion_vector_statistics() noexcept {
    try {
        auto& state = registry();
        std::scoped_lock lock(state.mutex);
        return state.statistics;
    } catch (...) {
        return {};
    }
}

}  // namespace xrfg
