#include "xrfg/ngx_guide_capture.hpp"

#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/dlss_motion_vectors.hpp"

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace xrfg {
namespace {

using Microsoft::WRL::ComPtr;

using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using EvaluateFeatureC = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback_C);
using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using CreateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using EvaluateFeature11C = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback_C);

// The modules whose exports are detoured, in slot order.
constexpr std::array<const wchar_t*, 3> kModuleNames{
    L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"_nvngx.dll"};

// Streams the capture publishes on, disjoint from a provider's: the provider
// chooses small numbers, these carry "NGX" in the top bytes.
constexpr std::uint64_t kStreamBase = 0x4E47580000000001ULL;

// Flight-log records (embedded_configuration):
//   820 a module's exports are detoured: a the slot
//   821 a feature was created: a the type, b the flags (bit 32: known),
//       c render width << 32 | height
//   822 a stream's first publication: a the stream's low bits, and from
//       V505 bit 32 depth came with it, 40-47 its format, 48 inverted, 49
//       its rectangle is the vectors'; b motion width << 32 | height, c
//       output width << 32 | height
//   823 a module's hook failed: a the slot
constexpr std::int64_t kRecordHooked = 820;
constexpr std::int64_t kRecordFeature = 821;
constexpr std::int64_t kRecordPublication = 822;
constexpr std::int64_t kRecordHookFailed = 823;
//   824 the first evaluation skipped for a reason: a the reason (1 unknown
//       feature, 2 missing resources, 3 nested in another hooked call, 4 the
//       vectors, the list and the queue not proven one device), b its count
//       so far, c for 2 which were present (bit 0 output, bit 1 motion
//       vectors); for 4 which pairs matched by pointer (bit 0 vectors and
//       list, bit 1 vectors and queue)
//   825 a feature's first evaluation: a output format << 48 | output array
//       size << 40 | motion format << 32 | the stream's low bits, b output
//       texture width << 32 | height, c motion array size << 56 | motion
//       texture width << 24 | height
//   829 an evaluation of a feature whose creation the capture did not see
//       (created before the layer hooked): the feature is registered from the
//       evaluation itself at the typed module - 821 with a = type + 100
//       follows - and this records its shapes once, as 825 does
//   826 every 1000th evaluation: a evaluations, b publications, c skipped
//       unknown << 40 | missing << 20 | nested
//   827 a publication the registry refused (first eight): a its invalid
//       publications so far, b its snapshot failures, c vector rect width
//       << 32 | height
//   830 (V512) the D3D11 path's first publication of a stream: a its low bits
//       | bit 32 the depth was shared | bit 33 the depth's format cannot be
//       shared (packed depth-stencil), b vectors format << 32 | depth format,
//       c output width << 32 | height
//   824 reasons 5 (the evaluation's D3D11 context is not the bridged
//       session's immediate context) and 6 (the copy into the shared textures
//       failed: c its HRESULT)
constexpr std::int64_t kRecordD3D11Publication = 830;
constexpr std::int64_t kRecordSkip = 824;
constexpr std::int64_t kRecordShape = 825;
constexpr std::int64_t kRecordSummary = 826;
constexpr std::int64_t kRecordRefused = 827;
constexpr std::int64_t kRecordUnknownShape = 829;

struct Module {
    SafetyHookInline create_hook;
    SafetyHookInline evaluate_hook;
    SafetyHookInline evaluate_c_hook;
    SafetyHookInline release_hook;
    CreateFeature create{};
    EvaluateFeature evaluate{};
    EvaluateFeatureC evaluate_c{};
    ReleaseFeature release{};
    bool installed{};
    bool failed{};
};

class GuideTransport11;

struct Feature {
    NVSDK_NGX_Feature type{NVSDK_NGX_Feature_SuperSampling};
    int flags{};
    bool flags_known{};
    unsigned width{}, height{}, out_width{}, out_height{};
    std::uint64_t stream{};
    std::uint64_t publications{};
    bool shape_recorded{};
    // D3D11 (V512): the shared textures this feature's evaluations go
    // through, and a stand-in for its output on the D3D12 device.
    std::shared_ptr<GuideTransport11> transport;
    ComPtr<ID3D12Resource> output_placeholder;
};

struct Module11 {
    SafetyHookInline create_hook;
    SafetyHookInline evaluate_hook;
    SafetyHookInline evaluate_c_hook;
    SafetyHookInline release_hook;
    CreateFeature11 create{};
    EvaluateFeature11 evaluate{};
    EvaluateFeature11C evaluate_c{};
    ReleaseFeature release{};
    bool installed{};
    bool failed{};
};

struct Capture {
    std::mutex mutex;
    ComPtr<ID3D12CommandQueue> queue;
    std::atomic_bool active{false};
    std::atomic_bool monitor_started{false};
    std::array<Module, kModuleNames.size()> modules{};
    // D3D11 (V512): the bridged session's game device, and the layer's
    // D3D12 device and queue the shared textures and publications live on.
    std::array<Module11, kModuleNames.size()> modules11{};
    ComPtr<ID3D11Device> d3d11_device;
    ComPtr<ID3D12Device> bridge_device;
    ComPtr<ID3D12CommandQueue> bridge_queue;
    std::atomic_bool d3d11_active{false};
    std::unordered_map<const NVSDK_NGX_Handle*, Feature> features;
    std::uint64_t next_stream{kStreamBase};
    NgxGuideCaptureStatistics statistics;
    std::array<bool, 7> skip_recorded{};
    std::uint64_t refusals_recorded{};
};

// Never destroyed: the hooks and their trampolines outlive every static
// destructor, and no unhook runs under the loader lock at exit.
Capture& state() noexcept {
    static auto* value = new Capture;
    return *value;
}

// Nesting of hooked calls on this thread: the core's export forwards to the
// feature module's, and both are detoured. Only the outermost is captured.
thread_local unsigned t_depth = 0;

struct DepthScope {
    DepthScope() noexcept { ++t_depth; }
    ~DepthScope() { --t_depth; }
    DepthScope(const DepthScope&) = delete;
    DepthScope& operator=(const DepthScope&) = delete;
    [[nodiscard]] bool outermost() const noexcept { return t_depth == 1; }
};

void record(std::int64_t result, std::uint64_t a = 0, std::uint64_t b = 0,
    std::uint64_t c = 0) noexcept {
    try {
        bridge_flight_logger().event(
            BridgeFlightOperation::embedded_configuration, result, a, b, c);
    } catch (...) {
    }
}

template <class T>
T get(const NVSDK_NGX_Parameter* parameters, const char* name, T fallback = {}) noexcept {
    T value = fallback;
    try {
        if (parameters == nullptr || !NVSDK_NGX_SUCCEED(parameters->Get(name, &value))) {
            return fallback;
        }
    } catch (...) {
        return fallback;
    }
    return value;
}

[[nodiscard]] bool guide_feature(NVSDK_NGX_Feature type) noexcept {
    return type == NVSDK_NGX_Feature_SuperSampling ||
        type == NVSDK_NGX_Feature_RayReconstruction;
}

[[nodiscard]] unsigned available(UINT64 extent, unsigned base) noexcept {
    return static_cast<unsigned>(extent - std::min<UINT64>(base, extent));
}

// A texture's shape, from either API.
struct Shape {
    UINT64 width{};
    UINT height{};
};

// The rectangles, scale, jitter and flags of a publication, from the
// feature's creation and the evaluation's parameters: the same for the
// D3D12 and the D3D11 entry points. Depth only where `depth` is given.
void fill_publication(
    DlssMotionVectorPublication& publication,
    const Feature& feature,
    const NVSDK_NGX_Parameter* parameters,
    Shape output,
    Shape motion,
    const Shape* depth) noexcept {
    publication.stream = feature.stream;
    publication.output_x =
        get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X);
    publication.output_y =
        get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y);
    // The creation's display size, else the allocation's.
    publication.output_width = std::min(
        feature.out_width != 0 ? feature.out_width : static_cast<unsigned>(output.width),
        available(output.width, publication.output_x));
    publication.output_height = std::min(
        feature.out_height != 0 ? feature.out_height : output.height,
        available(output.height, publication.output_y));
    publication.motion_x =
        get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X);
    publication.motion_y =
        get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y);
    const unsigned render_width = get<unsigned>(
        parameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, feature.width);
    const unsigned render_height = get<unsigned>(
        parameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, feature.height);
    // Without creation flags, assume render-resolution vectors: the usual
    // DLSS integration, and the one whose rectangle the render size gives.
    const bool full_resolution_vectors = feature.flags_known &&
        (feature.flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0;
    publication.motion_width = std::min(
        full_resolution_vectors
            ? publication.output_width
            : (render_width != 0 ? render_width : static_cast<unsigned>(motion.width)),
        available(motion.width, publication.motion_x));
    publication.motion_height = std::min(
        full_resolution_vectors
            ? publication.output_height
            : (render_height != 0 ? render_height : motion.height),
        available(motion.height, publication.motion_y));
    publication.scale_x = get<float>(parameters, NVSDK_NGX_Parameter_MV_Scale_X, 1.0F);
    publication.scale_y = get<float>(parameters, NVSDK_NGX_Parameter_MV_Scale_Y, 1.0F);
    publication.jitter_x = get<float>(parameters, NVSDK_NGX_Parameter_Jitter_Offset_X);
    publication.jitter_y = get<float>(parameters, NVSDK_NGX_Parameter_Jitter_Offset_Y);
    publication.jittered = (feature.flags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered) != 0;
    publication.reset = get<int>(parameters, NVSDK_NGX_Parameter_Reset) != 0;
    if (depth != nullptr) {
        publication.depth_x =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X);
        publication.depth_y =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y);
        publication.depth_width = std::min(
            render_width != 0 ? render_width : static_cast<unsigned>(depth->width),
            available(depth->width, publication.depth_x));
        publication.depth_height = std::min(
            render_height != 0 ? render_height : depth->height,
            available(depth->height, publication.depth_y));
        publication.depth_inverted =
            (feature.flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    }
}

// The publication for one evaluation. Before the original runs: the inputs
// are complete, and the snapshot copy then precedes DLSS on the list.
void capture(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    try {
        auto& s = state();
        if (!s.active.load(std::memory_order_acquire) || list == nullptr ||
            handle == nullptr || parameters == nullptr) {
            return;
        }
        Feature feature;
        ComPtr<ID3D12CommandQueue> queue;
        bool first_evaluation = false;
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.evaluations;
            if (s.statistics.evaluations % 1000 == 0) {
                record(kRecordSummary, s.statistics.evaluations, s.statistics.publications,
                    (s.statistics.skipped_unknown_feature << 40U) |
                        ((s.statistics.skipped_missing_resources & 0xFFFFFULL) << 20U) |
                        (s.statistics.skipped_nested & 0xFFFFFULL));
            }
            const auto found = s.features.find(handle);
            if (found == s.features.end()) {
                ++s.statistics.skipped_unknown_feature;
                if (!s.skip_recorded[1]) {
                    s.skip_recorded[1] = true;
                    record(kRecordSkip, 1, s.statistics.skipped_unknown_feature);
                    ID3D12Resource* const unknown_output =
                        get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Output);
                    ID3D12Resource* const unknown_motion =
                        get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_MotionVectors);
                    if (unknown_output != nullptr && unknown_motion != nullptr) {
                        const auto od = unknown_output->GetDesc();
                        const auto md = unknown_motion->GetDesc();
                        record(kRecordUnknownShape,
                            (static_cast<std::uint64_t>(od.Format) << 48U) |
                                (static_cast<std::uint64_t>(od.DepthOrArraySize) << 40U) |
                                (static_cast<std::uint64_t>(md.Format) << 32U),
                            (static_cast<std::uint64_t>(od.Width) << 32U) | od.Height,
                            (static_cast<std::uint64_t>(md.DepthOrArraySize) << 56U) |
                                (static_cast<std::uint64_t>(md.Width) << 24U) | md.Height);
                    }
                }
                return;
            }
            feature = found->second;
            queue = s.queue;
            first_evaluation = !found->second.shape_recorded;
            found->second.shape_recorded = true;
        }
        if (!queue) {
            return;
        }
        ID3D12Resource* const output =
            get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Output);
        ID3D12Resource* const motion =
            get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_MotionVectors);
        if (output == nullptr || motion == nullptr) {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.skipped_missing_resources;
            if (!s.skip_recorded[2]) {
                s.skip_recorded[2] = true;
                record(kRecordSkip, 2, s.statistics.skipped_missing_resources,
                    (output != nullptr ? 1U : 0U) | (motion != nullptr ? 2U : 0U));
            }
            return;
        }
        const D3D12_RESOURCE_DESC output_description = output->GetDesc();
        const D3D12_RESOURCE_DESC motion_description = motion->GetDesc();
        if (first_evaluation) {
            record(kRecordShape,
                (static_cast<std::uint64_t>(output_description.Format) << 48U) |
                    (static_cast<std::uint64_t>(output_description.DepthOrArraySize) << 40U) |
                    (static_cast<std::uint64_t>(motion_description.Format) << 32U) |
                    (feature.stream & 0xFFFFFFFFULL),
                (static_cast<std::uint64_t>(output_description.Width) << 32U) |
                    output_description.Height,
                (static_cast<std::uint64_t>(motion_description.DepthOrArraySize) << 56U) |
                    (static_cast<std::uint64_t>(motion_description.Width) << 24U) |
                    motion_description.Height);
        }

        // The game's DLSS runs on its real device; an interposer such as
        // Streamline (MSFS 2024) hands the game, and so the OpenXR session, a
        // proxy device and proxy queues. Prove the three share one device
        // underneath and allocate the snapshot on the resource's own.
        ComPtr<ID3D12Device> motion_device, list_device, queue_device;
        if (FAILED(motion->GetDevice(IID_PPV_ARGS(motion_device.GetAddressOf()))) ||
            FAILED(list->GetDevice(IID_PPV_ARGS(list_device.GetAddressOf()))) ||
            FAILED(queue->GetDevice(IID_PPV_ARGS(queue_device.GetAddressOf()))) ||
            !same_d3d12_device_backing(motion_device.Get(), list_device.Get()) ||
            !same_d3d12_device_backing(motion_device.Get(), queue_device.Get())) {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.skipped_device_mismatch;
            if (!s.skip_recorded[4]) {
                s.skip_recorded[4] = true;
                record(kRecordSkip, 4, s.statistics.skipped_device_mismatch,
                    (motion_device.Get() == list_device.Get() ? 1U : 0U) |
                    (motion_device.Get() == queue_device.Get() ? 2U : 0U));
            }
            return;
        }

        DlssMotionVectorPublication publication{};
        // And its depth, at render resolution (V505): what orders two surfaces
        // that both reach a pixel - a thin bar over the background behind it.
        auto* const depth = get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Depth);
        Shape depth_shape{};
        if (depth != nullptr) {
            const D3D12_RESOURCE_DESC depth_description = depth->GetDesc();
            depth_shape = {depth_description.Width, depth_description.Height};
        }
        fill_publication(publication, feature, parameters,
            {output_description.Width, output_description.Height},
            {motion_description.Width, motion_description.Height},
            depth != nullptr ? &depth_shape : nullptr);
        publication.output = output;
        publication.motion_vectors = motion;
        publication.producer_queue = queue.Get();
        publication.producer_command_list = list;
        publication.verified_producer_device = motion_device.Get();
        // DLSS reads its vectors as a shader resource; the game put them in
        // that state for it.
        publication.resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (depth != nullptr) {
            publication.depth = depth;
            publication.depth_resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }

        const auto before = dlss_motion_vector_statistics();
        publish_dlss_motion_vectors(publication);
        const auto after = dlss_motion_vector_statistics();
        if (after.published == before.published) {
            std::scoped_lock lock(s.mutex);
            if (s.refusals_recorded++ < 8) {
                record(kRecordRefused, after.invalid_publications, after.snapshot_failures,
                    (static_cast<std::uint64_t>(publication.motion_width) << 32U) |
                        publication.motion_height);
            }
            return;
        }
        std::scoped_lock lock(s.mutex);
        ++s.statistics.publications;
        const auto found = s.features.find(handle);
        if (found != s.features.end() && found->second.publications++ == 0) {
            // a's upper bits (V505): 32 the game's depth came with it, 40-47
            // its DXGI format, 48 inverted, 49 its rectangle is the vectors'.
            std::uint64_t depth_bits = 0;
            if (publication.depth != nullptr) {
                depth_bits = (1ULL << 32U) |
                    (static_cast<std::uint64_t>(publication.depth->GetDesc().Format & 0xFFU) << 40U) |
                    (publication.depth_inverted ? 1ULL << 48U : 0ULL) |
                    (publication.depth_x == publication.motion_x &&
                     publication.depth_y == publication.motion_y &&
                     publication.depth_width == publication.motion_width &&
                     publication.depth_height == publication.motion_height ? 1ULL << 49U : 0ULL);
            }
            record(kRecordPublication,
                (feature.stream & 0xFFFFFFFFULL) | depth_bits,
                (static_cast<std::uint64_t>(publication.motion_width) << 32U) |
                    publication.motion_height,
                (static_cast<std::uint64_t>(publication.output_width) << 32U) |
                    publication.output_height);
        }
    } catch (...) {
    }
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_create(
    ID3D12GraphicsCommandList* list,
    NVSDK_NGX_Feature type,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    DepthScope depth;
    const NVSDK_NGX_Result result = state().modules[Slot].create(list, type, parameters, handle);
    try {
        if (depth.outermost() && NVSDK_NGX_SUCCEED(result) && handle != nullptr &&
            *handle != nullptr && guide_feature(type)) {
            Feature feature;
            feature.type = type;
            int flags = 0;
            feature.flags_known = parameters != nullptr &&
                NVSDK_NGX_SUCCEED(
                    parameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags));
            feature.flags = flags;
            feature.width = get<unsigned>(parameters, NVSDK_NGX_Parameter_Width);
            feature.height = get<unsigned>(parameters, NVSDK_NGX_Parameter_Height);
            feature.out_width = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutWidth);
            feature.out_height = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutHeight);
            auto& s = state();
            std::scoped_lock lock(s.mutex);
            feature.stream = s.next_stream++;
            s.features[*handle] = feature;
            ++s.statistics.features_created;
            record(kRecordFeature, static_cast<std::uint64_t>(type),
                (feature.flags_known ? 1ULL << 32U : 0ULL) |
                    static_cast<std::uint32_t>(feature.flags),
                (static_cast<std::uint64_t>(feature.width) << 32U) | feature.height);
        }
    } catch (...) {
    }
    return result;
}

// A feature created before the layer hooked - the game started DLSS on the
// desktop, or an earlier layer instance saw its creation - evaluates on a
// handle the capture never registered. The typed module (nvngx_dlss.dll for
// super resolution, nvngx_dlssd.dll for ray reconstruction) identifies the
// type by itself, and the game's parameter object usually still carries the
// creation keys, so the feature is registered from its evaluation. The
// evaluation that registers it is not captured; the next one is.
template <unsigned Slot>
void register_late(const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters) noexcept {
    if constexpr (Slot >= 2) {
        return;
    } else {
        try {
            auto& s = state();
            if (!s.active.load(std::memory_order_acquire) || handle == nullptr ||
                parameters == nullptr) {
                return;
            }
            {
                std::scoped_lock lock(s.mutex);
                if (s.features.find(handle) != s.features.end()) {
                    return;
                }
            }
            Feature feature;
            feature.type = Slot == 0 ? NVSDK_NGX_Feature_SuperSampling
                                     : NVSDK_NGX_Feature_RayReconstruction;
            int flags = 0;
            feature.flags_known = NVSDK_NGX_SUCCEED(
                parameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags));
            feature.flags = flags;
            feature.width = get<unsigned>(parameters, NVSDK_NGX_Parameter_Width);
            feature.height = get<unsigned>(parameters, NVSDK_NGX_Parameter_Height);
            feature.out_width = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutWidth);
            feature.out_height = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutHeight);
            std::scoped_lock lock(s.mutex);
            if (s.features.find(handle) != s.features.end()) {
                return;
            }
            feature.stream = s.next_stream++;
            s.features[handle] = feature;
            ++s.statistics.features_created;
            record(kRecordFeature, static_cast<std::uint64_t>(feature.type) + 100U,
                (feature.flags_known ? 1ULL << 32U : 0ULL) |
                    static_cast<std::uint32_t>(feature.flags),
                (static_cast<std::uint64_t>(feature.width) << 32U) | feature.height);
        } catch (...) {
        }
    }
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters);
    if (depth.outermost()) {
        capture(list, handle, parameters);
    } else {
        try {
            auto& s = state();
            if (s.active.load(std::memory_order_acquire)) {
                std::scoped_lock lock(s.mutex);
                ++s.statistics.skipped_nested;
                if (!s.skip_recorded[3]) {
                    s.skip_recorded[3] = true;
                    record(kRecordSkip, 3, s.statistics.skipped_nested);
                }
            }
        } catch (...) {
        }
    }
    return state().modules[Slot].evaluate(list, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate_c(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters);
    if (depth.outermost()) {
        capture(list, handle, parameters);
    }
    return state().modules[Slot].evaluate_c(list, handle, parameters, callback);
}


// ------------------------------------------------------------------ D3D11
// A bridged D3D11 session (V512). The game's DLSS runs on its D3D11 device;
// the runtime, the history and the synthesis run on the layer's D3D12
// device (SessionState::d3d11_bridge). At each evaluation the vectors - and
// the depth, where the driver shares its format - are copied on the game's
// immediate context into textures shared with that D3D12 device, and the
// context signals a shared fence. The registry's snapshot copy is recorded
// then but submitted to the D3D12 queue only at the next image release of
// the bridged session (flush_ngx_guide_capture_d3d11), behind the bridge's
// own wait for the game's context: submitted at the evaluation, the queue's
// wait for the game's copy would sit mid-frame in front of the presenter's
// hand-overs. The queue still waits on the fence first, which by then is
// passed. Two slots per feature alternate, so a frame's copy never writes
// what the previous frame's snapshot may still be reading; nothing waits on
// the CPU unless the slot's last snapshot copy, a frame old, has not run.

// What the driver will share of a depth or vector format: the depth families
// as their typeless form; the packed depth-stencil families not at all (as
// the D3D11 bridge measured with Ready or Not's D32_FLOAT_S8X24_UINT).
[[nodiscard]] DXGI_FORMAT shareable_format(DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
            return format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_TYPELESS : format;
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            return DXGI_FORMAT_UNKNOWN;
        default:
            return format;
    }
}

[[nodiscard]] bool same_object(IUnknown* left, IUnknown* right) noexcept {
    if (left == nullptr || right == nullptr) return false;
    ComPtr<IUnknown> a, b;
    left->QueryInterface(IID_PPV_ARGS(a.GetAddressOf()));
    right->QueryInterface(IID_PPV_ARGS(b.GetAddressOf()));
    return a && a.Get() == b.Get();
}

class GuideTransport11 {
public:
    struct Staged {
        ID3D12Resource* motion{};
        ID3D12Resource* depth{};
        ID3D12GraphicsCommandList* list{};
        bool depth_unshareable{};
    };

    GuideTransport11() = default;
    GuideTransport11(const GuideTransport11&) = delete;
    GuideTransport11& operator=(const GuideTransport11&) = delete;

    ~GuideTransport11() {
        // A snapshot copy still queued reads these textures: wait for it, and
        // if the GPU never gets there keep everything rather than free it.
        if (submitted_ != 0 && to11_12_ && to11_12_->GetCompletedValue() < submitted_) {
            const bool done = event_ != nullptr &&
                SUCCEEDED(to11_12_->SetEventOnCompletion(submitted_, event_)) &&
                WaitForSingleObject(event_, 2000) == WAIT_OBJECT_0;
            if (!done) {
                static auto* kept = new std::vector<std::array<Slot, 2>>;
                kept->push_back(std::move(slots_));
            }
        }
        if (event_ != nullptr) CloseHandle(event_);
    }

    [[nodiscard]] HRESULT initialize(
        ID3D11Device* game, ID3D12Device* device, ID3D12CommandQueue* queue) noexcept {
        try {
            if (game == nullptr || device == nullptr || queue == nullptr) return E_INVALIDARG;
            HRESULT result = game->QueryInterface(IID_PPV_ARGS(d3d11_.GetAddressOf()));
            if (FAILED(result)) return result;
            ComPtr<ID3D11Device5> game5;
            result = game->QueryInterface(IID_PPV_ARGS(game5.GetAddressOf()));
            if (FAILED(result)) return result;
            device_ = device;
            queue_ = queue;
            // D3D11 to D3D12: the copy is done.
            result = game5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                IID_PPV_ARGS(to12_11_.GetAddressOf()));
            if (FAILED(result)) return result;
            HANDLE handle = nullptr;
            result = to12_11_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
            if (FAILED(result)) return result;
            result = device->OpenSharedHandle(handle, IID_PPV_ARGS(to12_12_.GetAddressOf()));
            CloseHandle(handle);
            if (FAILED(result)) return result;
            // D3D12 back: the snapshot copy has read the slot.
            result = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                IID_PPV_ARGS(to11_12_.GetAddressOf()));
            if (FAILED(result)) return result;
            for (Slot& slot : slots_) {
                result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(slot.allocator.GetAddressOf()));
                if (FAILED(result)) return result;
                result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    slot.allocator.Get(), nullptr, IID_PPV_ARGS(slot.list.GetAddressOf()));
                if (FAILED(result)) return result;
                result = slot.list->Close();
                if (FAILED(result)) return result;
            }
            event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            return event_ != nullptr ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        } catch (...) {
            return E_FAIL;
        }
    }

    // On the game's immediate context, before its DLSS: the copies, and an
    // open list on the D3D12 queue that runs after them, for the registry's
    // snapshot copies. finish() submits it.
    [[nodiscard]] HRESULT stage(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* motion,
        ID3D11Texture2D* depth,
        Staged* staged) noexcept {
        try {
            *staged = {};
            ComPtr<ID3D11DeviceContext4> context4;
            HRESULT result = context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf()));
            if (FAILED(result)) return result;
            std::scoped_lock lock(mutex_);
            Slot& slot = slots_[next_];
            // Its previous snapshot copy never reached a release: submit it.
            if (slot.pending) submit(slot);
            // The slot's previous snapshot copy, two evaluations back: long
            // done in practice; the allocator cannot be reset before it is.
            if (slot.read_done != 0 && to11_12_->GetCompletedValue() < slot.read_done) {
                result = to11_12_->SetEventOnCompletion(slot.read_done, event_);
                if (FAILED(result)) return result;
                if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0) {
                    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                }
            }
            result = prepare(slot.motion, motion, false);
            if (FAILED(result)) return result;
            bool depth_ready = false;
            if (depth != nullptr) {
                const HRESULT depth_result = prepare(slot.depth, depth, true);
                depth_ready = SUCCEEDED(depth_result);
                staged->depth_unshareable = depth_result == DXGI_ERROR_UNSUPPORTED;
            }
            copy(context, slot.motion, motion);
            if (depth_ready) copy(context, slot.depth, depth);
            result = context4->Signal(to12_11_.Get(), ++copied_);
            if (FAILED(result)) return result;
            context->Flush();
            slot.copied = copied_;
            result = slot.allocator->Reset();
            if (FAILED(result)) return result;
            result = slot.list->Reset(slot.allocator.Get(), nullptr);
            if (FAILED(result)) return result;
            recording_ = true;
            staged->motion = slot.motion.d3d12.Get();
            staged->depth = depth_ready ? slot.depth.d3d12.Get() : nullptr;
            staged->list = slot.list.Get();
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    // The registry has recorded into the list: close it for the next release.
    [[nodiscard]] HRESULT finish() noexcept {
        std::scoped_lock lock(mutex_);
        if (!recording_) return E_UNEXPECTED;
        recording_ = false;
        Slot& slot = slots_[next_];
        const HRESULT result = slot.list->Close();
        slot.pending = SUCCEEDED(result);
        next_ ^= 1U;
        return result;
    }

    // At an image release of the bridged session: every closed list goes to
    // the queue, behind the bridge's wait for the game's context.
    void flush() noexcept {
        std::scoped_lock lock(mutex_);
        for (Slot& slot : slots_) {
            if (slot.pending) submit(slot);
        }
    }

private:
    struct Shared {
        ComPtr<ID3D11Texture2D> d3d11;
        ComPtr<ID3D12Resource> d3d12;
        D3D11_TEXTURE2D_DESC source{};
    };
    struct Slot {
        Shared motion;
        Shared depth;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::uint64_t copied{};
        std::uint64_t read_done{};
        bool pending{};
    };

    void submit(Slot& slot) noexcept {
        slot.pending = false;
        if (FAILED(queue_->Wait(to12_12_.Get(), slot.copied))) return;
        ID3D12CommandList* lists[]{slot.list.Get()};
        queue_->ExecuteCommandLists(1, lists);
        if (SUCCEEDED(queue_->Signal(to11_12_.Get(), ++submitted_))) {
            slot.read_done = submitted_;
        }
    }

    // A single-mip texture of the source's size, format family and slices,
    // allocated on the D3D12 device and opened on the game's.
    [[nodiscard]] HRESULT prepare(Shared& shared, ID3D11Texture2D* source, bool depth) noexcept {
        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        if (description.SampleDesc.Count != 1) return DXGI_ERROR_UNSUPPORTED;
        const auto& old = shared.source;
        if (shared.d3d12 && old.Width == description.Width &&
            old.Height == description.Height && old.Format == description.Format &&
            old.ArraySize == description.ArraySize && old.MipLevels == description.MipLevels) {
            return S_OK;
        }
        const DXGI_FORMAT format = depth ? shareable_format(description.Format) : description.Format;
        if (format == DXGI_FORMAT_UNKNOWN) return DXGI_ERROR_UNSUPPORTED;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = description.Width;
        desc.Height = description.Height;
        desc.DepthOrArraySize = static_cast<UINT16>(description.ArraySize);
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Shared replacement;
        HRESULT result = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(replacement.d3d12.GetAddressOf()));
        if (FAILED(result)) return result;
        HANDLE handle = nullptr;
        result = device_->CreateSharedHandle(replacement.d3d12.Get(), nullptr, GENERIC_ALL,
            nullptr, &handle);
        if (FAILED(result)) return result;
        result = d3d11_->OpenSharedResource1(handle, IID_PPV_ARGS(replacement.d3d11.GetAddressOf()));
        CloseHandle(handle);
        if (FAILED(result)) return result;
        replacement.source = description;
        shared = std::move(replacement);
        return S_OK;
    }

    static void copy(ID3D11DeviceContext* context, const Shared& shared, ID3D11Texture2D* source) {
        if (shared.source.MipLevels == 1) {
            context->CopyResource(shared.d3d11.Get(), source);
            return;
        }
        for (UINT slice = 0; slice < shared.source.ArraySize; ++slice) {
            context->CopySubresourceRegion(shared.d3d11.Get(), slice, 0, 0, 0, source,
                D3D11CalcSubresource(0, slice, shared.source.MipLevels), nullptr);
        }
    }

    ComPtr<ID3D11Device1> d3d11_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D11Fence> to12_11_;
    ComPtr<ID3D12Fence> to12_12_;
    ComPtr<ID3D12Fence> to11_12_;
    std::array<Slot, 2> slots_{};
    std::uint32_t next_{};
    std::uint64_t copied_{};
    std::uint64_t submitted_{};
    bool recording_{};
    HANDLE event_{};
    std::mutex mutex_;
};

// The output on the D3D12 side: the resolver matches an evaluation to an eye
// image by its size, and nothing reads it, so it is a reserved resource with
// no memory behind it (a committed single-byte texture where the device has
// no tiled resources).
[[nodiscard]] ComPtr<ID3D12Resource> output_placeholder(
    ID3D12Device* device, const D3D11_TEXTURE2D_DESC& output) noexcept {
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = output.Width;
    desc.Height = output.Height;
    desc.DepthOrArraySize = static_cast<UINT16>(std::max<UINT>(output.ArraySize, 1));
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    if (FAILED(device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(resource.GetAddressOf())))) {
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.GetAddressOf())))) {
            resource.Reset();
        }
    }
    return resource;
}

void skip_d3d11(std::size_t reason, std::uint64_t detail = 0) noexcept {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    std::uint64_t count = 0;
    if (reason == 5) {
        count = ++s.statistics.skipped_device_mismatch;
    } else {
        count = ++s.statistics.d3d11_transport_failures;
    }
    if (!s.skip_recorded[reason]) {
        s.skip_recorded[reason] = true;
        record(kRecordSkip, reason, count, detail);
    }
}

void capture11(
    ID3D11DeviceContext* context,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    try {
        auto& s = state();
        if (!s.d3d11_active.load(std::memory_order_acquire) || context == nullptr ||
            handle == nullptr || parameters == nullptr) {
            return;
        }
        Feature feature;
        ComPtr<ID3D11Device> game;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.evaluations;
            if (s.statistics.evaluations % 1000 == 0) {
                record(kRecordSummary, s.statistics.evaluations, s.statistics.publications,
                    (s.statistics.skipped_unknown_feature << 40U) |
                        ((s.statistics.skipped_missing_resources & 0xFFFFFULL) << 20U) |
                        (s.statistics.skipped_nested & 0xFFFFFULL));
            }
            const auto found = s.features.find(handle);
            if (found == s.features.end()) {
                ++s.statistics.skipped_unknown_feature;
                if (!s.skip_recorded[1]) {
                    s.skip_recorded[1] = true;
                    record(kRecordSkip, 1, s.statistics.skipped_unknown_feature);
                }
                return;
            }
            feature = found->second;
            game = s.d3d11_device;
            device = s.bridge_device;
            queue = s.bridge_queue;
        }
        if (!game || !device || !queue) return;
        ComPtr<ID3D11Texture2D> output, motion, depth;
        if (auto* resource = get<ID3D11Resource*>(parameters, NVSDK_NGX_Parameter_Output)) {
            resource->QueryInterface(IID_PPV_ARGS(output.GetAddressOf()));
        }
        if (auto* resource = get<ID3D11Resource*>(parameters, NVSDK_NGX_Parameter_MotionVectors)) {
            resource->QueryInterface(IID_PPV_ARGS(motion.GetAddressOf()));
        }
        if (auto* resource = get<ID3D11Resource*>(parameters, NVSDK_NGX_Parameter_Depth)) {
            resource->QueryInterface(IID_PPV_ARGS(depth.GetAddressOf()));
        }
        if (!output || !motion) {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.skipped_missing_resources;
            if (!s.skip_recorded[2]) {
                s.skip_recorded[2] = true;
                record(kRecordSkip, 2, s.statistics.skipped_missing_resources,
                    (output ? 1U : 0U) | (motion ? 2U : 0U));
            }
            return;
        }
        // The copies go on the context DLSS runs on; it has to be the
        // session's device's immediate context, whose order they keep.
        ComPtr<ID3D11Device> context_device;
        context->GetDevice(context_device.GetAddressOf());
        if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            !same_object(context_device.Get(), game.Get())) {
            skip_d3d11(5);
            return;
        }
        D3D11_TEXTURE2D_DESC output_description{}, motion_description{}, depth_description{};
        output->GetDesc(&output_description);
        motion->GetDesc(&motion_description);
        if (depth) depth->GetDesc(&depth_description);

        std::shared_ptr<GuideTransport11> transport;
        ComPtr<ID3D12Resource> placeholder;
        bool first_evaluation = false;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found == s.features.end()) return;
            Feature& stored = found->second;
            first_evaluation = !stored.shape_recorded;
            stored.shape_recorded = true;
            if (!stored.transport) {
                auto created = std::make_shared<GuideTransport11>();
                const HRESULT result = created->initialize(game.Get(), device.Get(), queue.Get());
                if (FAILED(result)) {
                    ++s.statistics.d3d11_transport_failures;
                    if (!s.skip_recorded[6]) {
                        s.skip_recorded[6] = true;
                        record(kRecordSkip, 6, s.statistics.d3d11_transport_failures,
                            static_cast<std::uint32_t>(result));
                    }
                    return;
                }
                stored.transport = std::move(created);
            }
            if (!stored.output_placeholder ||
                stored.output_placeholder->GetDesc().Width != output_description.Width ||
                stored.output_placeholder->GetDesc().Height != output_description.Height) {
                stored.output_placeholder = output_placeholder(device.Get(), output_description);
            }
            transport = stored.transport;
            placeholder = stored.output_placeholder;
        }
        if (first_evaluation) {
            record(kRecordShape,
                (static_cast<std::uint64_t>(output_description.Format) << 48U) |
                    (static_cast<std::uint64_t>(output_description.ArraySize) << 40U) |
                    (static_cast<std::uint64_t>(motion_description.Format) << 32U) |
                    (feature.stream & 0xFFFFFFFFULL),
                (static_cast<std::uint64_t>(output_description.Width) << 32U) |
                    output_description.Height,
                (static_cast<std::uint64_t>(motion_description.ArraySize) << 56U) |
                    (static_cast<std::uint64_t>(motion_description.Width) << 24U) |
                    motion_description.Height);
        }
        if (!placeholder) {
            skip_d3d11(6, static_cast<std::uint32_t>(E_OUTOFMEMORY));
            return;
        }
        GuideTransport11::Staged staged{};
        const HRESULT staged_result =
            transport->stage(context, motion.Get(), depth.Get(), &staged);
        if (FAILED(staged_result)) {
            skip_d3d11(6, static_cast<std::uint32_t>(staged_result));
            return;
        }

        DlssMotionVectorPublication publication{};
        Shape depth_shape{depth_description.Width, depth_description.Height};
        fill_publication(publication, feature, parameters,
            {output_description.Width, output_description.Height},
            {motion_description.Width, motion_description.Height},
            staged.depth != nullptr ? &depth_shape : nullptr);
        publication.output = placeholder.Get();
        publication.motion_vectors = staged.motion;
        publication.producer_queue = queue.Get();
        publication.producer_command_list = staged.list;
        publication.verified_producer_device = device.Get();
        // Shared simultaneous-access textures, last touched by the D3D11 copy.
        publication.resource_state = D3D12_RESOURCE_STATE_COMMON;
        if (staged.depth != nullptr) {
            publication.depth = staged.depth;
            publication.depth_resource_state = D3D12_RESOURCE_STATE_COMMON;
        }
        const auto before = dlss_motion_vector_statistics();
        publish_dlss_motion_vectors(publication);
        const auto after = dlss_motion_vector_statistics();
        static_cast<void>(transport->finish());
        if (after.published == before.published) {
            std::scoped_lock lock(s.mutex);
            if (s.refusals_recorded++ < 8) {
                record(kRecordRefused, after.invalid_publications, after.snapshot_failures,
                    (static_cast<std::uint64_t>(publication.motion_width) << 32U) |
                        publication.motion_height);
            }
            return;
        }
        std::scoped_lock lock(s.mutex);
        ++s.statistics.publications;
        ++s.statistics.d3d11_publications;
        const auto found = s.features.find(handle);
        if (found != s.features.end() && found->second.publications++ == 0) {
            record(kRecordD3D11Publication,
                (feature.stream & 0xFFFFFFFFULL) |
                    (staged.depth != nullptr ? 1ULL << 32U : 0ULL) |
                    (staged.depth_unshareable ? 1ULL << 33U : 0ULL),
                (static_cast<std::uint64_t>(motion_description.Format) << 32U) |
                    static_cast<std::uint64_t>(depth ? depth_description.Format : 0),
                (static_cast<std::uint64_t>(publication.output_width) << 32U) |
                    publication.output_height);
        }
    } catch (...) {
    }
}

void register_feature(
    NVSDK_NGX_Feature type,
    const NVSDK_NGX_Parameter* parameters,
    const NVSDK_NGX_Handle* handle) noexcept {
    try {
        Feature feature;
        feature.type = type;
        int flags = 0;
        feature.flags_known = parameters != nullptr &&
            NVSDK_NGX_SUCCEED(parameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags));
        feature.flags = flags;
        feature.width = get<unsigned>(parameters, NVSDK_NGX_Parameter_Width);
        feature.height = get<unsigned>(parameters, NVSDK_NGX_Parameter_Height);
        feature.out_width = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutWidth);
        feature.out_height = get<unsigned>(parameters, NVSDK_NGX_Parameter_OutHeight);
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        feature.stream = s.next_stream++;
        s.features[handle] = feature;
        ++s.statistics.features_created;
        record(kRecordFeature, static_cast<std::uint64_t>(type) + 200U,
            (feature.flags_known ? 1ULL << 32U : 0ULL) | static_cast<std::uint32_t>(feature.flags),
            (static_cast<std::uint64_t>(feature.width) << 32U) | feature.height);
    } catch (...) {
    }
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_create11(
    ID3D11DeviceContext* context,
    NVSDK_NGX_Feature type,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    DepthScope depth;
    const NVSDK_NGX_Result result =
        state().modules11[Slot].create(context, type, parameters, handle);
    if (depth.outermost() && NVSDK_NGX_SUCCEED(result) && handle != nullptr &&
        *handle != nullptr && guide_feature(type)) {
        register_feature(type, parameters, *handle);
    }
    return result;
}

void retire_feature(NVSDK_NGX_Handle* handle) noexcept;

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate11(
    ID3D11DeviceContext* context,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters);
    if (depth.outermost()) {
        capture11(context, handle, parameters);
    }
    return state().modules11[Slot].evaluate(context, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate11_c(
    ID3D11DeviceContext* context,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters);
    if (depth.outermost()) {
        capture11(context, handle, parameters);
    }
    return state().modules11[Slot].evaluate_c(context, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_release11(NVSDK_NGX_Handle* handle) {
    DepthScope depth;
    if (depth.outermost()) {
        retire_feature(handle);
    }
    return state().modules11[Slot].release(handle);
}

template <unsigned Slot>
void install11(HMODULE module) noexcept {
    auto& s = state();
    Module11& m = s.modules11[Slot];
    if (module == nullptr || m.installed || m.failed) {
        return;
    }
    try {
        auto* create = GetProcAddress(module, "NVSDK_NGX_D3D11_CreateFeature");
        auto* evaluate = GetProcAddress(module, "NVSDK_NGX_D3D11_EvaluateFeature");
        auto* release = GetProcAddress(module, "NVSDK_NGX_D3D11_ReleaseFeature");
        auto* evaluate_c = GetProcAddress(module, "NVSDK_NGX_D3D11_EvaluateFeature_C");
        if (create == nullptr || evaluate == nullptr || release == nullptr) {
            m.failed = true;
            return;
        }
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(create), &pinned);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&install11<Slot>), &pinned);
        const bool separate_c = evaluate_c != nullptr &&
            reinterpret_cast<void*>(evaluate_c) != reinterpret_cast<void*>(evaluate);
        m.create_hook = safetyhook::create_inline(reinterpret_cast<void*>(create),
            reinterpret_cast<void*>(&hooked_create11<Slot>), SafetyHookInline::StartDisabled);
        m.evaluate_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate),
            reinterpret_cast<void*>(&hooked_evaluate11<Slot>), SafetyHookInline::StartDisabled);
        m.release_hook = safetyhook::create_inline(reinterpret_cast<void*>(release),
            reinterpret_cast<void*>(&hooked_release11<Slot>), SafetyHookInline::StartDisabled);
        if (separate_c) {
            m.evaluate_c_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate_c),
                reinterpret_cast<void*>(&hooked_evaluate11_c<Slot>),
                SafetyHookInline::StartDisabled);
        }
        if (!m.create_hook || !m.evaluate_hook || !m.release_hook ||
            (separate_c && !m.evaluate_c_hook)) {
            m.create_hook = {};
            m.evaluate_hook = {};
            m.evaluate_c_hook = {};
            m.release_hook = {};
            m.failed = true;
            {
                std::scoped_lock lock(s.mutex);
                ++s.statistics.modules_failed;
            }
            record(kRecordHookFailed, Slot + 10U);
            return;
        }
        m.create = m.create_hook.original<CreateFeature11>();
        m.evaluate = m.evaluate_hook.original<EvaluateFeature11>();
        m.evaluate_c = separate_c ? m.evaluate_c_hook.original<EvaluateFeature11C>() : nullptr;
        m.release = m.release_hook.original<ReleaseFeature>();
        m.installed = true;
        static_cast<void>(m.create_hook.enable());
        static_cast<void>(m.evaluate_hook.enable());
        static_cast<void>(m.release_hook.enable());
        if (separate_c) {
            static_cast<void>(m.evaluate_c_hook.enable());
        }
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.modules_hooked;
            ++s.statistics.d3d11_modules_hooked;
        }
        // 820 a = slot + 10: a module's D3D11 exports.
        record(kRecordHooked, Slot + 10U, separate_c ? 1U : 0U);
    } catch (...) {
        m.failed = true;
    }
}

// A feature's release, from either API: its stream retires, and a D3D11
// feature's transport goes once its last snapshot copy has run.
void retire_feature(NVSDK_NGX_Handle* handle) noexcept {
    try {
        if (handle == nullptr) return;
        auto& s = state();
        std::uint64_t stream = 0;
        std::shared_ptr<GuideTransport11> transport;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found != s.features.end()) {
                stream = found->second.stream;
                transport = std::move(found->second.transport);
                s.features.erase(found);
                ++s.statistics.features_released;
            }
        }
        if (stream != 0) {
            retire_dlss_motion_vector_stream(stream);
        }
        transport.reset();
    } catch (...) {
    }
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_release(NVSDK_NGX_Handle* handle) {
    DepthScope depth;
    if (depth.outermost()) {
        retire_feature(handle);
    }
    return state().modules[Slot].release(handle);
}

template <unsigned Slot>
void install(HMODULE module) noexcept {
    auto& s = state();
    Module& m = s.modules[Slot];
    if (module == nullptr || m.installed || m.failed) {
        return;
    }
    try {
        auto* create = GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature");
        auto* evaluate = GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature");
        auto* release = GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature");
        auto* evaluate_c = GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature_C");
        if (create == nullptr || evaluate == nullptr || release == nullptr) {
            // Not an NGX D3D12 module after all (a D3D11-only or Vulkan-only
            // build): nothing to detour, and nothing to retry.
            m.failed = true;
            return;
        }
        // The module and this one stay for the life of the process: a hook
        // in an unloaded module, or a trampoline into one, would crash the
        // game's next call.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(create), &pinned);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&install<Slot>), &pinned);
        const bool separate_c = evaluate_c != nullptr &&
            reinterpret_cast<void*>(evaluate_c) != reinterpret_cast<void*>(evaluate);
        m.create_hook = safetyhook::create_inline(reinterpret_cast<void*>(create),
            reinterpret_cast<void*>(&hooked_create<Slot>), SafetyHookInline::StartDisabled);
        m.evaluate_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate),
            reinterpret_cast<void*>(&hooked_evaluate<Slot>), SafetyHookInline::StartDisabled);
        m.release_hook = safetyhook::create_inline(reinterpret_cast<void*>(release),
            reinterpret_cast<void*>(&hooked_release<Slot>), SafetyHookInline::StartDisabled);
        if (separate_c) {
            m.evaluate_c_hook = safetyhook::create_inline(
                reinterpret_cast<void*>(evaluate_c),
                reinterpret_cast<void*>(&hooked_evaluate_c<Slot>),
                SafetyHookInline::StartDisabled);
        }
        if (!m.create_hook || !m.evaluate_hook || !m.release_hook ||
            (separate_c && !m.evaluate_c_hook)) {
            m.create_hook = {};
            m.evaluate_hook = {};
            m.evaluate_c_hook = {};
            m.release_hook = {};
            m.failed = true;
            {
                std::scoped_lock lock(s.mutex);
                ++s.statistics.modules_failed;
            }
            record(kRecordHookFailed, Slot);
            return;
        }
        m.create = m.create_hook.original<CreateFeature>();
        m.evaluate = m.evaluate_hook.original<EvaluateFeature>();
        m.evaluate_c = separate_c ? m.evaluate_c_hook.original<EvaluateFeatureC>() : nullptr;
        m.release = m.release_hook.original<ReleaseFeature>();
        m.installed = true;
        static_cast<void>(m.create_hook.enable());
        static_cast<void>(m.evaluate_hook.enable());
        static_cast<void>(m.release_hook.enable());
        if (separate_c) {
            static_cast<void>(m.evaluate_c_hook.enable());
        }
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.modules_hooked;
        }
        record(kRecordHooked, Slot, separate_c ? 1U : 0U);
    } catch (...) {
        m.failed = true;
    }
}

// Installation is serialised: the monitor and a poll may overlap.
std::mutex g_install_mutex;

void install_all() noexcept {
    std::scoped_lock lock(g_install_mutex);
    install<0>(GetModuleHandleW(kModuleNames[0]));
    install<1>(GetModuleHandleW(kModuleNames[1]));
    install<2>(GetModuleHandleW(kModuleNames[2]));
    install11<0>(GetModuleHandleW(kModuleNames[0]));
    install11<1>(GetModuleHandleW(kModuleNames[1]));
    install11<2>(GetModuleHandleW(kModuleNames[2]));
}

void start_monitor() noexcept {
    auto& s = state();
    if (s.monitor_started.exchange(true)) {
        return;
    }
    try {
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&start_monitor), &pinned);
        std::thread([] {
            for (;;) {
                install_all();
                Sleep(50);
            }
        }).detach();
    } catch (...) {
        s.monitor_started.store(false);
    }
}

}  // namespace

void configure_ngx_guide_capture(ID3D12CommandQueue* queue, bool enabled) noexcept {
    try {
        auto& s = state();
        {
            std::scoped_lock lock(s.mutex);
            s.queue = queue;
            s.active.store(enabled && queue != nullptr, std::memory_order_release);
            s.statistics.active = s.active.load();
        }
        if (enabled && queue != nullptr) {
            configure_dlss_motion_vector_tracking(true);
            start_monitor();
        }
    } catch (...) {
    }
}

void stop_ngx_guide_capture(ID3D12CommandQueue* queue) noexcept {
    try {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        if (s.queue.Get() == queue) {
            s.active.store(false, std::memory_order_release);
            s.statistics.active = false;
            s.queue.Reset();
        }
    } catch (...) {
    }
}

void configure_ngx_guide_capture_d3d11(
    ID3D11Device* game_device,
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    bool enabled) noexcept {
    try {
        auto& s = state();
        const bool active = enabled && game_device != nullptr && device != nullptr &&
            queue != nullptr;
        {
            std::scoped_lock lock(s.mutex);
            s.d3d11_device = game_device;
            s.bridge_device = device;
            s.bridge_queue = queue;
            s.d3d11_active.store(active, std::memory_order_release);
            s.statistics.d3d11_active = active;
        }
        if (active) {
            configure_dlss_motion_vector_tracking(true);
            start_monitor();
        }
    } catch (...) {
    }
}

void flush_ngx_guide_capture_d3d11() noexcept {
    try {
        auto& s = state();
        std::vector<std::shared_ptr<GuideTransport11>> transports;
        {
            std::scoped_lock lock(s.mutex);
            for (const auto& [handle, feature] : s.features) {
                static_cast<void>(handle);
                if (feature.transport) transports.push_back(feature.transport);
            }
        }
        for (const auto& transport : transports) transport->flush();
    } catch (...) {
    }
}

void stop_ngx_guide_capture_d3d11(ID3D12CommandQueue* queue) noexcept {
    try {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        if (s.bridge_queue.Get() == queue) {
            s.d3d11_active.store(false, std::memory_order_release);
            s.statistics.d3d11_active = false;
        }
    } catch (...) {
    }
}

void poll_ngx_guide_capture() noexcept {
    install_all();
}

NgxGuideCaptureStatistics ngx_guide_capture_statistics() noexcept {
    try {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        return s.statistics;
    } catch (...) {
        return {};
    }
}

}  // namespace xrfg
