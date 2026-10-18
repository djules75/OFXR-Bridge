#include "xrfg/ngx_guide_capture.hpp"

#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/dlss_motion_vectors.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

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
//   822 a stream's first publication: a the stream's low bits,
//       b motion width << 32 | height, c output width << 32 | height
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

struct Feature {
    NVSDK_NGX_Feature type{NVSDK_NGX_Feature_SuperSampling};
    int flags{};
    bool flags_known{};
    unsigned width{}, height{}, out_width{}, out_height{};
    std::uint64_t stream{};
    std::uint64_t publications{};
    bool shape_recorded{};
};

struct Capture {
    std::mutex mutex;
    ComPtr<ID3D12CommandQueue> queue;
    std::atomic_bool active{false};
    std::atomic_bool monitor_started{false};
    std::array<Module, kModuleNames.size()> modules{};
    std::unordered_map<const NVSDK_NGX_Handle*, Feature> features;
    std::uint64_t next_stream{kStreamBase};
    NgxGuideCaptureStatistics statistics;
    std::array<bool, 5> skip_recorded{};
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
        publication.stream = feature.stream;
        publication.output = output;
        publication.motion_vectors = motion;
        publication.producer_queue = queue.Get();
        publication.producer_command_list = list;
        publication.verified_producer_device = motion_device.Get();
        publication.output_x =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X);
        publication.output_y =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y);
        // The creation's display size, else the allocation's.
        publication.output_width = std::min(
            feature.out_width != 0 ? feature.out_width
                                   : static_cast<unsigned>(output_description.Width),
            available(output_description.Width, publication.output_x));
        publication.output_height = std::min(
            feature.out_height != 0 ? feature.out_height : output_description.Height,
            available(output_description.Height, publication.output_y));
        publication.motion_x =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X);
        publication.motion_y =
            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y);
        const unsigned render_width = get<unsigned>(
            parameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
            feature.width);
        const unsigned render_height = get<unsigned>(
            parameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
            feature.height);
        // Without creation flags, assume render-resolution vectors: the usual
        // DLSS integration, and the one whose rectangle the render size gives.
        const bool full_resolution_vectors = feature.flags_known &&
            (feature.flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0;
        publication.motion_width = std::min(
            full_resolution_vectors
                ? publication.output_width
                : (render_width != 0 ? render_width
                                     : static_cast<unsigned>(motion_description.Width)),
            available(motion_description.Width, publication.motion_x));
        publication.motion_height = std::min(
            full_resolution_vectors
                ? publication.output_height
                : (render_height != 0 ? render_height : motion_description.Height),
            available(motion_description.Height, publication.motion_y));
        publication.scale_x = get<float>(parameters, NVSDK_NGX_Parameter_MV_Scale_X, 1.0F);
        publication.scale_y = get<float>(parameters, NVSDK_NGX_Parameter_MV_Scale_Y, 1.0F);
        publication.jitter_x = get<float>(parameters, NVSDK_NGX_Parameter_Jitter_Offset_X);
        publication.jitter_y = get<float>(parameters, NVSDK_NGX_Parameter_Jitter_Offset_Y);
        publication.jittered =
            (feature.flags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered) != 0;
        publication.reset = get<int>(parameters, NVSDK_NGX_Parameter_Reset) != 0;
        // DLSS reads its vectors as a shader resource; the game put them in
        // that state for it.
        publication.resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        publication.producer_command_list = list;

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
            record(kRecordPublication,
                feature.stream & 0xFFFFFFFFULL,
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

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_release(NVSDK_NGX_Handle* handle) {
    DepthScope depth;
    try {
        if (depth.outermost() && handle != nullptr) {
            auto& s = state();
            std::uint64_t stream = 0;
            {
                std::scoped_lock lock(s.mutex);
                const auto found = s.features.find(handle);
                if (found != s.features.end()) {
                    stream = found->second.stream;
                    s.features.erase(found);
                    ++s.statistics.features_released;
                }
            }
            if (stream != 0) {
                retire_dlss_motion_vector_stream(stream);
            }
        }
    } catch (...) {
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
