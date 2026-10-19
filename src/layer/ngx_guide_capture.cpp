#include "xrfg/ngx_guide_capture.hpp"

#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/dlss_motion_vectors.hpp"

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_vk.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <limits>
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
using CreateFeatureVk = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using CreateFeatureVk1 = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkDevice, VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeatureVk = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using EvaluateFeatureVkC = NVSDK_NGX_Result(NVSDK_CONV*)(
    VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
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
//   831 (V513) the Vulkan path's first publication of a stream: a its low
//       bits | bit 32 the depth came with it | bit 33 the depth's format has
//       no route, b vectors VkFormat << 32 | depth VkFormat, c output width
//       << 32 | height
//   832 (V513) every 100th Vulkan copy dropped because the game had not
//       submitted it by the end of its frame: a the drops so far
//   824 reasons 7 (the Vulkan capture could not start: c the stage - 1 the
//       loader, 2 the device functions, 3 the queue layer is not loaded),
//       8 (a Vulkan evaluation's command buffer is not the session device's
//       or a resource not an image view) and 9 (the Vulkan transport failed,
//       c its HRESULT)
constexpr std::int64_t kRecordD3D11Publication = 830;
constexpr std::int64_t kRecordVulkanPublication = 831;
constexpr std::int64_t kRecordVulkanDropped = 832;
//   833 (V514) the Vulkan capture started without the queue layer: copies are
//       published at the bridged release. Once a session.
constexpr std::int64_t kRecordVulkanUntracked = 833;
//   834 (V515) a Streamline viewport's camera, the first sixteen constants
//       and every 4000th after: a the viewport << 32 | how the projection
//       was read (1 row vectors, 2 column vectors, 3 no off-centre terms, 0
//       not a perspective it can read), b its edges' tangents
//       (pack_view_tangents), c the camera's position along its right
//       vector (float bits)
//   835 (V515) a stream's first evaluation inside Streamline's DLSS
//       evaluation: a the stream's low bits | the viewport << 32 | bit 63 its
//       camera is known, b the camera's tangents
//   820 a = 30: sl.interposer.dll's slSetConstants and slEvaluateFeature
//       are detoured (823 a = 30: they could not be)
constexpr std::int64_t kRecordStreamlineCamera = 834;
constexpr std::int64_t kRecordStreamlineStream = 835;
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
class GuideTransportVk;

struct Feature {
    NVSDK_NGX_Feature type{NVSDK_NGX_Feature_SuperSampling};
    int flags{};
    bool flags_known{};
    unsigned width{}, height{}, out_width{}, out_height{};
    std::uint64_t stream{};
    std::uint64_t publications{};
    bool shape_recorded{};
    bool viewport_recorded{};
    // D3D11 (V512): the shared textures this feature's evaluations go
    // through, and a stand-in for its output on the D3D12 device.
    std::shared_ptr<GuideTransport11> transport;
    ComPtr<ID3D12Resource> output_placeholder;
    // Vulkan (V513).
    std::shared_ptr<GuideTransportVk> transport_vk;
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

struct ModuleVk {
    SafetyHookInline create_hook;
    SafetyHookInline create1_hook;
    SafetyHookInline evaluate_hook;
    SafetyHookInline evaluate_c_hook;
    SafetyHookInline release_hook;
    CreateFeatureVk create{};
    CreateFeatureVk1 create1{};
    EvaluateFeatureVk evaluate{};
    EvaluateFeatureVkC evaluate_c{};
    ReleaseFeature release{};
    bool installed{};
    bool failed{};
};

struct VulkanApi;

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
    // Vulkan (V513): the session device's functions, and the layer's D3D12
    // device and queue.
    std::array<ModuleVk, kModuleNames.size()> modules_vk{};
    std::shared_ptr<VulkanApi> vulkan;
    ComPtr<ID3D12Device> vulkan_d3d12_device;
    ComPtr<ID3D12CommandQueue> vulkan_d3d12_queue;
    std::atomic_bool vulkan_active{false};
    std::unordered_map<const NVSDK_NGX_Handle*, Feature> features;
    std::uint64_t next_stream{kStreamBase};
    NgxGuideCaptureStatistics statistics;
    std::array<bool, 10> skip_recorded{};
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

// ------------------------------------------------------------- Streamline
// V515. A title on NVIDIA Streamline (MSFS 2024) hands Streamline each
// viewport's camera every frame (slSetConstants), then evaluates DLSS for a
// viewport (slEvaluateFeature), inside which Streamline's DLSS plugin makes
// the NGX evaluation the capture sees. Both exports of sl.interposer.dll are
// detoured: the cameras are kept per viewport, and the viewport an
// evaluation runs for is held on its thread for the nested NGX call, whose
// publication then carries that camera. MSFS 2024's foveated rendering
// evaluates four viewports a frame - per eye a periphery and an inset, none
// the eye image's size - and the camera is what places each in its eye.
//
// The layouts are Streamline 2.x's (sl_struct.h, sl_consts.h,
// sl_core_types.h; MSFS 2024 ships 2.11.1): every structure starts with a
// next pointer, a 16-byte type id and a version; Constants' row-major
// cameraViewToClip comes first after that, the camera's position and right
// vector after the five matrices and three pairs.
namespace sl_abi {

struct TypeId {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::array<std::uint8_t, 8> data4;
    bool operator==(const TypeId&) const = default;
};

struct Header {
    const Header* next;
    TypeId type;
    std::size_t version;
};

struct Constants {
    Header header;
    float view_to_clip[4][4];
    float clip_to_view[4][4];
    float clip_to_lens_clip[4][4];
    float clip_to_previous_clip[4][4];
    float previous_clip_to_clip[4][4];
    float jitter_offset[2];
    float motion_vector_scale[2];
    float pinhole_offset[2];
    float position[3];
    float up[3];
    float right[3];
    float forward[3];
};
static_assert(offsetof(Constants, view_to_clip) == 32);
static_assert(offsetof(Constants, position) == 376);
static_assert(offsetof(Constants, right) == 400);

struct Viewport {
    Header header;
    std::uint32_t value;
};
static_assert(offsetof(Viewport, value) == 32);

// {DCD35AD7-4E4A-4BAD-A90C-E0C49EB23AFE}, {171B6435-9B3C-4FC8-9994-FBE52569AAA4}
constexpr TypeId kConstants{
    0xdcd35ad7, 0x4e4a, 0x4bad, {0xa9, 0x0c, 0xe0, 0xc4, 0x9e, 0xb2, 0x3a, 0xfe}};
constexpr TypeId kViewport{
    0x171b6435, 0x9b3c, 0x4fc8, {0x99, 0x94, 0xfb, 0xe5, 0x25, 0x69, 0xaa, 0xa4}};
constexpr std::uint32_t kFeatureDlss = 0;

// sl::Result is an int-sized enum; references arrive as pointers.
using SetConstants = int (*)(const Constants*, const void* frame, const Viewport*);
using EvaluateFeature = int (*)(std::uint32_t feature, const void* frame,
    const Header* const* inputs, std::uint32_t input_count, void* command_buffer);

}  // namespace sl_abi

struct Streamline {
    SafetyHookInline set_constants_hook;
    SafetyHookInline evaluate_hook;
    sl_abi::SetConstants set_constants{};
    sl_abi::EvaluateFeature evaluate{};
    bool installed{};
    bool failed{};
    std::mutex mutex;
    std::unordered_map<std::uint32_t, DlssViewGeometry> cameras;
    std::uint64_t constants_seen{};
};

// Never destroyed, as the capture's state.
Streamline& streamline() noexcept {
    static auto* value = new Streamline;
    return *value;
}

// The viewport of the Streamline DLSS evaluation running on this thread.
thread_local bool t_sl_dlss = false;
thread_local std::uint32_t t_sl_viewport = 0;

// The tangents of a view's image edges from its projection: x's left edge
// at NDC -1, its right at +1, the top row at y +1, the bottom at -1. Either
// vector convention: off-centre terms sit in the third row for row vectors
// (D3D, w = z * m[2][3]) and in the third column for column vectors (w =
// m[3][2] * z); w's sign is the handedness. A view direction with tangent t
// and depth d > 0 has z = sign(w) d, so NDC = (t m00 + sign(w) offset) /
// |w|, and t = (NDC |w| - sign(w) offset) / m00. method: 1 row vectors, 2
// column vectors, 3 no off-centre terms, 0 not a perspective it reads.
DlssViewGeometry camera_from_constants(
    const sl_abi::Constants& c, std::uint32_t viewport, std::uint32_t* method) noexcept {
    *method = 0;
    const auto& m = c.view_to_clip;
    for (const auto& row : m)
        for (const float value : row)
            if (!std::isfinite(value)) return {};
    constexpr float kZero = 1e-6F;
    if (std::fabs(m[3][3]) > kZero || std::fabs(m[0][0]) < kZero || std::fabs(m[1][1]) < kZero) {
        return {};
    }
    const bool row_offsets = std::fabs(m[2][0]) > kZero || std::fabs(m[2][1]) > kZero;
    const bool column_offsets = std::fabs(m[0][2]) > kZero || std::fabs(m[1][2]) > kZero;
    float w = 0.0F, offset_x = 0.0F, offset_y = 0.0F;
    if (row_offsets && !column_offsets) {
        w = m[2][3];
        offset_x = m[2][0];
        offset_y = m[2][1];
        *method = 1;
    } else if (column_offsets && !row_offsets) {
        w = m[3][2];
        offset_x = m[0][2];
        offset_y = m[1][2];
        *method = 2;
    } else if (!row_offsets && !column_offsets) {
        // Symmetric: only |w| matters, the term of magnitude one.
        w = std::fabs(std::fabs(m[2][3]) - 1.0F) < 1e-4F ? m[2][3]
            : std::fabs(std::fabs(m[3][2]) - 1.0F) < 1e-4F ? m[3][2] : 0.0F;
        *method = 3;
    }
    if (std::fabs(w) < kZero) {
        *method = 0;
        return {};
    }
    const float sign = w > 0.0F ? 1.0F : -1.0F;
    const float magnitude = std::fabs(w);
    DlssViewGeometry view;
    view.known = true;
    view.viewport = viewport;
    view.tan_left = (-magnitude - sign * offset_x) / m[0][0];
    view.tan_right = (magnitude - sign * offset_x) / m[0][0];
    view.tan_up = (magnitude - sign * offset_y) / m[1][1];
    view.tan_down = (-magnitude - sign * offset_y) / m[1][1];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        view.position[axis] = std::isfinite(c.position[axis]) ? c.position[axis] : 0.0F;
        view.right[axis] = std::isfinite(c.right[axis]) ? c.right[axis] : 0.0F;
    }
    return view;
}

int hooked_sl_set_constants(
    const sl_abi::Constants* values, const void* frame, const sl_abi::Viewport* viewport) {
    auto& sl = streamline();
    try {
        if (values != nullptr && viewport != nullptr &&
            values->header.type == sl_abi::kConstants &&
            viewport->header.type == sl_abi::kViewport) {
            std::uint32_t method = 0;
            const DlssViewGeometry camera =
                camera_from_constants(*values, viewport->value, &method);
            std::uint64_t seen = 0;
            {
                std::scoped_lock lock(sl.mutex);
                if (camera.known) {
                    sl.cameras[viewport->value] = camera;
                } else {
                    sl.cameras.erase(viewport->value);
                }
                seen = ++sl.constants_seen;
            }
            if (seen <= 16 || seen % 4000 == 0) {
                const float side = camera.position[0] * camera.right[0] +
                    camera.position[1] * camera.right[1] + camera.position[2] * camera.right[2];
                std::uint32_t side_bits = 0;
                std::memcpy(&side_bits, &side, sizeof(side_bits));
                record(kRecordStreamlineCamera,
                    (static_cast<std::uint64_t>(viewport->value) << 32U) | method,
                    pack_view_tangents(camera.tan_left, camera.tan_right, camera.tan_up,
                        camera.tan_down),
                    side_bits);
            }
        }
    } catch (...) {
    }
    return sl.set_constants(values, frame, viewport);
}

int hooked_sl_evaluate(std::uint32_t feature, const void* frame,
    const sl_abi::Header* const* inputs, std::uint32_t input_count, void* command_buffer) {
    bool found = false;
    std::uint32_t viewport = 0;
    try {
        if (feature == sl_abi::kFeatureDlss && inputs != nullptr) {
            for (std::uint32_t index = 0; index < std::min(input_count, 64U) && !found; ++index) {
                const sl_abi::Header* input = inputs[index];
                for (unsigned chained = 0; input != nullptr && chained < 16 && !found;
                     ++chained, input = input->next) {
                    if (input->type == sl_abi::kViewport) {
                        viewport = reinterpret_cast<const sl_abi::Viewport*>(input)->value;
                        found = true;
                    }
                }
            }
        }
    } catch (...) {
    }
    const bool outer_dlss = t_sl_dlss;
    const std::uint32_t outer_viewport = t_sl_viewport;
    if (found) {
        t_sl_dlss = true;
        t_sl_viewport = viewport;
    }
    const int result =
        streamline().evaluate(feature, frame, inputs, input_count, command_buffer);
    t_sl_dlss = outer_dlss;
    t_sl_viewport = outer_viewport;
    return result;
}

// The camera of the Streamline DLSS evaluation this thread is inside, if
// any: unknown (known = false) outside one or before its constants.
[[nodiscard]] DlssViewGeometry current_streamline_camera(bool* inside) noexcept {
    *inside = t_sl_dlss;
    if (!t_sl_dlss) return {};
    try {
        auto& sl = streamline();
        std::scoped_lock lock(sl.mutex);
        const auto found = sl.cameras.find(t_sl_viewport);
        if (found != sl.cameras.end()) return found->second;
    } catch (...) {
    }
    DlssViewGeometry unknown;
    unknown.viewport = t_sl_viewport;
    return unknown;
}

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
        // V515: inside Streamline's DLSS evaluation, its viewport's camera.
        bool inside_streamline = false;
        publication.view = current_streamline_camera(&inside_streamline);
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
        if (publication.view.known) ++s.statistics.publications_with_camera;
        const auto found = s.features.find(handle);
        if (found != s.features.end() && inside_streamline &&
            (!found->second.viewport_recorded || found->second.publications == 0)) {
            // Once with the camera, or once without when it never comes.
            if (publication.view.known || found->second.publications == 0) {
                record(kRecordStreamlineStream,
                    (feature.stream & 0xFFFFFFFFULL) |
                        (static_cast<std::uint64_t>(publication.view.viewport & 0x7FFFFFFFU) << 32U) |
                        (publication.view.known ? 1ULL << 63U : 0ULL),
                    pack_view_tangents(publication.view.tan_left, publication.view.tan_right,
                        publication.view.tan_up, publication.view.tan_down));
            }
            found->second.viewport_recorded = publication.view.known;
        }
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
//
// active is the capture of the API the evaluation came through: the native
// D3D12 one, or a bridged session's D3D11 or Vulkan one (V519 - before, only
// the D3D12 capture's flag was read, so a bridged game whose DLSS started
// before Hybrid was switched on never registered: Skyrim, 824 reason 1 on
// every evaluation). _nvngx.dll serves every feature type and is not typed;
// in D3D11 (untyped_is_upscaling) the only feature evaluated with an output
// and motion vectors is super resolution - there is no D3D11 frame
// generation - so there it registers too. PureDark's Skyrim upscaler links
// NGX statically, which makes _nvngx.dll the outermost call.
template <unsigned Slot>
void register_late(const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    const std::atomic<bool>& active, bool untyped_is_upscaling = false) noexcept {
    if constexpr (Slot >= 2) {
        if (!untyped_is_upscaling) return;
    }
    {
        try {
            auto& s = state();
            if (!active.load(std::memory_order_acquire) || handle == nullptr ||
                parameters == nullptr) {
                return;
            }
            if constexpr (Slot >= 2) {
                if (get<void*>(parameters, NVSDK_NGX_Parameter_Output) == nullptr ||
                    get<void*>(parameters, NVSDK_NGX_Parameter_MotionVectors) == nullptr) {
                    return;
                }
            }
            {
                std::scoped_lock lock(s.mutex);
                if (s.features.find(handle) != s.features.end()) {
                    return;
                }
            }
            Feature feature;
            feature.type = Slot == 1 ? NVSDK_NGX_Feature_RayReconstruction
                                     : NVSDK_NGX_Feature_SuperSampling;
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
    register_late<Slot>(handle, parameters, state().active);
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
    register_late<Slot>(handle, parameters, state().active);
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
    register_late<Slot>(handle, parameters, state().d3d11_active, true);
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
    register_late<Slot>(handle, parameters, state().d3d11_active, true);
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


// ------------------------------------------------------------------ Vulkan
// A bridged Vulkan session (V513). The game's DLSS runs on its Vulkan
// device; the runtime and the synthesis on the layer's D3D12 device. At each
// evaluation, in the game's own command buffer ahead of DLSS, the vectors are
// copied into an image imported from a D3D12 shared texture, and the depth
// through a buffer into another - Vulkan copies no depth aspect into a colour
// image directly, and through a buffer the packed depth-stencil formats give
// their 32-bit depth too. The command buffer runs whenever the game submits
// it, which only the queue sees: the Vulkan queue layer is told, and signals
// a timeline (a D3D12 fence imported into the game's device) right after
// that submission. The publication is made only then, at the next flush
// after it (an image release, the end of a frame, the feature's next
// evaluation): No Man's Sky records its DLSS ahead of releasing the frame
// before, so a publication made at the recording was sometimes the frame
// after the one released, and two captures in a row took the same DLSS frame
// and never paired (815). The snapshot copy goes to the D3D12 queue with the
// publication, behind a wait on the timeline. A copy whose command buffer is
// still unsubmitted when its slot comes round again is dropped, never
// published. DLSS's inputs are taken to be in
// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, as NVIDIA asks of them.
//
// Without the queue layer (V514): Star Citizen admits no implicit Vulkan
// layer but the driver's, so the submission cannot be seen. Its bridged
// release runs at the end of the frame (V472), after the game has submitted
// the frame's work, and the bridge makes the D3D12 queue wait for the game's
// queue there; the copies are then published at that release, their snapshot
// copies queued behind that wait, with no timeline of their own. Record 833
// says the capture runs this way.

struct VulkanApi {
    using Register = void (*)(VkCommandBuffer, VkSemaphore, std::uint64_t, volatile LONG64*);
    using Forget = void (*)(VkSemaphore);
    VulkanSessionBinding binding{};
    VkPhysicalDeviceMemoryProperties memory{};
    PFN_vkCreateImage create_image{};
    PFN_vkDestroyImage destroy_image{};
    PFN_vkGetImageMemoryRequirements image_requirements{};
    PFN_vkAllocateMemory allocate_memory{};
    PFN_vkFreeMemory free_memory{};
    PFN_vkBindImageMemory bind_image_memory{};
    PFN_vkCreateBuffer create_buffer{};
    PFN_vkDestroyBuffer destroy_buffer{};
    PFN_vkGetBufferMemoryRequirements buffer_requirements{};
    PFN_vkBindBufferMemory bind_buffer_memory{};
    PFN_vkCreateSemaphore create_semaphore{};
    PFN_vkDestroySemaphore destroy_semaphore{};
    PFN_vkImportSemaphoreWin32HandleKHR import_semaphore{};
    PFN_vkCmdPipelineBarrier barrier{};
    PFN_vkCmdCopyImage copy_image{};
    PFN_vkCmdCopyImageToBuffer copy_image_to_buffer{};
    PFN_vkCmdCopyBufferToImage copy_buffer_to_image{};
    Register register_guide{};
    Forget forget_guides{};

    // 0 loaded; else the stage that failed (824 reason 7).
    [[nodiscard]] std::uint32_t load(const VulkanSessionBinding& session) noexcept {
        binding = session;
        HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
        auto gipa = loader != nullptr
            ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"))
            : nullptr;
        if (gipa == nullptr || binding.instance == VK_NULL_HANDLE || binding.device == VK_NULL_HANDLE) return 1;
        auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(binding.instance, "vkGetDeviceProcAddr"));
        auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            gipa(binding.instance, "vkGetPhysicalDeviceMemoryProperties"));
        if (gdpa == nullptr || properties == nullptr) return 1;
        properties(binding.physical_device, &memory);
        const auto load = [&](auto& target, const char* name) {
            target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(gdpa(binding.device, name));
            return target != nullptr;
        };
        if (!load(create_image, "vkCreateImage") || !load(destroy_image, "vkDestroyImage") ||
            !load(image_requirements, "vkGetImageMemoryRequirements") ||
            !load(allocate_memory, "vkAllocateMemory") || !load(free_memory, "vkFreeMemory") ||
            !load(bind_image_memory, "vkBindImageMemory") || !load(create_buffer, "vkCreateBuffer") ||
            !load(destroy_buffer, "vkDestroyBuffer") ||
            !load(buffer_requirements, "vkGetBufferMemoryRequirements") ||
            !load(bind_buffer_memory, "vkBindBufferMemory") ||
            !load(create_semaphore, "vkCreateSemaphore") ||
            !load(destroy_semaphore, "vkDestroySemaphore") ||
            !load(import_semaphore, "vkImportSemaphoreWin32HandleKHR") ||
            !load(barrier, "vkCmdPipelineBarrier") || !load(copy_image, "vkCmdCopyImage") ||
            !load(copy_image_to_buffer, "vkCmdCopyImageToBuffer") ||
            !load(copy_buffer_to_image, "vkCmdCopyBufferToImage")) {
            return 2;
        }
        // The queue layer, where the game admits it: without it the copies
        // are published at the bridged release (see above).
        HMODULE queue_layer = GetModuleHandleW(L"OFXR_vulkan_queue_layer.dll");
        if (queue_layer != nullptr) {
            register_guide = reinterpret_cast<Register>(
                GetProcAddress(queue_layer, "OFXR_vkRegisterGuideSubmission"));
            forget_guides = reinterpret_cast<Forget>(
                GetProcAddress(queue_layer, "OFXR_vkForgetGuideSubmissions"));
        }
        if (register_guide == nullptr || forget_guides == nullptr) {
            register_guide = nullptr;
            forget_guides = nullptr;
        }
        return 0;
    }

    [[nodiscard]] bool tracks_submissions() const noexcept {
        return register_guide != nullptr;
    }

    [[nodiscard]] std::uint32_t device_memory_type(std::uint32_t bits) const noexcept {
        std::uint32_t any = UINT32_MAX;
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((bits & (1U << index)) == 0) continue;
            if ((memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
                return index;
            }
            if (any == UINT32_MAX) any = index;
        }
        return any;
    }
};

// The D3D12 format a vector image's copy takes.
[[nodiscard]] DXGI_FORMAT vector_dxgi_format(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_R16G16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case VK_FORMAT_R32G32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case VK_FORMAT_R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case VK_FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

// How a depth image reaches a colour copy the synthesizer reads.
struct DepthRoute {
    DXGI_FORMAT dxgi{DXGI_FORMAT_UNKNOWN};
    VkFormat shared{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags barrier_aspect{};
    bool via_buffer{};
    std::uint32_t texel_bytes{};
};

[[nodiscard]] DepthRoute depth_route(VkFormat format) noexcept {
    constexpr VkImageAspectFlags depth = VK_IMAGE_ASPECT_DEPTH_BIT;
    constexpr VkImageAspectFlags both = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
            return {DXGI_FORMAT_R32_FLOAT, VK_FORMAT_R32_SFLOAT, depth, true, 4};
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return {DXGI_FORMAT_R32_FLOAT, VK_FORMAT_R32_SFLOAT, both, true, 4};
        case VK_FORMAT_D16_UNORM:
            return {DXGI_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, depth, true, 2};
        case VK_FORMAT_D16_UNORM_S8_UINT:
            return {DXGI_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, both, true, 2};
        case VK_FORMAT_R32_SFLOAT:
            return {DXGI_FORMAT_R32_FLOAT, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, 4};
        case VK_FORMAT_R16_UNORM:
            return {DXGI_FORMAT_R16_UNORM, VK_FORMAT_R16_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, false, 2};
        default:
            return {};
    }
}

[[nodiscard]] HRESULT hresult_of(VkResult result) noexcept {
    return result == VK_SUCCESS ? S_OK
        : result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY
            ? E_OUTOFMEMORY
            : static_cast<HRESULT>(0x887A0000L | (static_cast<std::uint32_t>(-result) & 0xFFFFU));
}

class GuideTransportVk {
public:
    struct Staged {
        ID3D12Resource* motion{};
        ID3D12Resource* depth{};
        bool depth_unrouted{};
        // A copy two rounds old whose command buffer was never submitted.
        bool dropped{};
    };

    // A publication waiting for its command buffer's submission.
    struct Held {
        DlssMotionVectorPublication publication{};
        VkFormat motion_format{};
        VkFormat depth_format{};
        bool depth_unrouted{};
    };

    explicit GuideTransportVk(std::shared_ptr<VulkanApi> api) : vk_(std::move(api)) {}
    GuideTransportVk(const GuideTransportVk&) = delete;
    GuideTransportVk& operator=(const GuideTransportVk&) = delete;

    ~GuideTransportVk() {
        try {
            if (vk_ && semaphore_ != VK_NULL_HANDLE && vk_->tracks_submissions()) {
                vk_->forget_guides(semaphore_);
            }
            // Destroyed only once every copy recorded has run on the GPU and
            // every snapshot copy after it; otherwise kept for the process.
            // Untracked, the snapshot copies ran behind the copies.
            const bool copies_done = (vk_ && !vk_->tracks_submissions()) ||
                (fence_ && fence_->GetCompletedValue() >= copied_);
            bool snapshots_done = !done_ || done_->GetCompletedValue() >= submitted_;
            if (copies_done && !snapshots_done && event_ != nullptr &&
                SUCCEEDED(done_->SetEventOnCompletion(submitted_, event_))) {
                snapshots_done = WaitForSingleObject(event_, 2000) == WAIT_OBJECT_0;
            }
            if (copies_done && snapshots_done && vk_) {
                const VkDevice device = vk_->binding.device;
                for (Slot& slot : slots_) {
                    destroy(slot.motion);
                    destroy(slot.depth);
                    if (slot.buffer != VK_NULL_HANDLE) vk_->destroy_buffer(device, slot.buffer, nullptr);
                    if (slot.buffer_memory != VK_NULL_HANDLE) vk_->free_memory(device, slot.buffer_memory, nullptr);
                }
                if (semaphore_ != VK_NULL_HANDLE) vk_->destroy_semaphore(device, semaphore_, nullptr);
            }
            if (event_ != nullptr) CloseHandle(event_);
        } catch (...) {
        }
    }

    [[nodiscard]] HRESULT initialize(ID3D12Device* device, ID3D12CommandQueue* queue) noexcept {
        try {
            device_ = device;
            queue_ = queue;
            HRESULT result = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                IID_PPV_ARGS(fence_.GetAddressOf()));
            if (FAILED(result)) return result;
            result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(done_.GetAddressOf()));
            if (FAILED(result)) return result;
            HANDLE handle = nullptr;
            result = device->CreateSharedHandle(fence_.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
            if (FAILED(result)) return result;
            VkSemaphoreTypeCreateInfo type{};
            type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            info.pNext = &type;
            VkResult vk_result = vk_->create_semaphore(vk_->binding.device, &info, nullptr, &semaphore_);
            if (vk_result != VK_SUCCESS) {
                CloseHandle(handle);
                return hresult_of(vk_result);
            }
            VkImportSemaphoreWin32HandleInfoKHR import{};
            import.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
            import.semaphore = semaphore_;
            import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
            import.handle = handle;
            vk_result = vk_->import_semaphore(vk_->binding.device, &import);
            // An NT handle stays the importer's to close.
            CloseHandle(handle);
            if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
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

    // In the game's command buffer, ahead of its DLSS: the copies and the
    // registration with the queue layer. hold() then keeps the publication.
    [[nodiscard]] HRESULT stage(
        VkCommandBuffer buffer,
        const NVSDK_NGX_ImageViewInfo_VK& motion,
        const NVSDK_NGX_ImageViewInfo_VK* depth,
        Staged* staged) noexcept {
        try {
            *staged = {};
            std::scoped_lock lock(mutex_);
            Slot& slot = slots_[next_];
            // A round of slots ago and still unsubmitted (the caller flushed
            // the submitted ones just before): it never will be.
            if (slot.pending) {
                slot.pending = false;
                staged->dropped = true;
            }
            if (slot.read_done != 0 && done_->GetCompletedValue() < slot.read_done) {
                HRESULT result = done_->SetEventOnCompletion(slot.read_done, event_);
                if (FAILED(result)) return result;
                if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            const DXGI_FORMAT motion_format = vector_dxgi_format(motion.Format);
            if (motion_format == DXGI_FORMAT_UNKNOWN || motion.Width == 0 || motion.Height == 0) {
                return DXGI_ERROR_UNSUPPORTED;
            }
            HRESULT result = prepare(slot.motion, motion_format, motion.Format, motion.Width, motion.Height);
            if (FAILED(result)) return result;
            DepthRoute route{};
            bool depth_ready = false;
            if (depth != nullptr) {
                route = depth_route(depth->Format);
                staged->depth_unrouted = route.dxgi == DXGI_FORMAT_UNKNOWN;
                if (!staged->depth_unrouted && depth->Width != 0 && depth->Height != 0) {
                    depth_ready = SUCCEEDED(prepare(slot.depth, route.dxgi, route.shared,
                                      depth->Width, depth->Height)) &&
                        (!route.via_buffer ||
                         SUCCEEDED(prepare_buffer(slot,
                             static_cast<VkDeviceSize>(depth->Width) * depth->Height * route.texel_bytes)));
                }
            }
            record(buffer, motion, slot.motion, depth_ready ? depth : nullptr, slot.depth, route, slot.buffer);
            slot.copied = ++copied_;
            if (vk_->tracks_submissions()) {
                vk_->register_guide(buffer, semaphore_, slot.copied, &submitted_value_);
            }
            recording_ = true;
            staged->motion = slot.motion.d3d12.Get();
            staged->depth = depth_ready ? slot.depth.d3d12.Get() : nullptr;
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    // Keeps the staged copy's publication until its submission.
    void hold(const Held& held) noexcept {
        std::scoped_lock lock(mutex_);
        if (!recording_) return;
        recording_ = false;
        Slot& slot = slots_[next_];
        slot.held = held;
        slot.pending = true;
        next_ = (next_ + 1U) % kSlots;
    }

    // Publishes, oldest first, every held copy whose command buffer the game
    // has submitted: `publish` records the registry's snapshot copy into the
    // list given, which then goes to the queue behind the timeline wait.
    template <typename Publish>
    void flush(Publish&& publish) noexcept {
        std::scoped_lock lock(mutex_);
        // Untracked, every held copy counts as submitted at the flush.
        const std::uint64_t submitted = vk_->tracks_submissions()
            ? submitted_value() : std::numeric_limits<std::uint64_t>::max();
        std::array<Slot*, kSlots> ready{};
        std::uint32_t count = 0;
        for (Slot& slot : slots_) {
            if (slot.pending && submitted >= slot.copied) ready[count++] = &slot;
        }
        std::sort(ready.begin(), ready.begin() + count,
            [](const Slot* a, const Slot* b) { return a->copied < b->copied; });
        for (std::uint32_t i = 0; i < count; ++i) {
            Slot& slot = *ready[i];
            slot.pending = false;
            if (FAILED(slot.allocator->Reset()) ||
                FAILED(slot.list->Reset(slot.allocator.Get(), nullptr))) {
                continue;
            }
            slot.held.publication.producer_command_list = slot.list.Get();
            publish(slot.held);
            if (FAILED(slot.list->Close())) continue;
            submit(slot);
        }
    }

private:
    static constexpr std::uint32_t kSlots = 4;

    struct Shared {
        ComPtr<ID3D12Resource> d3d12;
        VkImage image{};
        VkDeviceMemory memory{};
        DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
        std::uint32_t width{}, height{};
    };
    struct Slot {
        Shared motion;
        Shared depth;
        VkBuffer buffer{};
        VkDeviceMemory buffer_memory{};
        VkDeviceSize buffer_size{};
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::uint64_t copied{};
        std::uint64_t read_done{};
        bool pending{};
        Held held;
    };

    [[nodiscard]] std::uint64_t submitted_value() const noexcept {
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(
            const_cast<volatile LONG64*>(&submitted_value_), 0, 0));
    }

    void submit(Slot& slot) noexcept {
        // The timeline is the queue layer's to signal; untracked, the
        // bridged release's own wait orders the queue instead.
        if (vk_->tracks_submissions() && FAILED(queue_->Wait(fence_.Get(), slot.copied))) return;
        ID3D12CommandList* lists[]{slot.list.Get()};
        queue_->ExecuteCommandLists(1, lists);
        if (SUCCEEDED(queue_->Signal(done_.Get(), ++submitted_))) slot.read_done = submitted_;
    }

    void destroy(Shared& shared) noexcept {
        const VkDevice device = vk_->binding.device;
        if (shared.image != VK_NULL_HANDLE) vk_->destroy_image(device, shared.image, nullptr);
        if (shared.memory != VK_NULL_HANDLE) vk_->free_memory(device, shared.memory, nullptr);
        shared = {};
    }

    // A D3D12 shared texture, imported into the game's device.
    [[nodiscard]] HRESULT prepare(Shared& shared, DXGI_FORMAT format, VkFormat vk_format,
        std::uint32_t width, std::uint32_t height) noexcept {
        if (shared.d3d12 && shared.format == format && shared.width == width && shared.height == height) {
            return S_OK;
        }
        // The old one may still be read by a copy in flight: kept until the
        // transport goes (rare: only a resolution change).
        if (shared.d3d12) retired_.push_back(std::move(shared));
        shared = {};
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
            D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT result = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(shared.d3d12.GetAddressOf()));
        if (FAILED(result)) return result;
        HANDLE handle = nullptr;
        result = device_->CreateSharedHandle(shared.d3d12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(result)) return result;
        VkExternalMemoryImageCreateInfo external{};
        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.pNext = &external;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = vk_format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const VkDevice device = vk_->binding.device;
        VkResult vk_result = vk_->create_image(device, &info, nullptr, &shared.image);
        if (vk_result != VK_SUCCESS) {
            CloseHandle(handle);
            return hresult_of(vk_result);
        }
        VkMemoryRequirements requirements{};
        vk_->image_requirements(device, shared.image, &requirements);
        const std::uint32_t type = vk_->device_memory_type(requirements.memoryTypeBits);
        if (type == UINT32_MAX) {
            CloseHandle(handle);
            return E_FAIL;
        }
        VkImportMemoryWin32HandleInfoKHR import{};
        import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        import.handle = handle;
        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.pNext = &import;
        dedicated.image = shared.image;
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &dedicated;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        vk_result = vk_->allocate_memory(device, &allocate, nullptr, &shared.memory);
        CloseHandle(handle);
        if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
        vk_result = vk_->bind_image_memory(device, shared.image, shared.memory, 0);
        if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
        shared.format = format;
        shared.width = width;
        shared.height = height;
        return S_OK;
    }

    [[nodiscard]] HRESULT prepare_buffer(Slot& slot, VkDeviceSize size) noexcept {
        if (slot.buffer != VK_NULL_HANDLE && slot.buffer_size >= size) return S_OK;
        const VkDevice device = vk_->binding.device;
        // As for an image: an old buffer may still be read.
        if (slot.buffer != VK_NULL_HANDLE) {
            retired_buffers_.push_back({slot.buffer, slot.buffer_memory});
            slot.buffer = VK_NULL_HANDLE;
            slot.buffer_memory = VK_NULL_HANDLE;
        }
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vk_result = vk_->create_buffer(device, &info, nullptr, &slot.buffer);
        if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
        VkMemoryRequirements requirements{};
        vk_->buffer_requirements(device, slot.buffer, &requirements);
        const std::uint32_t type = vk_->device_memory_type(requirements.memoryTypeBits);
        if (type == UINT32_MAX) return E_FAIL;
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        vk_result = vk_->allocate_memory(device, &allocate, nullptr, &slot.buffer_memory);
        if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
        vk_result = vk_->bind_buffer_memory(device, slot.buffer, slot.buffer_memory, 0);
        if (vk_result != VK_SUCCESS) return hresult_of(vk_result);
        slot.buffer_size = size;
        return S_OK;
    }

    static VkImageMemoryBarrier image_barrier(VkImage image, VkImageAspectFlags aspect,
        std::uint32_t mip, std::uint32_t layer, VkImageLayout from, VkImageLayout to,
        VkAccessFlags source, VkAccessFlags destination) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = source;
        barrier.dstAccessMask = destination;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspect, mip, 1, layer, 1};
        return barrier;
    }

    void record(VkCommandBuffer buffer, const NVSDK_NGX_ImageViewInfo_VK& motion, const Shared& motion_copy,
        const NVSDK_NGX_ImageViewInfo_VK* depth, const Shared& depth_copy, const DepthRoute& route,
        VkBuffer staging) noexcept {
        constexpr VkImageLayout kInput = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const std::uint32_t motion_mip = motion.SubresourceRange.baseMipLevel;
        const std::uint32_t motion_layer = motion.SubresourceRange.baseArrayLayer;
        std::array<VkImageMemoryBarrier, 4> before{};
        std::uint32_t count = 0;
        before[count++] = image_barrier(motion.Image, VK_IMAGE_ASPECT_COLOR_BIT, motion_mip, motion_layer,
            kInput, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        before[count++] = image_barrier(motion_copy.image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        const std::uint32_t depth_mip = depth ? depth->SubresourceRange.baseMipLevel : 0;
        const std::uint32_t depth_layer = depth ? depth->SubresourceRange.baseArrayLayer : 0;
        if (depth != nullptr) {
            before[count++] = image_barrier(depth->Image, route.barrier_aspect, depth_mip, depth_layer,
                kInput, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            before[count++] = image_barrier(depth_copy.image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        }
        vk_->barrier(buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, count, before.data());
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, motion_mip, motion_layer, 1};
        copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {motion.Width, motion.Height, 1};
        vk_->copy_image(buffer, motion.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            motion_copy.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        if (depth != nullptr) {
            if (route.via_buffer) {
                VkBufferImageCopy out{};
                out.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, depth_mip, depth_layer, 1};
                out.imageExtent = {depth->Width, depth->Height, 1};
                vk_->copy_image_to_buffer(buffer, depth->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    staging, 1, &out);
                VkBufferMemoryBarrier through{};
                through.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                through.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                through.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                through.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                through.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                through.buffer = staging;
                through.size = VK_WHOLE_SIZE;
                vk_->barrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr, 1, &through, 0, nullptr);
                VkBufferImageCopy in{};
                in.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                in.imageExtent = {depth->Width, depth->Height, 1};
                vk_->copy_buffer_to_image(buffer, staging, depth_copy.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &in);
            } else {
                VkImageCopy depth_region{};
                depth_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, depth_mip, depth_layer, 1};
                depth_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                depth_region.extent = {depth->Width, depth->Height, 1};
                vk_->copy_image(buffer, depth->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    depth_copy.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &depth_region);
            }
        }
        std::array<VkImageMemoryBarrier, 4> after{};
        count = 0;
        after[count++] = image_barrier(motion.Image, VK_IMAGE_ASPECT_COLOR_BIT, motion_mip, motion_layer,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kInput, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
        after[count++] = image_barrier(motion_copy.image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        if (depth != nullptr) {
            after[count++] = image_barrier(depth->Image, route.barrier_aspect, depth_mip, depth_layer,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kInput, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
            after[count++] = image_barrier(depth_copy.image, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        }
        vk_->barrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, nullptr, 0, nullptr, count, after.data());
    }

    std::shared_ptr<VulkanApi> vk_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12Fence> done_;
    VkSemaphore semaphore_{};
    alignas(8) volatile LONG64 submitted_value_{};
    std::array<Slot, kSlots> slots_{};
    std::vector<Shared> retired_;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> retired_buffers_;
    std::uint32_t next_{};
    std::uint64_t copied_{};
    std::uint64_t submitted_{};
    bool recording_{};
    HANDLE event_{};
    std::mutex mutex_;
};

void skip_vulkan(std::size_t reason, std::uint64_t detail = 0) noexcept {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    const std::uint64_t count = ++s.statistics.vulkan_transport_failures;
    if (!s.skip_recorded[reason]) {
        s.skip_recorded[reason] = true;
        record(kRecordSkip, reason, count, detail);
    }
}

[[nodiscard]] const NVSDK_NGX_ImageViewInfo_VK* image_view(
    const NVSDK_NGX_Parameter* parameters, const char* name) noexcept {
    void* pointer = get<void*>(parameters, name);
    if (pointer == nullptr) return nullptr;
    const auto* resource = static_cast<const NVSDK_NGX_Resource_VK*>(pointer);
    if (resource->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW ||
        resource->Resource.ImageViewInfo.Image == VK_NULL_HANDLE) {
        return nullptr;
    }
    return &resource->Resource.ImageViewInfo;
}

void note_vulkan_drop() noexcept {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    if (++s.statistics.vulkan_dropped % 100 == 1) {
        record(kRecordVulkanDropped, s.statistics.vulkan_dropped);
    }
}

// Publishes one transport's submitted copies, with the bookkeeping a
// publication has on the other paths.
void flush_transport_vk(GuideTransportVk& transport) noexcept {
    transport.flush([](GuideTransportVk::Held& held) {
        try {
            auto& s = state();
            const DlssMotionVectorPublication& publication = held.publication;
            const auto before = dlss_motion_vector_statistics();
            publish_dlss_motion_vectors(publication);
            const auto after = dlss_motion_vector_statistics();
            std::scoped_lock lock(s.mutex);
            if (after.published == before.published) {
                if (s.refusals_recorded++ < 8) {
                    record(kRecordRefused, after.invalid_publications, after.snapshot_failures,
                        (static_cast<std::uint64_t>(publication.motion_width) << 32U) |
                            publication.motion_height);
                }
                return;
            }
            ++s.statistics.publications;
            ++s.statistics.vulkan_publications;
            for (auto& [handle, feature] : s.features) {
                static_cast<void>(handle);
                if (feature.stream != publication.stream) continue;
                if (feature.publications++ == 0) {
                    record(kRecordVulkanPublication,
                        (feature.stream & 0xFFFFFFFFULL) |
                            (publication.depth != nullptr ? 1ULL << 32U : 0ULL) |
                            (held.depth_unrouted ? 1ULL << 33U : 0ULL),
                        (static_cast<std::uint64_t>(held.motion_format) << 32U) |
                            static_cast<std::uint64_t>(held.depth_format),
                        (static_cast<std::uint64_t>(publication.output_width) << 32U) |
                            publication.output_height);
                }
                break;
            }
        } catch (...) {
        }
    });
}

void capture_vk(
    VkCommandBuffer buffer,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    try {
        auto& s = state();
        if (!s.vulkan_active.load(std::memory_order_acquire) || buffer == VK_NULL_HANDLE ||
            handle == nullptr || parameters == nullptr) {
            return;
        }
        Feature feature;
        std::shared_ptr<VulkanApi> api;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.evaluations;
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
            api = s.vulkan;
            device = s.vulkan_d3d12_device;
            queue = s.vulkan_d3d12_queue;
        }
        if (!api || !device || !queue) return;
        const auto* output = image_view(parameters, NVSDK_NGX_Parameter_Output);
        const auto* motion = image_view(parameters, NVSDK_NGX_Parameter_MotionVectors);
        const auto* depth = image_view(parameters, NVSDK_NGX_Parameter_Depth);
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
        // The command buffer has to be the session device's: the loader
        // keeps a device's dispatch table at the start of all its objects.
        if (*reinterpret_cast<void* const*>(buffer) !=
            *reinterpret_cast<void* const*>(api->binding.device)) {
            skip_vulkan(8);
            return;
        }
        std::shared_ptr<GuideTransportVk> transport;
        ComPtr<ID3D12Resource> placeholder;
        bool first_evaluation = false;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found == s.features.end()) return;
            Feature& stored = found->second;
            first_evaluation = !stored.shape_recorded;
            stored.shape_recorded = true;
            if (!stored.transport_vk) {
                auto created = std::make_shared<GuideTransportVk>(api);
                const HRESULT result = created->initialize(device.Get(), queue.Get());
                if (FAILED(result)) {
                    ++s.statistics.vulkan_transport_failures;
                    if (!s.skip_recorded[9]) {
                        s.skip_recorded[9] = true;
                        record(kRecordSkip, 9, s.statistics.vulkan_transport_failures,
                            static_cast<std::uint32_t>(result));
                    }
                    return;
                }
                stored.transport_vk = std::move(created);
            }
            if (!stored.output_placeholder ||
                stored.output_placeholder->GetDesc().Width != output->Width ||
                stored.output_placeholder->GetDesc().Height != output->Height) {
                D3D11_TEXTURE2D_DESC shape{};
                shape.Width = output->Width;
                shape.Height = output->Height;
                shape.ArraySize = 1;
                stored.output_placeholder = output_placeholder(device.Get(), shape);
            }
            transport = stored.transport_vk;
            placeholder = stored.output_placeholder;
        }
        if (first_evaluation) {
            record(kRecordShape,
                (static_cast<std::uint64_t>(motion->Format) << 32U) | (feature.stream & 0xFFFFFFFFULL),
                (static_cast<std::uint64_t>(output->Width) << 32U) | output->Height,
                (static_cast<std::uint64_t>(motion->Width) << 24U) | motion->Height);
        }
        if (!placeholder) {
            skip_vulkan(9, static_cast<std::uint32_t>(E_OUTOFMEMORY));
            return;
        }
        // What the game has submitted since: published before this copy.
        flush_transport_vk(*transport);
        GuideTransportVk::Staged staged{};
        const HRESULT staged_result = transport->stage(buffer, *motion, depth, &staged);
        if (staged.dropped) note_vulkan_drop();
        if (FAILED(staged_result)) {
            skip_vulkan(9, static_cast<std::uint32_t>(staged_result));
            return;
        }
        GuideTransportVk::Held held{};
        DlssMotionVectorPublication& publication = held.publication;
        Shape depth_shape{depth != nullptr ? depth->Width : 0U, depth != nullptr ? depth->Height : 0U};
        fill_publication(publication, feature, parameters,
            {output->Width, output->Height}, {motion->Width, motion->Height},
            staged.depth != nullptr ? &depth_shape : nullptr);
        publication.output = placeholder.Get();
        publication.motion_vectors = staged.motion;
        publication.producer_queue = queue.Get();
        publication.verified_producer_device = device.Get();
        publication.resource_state = D3D12_RESOURCE_STATE_COMMON;
        if (staged.depth != nullptr) {
            publication.depth = staged.depth;
            publication.depth_resource_state = D3D12_RESOURCE_STATE_COMMON;
        }
        held.motion_format = motion->Format;
        held.depth_format = depth != nullptr ? depth->Format : VK_FORMAT_UNDEFINED;
        held.depth_unrouted = staged.depth_unrouted;
        transport->hold(held);
    } catch (...) {
    }
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_create_vk(
    VkCommandBuffer buffer, NVSDK_NGX_Feature type, NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Handle** handle) {
    DepthScope depth;
    const NVSDK_NGX_Result result = state().modules_vk[Slot].create(buffer, type, parameters, handle);
    if (depth.outermost() && NVSDK_NGX_SUCCEED(result) && handle != nullptr &&
        *handle != nullptr && guide_feature(type)) {
        register_feature(type, parameters, *handle);
    }
    return result;
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_create_vk1(
    VkDevice vk_device, VkCommandBuffer buffer, NVSDK_NGX_Feature type,
    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle) {
    DepthScope depth;
    const NVSDK_NGX_Result result =
        state().modules_vk[Slot].create1(vk_device, buffer, type, parameters, handle);
    if (depth.outermost() && NVSDK_NGX_SUCCEED(result) && handle != nullptr &&
        *handle != nullptr && guide_feature(type)) {
        register_feature(type, parameters, *handle);
    }
    return result;
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate_vk(
    VkCommandBuffer buffer, const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters, state().vulkan_active);
    if (depth.outermost()) capture_vk(buffer, handle, parameters);
    return state().modules_vk[Slot].evaluate(buffer, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate_vk_c(
    VkCommandBuffer buffer, const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters, state().vulkan_active);
    if (depth.outermost()) capture_vk(buffer, handle, parameters);
    return state().modules_vk[Slot].evaluate_c(buffer, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_release_vk(NVSDK_NGX_Handle* handle) {
    DepthScope depth;
    if (depth.outermost()) retire_feature(handle);
    return state().modules_vk[Slot].release(handle);
}

template <unsigned Slot>
void install_vk(HMODULE module) noexcept {
    auto& s = state();
    ModuleVk& m = s.modules_vk[Slot];
    if (module == nullptr || m.installed || m.failed) return;
    try {
        auto* create = GetProcAddress(module, "NVSDK_NGX_VULKAN_CreateFeature");
        auto* create1 = GetProcAddress(module, "NVSDK_NGX_VULKAN_CreateFeature1");
        auto* evaluate = GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature");
        auto* release = GetProcAddress(module, "NVSDK_NGX_VULKAN_ReleaseFeature");
        auto* evaluate_c = GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature_C");
        if (create == nullptr || evaluate == nullptr || release == nullptr) {
            m.failed = true;
            return;
        }
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(create), &pinned);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&install_vk<Slot>), &pinned);
        const bool separate_c = evaluate_c != nullptr &&
            reinterpret_cast<void*>(evaluate_c) != reinterpret_cast<void*>(evaluate);
        m.create_hook = safetyhook::create_inline(reinterpret_cast<void*>(create),
            reinterpret_cast<void*>(&hooked_create_vk<Slot>), SafetyHookInline::StartDisabled);
        if (create1 != nullptr) {
            m.create1_hook = safetyhook::create_inline(reinterpret_cast<void*>(create1),
                reinterpret_cast<void*>(&hooked_create_vk1<Slot>), SafetyHookInline::StartDisabled);
        }
        m.evaluate_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate),
            reinterpret_cast<void*>(&hooked_evaluate_vk<Slot>), SafetyHookInline::StartDisabled);
        m.release_hook = safetyhook::create_inline(reinterpret_cast<void*>(release),
            reinterpret_cast<void*>(&hooked_release_vk<Slot>), SafetyHookInline::StartDisabled);
        if (separate_c) {
            m.evaluate_c_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate_c),
                reinterpret_cast<void*>(&hooked_evaluate_vk_c<Slot>), SafetyHookInline::StartDisabled);
        }
        if (!m.create_hook || !m.evaluate_hook || !m.release_hook ||
            (create1 != nullptr && !m.create1_hook) || (separate_c && !m.evaluate_c_hook)) {
            m.create_hook = {};
            m.create1_hook = {};
            m.evaluate_hook = {};
            m.evaluate_c_hook = {};
            m.release_hook = {};
            m.failed = true;
            {
                std::scoped_lock lock(s.mutex);
                ++s.statistics.modules_failed;
            }
            record(kRecordHookFailed, Slot + 20U);
            return;
        }
        m.create = m.create_hook.original<CreateFeatureVk>();
        m.create1 = create1 != nullptr ? m.create1_hook.original<CreateFeatureVk1>() : nullptr;
        m.evaluate = m.evaluate_hook.original<EvaluateFeatureVk>();
        m.evaluate_c = separate_c ? m.evaluate_c_hook.original<EvaluateFeatureVkC>() : nullptr;
        m.release = m.release_hook.original<ReleaseFeature>();
        m.installed = true;
        static_cast<void>(m.create_hook.enable());
        if (create1 != nullptr) static_cast<void>(m.create1_hook.enable());
        static_cast<void>(m.evaluate_hook.enable());
        static_cast<void>(m.release_hook.enable());
        if (separate_c) static_cast<void>(m.evaluate_c_hook.enable());
        {
            std::scoped_lock lock(s.mutex);
            ++s.statistics.modules_hooked;
            ++s.statistics.vulkan_modules_hooked;
        }
        // 820 a = slot + 20: a module's Vulkan exports.
        record(kRecordHooked, Slot + 20U, separate_c ? 1U : 0U);
    } catch (...) {
        m.failed = true;
    }
}

// A feature's release, from any API: its stream retires, and a D3D11 or
// Vulkan feature's transport goes once its last copies have run.
void retire_feature(NVSDK_NGX_Handle* handle) noexcept {
    try {
        if (handle == nullptr) return;
        auto& s = state();
        std::uint64_t stream = 0;
        std::shared_ptr<GuideTransport11> transport;
        std::shared_ptr<GuideTransportVk> transport_vk;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found != s.features.end()) {
                stream = found->second.stream;
                transport = std::move(found->second.transport);
                transport_vk = std::move(found->second.transport_vk);
                s.features.erase(found);
                ++s.statistics.features_released;
            }
        }
        if (stream != 0) {
            retire_dlss_motion_vector_stream(stream);
        }
        transport.reset();
        transport_vk.reset();
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

// V515: Streamline's interposer, for the viewports' cameras.
void install_streamline(HMODULE module) noexcept {
    auto& sl = streamline();
    if (module == nullptr || sl.installed || sl.failed) {
        return;
    }
    try {
        auto* set_constants = GetProcAddress(module, "slSetConstants");
        auto* evaluate = GetProcAddress(module, "slEvaluateFeature");
        if (set_constants == nullptr || evaluate == nullptr) {
            sl.failed = true;
            return;
        }
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(set_constants), &pinned);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&install_streamline), &pinned);
        sl.set_constants_hook = safetyhook::create_inline(reinterpret_cast<void*>(set_constants),
            reinterpret_cast<void*>(&hooked_sl_set_constants), SafetyHookInline::StartDisabled);
        sl.evaluate_hook = safetyhook::create_inline(reinterpret_cast<void*>(evaluate),
            reinterpret_cast<void*>(&hooked_sl_evaluate), SafetyHookInline::StartDisabled);
        if (!sl.set_constants_hook || !sl.evaluate_hook) {
            sl.set_constants_hook = {};
            sl.evaluate_hook = {};
            sl.failed = true;
            record(kRecordHookFailed, 30U);
            return;
        }
        sl.set_constants = sl.set_constants_hook.original<sl_abi::SetConstants>();
        sl.evaluate = sl.evaluate_hook.original<sl_abi::EvaluateFeature>();
        sl.installed = true;
        static_cast<void>(sl.set_constants_hook.enable());
        static_cast<void>(sl.evaluate_hook.enable());
        {
            auto& s = state();
            std::scoped_lock lock(s.mutex);
            s.statistics.streamline_hooked = true;
        }
        record(kRecordHooked, 30U);
    } catch (...) {
        sl.failed = true;
    }
}

// Installation is serialised: the monitor and a poll may overlap.
std::mutex g_install_mutex;

void install_all() noexcept {
    std::scoped_lock lock(g_install_mutex);
    install_streamline(GetModuleHandleW(L"sl.interposer.dll"));
    install<0>(GetModuleHandleW(kModuleNames[0]));
    install<1>(GetModuleHandleW(kModuleNames[1]));
    install<2>(GetModuleHandleW(kModuleNames[2]));
    install11<0>(GetModuleHandleW(kModuleNames[0]));
    install11<1>(GetModuleHandleW(kModuleNames[1]));
    install11<2>(GetModuleHandleW(kModuleNames[2]));
    install_vk<0>(GetModuleHandleW(kModuleNames[0]));
    install_vk<1>(GetModuleHandleW(kModuleNames[1]));
    install_vk<2>(GetModuleHandleW(kModuleNames[2]));
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

void configure_ngx_guide_capture_vulkan(
    const VulkanSessionBinding& binding,
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    bool enabled) noexcept {
    try {
        auto& s = state();
        if (!enabled || device == nullptr || queue == nullptr) {
            std::scoped_lock lock(s.mutex);
            s.vulkan_active.store(false, std::memory_order_release);
            s.statistics.vulkan_active = false;
            return;
        }
        std::shared_ptr<VulkanApi> api;
        {
            std::scoped_lock lock(s.mutex);
            if (s.vulkan && s.vulkan->binding.device == binding.device) api = s.vulkan;
        }
        if (!api) {
            auto created = std::make_shared<VulkanApi>();
            const std::uint32_t stage = created->load(binding);
            if (stage != 0) {
                std::scoped_lock lock(s.mutex);
                ++s.statistics.vulkan_transport_failures;
                s.statistics.vulkan_start_stage = stage;
                if (!s.skip_recorded[7]) {
                    s.skip_recorded[7] = true;
                    record(kRecordSkip, 7, s.statistics.vulkan_transport_failures, stage);
                }
                return;
            }
            api = std::move(created);
            if (!api->tracks_submissions()) {
                record(kRecordVulkanUntracked, 1);
            }
        }
        {
            std::scoped_lock lock(s.mutex);
            s.vulkan = api;
            s.vulkan_d3d12_device = device;
            s.vulkan_d3d12_queue = queue;
            s.vulkan_active.store(true, std::memory_order_release);
            s.statistics.vulkan_active = true;
            s.statistics.vulkan_untracked = !api->tracks_submissions();
        }
        configure_dlss_motion_vector_tracking(true);
        start_monitor();
    } catch (...) {
    }
}

void stop_ngx_guide_capture_vulkan(ID3D12CommandQueue* queue) noexcept {
    try {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        if (s.vulkan_d3d12_queue.Get() == queue) {
            s.vulkan_active.store(false, std::memory_order_release);
            s.statistics.vulkan_active = false;
        }
    } catch (...) {
    }
}

void flush_ngx_guide_capture_vulkan(bool end_of_frame) noexcept {
    try {
        auto& s = state();
        // end_of_frame no longer drops (a frame recorded ahead is still to
        // be submitted); the slot's next round does.
        static_cast<void>(end_of_frame);
        std::vector<std::shared_ptr<GuideTransportVk>> transports;
        {
            std::scoped_lock lock(s.mutex);
            for (const auto& [handle, feature] : s.features) {
                static_cast<void>(handle);
                if (feature.transport_vk) transports.push_back(feature.transport_vk);
            }
        }
        for (const auto& transport : transports) flush_transport_vk(*transport);
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
