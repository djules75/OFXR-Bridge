#include "xrfg/ngx_guide_capture.hpp"

#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/dlss_motion_vectors.hpp"

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_vk.h>

#include <safetyhook.hpp>

#include "depth_unpack_shader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <limits>
#include <numeric>
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
//       shared (packed depth-stencil) | bit 34 (V521) it went over unpacked
//       to a float, b vectors format << 32 | depth format,
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
//   839 (V520) an evaluation's DLSS parameters as published, for a stream's
//       first four publications and every 2000th after: a the stream's low
//       16 bits | the creation flags << 16 | bit 48 reset | bit 49 jittered,
//       b MV scale x (float bits) << 32 | MV scale y, c the vector rectangle:
//       x << 48 | y << 32 | width << 16 | height
//   840 (V520) with each 839: a the output rectangle, x << 48 | y << 32 |
//       width << 16 | height, b jitter x (float bits) << 32 | jitter y, c the
//       vector texture's DXGI format << 48 | its width << 24 | its height
constexpr std::int64_t kRecordParameters = 839;
constexpr std::int64_t kRecordParametersMore = 840;
//   841 (V521) an input rectangle moved back onto its eye's render region,
//       the first sixteen: a the stream's low 32 bits, b the vectors' shift
//       x << 32 | y, c the depth's, each a signed 32-bit count of render
//       texels removed
constexpr std::int64_t kRecordRealigned = 841;
//   842 (V521) with each 839: a the depth rectangle, x << 48 | y << 32 |
//       width << 16 | height, b 1 when the depth came with the publication,
//       c 1 when it is inverted
constexpr std::int64_t kRecordDepthRect = 842;
//   843 (V524) the first publication of a stream a D3D12 game evaluated in a
//       bridged D3D11 session: a its low bits | bit 32 the depth was shared |
//       bit 33 the depth's format cannot be shared (packed depth-stencil) |
//       bit 34 the game's device backs the layer's, b vectors format << 32 |
//       depth format, c output width << 32 | height
//   824 reason 10 (V524): that path's transport failed, c its HRESULT
constexpr std::int64_t kRecordBridgedD3D12Publication = 843;
//   844 (V524) a publication on that path, the first 512 and every 2000th
//       after: a the registry's publication number, b the stream's low 32
//       bits | bit 32 inside Streamline's DLSS evaluation | bit 33 its camera
//       known | the viewport << 40, c where the camera stands along its right
//       vector (float bits; the left eye's is the smaller)
constexpr std::int64_t kRecordBridgedD3D12Evaluation = 844;
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
class GuideTransport12;
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
    // A D3D12 game in a bridged D3D11 session (V524): the same, from the
    // game's D3D12 device.
    std::shared_ptr<GuideTransport12> transport12;
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
    // V526, R.E.A.L. VR: the one stream and transport its per-eye handles
    // publish through.
    std::uint64_t realvr_stream{};
    std::shared_ptr<GuideTransport12> realvr_transport;
    std::uint64_t next_stream{kStreamBase};
    NgxGuideCaptureStatistics statistics;
    std::array<bool, 11> skip_recorded{};
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

// How far DLSS's input rectangle starts off its eye's render region, in
// render texels (V521); 0 when it does not. The eye image is the game's own
// render of its field, so an eye pixel maps to its region's origin, not to
// wherever an upscaler chose to start reading: PureDark's Skyrim upscaler
// keeps its foveated offsets (0.02, 0.04) at foveated scale 1.0, reads from
// (56, 88) and (2768, 88) of a 2824-wide region per eye while saying its
// output starts at (0, 0), and places its output back by the same offset.
// Hybrid then read every vector about 97 px across and 152 px down from its
// pixel - a halo round everything that moved against its background. The
// regions are taken to lie on a grid of their own size; a start within a
// tenth of a region of a grid line is that line, anything further (an eye at
// the half of a larger allocation) is left as it is.
[[nodiscard]] long input_offset_within_region(
    unsigned base, unsigned output_base, unsigned extent, unsigned output_extent) noexcept {
    if (extent == 0 || output_extent == 0) return 0;
    const double ratio = static_cast<double>(extent) / static_cast<double>(output_extent);
    const double start = static_cast<double>(base) - static_cast<double>(output_base) * ratio;
    const double origin = std::round(start / static_cast<double>(extent)) * static_cast<double>(extent);
    const double offset = start - origin;
    if (std::abs(offset) < 0.5 || std::abs(offset) > 0.1 * static_cast<double>(extent)) return 0;
    return std::lround(offset);
}

// Moves base back by the offset when the result stays inside the texture.
[[nodiscard]] long realign(unsigned& base, unsigned output_base, unsigned extent,
    unsigned output_extent) noexcept {
    const long offset = input_offset_within_region(base, output_base, extent, output_extent);
    const long moved = static_cast<long>(base) - offset;
    if (offset == 0 || moved < 0) return 0;
    base = static_cast<unsigned>(moved);
    return offset;
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

// sl::Result is an int-sized enum; references arrive as pointers. V535: the
// 32-bit arguments are taken and passed on as whole registers, so a call
// of another shape reaches the original as it was made.
using SetConstants = int (*)(const Constants*, const void* frame, const Viewport*);
using EvaluateFeature = int (*)(std::uint64_t feature, const void* frame,
    const Header* const* inputs, std::uint64_t input_count, void* command_buffer);

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
        view.up[axis] = std::isfinite(c.up[axis]) ? c.up[axis] : 0.0F;
        view.forward[axis] = std::isfinite(c.forward[axis]) ? c.forward[axis] : 0.0F;
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

int hooked_sl_evaluate(std::uint64_t feature, const void* frame,
    const sl_abi::Header* const* inputs, std::uint64_t input_count, void* command_buffer) {
    bool found = false;
    std::uint32_t viewport = 0;
    try {
        if (static_cast<std::uint32_t>(feature) == sl_abi::kFeatureDlss && inputs != nullptr) {
            const std::uint32_t count = std::min(static_cast<std::uint32_t>(input_count), 64U);
            for (std::uint32_t index = 0; index < count && !found; ++index) {
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
    // V521: the input rectangles back on the eye's render region.
    const long motion_dx = realign(publication.motion_x, publication.output_x,
        publication.motion_width, publication.output_width);
    const long motion_dy = realign(publication.motion_y, publication.output_y,
        publication.motion_height, publication.output_height);
    publication.motion_width = std::min(publication.motion_width,
        available(motion.width, publication.motion_x));
    publication.motion_height = std::min(publication.motion_height,
        available(motion.height, publication.motion_y));
    long depth_dx = 0, depth_dy = 0;
    if (depth != nullptr) {
        depth_dx = realign(publication.depth_x, publication.output_x,
            publication.depth_width, publication.output_width);
        depth_dy = realign(publication.depth_y, publication.output_y,
            publication.depth_height, publication.output_height);
        publication.depth_width = std::min(publication.depth_width,
            available(depth->width, publication.depth_x));
        publication.depth_height = std::min(publication.depth_height,
            available(depth->height, publication.depth_y));
    }
    if (motion_dx != 0 || motion_dy != 0 || depth_dx != 0 || depth_dy != 0) {
        static std::atomic<unsigned> recorded{0};
        if (recorded.fetch_add(1, std::memory_order_relaxed) < 16) {
            const auto pair = [](long x, long y) {
                return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
                    static_cast<std::uint32_t>(y);
            };
            record(kRecordRealigned, feature.stream & 0xFFFFFFFFULL, pair(motion_dx, motion_dy),
                pair(depth_dx, depth_dy));
        }
    }
}

[[nodiscard]] std::uint64_t float_bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] std::uint64_t pack_rect(std::uint32_t x, std::uint32_t y, std::uint32_t width,
    std::uint32_t height) noexcept {
    return (static_cast<std::uint64_t>(x & 0xFFFFU) << 48U) |
        (static_cast<std::uint64_t>(y & 0xFFFFU) << 32U) |
        (static_cast<std::uint64_t>(width & 0xFFFFU) << 16U) | (height & 0xFFFFU);
}

// 839/840 (V520): what a publication says, as the capture read it, to set
// against what the game's upscaler meant (Skyrim's halo, PureDark's DLSS).
void record_parameters(const DlssMotionVectorPublication& publication, const Feature& feature,
    std::uint64_t publications, std::uint32_t motion_format, std::uint32_t motion_width,
    std::uint32_t motion_height) noexcept {
    if (publications >= 4 && publications % 2000 != 0) return;
    record(kRecordParameters,
        (feature.stream & 0xFFFFULL) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(feature.flags)) << 16U) |
            (publication.reset ? 1ULL << 48U : 0ULL) |
            (publication.jittered ? 1ULL << 49U : 0ULL),
        (float_bits(publication.scale_x) << 32U) | float_bits(publication.scale_y),
        pack_rect(publication.motion_x, publication.motion_y, publication.motion_width,
            publication.motion_height));
    record(kRecordDepthRect,
        pack_rect(publication.depth_x, publication.depth_y, publication.depth_width,
            publication.depth_height),
        publication.depth != nullptr ? 1U : 0U, publication.depth_inverted ? 1U : 0U);
    record(kRecordParametersMore,
        pack_rect(publication.output_x, publication.output_y, publication.output_width,
            publication.output_height),
        (float_bits(publication.jitter_x) << 32U) | float_bits(publication.jitter_y),
        (static_cast<std::uint64_t>(motion_format & 0xFFFFU) << 48U) |
            (static_cast<std::uint64_t>(motion_width & 0xFFFFFFU) << 24U) |
            (motion_height & 0xFFFFFFU));
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
        if (found != s.features.end()) {
            record_parameters(publication, feature, found->second.publications,
                motion_description.Format, static_cast<std::uint32_t>(motion_description.Width),
                motion_description.Height);
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
// D3D12 one (or, V524, the bridged D3D11 session's for a D3D12 game), or a
// bridged session's D3D11 or Vulkan one (V519 - before, only
// the D3D12 capture's flag was read, so a bridged game whose DLSS started
// before Hybrid was switched on never registered: Skyrim, 824 reason 1 on
// every evaluation). _nvngx.dll serves every feature type and is not typed;
// in D3D11 (untyped_is_upscaling) the only feature evaluated with an output
// and motion vectors is super resolution - there is no D3D11 frame
// generation - so there it registers too. PureDark's Skyrim upscaler links
// NGX statically, which makes _nvngx.dll the outermost call.
// A D3D12 evaluation is captured by the native session's capture, or (V524)
// in a bridged D3D11 session whose game renders in D3D12 - Cyberpunk 2077
// under R.E.A.L. VR - by capture12_bridged.
[[nodiscard]] bool d3d12_capturing() noexcept {
    auto& s = state();
    return s.active.load(std::memory_order_acquire) ||
        s.d3d11_active.load(std::memory_order_acquire);
}

void capture12_bridged(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept;
// V524: after the DLSS call, the alternating-eye anchor's output patches.
void complete12(ID3D12GraphicsCommandList* list, const NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Result result) noexcept;

// The native capture first: a native D3D12 session never runs the bridge.
void clear_patch_transport() noexcept;

void capture12(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    clear_patch_transport();
    if (state().active.load(std::memory_order_acquire)) {
        capture(list, handle, parameters);
    } else {
        capture12_bridged(list, handle, parameters);
    }
}

template <unsigned Slot>
void register_late(const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    bool active, bool untyped_is_upscaling = false) noexcept {
    if constexpr (Slot >= 2) {
        if (!untyped_is_upscaling) return;
    }
    {
        try {
            auto& s = state();
            if (!active || handle == nullptr || parameters == nullptr) {
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

// V526: where a D3D12 evaluation is captured (capture12_here), and what an
// evaluation the capture passes over carried (record 855).
thread_local bool t_chain_captured = false;
[[nodiscard]] bool capture12_here(unsigned slot, bool outermost) noexcept;
void note_uncaptured12(unsigned slot, bool outermost, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept;

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters, d3d12_capturing());
    const bool outermost = depth.outermost();
    const bool capture_here = capture12_here(Slot, outermost);
    if (capture_here) {
        capture12(list, handle, parameters);
    } else {
        note_uncaptured12(Slot, outermost, handle, parameters);
        try {
            auto& s = state();
            if (d3d12_capturing()) {
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
    const NVSDK_NGX_Result result =
        state().modules[Slot].evaluate(list, handle, parameters, callback);
    if (capture_here) complete12(list, parameters, result);
    if (outermost) t_chain_captured = false;
    return result;
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate_c(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters, d3d12_capturing());
    const bool outermost = depth.outermost();
    const bool capture_here = capture12_here(Slot, outermost);
    if (capture_here) {
        capture12(list, handle, parameters);
    } else {
        note_uncaptured12(Slot, outermost, handle, parameters);
    }
    const NVSDK_NGX_Result result =
        state().modules[Slot].evaluate_c(list, handle, parameters, callback);
    if (capture_here) complete12(list, parameters, result);
    if (outermost) t_chain_captured = false;
    return result;
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
        // V521: the depth went over as a float unpacked from a packed
        // depth-stencil buffer.
        bool depth_unpacked{};
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
        const D3D11_RECT& depth_rect,
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
                HRESULT depth_result = prepare(slot.depth, depth, true);
                // V521: a packed depth-stencil goes over unpacked to a float.
                if (depth_result == DXGI_ERROR_UNSUPPORTED) {
                    depth_result = prepare_unpacked(slot.depth, depth);
                }
                depth_ready = SUCCEEDED(depth_result);
                staged->depth_unshareable = depth_result == DXGI_ERROR_UNSUPPORTED;
            }
            copy(context, slot.motion, motion);
            if (depth_ready && slot.depth.unpacked) {
                depth_ready = SUCCEEDED(unpack(context, slot.depth, depth, depth_rect));
                staged->depth_unpacked = depth_ready;
            } else if (depth_ready) {
                copy(context, slot.depth, depth);
            }
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
        // V521, an unpacked depth: the float texture written through
        // `target`, from the game's depth read through `read_view`, made for
        // `read_from` (held, so its address cannot come back as another).
        bool unpacked{};
        ComPtr<ID3D11UnorderedAccessView> target;
        ComPtr<ID3D11ShaderResourceView> read_view;
        ComPtr<ID3D11Texture2D> read_from;
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

    // The depth plane's view of a packed depth-stencil format a shader can
    // read; unknown where there is none (a typed format: no shader view).
    [[nodiscard]] static DXGI_FORMAT unpack_view_format(DXGI_FORMAT format) noexcept {
        switch (format) {
            case DXGI_FORMAT_R24G8_TYPELESS:
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            default:
                return DXGI_FORMAT_UNKNOWN;
        }
    }

    // V521: for a packed depth-stencil buffer, a shared R32_FLOAT texture of
    // its size the unpack writes through a view of its own.
    [[nodiscard]] HRESULT prepare_unpacked(Shared& shared, ID3D11Texture2D* source) noexcept {
        try {
            D3D11_TEXTURE2D_DESC description{};
            source->GetDesc(&description);
            if (description.SampleDesc.Count != 1 || description.ArraySize != 1 ||
                (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 ||
                unpack_view_format(description.Format) == DXGI_FORMAT_UNKNOWN) {
                return DXGI_ERROR_UNSUPPORTED;
            }
            HRESULT result = S_OK;
            if (!unpack_shader_) {
                result = d3d11_->CreateComputeShader(g_xrfg_depth_unpack_shader,
                    sizeof(g_xrfg_depth_unpack_shader), nullptr, unpack_shader_.GetAddressOf());
                if (FAILED(result)) return result;
                D3D11_BUFFER_DESC region{};
                region.ByteWidth = 16;
                region.Usage = D3D11_USAGE_DEFAULT;
                region.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                result = d3d11_->CreateBuffer(&region, nullptr, unpack_region_.GetAddressOf());
                if (FAILED(result)) {
                    unpack_shader_.Reset();
                    return result;
                }
            }
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = description.Width;
            desc.Height = description.Height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Shared replacement;
            result = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(replacement.d3d12.GetAddressOf()));
            if (FAILED(result)) return result;
            HANDLE handle = nullptr;
            result = device_->CreateSharedHandle(replacement.d3d12.Get(), nullptr, GENERIC_ALL,
                nullptr, &handle);
            if (FAILED(result)) return result;
            result = d3d11_->OpenSharedResource1(handle, IID_PPV_ARGS(replacement.d3d11.GetAddressOf()));
            CloseHandle(handle);
            if (FAILED(result)) return result;
            D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_R32_FLOAT;
            view.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            result = d3d11_->CreateUnorderedAccessView(replacement.d3d11.Get(), &view,
                replacement.target.GetAddressOf());
            if (FAILED(result)) return result;
            replacement.source = description;
            replacement.unpacked = true;
            shared = std::move(replacement);
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    // V521: the depth plane over `rect` into the shared float texture, on the
    // game's context. Its compute bindings are put back after; a depth buffer
    // still bound for writing cannot be read, so it is unbound for the
    // dispatch and bound again.
    [[nodiscard]] HRESULT unpack(ID3D11DeviceContext* context, Shared& shared,
        ID3D11Texture2D* source, const D3D11_RECT& rect) noexcept {
        try {
            if (!shared.read_view || shared.read_from.Get() != source) {
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = unpack_view_format(shared.source.Format);
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = 1;
                shared.read_view.Reset();
                shared.read_from.Reset();
                const HRESULT result = d3d11_->CreateShaderResourceView(source, &view,
                    shared.read_view.GetAddressOf());
                if (FAILED(result)) return result;
                shared.read_from = source;
            }
            const LONG width = static_cast<LONG>(shared.source.Width);
            const LONG height = static_cast<LONG>(shared.source.Height);
            LONG left = std::clamp<LONG>(rect.left, 0, width);
            LONG top = std::clamp<LONG>(rect.top, 0, height);
            LONG right = std::clamp<LONG>(rect.right, 0, width);
            LONG bottom = std::clamp<LONG>(rect.bottom, 0, height);
            if (right <= left || bottom <= top) {
                left = 0;
                top = 0;
                right = width;
                bottom = height;
            }
            const std::array<UINT, 4> region{static_cast<UINT>(left), static_cast<UINT>(top),
                static_cast<UINT>(right - left), static_cast<UINT>(bottom - top)};

            ComPtr<ID3D11ComputeShader> shader;
            context->CSGetShader(shader.GetAddressOf(), nullptr, nullptr);
            ComPtr<ID3D11ShaderResourceView> read;
            context->CSGetShaderResources(0, 1, read.GetAddressOf());
            ComPtr<ID3D11UnorderedAccessView> write;
            context->CSGetUnorderedAccessViews(0, 1, write.GetAddressOf());
            ComPtr<ID3D11Buffer> constants;
            context->CSGetConstantBuffers(0, 1, constants.GetAddressOf());
            std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
            ID3D11DepthStencilView* depth_view = nullptr;
            context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth_view);
            bool unbound = false;
            if (depth_view != nullptr) {
                ComPtr<ID3D11Resource> bound;
                depth_view->GetResource(bound.GetAddressOf());
                if (same_object(bound.Get(), source)) {
                    context->OMSetRenderTargetsAndUnorderedAccessViews(
                        static_cast<UINT>(targets.size()), targets.data(), nullptr, 0,
                        D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
                    unbound = true;
                }
            }

            context->UpdateSubresource(unpack_region_.Get(), 0, nullptr, region.data(), 0, 0);
            ID3D11ShaderResourceView* depth_read = shared.read_view.Get();
            ID3D11UnorderedAccessView* depth_write = shared.target.Get();
            ID3D11Buffer* region_buffer = unpack_region_.Get();
            const UINT keep_counter = static_cast<UINT>(-1);
            context->CSSetShader(unpack_shader_.Get(), nullptr, 0);
            context->CSSetShaderResources(0, 1, &depth_read);
            context->CSSetUnorderedAccessViews(0, 1, &depth_write, &keep_counter);
            context->CSSetConstantBuffers(0, 1, &region_buffer);
            context->Dispatch((region[2] + 7U) / 8U, (region[3] + 7U) / 8U, 1);

            ID3D11ShaderResourceView* old_read = read.Get();
            ID3D11UnorderedAccessView* old_write = write.Get();
            ID3D11Buffer* old_constants = constants.Get();
            context->CSSetShader(shader.Get(), nullptr, 0);
            context->CSSetShaderResources(0, 1, &old_read);
            context->CSSetUnorderedAccessViews(0, 1, &old_write, &keep_counter);
            context->CSSetConstantBuffers(0, 1, &old_constants);
            if (unbound) {
                context->OMSetRenderTargetsAndUnorderedAccessViews(
                    static_cast<UINT>(targets.size()), targets.data(), depth_view, 0,
                    D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
            }
            for (auto* target : targets) {
                if (target != nullptr) target->Release();
            }
            if (depth_view != nullptr) depth_view->Release();
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
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
    // V521: the depth unpack, made on the game's device at its first use.
    ComPtr<ID3D11ComputeShader> unpack_shader_;
    ComPtr<ID3D11Buffer> unpack_region_;
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
        // The publication's rectangles first (V521): the depth's is what the
        // unpack of a packed depth covers.
        DlssMotionVectorPublication publication{};
        Shape depth_shape{depth_description.Width, depth_description.Height};
        fill_publication(publication, feature, parameters,
            {output_description.Width, output_description.Height},
            {motion_description.Width, motion_description.Height},
            depth ? &depth_shape : nullptr);
        const D3D11_RECT depth_rect{static_cast<LONG>(publication.depth_x),
            static_cast<LONG>(publication.depth_y),
            static_cast<LONG>(publication.depth_x + publication.depth_width),
            static_cast<LONG>(publication.depth_y + publication.depth_height)};
        GuideTransport11::Staged staged{};
        const HRESULT staged_result =
            transport->stage(context, motion.Get(), depth.Get(), depth_rect, &staged);
        if (FAILED(staged_result)) {
            skip_d3d11(6, static_cast<std::uint32_t>(staged_result));
            return;
        }
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
        if (found != s.features.end()) {
            record_parameters(publication, feature, found->second.publications,
                motion_description.Format, motion_description.Width, motion_description.Height);
        }
        if (found != s.features.end() && found->second.publications++ == 0) {
            record(kRecordD3D11Publication,
                (feature.stream & 0xFFFFFFFFULL) |
                    (staged.depth != nullptr ? 1ULL << 32U : 0ULL) |
                    (staged.depth_unshareable ? 1ULL << 33U : 0ULL) |
                    (staged.depth_unpacked ? 1ULL << 34U : 0ULL),
                (static_cast<std::uint64_t>(motion_description.Format) << 32U) |
                    static_cast<std::uint64_t>(depth ? depth_description.Format : 0),
                (static_cast<std::uint64_t>(publication.output_width) << 32U) |
                    publication.output_height);
        }
    } catch (...) {
    }
}

// ------------------------------------------- alternating-eye anchor (V524)
// R.E.A.L. VR renders one eye per game frame through one DLSS feature; the
// registry gives such a stream's evaluations to the eyes by serial parity
// once it knows which parity is the left (configure_dlss_alternating_eyes).
// That is read from the pictures: 16 patches of an evaluation's finished DLSS
// output against the same places of each eye image, by rank correlation of
// their luminance - the eye images are tone-mapped, DLSS's output is not, and
// ranks do not care. An evaluation is most like the eye it was rendered for:
// the other eye is another viewpoint (the near scene shifts by the eyes'
// parallax) and another moment.
constexpr std::uint32_t kAnchorPatches = 16;
constexpr std::uint32_t kAnchorPatch = 32;
constexpr std::uint32_t kAnchorAtlas = 4 * kAnchorPatch;
constexpr std::uint32_t kAnchorVotes = 5;
constexpr double kAnchorMargin = 0.02;
//   846 (V524) an anchor comparison: a the older evaluation's publication
//       << 32 | the newer's, b the pairing older-left's summed correlation
//       (float bits) << 32 | older-right's, c the outcome (0 a vote, 1 too
//       few patches with detail, 2 the pairings too close) | the vote's left
//       parity << 8 | agreeing votes << 16 | bit 24 anchored | the fewest
//       patches used << 32
//   847 (V524) a bridged evaluation's vectors, the first 512 (read for
//       every evaluation under alternating eyes, for their stretch): a its
//       publication, b the mean x (float bits) << 32 | the mean y, c the mean
//       |x| << 32 | the mean |y|, over 16 patches of 32x32 texels, in the
//       vectors' own units
constexpr std::int64_t kRecordAnchor = 846;
constexpr std::int64_t kRecordMotionMean = 847;
//   849 (V524) a comparison the anchor could not queue, the first of each
//       reason and every 1000th after: a the reason (1 no bridged stream or
//       device, 2 the newest evaluations' patches not taken, 3 they are not
//       the stream's two newest before the release, 4 an eye image it cannot
//       read: c its DXGI format | its flags << 32, 5 a patch outside the eye:
//       c width << 32 | height, 6 its resources could not be made), b the
//       count so far
constexpr std::int64_t kRecordAnchorSkip = 849;

// Alternating eyes (configure_ngx_alternating_eye_anchor): the anchor, and
// the vectors' stretch to each eye's own frame.
std::atomic_bool g_anchor_enabled{false};

// ------------------------------------------- R.E.A.L. VR's inner call (V526)
// R.E.A.L. VR hooks _nvngx.dll's exports before the layer does, and fixes the
// call inside its hook (realvr-dlss-handover, the RealVR64.dll analysis of
// 2026-10-19): it replaces the motion vectors with its own, rebased from "the
// game's last step" (the other eye's under alternate-eye rendering) to "the
// same eye two evaluations back" ("DLSS combined MV", in the game's units),
// replaces the colour with its copy, doubles the frame-time delta, and
// evaluates through a feature handle per eye, alternating. The layer's
// _nvngx.dll hook, outermost, saw the game's raw call - one handle, raw
// vectors spanning the other eye's step (alternating 3:1 while turning), no
// frame-time delta - and skipped the nested call into nvngx_dlss.dll (999
// nested in every 1000 evaluations, record 826) that carries the fixed one.
// Under R.E.A.L. VR the capture takes the first call into a typed module
// (nvngx_dlss.dll, nvngx_dlssd.dll) instead: the nested one in the game, the
// game's own where it calls the module directly. Its per-eye handles publish
// as one stream through one transport (capture12_bridged), so the eyes still
// come from the anchor and the serial parity, now on same-eye vectors.
bool capture12_here(unsigned slot, bool outermost) noexcept {
    if (!g_anchor_enabled.load(std::memory_order_acquire)) return outermost;
    if (slot >= 2 || t_chain_captured) return false;
    t_chain_captured = true;
    return true;
}

// The first eight characters of a resource's debug name, ASCII, low byte
// first: R.E.A.L. VR names its textures ("DLSS combined MV").
[[nodiscard]] std::uint64_t debug_name_chars(ID3D12Object* object) noexcept {
    if (object == nullptr) return 0;
    static const GUID kNameW{0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};
    static const GUID kName{0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};
    std::array<wchar_t, 64> wide{};
    UINT size = static_cast<UINT>((wide.size() - 1) * sizeof(wchar_t));
    std::uint64_t packed = 0;
    if (SUCCEEDED(object->GetPrivateData(kNameW, &size, wide.data()))) {
        for (std::size_t index = 0; index < 8 && wide[index] != 0; ++index) {
            packed |= static_cast<std::uint64_t>(wide[index] & 0x7F) << (8U * index);
        }
        return packed;
    }
    std::array<char, 64> narrow{};
    size = static_cast<UINT>(narrow.size() - 1);
    if (SUCCEEDED(object->GetPrivateData(kName, &size, narrow.data()))) {
        for (std::size_t index = 0; index < 8 && narrow[index] != 0; ++index) {
            packed |= static_cast<std::uint64_t>(narrow[index] & 0x7F) << (8U * index);
        }
    }
    return packed;
}

//   854 (V526) a captured evaluation under R.E.A.L. VR, the first 64 and
//       every 2000th: a its publication | the handle's index (first seen
//       order) << 48 | the call's depth << 56, b its vectors' resource
//       (low 48 bits), c the vectors' debug name, 8 ASCII characters
//   855 (V526) an evaluation passed over under R.E.A.L. VR (the outer,
//       raw call), the first 64: a the handle's index << 48 | the module
//       slot << 40 | depth << 56, b the vectors' resource, c their name
constexpr std::int64_t kRecordInnerEvaluation = 854;
constexpr std::int64_t kRecordOuterEvaluation = 855;

[[nodiscard]] std::uint64_t realvr_handle_index(const NVSDK_NGX_Handle* handle) noexcept {
    static std::mutex mutex;
    static auto* seen = new std::vector<const NVSDK_NGX_Handle*>;
    std::scoped_lock lock(mutex);
    const auto found = std::find(seen->begin(), seen->end(), handle);
    if (found != seen->end()) return static_cast<std::uint64_t>(found - seen->begin());
    seen->push_back(handle);
    return seen->size() - 1;
}

void note_uncaptured12(unsigned slot, bool outermost, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    try {
        static_cast<void>(outermost);
        if (!g_anchor_enabled.load(std::memory_order_acquire)) return;
        static std::atomic<std::uint32_t> recorded{0};
        if (recorded.fetch_add(1, std::memory_order_relaxed) >= 64) return;
        auto* motion = get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_MotionVectors);
        record(kRecordOuterEvaluation,
            (realvr_handle_index(handle) << 48U) | (static_cast<std::uint64_t>(slot) << 40U) |
                (static_cast<std::uint64_t>(t_depth) << 56U),
            reinterpret_cast<std::uintptr_t>(motion) & 0xFFFFFFFFFFFFULL, debug_name_chars(motion));
    } catch (...) {
    }
}


void skip_anchor(std::uint32_t reason, std::uint64_t detail = 0) noexcept {
    static std::array<std::atomic<std::uint64_t>, 8> counts{};
    const std::uint64_t count = counts[reason & 7U].fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || count % 1000 == 0) record(kRecordAnchorSkip, reason, count, detail);
}

// Patch `index` of a 4x4 grid over the middle 70% of an extent.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> anchor_patch_origin(
    std::uint32_t index, std::uint32_t width, std::uint32_t height) noexcept {
    const auto place = [](std::uint32_t cell, std::uint32_t extent) {
        const double centre =
            (0.15 + 0.7 * (static_cast<double>(cell) + 0.5) / 4.0) * static_cast<double>(extent);
        const double last = extent > kAnchorPatch ? static_cast<double>(extent - kAnchorPatch) : 0.0;
        return static_cast<std::uint32_t>(std::clamp(centre - kAnchorPatch / 2.0, 0.0, last));
    };
    return {place(index % 4U, width), place(index / 4U, height)};
}

// V533: where an output patch's texel (0, 0) lies on eye `eye`'s image: the
// eye rectangle's origin, or - where the eye image is a window of a wider
// output (R.E.A.L. VR's Ghost of Tsushima, dlss_inset_origin) - the output's
// origin on it, so a patch is compared with the same picture.
[[nodiscard]] std::pair<std::int32_t, std::int32_t> eye_patch_origin(
    std::uint64_t stream, std::uint32_t eye, const NgxEyeImage& image) noexcept {
    std::int32_t x = image.x, y = image.y;
    if (!dlss_inset_origin(stream, eye, &x, &y)) return {image.x, image.y};
    return {x, y};
}

// Whether a patch at (x, y) of the output, `margin` wider either way, lies
// on the eye's rectangle once moved by `origin`.
[[nodiscard]] bool patch_on_eye(std::pair<std::int32_t, std::int32_t> origin,
    std::uint32_t x, std::uint32_t y, std::uint32_t margin, const NgxEyeImage& image) noexcept {
    const std::int64_t left = static_cast<std::int64_t>(origin.first) + x - margin;
    const std::int64_t top = static_cast<std::int64_t>(origin.second) + y;
    return left >= image.x && top >= image.y &&
        left + kAnchorPatch + 2 * static_cast<std::int64_t>(margin) <=
            static_cast<std::int64_t>(image.x) + image.width &&
        top + kAnchorPatch <= static_cast<std::int64_t>(image.y) + image.height;
}

// The typeless families read as their typed members: a runtime's images are
// typeless (R8G8B8A8_TYPELESS on Virtual Desktop and Pimax), and so are the
// bridge's shared textures made from them.
[[nodiscard]] DXGI_FORMAT anchor_typed_format(DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R32G32_TYPELESS:
            return DXGI_FORMAT_R32G32_FLOAT;
        case DXGI_FORMAT_R16G16_TYPELESS:
            return DXGI_FORMAT_R16G16_FLOAT;
        default:
            return format;
    }
}

[[nodiscard]] std::uint32_t anchor_texel_bytes(DXGI_FORMAT format) noexcept {
    switch (anchor_typed_format(format)) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32_FLOAT:
            return 8;
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16_FLOAT:
            return 4;
        default:
            return 0;
    }
}

[[nodiscard]] constexpr std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept {
    return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept {
    const std::uint32_t exponent = (bits >> 10U) & 0x1FU;
    const std::uint32_t mantissa = bits & 0x3FFU;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = 0.0F;
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<int>(exponent) - 25);
    }
    return (bits & 0x8000U) != 0 ? -value : value;
}

// An unsigned small float (R11G11B10's channels): a five-bit exponent.
[[nodiscard]] float small_float(std::uint32_t bits, int mantissa_bits) noexcept {
    const std::uint32_t mantissa = bits & ((1U << mantissa_bits) - 1U);
    const std::uint32_t exponent = (bits >> mantissa_bits) & 0x1FU;
    if (exponent == 0) return std::ldexp(static_cast<float>(mantissa), -14 - mantissa_bits);
    if (exponent == 31) return 0.0F;
    return std::ldexp(static_cast<float>(mantissa | (1U << mantissa_bits)),
        static_cast<int>(exponent) - 15 - mantissa_bits);
}

// A texel's luminance, its colour channels as stored (no sRGB decode: the
// rank correlation does not need one).
[[nodiscard]] float anchor_luminance(const std::byte* texel, DXGI_FORMAT format) noexcept {
    float r = 0.0F, g = 0.0F, b = 0.0F;
    switch (anchor_typed_format(format)) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: {
            std::array<float, 4> value{};
            std::memcpy(value.data(), texel, sizeof(value));
            r = value[0];
            g = value[1];
            b = value[2];
            break;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
            std::array<std::uint16_t, 4> value{};
            std::memcpy(value.data(), texel, sizeof(value));
            r = half_to_float(value[0]);
            g = half_to_float(value[1]);
            b = half_to_float(value[2]);
            break;
        }
        case DXGI_FORMAT_R11G11B10_FLOAT: {
            std::uint32_t value = 0;
            std::memcpy(&value, texel, sizeof(value));
            r = small_float(value, 6);
            g = small_float(value >> 11U, 6);
            b = small_float(value >> 22U, 5);
            break;
        }
        case DXGI_FORMAT_R10G10B10A2_UNORM: {
            std::uint32_t value = 0;
            std::memcpy(&value, texel, sizeof(value));
            r = static_cast<float>(value & 1023U) / 1023.0F;
            g = static_cast<float>((value >> 10U) & 1023U) / 1023.0F;
            b = static_cast<float>((value >> 20U) & 1023U) / 1023.0F;
            break;
        }
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            r = static_cast<float>(std::to_integer<unsigned>(texel[0])) / 255.0F;
            g = static_cast<float>(std::to_integer<unsigned>(texel[1])) / 255.0F;
            b = static_cast<float>(std::to_integer<unsigned>(texel[2])) / 255.0F;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            b = static_cast<float>(std::to_integer<unsigned>(texel[0])) / 255.0F;
            g = static_cast<float>(std::to_integer<unsigned>(texel[1])) / 255.0F;
            r = static_cast<float>(std::to_integer<unsigned>(texel[2])) / 255.0F;
            break;
        default:
            break;
    }
    const float luminance = 0.2126F * r + 0.7152F * g + 0.0722F * b;
    return std::isfinite(luminance) ? luminance : 0.0F;
}

// A patch's luminance, kAnchorPatch square, from a footprint in a mapped
// readback buffer.
void read_anchor_patch(const std::byte* base, std::uint64_t pitch, std::uint32_t x,
    std::uint32_t y, DXGI_FORMAT format, std::vector<float>* out) {
    const std::uint32_t bytes = anchor_texel_bytes(format);
    out->resize(static_cast<std::size_t>(kAnchorPatch) * kAnchorPatch);
    for (std::uint32_t row = 0; row < kAnchorPatch; ++row) {
        const std::byte* line = base + (y + row) * pitch + static_cast<std::uint64_t>(x) * bytes;
        for (std::uint32_t column = 0; column < kAnchorPatch; ++column) {
            (*out)[static_cast<std::size_t>(row) * kAnchorPatch + column] =
                anchor_luminance(line + static_cast<std::size_t>(column) * bytes, format);
        }
    }
}

// Spearman's rank correlation; NaN where either patch is flat.
[[nodiscard]] double rank_correlation(const std::vector<float>& a, const std::vector<float>& b) {
    const auto ranks = [](const std::vector<float>& values) {
        std::vector<std::uint32_t> order(values.size());
        std::iota(order.begin(), order.end(), 0U);
        std::sort(order.begin(), order.end(),
            [&](std::uint32_t l, std::uint32_t r) { return values[l] < values[r]; });
        std::vector<double> rank(values.size());
        for (std::size_t first = 0; first < order.size();) {
            std::size_t last = first;
            while (last + 1 < order.size() && values[order[last + 1]] == values[order[first]]) ++last;
            const double shared = (static_cast<double>(first) + static_cast<double>(last)) / 2.0;
            for (std::size_t index = first; index <= last; ++index) rank[order[index]] = shared;
            first = last + 1;
        }
        return rank;
    };
    if (a.size() != b.size() || a.empty()) return std::numeric_limits<double>::quiet_NaN();
    const auto ra = ranks(a), rb = ranks(b);
    const double mean = (static_cast<double>(a.size()) - 1.0) / 2.0;
    double covariance = 0.0, va = 0.0, vb = 0.0;
    for (std::size_t index = 0; index < ra.size(); ++index) {
        covariance += (ra[index] - mean) * (rb[index] - mean);
        va += (ra[index] - mean) * (ra[index] - mean);
        vb += (rb[index] - mean) * (rb[index] - mean);
    }
    const double floor = 1.0e-3 * static_cast<double>(a.size()) * static_cast<double>(a.size());
    if (va < floor || vb < floor) return std::numeric_limits<double>::quiet_NaN();
    return covariance / std::sqrt(va * vb);
}


// ------------------------------------------ vector alignment probe (V524)
// R.E.A.L. VR in Cyberpunk 2077: Hybrid's ghost of near things sits on each
// eye's outer side, the left eye's to the left and the right eye's to the
// right - parallax between the vectors and the picture, larger near than
// far. The probe measures it: in 16 regions of 64x64 render texels, the
// sideways shift that best lines up the edges of an evaluation's vectors
// with the edges of its colour (DLSS's input, the eye's picture), of its
// vectors with its depth, and of its depth with its colour. A shift that
// follows the raw depth (reversed Z: proportional to 1 / distance) is
// parallax; one that does not is an image offset. Record 850 per region:
//   a the publication << 24 | the serial's low 16 bits << 8 | the region
//   b vectors-to-colour shift (int16) << 48 | vectors-to-depth shift << 32 |
//     the region's median raw depth (float bits)
//   c depth-to-colour shift (int16) | the three matches' correlations, each
//     x1000 as a uint16, vectors-colour << 16, vectors-depth << 32,
//     depth-colour << 48
// A shift s means the first's edges sit s texels right of the second's.
// Analysed on a thread of its own, off the frame.
constexpr std::int64_t kRecordAlignmentProbe = 850;
constexpr std::uint32_t kProbeRegions = 16;
constexpr std::uint32_t kProbeRegion = 64;
constexpr std::uint32_t kProbeAtlas = 4 * kProbeRegion;
constexpr int kProbeShift = 12;

// Region `index` of a 4x4 grid over the middle 70% of an extent.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> probe_region_origin(
    std::uint32_t index, std::uint32_t width, std::uint32_t height) noexcept {
    const auto place = [](std::uint32_t cell, std::uint32_t extent) {
        const double centre =
            (0.15 + 0.7 * (static_cast<double>(cell) + 0.5) / 4.0) * static_cast<double>(extent);
        const double last = extent > kProbeRegion ? static_cast<double>(extent - kProbeRegion) : 0.0;
        return static_cast<std::uint32_t>(std::clamp(centre - kProbeRegion / 2.0, 0.0, last));
    };
    return {place(index % 4U, width), place(index / 4U, height)};
}

struct ProbeData {
    std::uint64_t publication{};
    std::uint64_t serial{};
    bool has_depth{};
    // Per region, kProbeRegion square, row-major.
    std::vector<float> colour, motion_x, motion_y, depth;
};

// The sideways shift that best lines up two edge maps of a region: the
// Pearson correlation of a(x + s, y) with b(x, y) over the overlap.
[[nodiscard]] std::pair<int, double> best_shift(
    const std::vector<float>& a, const std::vector<float>& b, std::size_t base) {
    int best = 0;
    double best_value = -2.0;
    for (int shift = -kProbeShift; shift <= kProbeShift; ++shift) {
        double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < kProbeRegion; ++y) {
            for (int x = 1 + std::max(0, -shift); x < static_cast<int>(kProbeRegion) - 1 - std::max(0, shift); ++x) {
                const double va = a[base + y * kProbeRegion + static_cast<std::size_t>(x + shift)];
                const double vb = b[base + y * kProbeRegion + static_cast<std::size_t>(x)];
                sa += va;
                sb += vb;
                saa += va * va;
                sbb += vb * vb;
                sab += va * vb;
                ++n;
            }
        }
        if (n < 64) continue;
        const double count = static_cast<double>(n);
        const double cov = sab - sa * sb / count;
        const double var_a = saa - sa * sa / count;
        const double var_b = sbb - sb * sb / count;
        if (var_a <= 1.0e-18 || var_b <= 1.0e-18) continue;
        const double value = cov / std::sqrt(var_a * var_b);
        if (value > best_value) {
            best_value = value;
            best = shift;
        }
    }
    return {best, best_value};
}

void analyse_probe(const ProbeData& data) noexcept {
    try {
        const std::size_t area = static_cast<std::size_t>(kProbeRegion) * kProbeRegion;
        // Horizontal edge strength: |f(x + 1) - f(x - 1)|.
        const auto edges = [&](const std::vector<float>& f, const std::vector<float>* g) {
            std::vector<float> e(f.size(), 0.0F);
            for (std::size_t region = 0; region < kProbeRegions; ++region) {
                for (std::uint32_t y = 0; y < kProbeRegion; ++y) {
                    for (std::uint32_t x = 1; x + 1 < kProbeRegion; ++x) {
                        const std::size_t at = region * area + y * kProbeRegion + x;
                        float value = std::fabs(f[at + 1] - f[at - 1]);
                        if (g != nullptr) value += std::fabs((*g)[at + 1] - (*g)[at - 1]);
                        e[at] = std::isfinite(value) ? value : 0.0F;
                    }
                }
            }
            return e;
        };
        const auto colour = edges(data.colour, nullptr);
        const auto motion = edges(data.motion_x, &data.motion_y);
        const auto depth = data.has_depth ? edges(data.depth, nullptr) : std::vector<float>{};
        const auto pack = [](int shift) {
            return static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(shift)));
        };
        const auto quality = [](double value) {
            return static_cast<std::uint64_t>(std::clamp(value, 0.0, 1.0) * 1000.0);
        };
        for (std::uint32_t region = 0; region < kProbeRegions; ++region) {
            const std::size_t base = region * area;
            const auto [mv_colour, q_mv_colour] = best_shift(motion, colour, base);
            std::pair<int, double> mv_depth{0, -1.0}, depth_colour{0, -1.0};
            float median_depth = 0.0F;
            if (data.has_depth) {
                mv_depth = best_shift(motion, depth, base);
                depth_colour = best_shift(depth, colour, base);
                std::vector<float> values(data.depth.begin() + static_cast<std::ptrdiff_t>(base),
                    data.depth.begin() + static_cast<std::ptrdiff_t>(base + area));
                std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
                median_depth = values[values.size() / 2];
            }
            record(kRecordAlignmentProbe,
                (data.publication << 24U) | ((data.serial & 0xFFFFU) << 8U) | region,
                (pack(mv_colour) << 48U) | (pack(mv_depth.first) << 32U) | float_bits(median_depth),
                pack(depth_colour.first) | (quality(q_mv_colour) << 16U) |
                    (quality(mv_depth.second) << 32U) | (quality(depth_colour.second) << 48U));
        }
    } catch (...) {
    }
}

[[nodiscard]] float probe_value(const std::byte* texel, DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT: {
            float value = 0.0F;
            std::memcpy(&value, texel, sizeof(value));
            return value;
        }
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_D16_UNORM: {
            std::uint16_t value = 0;
            std::memcpy(&value, texel, sizeof(value));
            return static_cast<float>(value) / 65535.0F;
        }
        default:
            return 0.0F;
    }
}

[[nodiscard]] std::uint32_t probe_depth_bytes(DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
            return 4;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_D16_UNORM:
            return 2;
        default:
            return 0;
    }
}


// --------------------------------------- eye image measurement (V524, 851)
// R.E.A.L. VR's AERv2 renders one eye per game step and interpolates the
// other (RealVR.ini HighQualityOF2), so an eye image may be an evaluation's
// picture or the mod's interpolation, and which one changes frame by frame.
// The measurement says which: at every 4th release (the first 400, then
// every 64th) 16 regions of each eye image are compared with the finished
// DLSS output of each of the last four evaluations, sliding up to 16 output
// pixels sideways. Record 851 per eye per comparison:
//   a the newest evaluation's publication << 8 | the eye << 4 | the best
//     evaluation (0 the newest, 3 the oldest)
//   b each evaluation's best correlation x100 as a byte (newest in the low
//     byte) << 32 | each one's best shift as an int8 (newest low)
//   c the newest evaluation's serial (low 32 bits) | each evaluation's
//     correlation at no shift x100 as a byte << 32
// The correlation is Pearson's of ranked luminance (the output is not tone
// mapped, the eye image is).
constexpr std::int64_t kRecordEyeMeasure = 851;
constexpr int kMeasureShift = 16;
constexpr std::uint32_t kMeasureWindow = kAnchorPatch + 2U * kMeasureShift;

struct MeasureData {
    std::uint64_t publication{};
    std::uint64_t serial{};
    // [evaluation][patch][kAnchorPatch x kAnchorPatch], newest first.
    std::array<std::vector<float>, 4> outputs;
    // [eye][patch][kAnchorPatch rows x kMeasureWindow].
    std::array<std::vector<float>, 2> eyes;
};

[[nodiscard]] std::vector<double> rank_values(const float* values, std::size_t count) {
    std::vector<std::uint32_t> order(count);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(),
        [&](std::uint32_t l, std::uint32_t r) { return values[l] < values[r]; });
    std::vector<double> rank(count);
    for (std::size_t first = 0; first < count;) {
        std::size_t last = first;
        while (last + 1 < count && values[order[last + 1]] == values[order[first]]) ++last;
        const double shared = (static_cast<double>(first) + static_cast<double>(last)) / 2.0;
        for (std::size_t index = first; index <= last; ++index) rank[order[index]] = shared;
        first = last + 1;
    }
    return rank;
}

void analyse_measure(const MeasureData& data) noexcept {
    try {
        const std::size_t patch_area = static_cast<std::size_t>(kAnchorPatch) * kAnchorPatch;
        const std::size_t window_area = static_cast<std::size_t>(kAnchorPatch) * kMeasureWindow;
        // Ranked per patch once: the outputs' 32x32, the eyes' 32x64 windows.
        std::array<std::vector<double>, 4> outputs;
        for (std::size_t j = 0; j < 4; ++j) {
            outputs[j].resize(kAnchorPatches * patch_area);
            for (std::size_t p = 0; p < kAnchorPatches; ++p) {
                const auto ranked = rank_values(data.outputs[j].data() + p * patch_area, patch_area);
                std::copy(ranked.begin(), ranked.end(), outputs[j].begin() + static_cast<std::ptrdiff_t>(p * patch_area));
            }
        }
        std::array<std::vector<double>, 2> eyes;
        for (std::size_t k = 0; k < 2; ++k) {
            eyes[k].resize(kAnchorPatches * window_area);
            for (std::size_t p = 0; p < kAnchorPatches; ++p) {
                const auto ranked = rank_values(data.eyes[k].data() + p * window_area, window_area);
                std::copy(ranked.begin(), ranked.end(), eyes[k].begin() + static_cast<std::ptrdiff_t>(p * window_area));
            }
        }
        const auto correlation = [&](std::size_t j, std::size_t k, int shift) {
            double total = 0.0;
            std::uint32_t used = 0;
            for (std::size_t p = 0; p < kAnchorPatches; ++p) {
                double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
                for (std::uint32_t y = 0; y < kAnchorPatch; ++y) {
                    for (std::uint32_t x = 0; x < kAnchorPatch; ++x) {
                        const double a = outputs[j][p * patch_area + y * kAnchorPatch + x];
                        const double b = eyes[k][p * window_area + y * kMeasureWindow +
                            static_cast<std::size_t>(static_cast<int>(x) + kMeasureShift + shift)];
                        sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b;
                    }
                }
                const double n = static_cast<double>(patch_area);
                const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
                if (va < 1.0 || vb < 1.0) continue;
                total += (sab - sa * sb / n) / std::sqrt(va * vb);
                ++used;
            }
            return used >= 4 ? total / used : -1.0;
        };
        for (std::uint32_t k = 0; k < 2; ++k) {
            std::array<double, 4> best{-2, -2, -2, -2}, at_zero{};
            std::array<int, 4> shift{};
            for (std::size_t j = 0; j < 4; ++j) {
                at_zero[j] = correlation(j, k, 0);
                for (int s = -kMeasureShift; s <= kMeasureShift; ++s) {
                    const double value = correlation(j, k, s);
                    if (value > best[j]) {
                        best[j] = value;
                        shift[j] = s;
                    }
                }
            }
            std::uint32_t chosen = 0;
            for (std::uint32_t j = 1; j < 4; ++j) if (best[j] > best[chosen]) chosen = j;
            const auto byte = [](double value) {
                return static_cast<std::uint64_t>(std::clamp(value, 0.0, 1.0) * 100.0 + 0.5);
            };
            std::uint64_t b = 0, c = data.serial & 0xFFFFFFFFULL;
            for (std::uint32_t j = 0; j < 4; ++j) {
                b |= byte(best[j]) << (32U + 8U * j);
                b |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(shift[j]))) << (8U * j);
                c |= byte(at_zero[j]) << (32U + 8U * j);
            }
            record(kRecordEyeMeasure, (data.publication << 8U) | (k << 4U) | chosen, b, c);
        }
    } catch (...) {
    }
}

// -------------------------------------------- D3D12 in a bridged D3D11 session
// V524. A bridged D3D11 session whose game renders, and runs DLSS, in D3D12:
// Cyberpunk 2077 under R.E.A.L. VR hands the runtime a D3D11 session and keeps
// its rendering on the game's D3D12 device, so the D3D12 exports see the
// evaluations, but only a native D3D12 session captured them - Hybrid read KO
// 821 with the game's feature created and evaluated every frame. At each
// evaluation the vectors - and the depth, where its format can be shared - are
// copied in the game's own command list, ahead of DLSS, into textures made on
// the layer's D3D12 device and opened on the game's. A list cannot signal a
// fence, and the game's queue is not known here; the registry's snapshot copy
// waits for the bridged session's next image release
// (flush_ngx_guide_capture_d3d11), behind the bridge's wait for the game's
// D3D11 context. The mod's D3D11 copy of an eye image runs after the D3D12
// work that drew it, the DLSS evaluation and so these copies among it. Two
// slots per feature alternate as in the D3D11 transport; a slot is written
// again only once its last snapshot copy has run.
class GuideTransport12 {
public:
    struct Staged {
        ID3D12Resource* motion{};
        ID3D12Resource* depth{};
        ID3D12GraphicsCommandList* list{};
        bool depth_unshareable{};
    };

    GuideTransport12() = default;
    GuideTransport12(const GuideTransport12&) = delete;
    GuideTransport12& operator=(const GuideTransport12&) = delete;

    ~GuideTransport12() {
        // The game's list may still hold a copy into these textures, and a
        // snapshot copy may still read them: keep them past both.
        if (submitted_ != 0 && read_ && read_->GetCompletedValue() < submitted_ &&
            event_ != nullptr && SUCCEEDED(read_->SetEventOnCompletion(submitted_, event_))) {
            WaitForSingleObject(event_, 2000);
        }
        bury(std::move(slots_));
        if (event_ != nullptr) CloseHandle(event_);
    }

    [[nodiscard]] HRESULT initialize(
        ID3D12Device* game, ID3D12Device* device, ID3D12CommandQueue* queue) noexcept {
        try {
            if (game == nullptr || device == nullptr || queue == nullptr) return E_INVALIDARG;
            game_ = game;
            device_ = device;
            queue_ = queue;
            HRESULT result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(read_.GetAddressOf()));
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

    // In the game's list, before its DLSS: the copies out of the game's
    // resources, which DLSS reads in the given states; and an open list on
    // the layer's queue for the registry's snapshot copies. finish() closes it.
    [[nodiscard]] HRESULT stage(
        ID3D12GraphicsCommandList* game_list,
        ID3D12Resource* motion,
        D3D12_RESOURCE_STATES motion_state,
        ID3D12Resource* depth,
        D3D12_RESOURCE_STATES depth_state,
        Staged* staged) noexcept {
        try {
            *staged = {};
            std::scoped_lock lock(mutex_);
            Slot& slot = slots_[next_];
            last_slot_ = next_;
            // Its previous snapshot copy never reached a release: submit it.
            if (slot.pending) submit(slot);
            if (slot.read_done != 0 && read_->GetCompletedValue() < slot.read_done) {
                const HRESULT waited = read_->SetEventOnCompletion(slot.read_done, event_);
                if (FAILED(waited)) return waited;
                if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0) {
                    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                }
            }
            HRESULT result = prepare(slot.motion, motion, false);
            if (FAILED(result)) return result;
            bool depth_ready = false;
            if (depth != nullptr) {
                const HRESULT depth_result = prepare(slot.depth, depth, true);
                depth_ready = SUCCEEDED(depth_result);
                staged->depth_unshareable = depth_result == DXGI_ERROR_UNSUPPORTED;
            }
            copy(game_list, slot.motion, motion, motion_state);
            if (depth_ready) copy(game_list, slot.depth, depth, depth_state);
            result = slot.allocator->Reset();
            if (FAILED(result)) return result;
            result = slot.list->Reset(slot.allocator.Get(), nullptr);
            if (FAILED(result)) return result;
            recording_ = true;
            staged->motion = slot.motion.layer.Get();
            staged->depth = depth_ready ? slot.depth.layer.Get() : nullptr;
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
        poll_motion_means();
        poll_probes();
        for (Slot& slot : slots_) {
            if (slot.pending) submit(slot);
        }
    }

    // V524: an evaluation's place in its stream, and whether its slot's atlas
    // holds the patches of its finished output (alternating-eye anchor).
    struct AnchorSample {
        bool valid{};
        std::uint64_t stream{};
        std::uint64_t epoch{};
        std::uint64_t serial{};
        std::uint64_t publication{};
        DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    // The evaluation just staged was published at this place in its stream.
    void note_publication(std::uint64_t stream, const DlssStreamPosition& position) noexcept {
        std::scoped_lock lock(mutex_);
        Slot& slot = slots_[last_slot_];
        slot.sample = {};
        slot.sample.stream = stream;
        slot.sample.epoch = position.epoch;
        slot.sample.serial = position.serial;
        slot.sample.publication = position.publication;
    }

    // In the game's list after its DLSS: 16 patches of the finished output
    // into the slot's atlas. DLSS writes its output as an unordered-access
    // resource, the state the game leaves it in for the call.
    [[nodiscard]] HRESULT record_output_patches(
        ID3D12GraphicsCommandList* game_list, ID3D12Resource* output) noexcept {
        try {
            std::scoped_lock lock(mutex_);
            Slot& slot = slots_[last_slot_];
            const D3D12_RESOURCE_DESC description = output->GetDesc();
            const auto width = static_cast<std::uint32_t>(description.Width);
            const std::uint32_t height = description.Height;
            if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                description.SampleDesc.Count != 1 || anchor_texel_bytes(description.Format) == 0 ||
                width < 2U * kAnchorPatch || height < kAnchorPatch) {
                return DXGI_ERROR_UNSUPPORTED;
            }
            const HRESULT result = prepare_atlas(slot.atlas, description.Format);
            if (FAILED(result)) return result;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = output;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            game_list->ResourceBarrier(1, &barrier);
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                const auto [x, y] = anchor_patch_origin(patch, width, height);
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = output;
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = slot.atlas.game.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                const D3D12_BOX box{x, y, 0, x + kAnchorPatch, y + kAnchorPatch, 1};
                game_list->CopyTextureRegion(&to, (patch % 4U) * kAnchorPatch,
                    (patch / 4U) * kAnchorPatch, 0, &from, &box);
            }
            // V524, record 851: the same patches into the four-deep history,
            // by serial, once its last read has run.
            const std::size_t entry = slot.sample.serial & 3U;
            bool history_free = history_read_done_[entry] == 0 ||
                read_->GetCompletedValue() >= history_read_done_[entry];
            if (!history_free &&
                SUCCEEDED(read_->SetEventOnCompletion(history_read_done_[entry], event_))) {
                history_free = WaitForSingleObject(event_, 100) == WAIT_OBJECT_0;
            }
            if (history_free && SUCCEEDED(prepare_atlas(history_atlas_[entry], description.Format))) {
                for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                    const auto [x, y] = anchor_patch_origin(patch, width, height);
                    D3D12_TEXTURE_COPY_LOCATION from{};
                    from.pResource = output;
                    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION to{};
                    to.pResource = history_atlas_[entry].game.Get();
                    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    const D3D12_BOX box{x, y, 0, x + kAnchorPatch, y + kAnchorPatch, 1};
                    game_list->CopyTextureRegion(&to, (patch % 4U) * kAnchorPatch,
                        (patch / 4U) * kAnchorPatch, 0, &from, &box);
                }
            }
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            game_list->ResourceBarrier(1, &barrier);
            slot.sample.valid = true;
            slot.sample.format = description.Format;
            slot.sample.width = width;
            slot.sample.height = height;
            if (history_free) history_samples_[entry] = slot.sample;
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    // Both slots' samples, older first, and their slot indices.
    void samples(std::array<AnchorSample, 2>* out, std::array<std::uint32_t, 2>* indices) noexcept {
        std::scoped_lock lock(mutex_);
        const bool swapped = slots_[0].sample.serial > slots_[1].sample.serial;
        (*out)[0] = slots_[swapped ? 1 : 0].sample;
        (*out)[1] = slots_[swapped ? 0 : 1].sample;
        (*indices)[0] = swapped ? 1U : 0U;
        (*indices)[1] = swapped ? 0U : 1U;
    }

    // In a list for the layer's queue: a slot's atlas into a readback buffer.
    void read_atlas(std::uint32_t index, ID3D12GraphicsCommandList* list,
        ID3D12Resource* readback, std::uint64_t offset, std::uint64_t pitch) noexcept {
        std::scoped_lock lock(mutex_);
        const Slot& slot = slots_[index & 1U];
        if (!slot.atlas.layer) return;
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = slot.atlas.layer.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = readback;
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Offset = offset;
        to.PlacedFootprint.Footprint = {slot.atlas.source.Format, kAnchorAtlas, kAnchorAtlas, 1,
            static_cast<UINT>(pitch)};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }

    // V524, alternating eyes. R.E.A.L. VR's vectors span the game's last
    // frame, which rendered the other eye, while an eye's own frames are two
    // evaluations apart; its frames are not evenly spaced, and the vectors'
    // size follows (one eye's about three times the other's while turning).
    // Neither the recording times nor a frame-time delta tell the spans -
    // the game gives DLSS none, and the evaluation recorded 4 ms after the
    // previous one carries the larger motion - so they are measured from the
    // vectors: each serial parity's mean motion (record 847, a running
    // average while there is motion), and an eye's vectors stretched by
    // (its own + the other's) / its own. Nominal 2 until both are measured.
    struct Stretch {
        float factor{2.0F};
        float own{};
        float other{};
        std::uint32_t source{};
    };

    [[nodiscard]] Stretch motion_stretch(std::uint64_t serial, std::uint64_t epoch) noexcept {
        std::scoped_lock lock(mutex_);
        Stretch stretch;
        if (epoch != motion_epoch_) return stretch;
        const std::size_t own = serial & 1U;
        if (!(parity_motion_[own] > 0.0) || !(parity_motion_[own ^ 1U] > 0.0)) return stretch;
        stretch.own = static_cast<float>(parity_motion_[own]);
        stretch.other = static_cast<float>(parity_motion_[own ^ 1U]);
        stretch.factor = std::clamp(
            static_cast<float>((parity_motion_[own] + parity_motion_[own ^ 1U]) / parity_motion_[own]),
            1.0F, 6.0F);
        stretch.source = 1;
        return stretch;
    }

    // V524, the alignment probe (record 850): in the game's list before its
    // DLSS, 16 regions of its colour into the slot's colour atlas; the
    // vectors and depth are already copied whole. `colour_base` is the
    // colour's input rectangle origin, `motion_base` and `depth_base` the
    // vectors' and depth's, `extent` the render size they share.
    [[nodiscard]] HRESULT probe_colour(ID3D12GraphicsCommandList* game_list, ID3D12Resource* colour,
        D3D12_RESOURCE_STATES colour_state, std::array<std::uint32_t, 2> colour_base,
        std::array<std::uint32_t, 2> motion_base, std::array<std::uint32_t, 2> depth_base,
        std::array<std::uint32_t, 2> extent) noexcept {
        try {
            std::scoped_lock lock(mutex_);
            Slot& slot = slots_[last_slot_];
            slot.probe = {};
            const D3D12_RESOURCE_DESC description = colour->GetDesc();
            if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                description.SampleDesc.Count != 1 || anchor_texel_bytes(description.Format) == 0 ||
                extent[0] < kProbeRegion || extent[1] < kProbeRegion ||
                colour_base[0] + extent[0] > description.Width ||
                colour_base[1] + extent[1] > description.Height) {
                return DXGI_ERROR_UNSUPPORTED;
            }
            const HRESULT result = prepare_atlas(slot.probe_colour, description.Format, kProbeAtlas);
            if (FAILED(result)) return result;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = colour;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = colour_state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            if (colour_state != D3D12_RESOURCE_STATE_COPY_SOURCE) game_list->ResourceBarrier(1, &barrier);
            for (std::uint32_t region = 0; region < kProbeRegions; ++region) {
                const auto [x, y] = probe_region_origin(region, extent[0], extent[1]);
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = colour;
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = slot.probe_colour.game.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                const UINT left = colour_base[0] + x;
                const UINT top = colour_base[1] + y;
                const D3D12_BOX box{left, top, 0, left + kProbeRegion, top + kProbeRegion, 1};
                game_list->CopyTextureRegion(&to, (region % 4U) * kProbeRegion,
                    (region / 4U) * kProbeRegion, 0, &from, &box);
            }
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            if (colour_state != D3D12_RESOURCE_STATE_COPY_SOURCE) game_list->ResourceBarrier(1, &barrier);
            slot.probe.pending = true;
            slot.probe.motion_base = motion_base;
            slot.probe.depth_base = depth_base;
            slot.probe.extent = extent;
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    // V524, record 851: the last four evaluations' output patches.
    void history(std::array<AnchorSample, 4>* out) noexcept {
        std::scoped_lock lock(mutex_);
        *out = history_samples_;
    }

    void read_history(std::uint32_t index, ID3D12GraphicsCommandList* list,
        ID3D12Resource* readback, std::uint64_t offset, std::uint64_t pitch) noexcept {
        std::scoped_lock lock(mutex_);
        const Shared& atlas = history_atlas_[index & 3U];
        if (!atlas.layer) return;
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = atlas.layer.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = readback;
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Offset = offset;
        to.PlacedFootprint.Footprint = {atlas.source.Format, kAnchorAtlas, kAnchorAtlas, 1,
            static_cast<UINT>(pitch)};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }

    // After a list that read the history has gone to the queue: no entry is
    // written again before it has run.
    void protect_history() noexcept {
        std::scoped_lock lock(mutex_);
        if (SUCCEEDED(queue_->Signal(read_.Get(), ++submitted_))) {
            for (auto& done : history_read_done_) done = std::max(done, submitted_);
        }
    }

    // After a list that read the atlases has gone to the queue: neither slot
    // is written again before it has run.
    void protect_slots() noexcept {
        std::scoped_lock lock(mutex_);
        if (SUCCEEDED(queue_->Signal(read_.Get(), ++submitted_))) {
            for (Slot& slot : slots_) slot.read_done = std::max(slot.read_done, submitted_);
        }
    }

private:
    struct Shared {
        ComPtr<ID3D12Resource> layer;
        ComPtr<ID3D12Resource> game;
        D3D12_RESOURCE_DESC source{};
    };
    struct Slot {
        Shared motion;
        Shared depth;
        // V524: the finished output's patches (alternating-eye anchor).
        Shared atlas;
        AnchorSample sample;
        // V524: the alignment probe's colour regions, and where the same
        // regions lie on the vectors and depth (probe_colour).
        Shared probe_colour;
        struct {
            bool pending{};
            std::array<std::uint32_t, 2> motion_base{};
            std::array<std::uint32_t, 2> depth_base{};
            std::array<std::uint32_t, 2> extent{};
        } probe;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::uint64_t read_done{};
        bool pending{};
    };
    // V524: record 847, the mean of an evaluation's vectors over 16 patches,
    // read back behind its snapshot copy.
    struct MotionMean {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> readback;
        std::uint64_t value{};
        std::uint64_t publication{};
        std::uint64_t serial{};
        std::uint64_t epoch{};
        DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
        bool pending{};
    };
    // Below this mean motion (vectors' units: a hundredth of a texel at
    // R.E.A.L. VR's 2240-wide vectors) the head is still and the averages
    // stay as they were.
    static constexpr double kStillMotion = 5.0e-6;
    static constexpr std::uint64_t kMotionPatchPitch = 256;
    static constexpr std::uint64_t kMotionPatchBytes = kMotionPatchPitch * kAnchorPatch;
    static std::atomic<std::uint32_t>& motion_means_queued() noexcept {
        static std::atomic<std::uint32_t> value{0};
        return value;
    }

    // V524, the alignment probe's readbacks (probe_colour, record 850).
    struct ProbeRead {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> readback;
        std::uint64_t value{};
        std::uint64_t publication{};
        std::uint64_t serial{};
        DXGI_FORMAT colour_format{DXGI_FORMAT_UNKNOWN};
        DXGI_FORMAT motion_format{DXGI_FORMAT_UNKNOWN};
        DXGI_FORMAT depth_format{DXGI_FORMAT_UNKNOWN};
        std::uint64_t colour_pitch{};
        std::uint64_t motion_offset{};
        std::uint64_t motion_pitch{};
        std::uint64_t depth_offset{};
        std::uint64_t depth_pitch{};
        bool has_depth{};
        bool pending{};
    };
    static constexpr std::uint64_t kProbeReadbackBytes = 2U * 1024U * 1024U;

    // A list reading the slot's colour atlas and the same regions of its
    // vectors and depth back, behind its snapshot copy; null when none is
    // probed or free.
    ProbeRead* queue_probe(Slot& slot) noexcept {
        try {
            if (!slot.probe.pending || !slot.probe_colour.layer || !slot.motion.layer) return nullptr;
            slot.probe.pending = false;
            const DXGI_FORMAT motion_format = slot.motion.source.Format;
            if (motion_format != DXGI_FORMAT_R16G16_FLOAT && motion_format != DXGI_FORMAT_R32G32_FLOAT) {
                return nullptr;
            }
            ProbeRead* free = nullptr;
            for (ProbeRead& candidate : probe_reads_) {
                if (!candidate.pending) free = &candidate;
            }
            if (free == nullptr) return nullptr;
            if (!free->readback) {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC buffer{};
                buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                buffer.Width = kProbeReadbackBytes;
                buffer.Height = 1;
                buffer.DepthOrArraySize = 1;
                buffer.MipLevels = 1;
                buffer.SampleDesc.Count = 1;
                buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(free->readback.GetAddressOf()))) ||
                    FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(free->allocator.GetAddressOf()))) ||
                    FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        free->allocator.Get(), nullptr, IID_PPV_ARGS(free->list.GetAddressOf())))) {
                    free->readback.Reset();
                    return nullptr;
                }
            } else if (FAILED(free->allocator->Reset()) ||
                       FAILED(free->list->Reset(free->allocator.Get(), nullptr))) {
                return nullptr;
            }
            free->colour_format = slot.probe_colour.source.Format;
            free->colour_pitch = align_up(static_cast<std::uint64_t>(kProbeAtlas) *
                anchor_texel_bytes(free->colour_format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            const std::uint64_t colour_size =
                align_up(free->colour_pitch * kProbeAtlas, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            free->motion_format = motion_format;
            free->motion_pitch = align_up(static_cast<std::uint64_t>(kProbeRegion) *
                (motion_format == DXGI_FORMAT_R16G16_FLOAT ? 4U : 8U), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            free->motion_offset = colour_size;
            const std::uint64_t motion_region =
                align_up(free->motion_pitch * kProbeRegion, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            free->depth_format = slot.depth.layer ? slot.depth.source.Format : DXGI_FORMAT_UNKNOWN;
            free->has_depth = slot.depth.layer && probe_depth_bytes(free->depth_format) != 0;
            if (free->has_depth) {
                // The shared copy is the depth's typeless family.
                free->depth_format = shareable_format(free->depth_format);
                if (probe_depth_bytes(free->depth_format) == 0) free->has_depth = false;
            }
            free->depth_pitch = align_up(static_cast<std::uint64_t>(kProbeRegion) *
                std::max(1U, probe_depth_bytes(free->depth_format)), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            free->depth_offset = free->motion_offset + kProbeRegions * motion_region;
            const std::uint64_t depth_region =
                align_up(free->depth_pitch * kProbeRegion, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            if (free->depth_offset + kProbeRegions * depth_region > kProbeReadbackBytes) return nullptr;
            const auto read_region = [&](ID3D12Resource* source, DXGI_FORMAT format, UINT left,
                                         UINT top, std::uint64_t offset, std::uint64_t pitch) {
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = source;
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = free->readback.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = offset;
                to.PlacedFootprint.Footprint = {format, kProbeRegion, kProbeRegion, 1,
                    static_cast<UINT>(pitch)};
                const D3D12_BOX box{left, top, 0, left + kProbeRegion, top + kProbeRegion, 1};
                free->list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            };
            {
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = slot.probe_colour.layer.Get();
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = free->readback.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Footprint = {free->colour_format, kProbeAtlas, kProbeAtlas, 1,
                    static_cast<UINT>(free->colour_pitch)};
                free->list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            }
            const auto motion_width = static_cast<std::uint32_t>(slot.motion.source.Width);
            const std::uint32_t motion_height = slot.motion.source.Height;
            const auto depth_width = static_cast<std::uint32_t>(slot.depth.source.Width);
            const std::uint32_t depth_height = slot.depth.source.Height;
            for (std::uint32_t region = 0; region < kProbeRegions; ++region) {
                const auto [x, y] = probe_region_origin(region, slot.probe.extent[0], slot.probe.extent[1]);
                const UINT mx = slot.probe.motion_base[0] + x, my = slot.probe.motion_base[1] + y;
                if (mx + kProbeRegion > motion_width || my + kProbeRegion > motion_height) return nullptr;
                read_region(slot.motion.layer.Get(), motion_format, mx, my,
                    free->motion_offset + region * motion_region, free->motion_pitch);
                if (free->has_depth) {
                    const UINT dx = slot.probe.depth_base[0] + x, dy = slot.probe.depth_base[1] + y;
                    if (dx + kProbeRegion > depth_width || dy + kProbeRegion > depth_height) {
                        free->has_depth = false;
                        continue;
                    }
                    read_region(slot.depth.layer.Get(), free->depth_format, dx, dy,
                        free->depth_offset + region * depth_region, free->depth_pitch);
                }
            }
            if (FAILED(free->list->Close())) return nullptr;
            free->pending = true;
            free->value = ~0ULL;
            free->publication = slot.sample.publication;
            free->serial = slot.sample.serial;
            return free;
        } catch (...) {
            return nullptr;
        }
    }

    // Probes the GPU has read back: their data to a thread of their own.
    void poll_probes() noexcept {
        for (ProbeRead& read : probe_reads_) {
            if (!read.pending || read.value == ~0ULL || read_->GetCompletedValue() < read.value) continue;
            read.pending = false;
            std::byte* mapped = nullptr;
            const D3D12_RANGE range{0, static_cast<SIZE_T>(kProbeReadbackBytes)};
            if (FAILED(read.readback->Map(0, &range, reinterpret_cast<void**>(&mapped))) ||
                mapped == nullptr) {
                continue;
            }
            try {
                ProbeData data;
                data.publication = read.publication;
                data.serial = read.serial;
                data.has_depth = read.has_depth;
                const std::size_t area = static_cast<std::size_t>(kProbeRegion) * kProbeRegion;
                data.colour.resize(kProbeRegions * area);
                data.motion_x.resize(kProbeRegions * area);
                data.motion_y.resize(kProbeRegions * area);
                if (data.has_depth) data.depth.resize(kProbeRegions * area);
                const std::uint32_t colour_bytes = anchor_texel_bytes(read.colour_format);
                const std::uint64_t motion_region =
                    align_up(read.motion_pitch * kProbeRegion, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
                const std::uint64_t depth_region =
                    align_up(read.depth_pitch * kProbeRegion, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
                const std::uint32_t depth_bytes = probe_depth_bytes(read.depth_format);
                for (std::uint32_t region = 0; region < kProbeRegions; ++region) {
                    for (std::uint32_t y = 0; y < kProbeRegion; ++y) {
                        for (std::uint32_t x = 0; x < kProbeRegion; ++x) {
                            const std::size_t at = region * area + y * kProbeRegion + x;
                            const std::uint64_t cx = (region % 4U) * kProbeRegion + x;
                            const std::uint64_t cy = (region / 4U) * kProbeRegion + y;
                            data.colour[at] = anchor_luminance(
                                mapped + cy * read.colour_pitch + cx * colour_bytes, read.colour_format);
                            const std::byte* motion = mapped + read.motion_offset +
                                region * motion_region + y * read.motion_pitch;
                            if (read.motion_format == DXGI_FORMAT_R16G16_FLOAT) {
                                std::array<std::uint16_t, 2> value{};
                                std::memcpy(value.data(), motion + x * 4U, sizeof(value));
                                data.motion_x[at] = half_to_float(value[0]);
                                data.motion_y[at] = half_to_float(value[1]);
                            } else {
                                std::array<float, 2> value{};
                                std::memcpy(value.data(), motion + x * 8U, sizeof(value));
                                data.motion_x[at] = value[0];
                                data.motion_y[at] = value[1];
                            }
                            if (data.has_depth) {
                                data.depth[at] = probe_value(mapped + read.depth_offset +
                                    region * depth_region + y * read.depth_pitch + x * depth_bytes,
                                    read.depth_format);
                            }
                        }
                    }
                }
                read.readback->Unmap(0, nullptr);
                std::thread([data = std::move(data)] { analyse_probe(data); }).detach();
            } catch (...) {
                read.readback->Unmap(0, nullptr);
            }
        }
    }

    // Textures the game's queue may not have finished writing: kept two
    // seconds, as the registry keeps a retired snapshot.
    static void bury(std::array<Slot, 2>&& slots) noexcept {
        try {
            struct Grave {
                ULONGLONG at;
                std::array<Slot, 2> slots;
            };
            static std::mutex mutex;
            static auto* graves = new std::vector<Grave>;
            std::scoped_lock lock(mutex);
            const ULONGLONG now = GetTickCount64();
            graves->erase(std::remove_if(graves->begin(), graves->end(),
                [now](const Grave& grave) { return now - grave.at > 2000; }), graves->end());
            graves->push_back({now, std::move(slots)});
        } catch (...) {
        }
    }

    void submit(Slot& slot) noexcept {
        slot.pending = false;
        MotionMean* mean = queue_motion_mean(slot);
        ProbeRead* probe = queue_probe(slot);
        std::array<ID3D12CommandList*, 3> lists{slot.list.Get()};
        UINT count = 1;
        if (mean != nullptr) lists[count++] = mean->list.Get();
        if (probe != nullptr) lists[count++] = probe->list.Get();
        queue_->ExecuteCommandLists(count, lists.data());
        if (SUCCEEDED(queue_->Signal(read_.Get(), ++submitted_))) {
            slot.read_done = submitted_;
            if (mean != nullptr) mean->value = submitted_;
            if (probe != nullptr) probe->value = submitted_;
        } else {
            if (mean != nullptr) mean->pending = false;
            if (probe != nullptr) probe->pending = false;
        }
    }

    // A list copying 16 patches of the slot's vectors to a readback buffer,
    // for the first 512 evaluations; null when none is due or free.
    MotionMean* queue_motion_mean(Slot& slot) noexcept {
        try {
            const DXGI_FORMAT format = slot.motion.source.Format;
            if (!slot.motion.layer || (format != DXGI_FORMAT_R16G16_FLOAT &&
                    format != DXGI_FORMAT_R32G32_FLOAT) ||
                slot.motion.source.Width < kAnchorPatch ||
                slot.motion.source.Height < kAnchorPatch ||
                (!g_anchor_enabled.load(std::memory_order_acquire) &&
                    motion_means_queued().load(std::memory_order_relaxed) >= 512)) {
                return nullptr;
            }
            MotionMean* free = nullptr;
            for (MotionMean& candidate : motion_means_) {
                if (!candidate.pending) free = &candidate;
            }
            if (free == nullptr) return nullptr;
            if (!free->readback) {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC buffer{};
                buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                buffer.Width = kMotionPatchBytes * kAnchorPatches;
                buffer.Height = 1;
                buffer.DepthOrArraySize = 1;
                buffer.MipLevels = 1;
                buffer.SampleDesc.Count = 1;
                buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(free->readback.GetAddressOf()))) ||
                    FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(free->allocator.GetAddressOf()))) ||
                    FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        free->allocator.Get(), nullptr, IID_PPV_ARGS(free->list.GetAddressOf())))) {
                    free->readback.Reset();
                    return nullptr;
                }
            } else if (FAILED(free->allocator->Reset()) ||
                       FAILED(free->list->Reset(free->allocator.Get(), nullptr))) {
                return nullptr;
            }
            const auto width = static_cast<std::uint32_t>(slot.motion.source.Width);
            const std::uint32_t height = slot.motion.source.Height;
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                const auto [x, y] = anchor_patch_origin(patch, width, height);
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = slot.motion.layer.Get();
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = free->readback.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = patch * kMotionPatchBytes;
                to.PlacedFootprint.Footprint = {format, kAnchorPatch, kAnchorPatch, 1,
                    static_cast<UINT>(kMotionPatchPitch)};
                const D3D12_BOX box{x, y, 0, x + kAnchorPatch, y + kAnchorPatch, 1};
                free->list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            }
            if (FAILED(free->list->Close())) return nullptr;
            free->pending = true;
            free->value = ~0ULL;
            free->publication = slot.sample.publication;
            free->serial = slot.sample.serial;
            free->epoch = slot.sample.epoch;
            free->format = format;
            motion_means_queued().fetch_add(1, std::memory_order_relaxed);
            return free;
        } catch (...) {
            return nullptr;
        }
    }

    // The means the GPU has read back since, as record 847.
    void poll_motion_means() noexcept {
        for (MotionMean& mean : motion_means_) {
            if (!mean.pending || mean.value == ~0ULL || read_->GetCompletedValue() < mean.value) {
                continue;
            }
            mean.pending = false;
            std::byte* mapped = nullptr;
            const D3D12_RANGE range{0, static_cast<SIZE_T>(kMotionPatchBytes * kAnchorPatches)};
            if (FAILED(mean.readback->Map(0, &range, reinterpret_cast<void**>(&mapped))) ||
                mapped == nullptr) {
                continue;
            }
            double sum_x = 0.0, sum_y = 0.0, abs_x = 0.0, abs_y = 0.0;
            std::uint64_t count = 0;
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                for (std::uint32_t row = 0; row < kAnchorPatch; ++row) {
                    const std::byte* line = mapped + patch * kMotionPatchBytes + row * kMotionPatchPitch;
                    for (std::uint32_t column = 0; column < kAnchorPatch; ++column) {
                        float x = 0.0F, y = 0.0F;
                        if (mean.format == DXGI_FORMAT_R16G16_FLOAT) {
                            std::array<std::uint16_t, 2> value{};
                            std::memcpy(value.data(), line + column * 4U, sizeof(value));
                            x = half_to_float(value[0]);
                            y = half_to_float(value[1]);
                        } else {
                            std::array<float, 2> value{};
                            std::memcpy(value.data(), line + column * 8U, sizeof(value));
                            x = value[0];
                            y = value[1];
                        }
                        if (!std::isfinite(x) || !std::isfinite(y)) continue;
                        sum_x += x;
                        sum_y += y;
                        abs_x += std::fabs(x);
                        abs_y += std::fabs(y);
                        ++count;
                    }
                }
            }
            mean.readback->Unmap(0, nullptr);
            const double n = count != 0 ? static_cast<double>(count) : 1.0;
            // The parity's running average for the stretch; a new epoch
            // starts the averages again (its serials start again).
            if (mean.epoch != motion_epoch_) {
                motion_epoch_ = mean.epoch;
                parity_motion_ = {};
            }
            const double motion = (abs_x + abs_y) / n;
            if (std::isfinite(motion) && motion > kStillMotion) {
                double& average = parity_motion_[mean.serial & 1U];
                average = average > 0.0 ? average + 0.2 * (motion - average) : motion;
            }
            static std::atomic<std::uint32_t> means_recorded{0};
            if (means_recorded.fetch_add(1, std::memory_order_relaxed) < 512) {
                record(kRecordMotionMean, mean.publication,
                    (float_bits(static_cast<float>(sum_x / n)) << 32U) |
                        float_bits(static_cast<float>(sum_y / n)),
                    (float_bits(static_cast<float>(abs_x / n)) << 32U) |
                        float_bits(static_cast<float>(abs_y / n)));
            }
        }
    }

    // The slot's atlas: kAnchorAtlas square of the output's format, made on
    // the layer's device and opened on the game's.
    [[nodiscard]] HRESULT prepare_atlas(
        Shared& shared, DXGI_FORMAT format, std::uint32_t size = kAnchorAtlas) noexcept {
        if (shared.layer && shared.source.Format == format && shared.source.Width == size) return S_OK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = size;
        desc.Height = size;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Shared replacement;
        HRESULT result = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(replacement.layer.GetAddressOf()));
        if (FAILED(result)) return result;
        HANDLE handle = nullptr;
        result = device_->CreateSharedHandle(replacement.layer.Get(), nullptr, GENERIC_ALL,
            nullptr, &handle);
        if (FAILED(result)) return result;
        result = game_->OpenSharedHandle(handle, IID_PPV_ARGS(replacement.game.GetAddressOf()));
        CloseHandle(handle);
        if (FAILED(result)) return result;
        replacement.source = desc;
        if (shared.layer) {
            std::array<Slot, 2> old_slots{};
            old_slots[0].atlas = std::move(shared);
            bury(std::move(old_slots));
        }
        shared = std::move(replacement);
        return S_OK;
    }

    // A single-mip texture of the source's size, format family and slices,
    // made on the layer's device and opened on the game's.
    [[nodiscard]] HRESULT prepare(Shared& shared, ID3D12Resource* source, bool depth) noexcept {
        const D3D12_RESOURCE_DESC description = source->GetDesc();
        if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            description.SampleDesc.Count != 1) {
            return DXGI_ERROR_UNSUPPORTED;
        }
        const auto& old = shared.source;
        if (shared.layer && old.Width == description.Width &&
            old.Height == description.Height && old.Format == description.Format &&
            old.DepthOrArraySize == description.DepthOrArraySize &&
            old.MipLevels == description.MipLevels) {
            return S_OK;
        }
        const DXGI_FORMAT format = depth ? shareable_format(description.Format) : description.Format;
        if (format == DXGI_FORMAT_UNKNOWN) return DXGI_ERROR_UNSUPPORTED;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = description.Width;
        desc.Height = description.Height;
        desc.DepthOrArraySize = description.DepthOrArraySize;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        // Simultaneous access: COMMON at both ends of every list, so neither
        // device tracks the other's state.
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Shared replacement;
        HRESULT result = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(replacement.layer.GetAddressOf()));
        if (FAILED(result)) return result;
        HANDLE handle = nullptr;
        result = device_->CreateSharedHandle(replacement.layer.Get(), nullptr, GENERIC_ALL,
            nullptr, &handle);
        if (FAILED(result)) return result;
        result = game_->OpenSharedHandle(handle, IID_PPV_ARGS(replacement.game.GetAddressOf()));
        CloseHandle(handle);
        if (FAILED(result)) return result;
        replacement.source = description;
        // The pair it replaces may still be in a list the game has not run.
        if (shared.layer) {
            std::array<Slot, 2> old_slots{};
            old_slots[0].motion = std::move(shared);
            bury(std::move(old_slots));
        }
        shared = std::move(replacement);
        return S_OK;
    }

    // Mip 0 of every slice, out of the game's resource in its DLSS state and
    // back to it. The shared texture is promoted from COMMON by the copy.
    static void copy(ID3D12GraphicsCommandList* list, const Shared& shared,
        ID3D12Resource* source, D3D12_RESOURCE_STATES state) noexcept {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = source;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) list->ResourceBarrier(1, &barrier);
        for (UINT slice = 0; slice < shared.source.DepthOrArraySize; ++slice) {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = source;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.SubresourceIndex = slice * shared.source.MipLevels;
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = shared.game.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.SubresourceIndex = slice;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) list->ResourceBarrier(1, &barrier);
    }

    ComPtr<ID3D12Device> game_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> read_;
    std::array<Slot, 2> slots_{};
    std::array<MotionMean, 4> motion_means_{};
    std::array<ProbeRead, 2> probe_reads_{};
    // V524, record 851: the last four evaluations' output patches, by serial.
    std::array<Shared, 4> history_atlas_{};
    std::array<AnchorSample, 4> history_samples_{};
    std::array<std::uint64_t, 4> history_read_done_{};
    // V524: each serial parity's mean motion in motion_epoch_ (motion_stretch).
    std::array<double, 2> parity_motion_{};
    std::uint64_t motion_epoch_{};
    std::uint32_t next_{};
    std::uint32_t last_slot_{};
    std::uint64_t submitted_{};
    bool recording_{};
    HANDLE event_{};
    std::mutex mutex_;
};

// ------------------------------------------- alternating-eye anchor (V524)
// The evaluation thread's transport, between its staging and the end of the
// DLSS call, when its stream still needs an anchor: complete12 then copies
// the finished output's patches.
thread_local std::shared_ptr<GuideTransport12> t_patch_transport;

void clear_patch_transport() noexcept {
    t_patch_transport.reset();
}

void complete12(ID3D12GraphicsCommandList* list, const NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Result result) noexcept {
    try {
        const auto transport = std::move(t_patch_transport);
        t_patch_transport.reset();
        if (!transport || list == nullptr || !NVSDK_NGX_SUCCEED(result)) return;
        if (auto* output = get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Output)) {
            static_cast<void>(transport->record_output_patches(list, output));
        }
    } catch (...) {
    }
}

struct AnchorJob {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> readback;
    std::uint64_t value{};
    bool pending{};
    // What the queued comparison read: the two evaluations, older first,
    // the readback layout and the eye images' formats.
    std::array<GuideTransport12::AnchorSample, 2> samples{};
    std::array<DXGI_FORMAT, 2> eye_formats{};
    std::uint64_t atlas_pitch{};
    std::array<std::uint64_t, 2> atlas_offsets{};
    std::uint64_t patch_pitch_bytes[2]{};
    std::uint64_t eye_base{};
    std::uint64_t patch_size{};
    // The votes for the stream in its epoch.
    std::uint64_t stream{};
    std::uint64_t epoch{};
    std::uint64_t last_publication{};
    std::uint32_t vote_parity{};
    std::uint32_t votes{};
    std::uint64_t records{};
};

constexpr std::uint64_t kAnchorReadbackBytes = 2U * 2048U * kAnchorAtlas +
    2U * kAnchorPatches * 32U * 512U;

AnchorJob& anchor_job() noexcept {
    static auto* value = new AnchorJob;
    return *value;
}

std::mutex& anchor_mutex() noexcept {
    static auto* value = new std::mutex;
    return *value;
}

// The queued comparison has run: its votes, and the anchor once five agree.
void judge_anchor(AnchorJob& job) noexcept {
    std::array<std::array<double, 2>, 2> correlation{};
    std::array<std::array<std::uint32_t, 2>, 2> used{};
    std::byte* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(kAnchorReadbackBytes)};
    if (FAILED(job.readback->Map(0, &range, reinterpret_cast<void**>(&mapped))) || mapped == nullptr) {
        return;
    }
    std::vector<float> evaluation, eye_patch;
    for (std::uint32_t source = 0; source < 2; ++source) {
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            double total = 0.0;
            std::uint32_t count = 0;
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                read_anchor_patch(mapped + job.atlas_offsets[source], job.atlas_pitch,
                    (patch % 4U) * kAnchorPatch, (patch / 4U) * kAnchorPatch,
                    job.samples[source].format, &evaluation);
                read_anchor_patch(mapped + job.eye_base +
                        (static_cast<std::uint64_t>(eye) * kAnchorPatches + patch) * job.patch_size,
                    job.patch_pitch_bytes[eye], 0, 0, job.eye_formats[eye], &eye_patch);
                const double value = rank_correlation(evaluation, eye_patch);
                if (std::isfinite(value)) {
                    total += value;
                    ++count;
                }
            }
            correlation[source][eye] = count != 0 ? total / count : 0.0;
            used[source][eye] = count;
        }
    }
    job.readback->Unmap(0, nullptr);
    const std::uint32_t fewest =
        std::min({used[0][0], used[0][1], used[1][0], used[1][1]});
    const double older_left = correlation[0][0] + correlation[1][1];
    const double older_right = correlation[0][1] + correlation[1][0];
    std::uint32_t outcome = 0;
    std::uint32_t parity = 0;
    if (fewest < 4) {
        outcome = 1;
    } else if (std::fabs(older_left - older_right) < kAnchorMargin) {
        outcome = 2;
    } else {
        const auto& left = older_left > older_right ? job.samples[0] : job.samples[1];
        parity = static_cast<std::uint32_t>(left.serial & 1U);
        job.votes = job.votes != 0 && job.vote_parity == parity ? job.votes + 1 : 1;
        job.vote_parity = parity;
    }
    const bool anchored = outcome == 0 && job.votes >= kAnchorVotes;
    if (anchored) anchor_dlss_alternating_eyes(job.stream, job.epoch, job.vote_parity);
    if (job.records++ < 512 || anchored) {
        record(kRecordAnchor,
            (job.samples[0].publication << 32U) | (job.samples[1].publication & 0xFFFFFFFFULL),
            (float_bits(static_cast<float>(older_left)) << 32U) |
                float_bits(static_cast<float>(older_right)),
            outcome | (static_cast<std::uint64_t>(parity) << 8U) |
                (static_cast<std::uint64_t>(std::min(job.votes, 255U)) << 16U) |
                (anchored ? 1ULL << 24U : 0ULL) |
                (static_cast<std::uint64_t>(fewest) << 32U));
    }
}

// V524, alternating eyes: record 848, the stretch of an evaluation's
// vectors to its eye's own frame (GuideTransport12::motion_stretch), the
// first 512 and every 2000th after: a the publication, b its eye's mean
// motion (float bits) << 32 | the other eye's, c the factor (float bits) <<
// 32 | the source (1 measured, 0 not yet: the nominal 2). This build
// measures and records the factor but does not apply it: Cyberpunk 2077's
// R.E.A.L. VR may already hand DLSS each eye's own span (FixDLSS=1), and
// with the stretch both eye assignments smeared at walls.
constexpr std::int64_t kRecordSpanScale = 848;


// The measurement job (record 851), queued at a release and read at a
// later one.
struct MeasureJob {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> readback;
    std::uint64_t value{};
    bool pending{};
    std::array<GuideTransport12::AnchorSample, 4> samples{};  // newest first
    std::array<DXGI_FORMAT, 2> eye_formats{};
    std::uint64_t atlas_pitch{};
    std::uint64_t atlas_size{};
    std::array<std::uint64_t, 2> eye_pitch{};
    std::uint64_t eye_base{};
    std::uint64_t window_size{};
    std::uint64_t calls{};
    std::uint64_t queued{};
};

constexpr std::uint64_t kMeasureReadbackBytes = 4U * 2048U * kAnchorAtlas +
    2U * kAnchorPatches * kAnchorPatch * 2048U;

MeasureJob& measure_job() noexcept {
    static auto* value = new MeasureJob;
    return *value;
}

void read_measure(MeasureJob& job) noexcept {
    std::byte* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(kMeasureReadbackBytes)};
    if (FAILED(job.readback->Map(0, &range, reinterpret_cast<void**>(&mapped))) || mapped == nullptr) {
        return;
    }
    try {
        MeasureData data;
        data.publication = job.samples[0].publication;
        data.serial = job.samples[0].serial;
        const std::size_t patch_area = static_cast<std::size_t>(kAnchorPatch) * kAnchorPatch;
        const std::size_t window_area = static_cast<std::size_t>(kAnchorPatch) * kMeasureWindow;
        const std::uint32_t output_bytes = anchor_texel_bytes(job.samples[0].format);
        for (std::size_t j = 0; j < 4; ++j) {
            data.outputs[j].resize(kAnchorPatches * patch_area);
            const std::byte* base = mapped + j * job.atlas_size;
            for (std::uint32_t p = 0; p < kAnchorPatches; ++p) {
                for (std::uint32_t y = 0; y < kAnchorPatch; ++y) {
                    for (std::uint32_t x = 0; x < kAnchorPatch; ++x) {
                        const std::uint64_t ax = (p % 4U) * kAnchorPatch + x;
                        const std::uint64_t ay = (p / 4U) * kAnchorPatch + y;
                        data.outputs[j][p * patch_area + y * kAnchorPatch + x] = anchor_luminance(
                            base + ay * job.atlas_pitch + ax * output_bytes, job.samples[j].format);
                    }
                }
            }
        }
        for (std::size_t k = 0; k < 2; ++k) {
            data.eyes[k].resize(kAnchorPatches * window_area);
            const std::uint32_t bytes = anchor_texel_bytes(job.eye_formats[k]);
            for (std::uint32_t p = 0; p < kAnchorPatches; ++p) {
                const std::byte* base = mapped + job.eye_base + (k * kAnchorPatches + p) * job.window_size;
                for (std::uint32_t y = 0; y < kAnchorPatch; ++y) {
                    for (std::uint32_t x = 0; x < kMeasureWindow; ++x) {
                        data.eyes[k][p * window_area + y * kMeasureWindow + x] =
                            anchor_luminance(base + y * job.eye_pitch[k] + x * bytes, job.eye_formats[k]);
                    }
                }
            }
        }
        job.readback->Unmap(0, nullptr);
        std::thread([data = std::move(data)] { analyse_measure(data); }).detach();
    } catch (...) {
        job.readback->Unmap(0, nullptr);
    }
}

// At a release: read the last comparison if the GPU has run it, and queue
// the next when one is due and the last four evaluations are in the history.
void measure_eyes(const std::shared_ptr<GuideTransport12>& transport, ID3D12Device* device,
    ID3D12CommandQueue* queue, const std::array<NgxEyeImage, 2>& eyes,
    std::uint64_t released_at, std::uint64_t stream) noexcept {
    try {
        MeasureJob& job = measure_job();
        if (job.pending) {
            if (job.fence->GetCompletedValue() < job.value) return;
            job.pending = false;
            read_measure(job);
        }
        const std::uint64_t call = job.calls++;
        if (call % 4 != 0 || (job.queued >= 400 && call % 64 != 0)) return;
        std::array<GuideTransport12::AnchorSample, 4> history{};
        transport->history(&history);
        // Newest first, four consecutive serials of one epoch, all before the release.
        std::array<GuideTransport12::AnchorSample, 4> samples = history;
        std::sort(samples.begin(), samples.end(),
            [](const auto& a, const auto& b) { return a.serial > b.serial; });
        for (std::size_t j = 0; j < 4; ++j) {
            const auto& sample = samples[j];
            if (!sample.valid || sample.epoch != samples[0].epoch ||
                sample.serial + j != samples[0].serial || sample.publication > released_at ||
                sample.format != samples[0].format || sample.width != samples[0].width ||
                sample.height != samples[0].height) {
                return;
            }
        }
        std::array<std::uint64_t, 2> eye_pitch{};
        std::array<DXGI_FORMAT, 2> eye_formats{};
        std::array<std::pair<std::int32_t, std::int32_t>, 2> origins{};
        for (std::uint32_t k = 0; k < 2; ++k) {
            const NgxEyeImage& image = eyes[k];
            if (image.image == nullptr || image.x < 0 || image.y < 0) return;
            const D3D12_RESOURCE_DESC description = image.image->GetDesc();
            const std::uint32_t bytes = anchor_texel_bytes(description.Format);
            if (bytes == 0 || (description.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0) {
                return;
            }
            origins[k] = eye_patch_origin(stream, k, image);
            for (std::uint32_t p = 0; p < kAnchorPatches; ++p) {
                const auto [x, y] = anchor_patch_origin(p, samples[0].width, samples[0].height);
                if (!patch_on_eye(origins[k], x, y, kMeasureShift, image)) return;
            }
            eye_formats[k] = description.Format;
            eye_pitch[k] = align_up(static_cast<std::uint64_t>(kMeasureWindow) * bytes,
                D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        }
        if (!job.readback) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = kMeasureReadbackBytes;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(job.readback.GetAddressOf()))) ||
                FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(job.allocator.GetAddressOf()))) ||
                FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    job.allocator.Get(), nullptr, IID_PPV_ARGS(job.list.GetAddressOf()))) ||
                FAILED(job.list->Close()) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(job.fence.GetAddressOf())))) {
                job.readback.Reset();
                return;
            }
        }
        if (FAILED(job.allocator->Reset()) || FAILED(job.list->Reset(job.allocator.Get(), nullptr))) return;
        job.atlas_pitch = align_up(static_cast<std::uint64_t>(kAnchorAtlas) *
            anchor_texel_bytes(samples[0].format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        job.atlas_size = align_up(job.atlas_pitch * kAnchorAtlas, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        job.eye_base = 4U * job.atlas_size;
        job.window_size = align_up(std::max(eye_pitch[0], eye_pitch[1]) * kAnchorPatch,
            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (job.eye_base + 2U * kAnchorPatches * job.window_size > kMeasureReadbackBytes) return;
        for (std::uint32_t j = 0; j < 4; ++j) {
            transport->read_history(static_cast<std::uint32_t>(samples[j].serial & 3U), job.list.Get(),
                job.readback.Get(), j * job.atlas_size, job.atlas_pitch);
        }
        for (std::uint32_t k = 0; k < 2; ++k) {
            for (std::uint32_t p = 0; p < kAnchorPatches; ++p) {
                const auto [x, y] = anchor_patch_origin(p, samples[0].width, samples[0].height);
                const auto left = static_cast<UINT>(origins[k].first + static_cast<std::int32_t>(x) -
                    static_cast<std::int32_t>(kMeasureShift));
                const auto top = static_cast<UINT>(origins[k].second + static_cast<std::int32_t>(y));
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = eyes[k].image;
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = job.readback.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = job.eye_base + (k * kAnchorPatches + p) * job.window_size;
                to.PlacedFootprint.Footprint = {eye_formats[k], kMeasureWindow, kAnchorPatch, 1,
                    static_cast<UINT>(eye_pitch[k])};
                const D3D12_BOX box{left, top, 0, left + kMeasureWindow, top + kAnchorPatch, 1};
                job.list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            }
        }
        if (FAILED(job.list->Close())) return;
        ID3D12CommandList* lists[]{job.list.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (FAILED(queue->Signal(job.fence.Get(), ++job.value))) return;
        transport->protect_history();
        job.samples = samples;
        job.eye_formats = eye_formats;
        job.eye_pitch = eye_pitch;
        job.pending = true;
        ++job.queued;
    } catch (...) {
    }
}

void skip_bridged_d3d12(std::size_t reason, std::uint64_t detail = 0) noexcept {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    const std::uint64_t count = reason == 4 ? ++s.statistics.skipped_device_mismatch
                                            : ++s.statistics.d3d12_bridged_transport_failures;
    if (!s.skip_recorded[reason]) {
        s.skip_recorded[reason] = true;
        record(kRecordSkip, reason, count, detail);
    }
}

void capture12_bridged(
    ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters) noexcept {
    try {
        auto& s = state();
        if (!s.d3d11_active.load(std::memory_order_acquire) || list == nullptr ||
            handle == nullptr || parameters == nullptr) {
            return;
        }
        Feature feature;
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
            device = s.bridge_device;
            queue = s.bridge_queue;
            // V526: R.E.A.L. VR's per-eye handles are one alternating stream.
            if (g_anchor_enabled.load(std::memory_order_acquire)) {
                if (s.realvr_stream == 0) s.realvr_stream = feature.stream;
                feature.stream = s.realvr_stream;
            }
        }
        if (!device || !queue) return;
        ID3D12Resource* const output =
            get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Output);
        ID3D12Resource* const motion =
            get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_MotionVectors);
        ID3D12Resource* const depth = get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Depth);
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
        // The copies go in the list DLSS is recorded into: it has to be the
        // vectors' device's.
        ComPtr<ID3D12Device> game, list_device;
        if (FAILED(motion->GetDevice(IID_PPV_ARGS(game.GetAddressOf()))) ||
            FAILED(list->GetDevice(IID_PPV_ARGS(list_device.GetAddressOf()))) ||
            !same_d3d12_device_backing(game.Get(), list_device.Get())) {
            skip_bridged_d3d12(4, game.Get() == list_device.Get() ? 1U : 0U);
            return;
        }
        const D3D12_RESOURCE_DESC output_description = output->GetDesc();
        const D3D12_RESOURCE_DESC motion_description = motion->GetDesc();
        D3D12_RESOURCE_DESC depth_description{};
        if (depth != nullptr) depth_description = depth->GetDesc();

        std::shared_ptr<GuideTransport12> transport;
        ComPtr<ID3D12Resource> placeholder;
        bool first_evaluation = false;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found == s.features.end()) return;
            Feature& stored = found->second;
            first_evaluation = !stored.shape_recorded;
            stored.shape_recorded = true;
            if (!stored.transport12 && g_anchor_enabled.load(std::memory_order_acquire) &&
                s.realvr_transport) {
                stored.transport12 = s.realvr_transport;
            }
            if (!stored.transport12) {
                auto created = std::make_shared<GuideTransport12>();
                const HRESULT result = created->initialize(game.Get(), device.Get(), queue.Get());
                if (FAILED(result)) {
                    ++s.statistics.d3d12_bridged_transport_failures;
                    if (!s.skip_recorded[10]) {
                        s.skip_recorded[10] = true;
                        record(kRecordSkip, 10, s.statistics.d3d12_bridged_transport_failures,
                            static_cast<std::uint32_t>(result));
                    }
                    return;
                }
                stored.transport12 = std::move(created);
                if (g_anchor_enabled.load(std::memory_order_acquire) && !s.realvr_transport) {
                    s.realvr_transport = stored.transport12;
                }
            }
            if (!stored.output_placeholder ||
                stored.output_placeholder->GetDesc().Width != output_description.Width ||
                stored.output_placeholder->GetDesc().Height != output_description.Height) {
                D3D11_TEXTURE2D_DESC shape{};
                shape.Width = static_cast<UINT>(output_description.Width);
                shape.Height = output_description.Height;
                shape.ArraySize = output_description.DepthOrArraySize;
                stored.output_placeholder = output_placeholder(device.Get(), shape);
            }
            transport = stored.transport12;
            placeholder = stored.output_placeholder;
        }
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
        if (!placeholder) {
            skip_bridged_d3d12(10, static_cast<std::uint32_t>(E_OUTOFMEMORY));
            return;
        }
        DlssMotionVectorPublication publication{};
        Shape depth_shape{depth_description.Width, depth_description.Height};
        fill_publication(publication, feature, parameters,
            {output_description.Width, output_description.Height},
            {motion_description.Width, motion_description.Height},
            depth != nullptr ? &depth_shape : nullptr);
        // V524, alternating eyes: the stretch the stream's vectors measured
        // (motion_stretch), for the serial this evaluation is about to take -
        // recorded, not applied (kRecordSpanScale).
        GuideTransport12::Stretch span;
        if (g_anchor_enabled.load(std::memory_order_acquire)) {
            const DlssStreamPosition before_position =
                dlss_motion_vector_stream_position(feature.stream);
            const std::uint64_t serial =
                publication.reset || !before_position.known ? 1 : before_position.serial + 1;
            const std::uint64_t epoch = before_position.known
                ? before_position.epoch + (publication.reset ? 1U : 0U) : 1;
            span = transport->motion_stretch(serial, epoch);
        }
        // DLSS reads its inputs as shader resources; the game put them in that
        // state for it, as the native capture assumes.
        GuideTransport12::Staged staged{};
        const HRESULT staged_result = transport->stage(list, motion,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, depth,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &staged);
        if (FAILED(staged_result)) {
            skip_bridged_d3d12(10, static_cast<std::uint32_t>(staged_result));
            return;
        }
        // V524: the alignment probe (record 850), once the stream's eyes are
        // anchored: its first 256 evaluations, then every 64th.
        if (g_anchor_enabled.load(std::memory_order_acquire)) {
            static std::atomic<std::uint64_t> probed{0};
            const DlssStreamPosition probe_position = dlss_motion_vector_stream_position(feature.stream);
            if (probe_position.anchored) {
                const std::uint64_t index = probed.fetch_add(1, std::memory_order_relaxed);
                auto* colour = get<ID3D12Resource*>(parameters, NVSDK_NGX_Parameter_Color);
                if ((index < 256 || index % 64 == 0) && colour != nullptr) {
                    static_cast<void>(transport->probe_colour(list, colour,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        {get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X),
                            get<unsigned>(parameters, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y)},
                        {publication.motion_x, publication.motion_y},
                        {publication.depth_x, publication.depth_y},
                        {publication.motion_width, publication.motion_height}));
                }
            }
        }
        // V515: inside Streamline's DLSS evaluation, its viewport's camera.
        bool inside_streamline = false;
        publication.view = current_streamline_camera(&inside_streamline);
        publication.output = placeholder.Get();
        publication.motion_vectors = staged.motion;
        publication.producer_queue = queue.Get();
        publication.producer_command_list = staged.list;
        publication.verified_producer_device = device.Get();
        // Shared simultaneous-access textures, back to COMMON after the copy.
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
        {
            static std::atomic<std::uint64_t> recorded{0};
            const std::uint64_t index = recorded.fetch_add(1, std::memory_order_relaxed);
            if (index < 512 || index % 2000 == 0) {
                const auto& view = publication.view;
                const float side = view.known
                    ? view.position[0] * view.right[0] + view.position[1] * view.right[1] +
                        view.position[2] * view.right[2]
                    : 0.0F;
                record(kRecordBridgedD3D12Evaluation, after.published,
                    (feature.stream & 0xFFFFFFFFULL) | (inside_streamline ? 1ULL << 32U : 0ULL) |
                        (view.known ? 1ULL << 33U : 0ULL) |
                        (static_cast<std::uint64_t>(view.viewport & 0xFFFFFFU) << 40U),
                    float_bits(side));
            }
        }
        // V524: where the evaluation stands in its stream; and, while its
        // stream still needs its eyes anchored, its finished output's patches
        // after the DLSS call (complete12).
        const DlssStreamPosition position = dlss_motion_vector_stream_position(feature.stream);
        transport->note_publication(feature.stream, position);
        if (g_anchor_enabled.load(std::memory_order_acquire)) {
            static std::atomic<std::uint64_t> inner_recorded{0};
            const std::uint64_t inner = inner_recorded.fetch_add(1, std::memory_order_relaxed);
            if (inner < 64 || inner % 2000 == 0) {
                record(kRecordInnerEvaluation,
                    (after.published & 0xFFFFFFFFFFFFULL) | (realvr_handle_index(handle) << 48U) |
                        (static_cast<std::uint64_t>(t_depth) << 56U),
                    reinterpret_cast<std::uintptr_t>(motion) & 0xFFFFFFFFFFFFULL,
                    debug_name_chars(motion));
            }
        }
        if (g_anchor_enabled.load(std::memory_order_acquire)) {
            static std::atomic<std::uint64_t> spans_recorded{0};
            const std::uint64_t index = spans_recorded.fetch_add(1, std::memory_order_relaxed);
            if (index < 512 || index % 2000 == 0) {
                record(kRecordSpanScale, after.published,
                    (float_bits(span.own) << 32U) | float_bits(span.other),
                    (float_bits(span.factor) << 32U) | span.source);
            }
        }
        // V524: also every evaluation once anchored, for record 851's history.
        if (g_anchor_enabled.load(std::memory_order_acquire) && position.known) {
            t_patch_transport = transport;
        }
        std::scoped_lock lock(s.mutex);
        ++s.statistics.publications;
        ++s.statistics.d3d12_bridged_publications;
        if (publication.view.known) ++s.statistics.publications_with_camera;
        const auto found = s.features.find(handle);
        if (found != s.features.end()) {
            record_parameters(publication, feature, found->second.publications,
                motion_description.Format, static_cast<std::uint32_t>(motion_description.Width),
                motion_description.Height);
        }
        if (found != s.features.end() && found->second.publications++ == 0) {
            record(kRecordBridgedD3D12Publication,
                (feature.stream & 0xFFFFFFFFULL) |
                    (staged.depth != nullptr ? 1ULL << 32U : 0ULL) |
                    (staged.depth_unshareable ? 1ULL << 33U : 0ULL) |
                    (same_d3d12_device_backing(game.Get(), device.Get()) ? 1ULL << 34U : 0ULL),
                (static_cast<std::uint64_t>(motion_description.Format) << 32U) |
                    static_cast<std::uint64_t>(depth != nullptr ? depth_description.Format : 0),
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
    register_late<Slot>(handle, parameters,
        state().d3d11_active.load(std::memory_order_acquire), true);
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
    register_late<Slot>(handle, parameters,
        state().d3d11_active.load(std::memory_order_acquire), true);
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
    register_late<Slot>(handle, parameters,
        state().vulkan_active.load(std::memory_order_acquire));
    if (depth.outermost()) capture_vk(buffer, handle, parameters);
    return state().modules_vk[Slot].evaluate(buffer, handle, parameters, callback);
}

template <unsigned Slot>
NVSDK_NGX_Result NVSDK_CONV hooked_evaluate_vk_c(
    VkCommandBuffer buffer, const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* parameters,
    PFN_NVSDK_NGX_ProgressCallback_C callback) {
    DepthScope depth;
    register_late<Slot>(handle, parameters,
        state().vulkan_active.load(std::memory_order_acquire));
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
        std::shared_ptr<GuideTransport12> transport12;
        std::shared_ptr<GuideTransportVk> transport_vk;
        {
            std::scoped_lock lock(s.mutex);
            const auto found = s.features.find(handle);
            if (found != s.features.end()) {
                stream = found->second.stream;
                transport = std::move(found->second.transport);
                transport12 = std::move(found->second.transport12);
                transport_vk = std::move(found->second.transport_vk);
                s.features.erase(found);
                ++s.statistics.features_released;
            }
        }
        bool shared = false;
        if (stream != 0) {
            // V526: R.E.A.L. VR's handles share one stream; it goes with the last.
            std::scoped_lock lock(s.mutex);
            if (stream == s.realvr_stream) {
                for (const auto& [other, feature] : s.features) {
                    static_cast<void>(other);
                    if (feature.transport12 && feature.transport12 == s.realvr_transport) shared = true;
                }
                if (!shared) {
                    s.realvr_stream = 0;
                    s.realvr_transport.reset();
                }
            }
        }
        if (stream != 0 && !shared) {
            retire_dlss_motion_vector_stream(stream);
        }
        transport.reset();
        transport12.reset();
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
        // V535: Streamline 2.x only, the layouts above. 1.x (The Last of Us
        // Part I ships 1.4.7) has the same two exports with other arguments:
        // slEvaluateFeature(CommandBuffer*, Feature, frame index, id) and
        // slSetConstants(const Constants&, frame index, id), its Constants
        // without a header. Read as 2.x, the command buffer's pointer was
        // passed on cut to 32 bits and the game crashed inside Streamline at
        // its first evaluation with Hybrid on. 2.0 brought frame tokens;
        // 1.x has no slGetNewFrameToken, and its exports are left alone
        // (823 a = 31). Its NGX evaluations are still captured.
        if (GetProcAddress(module, "slGetNewFrameToken") == nullptr) {
            sl.failed = true;
            record(kRecordHookFailed, 31U);
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
        std::vector<std::shared_ptr<GuideTransport12>> transports12;
        {
            std::scoped_lock lock(s.mutex);
            for (const auto& [handle, feature] : s.features) {
                static_cast<void>(handle);
                if (feature.transport) transports.push_back(feature.transport);
                if (feature.transport12) transports12.push_back(feature.transport12);
            }
        }
        for (const auto& transport : transports) transport->flush();
        // V524: a D3D12 game's copies, behind the same wait.
        for (const auto& transport : transports12) transport->flush();
    } catch (...) {
    }
}

void configure_ngx_alternating_eye_anchor(bool enabled) noexcept {
    g_anchor_enabled.store(enabled, std::memory_order_release);
}

void anchor_ngx_alternating_eyes(
    const std::array<NgxEyeImage, 2>& eyes, std::uint64_t released_at) noexcept {
    try {
        auto& s = state();
        if (!g_anchor_enabled.load(std::memory_order_acquire) ||
            !s.d3d11_active.load(std::memory_order_acquire)) {
            return;
        }
        // The stream published most recently: a game can keep the feature of
        // an earlier session (Cyberpunk 2077 keeps its first one through the
        // OpenXR session it recreates) or register another beside it.
        std::shared_ptr<GuideTransport12> transport;
        std::uint64_t stream = 0;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        std::vector<std::pair<std::uint64_t, std::shared_ptr<GuideTransport12>>> bridged;
        {
            std::scoped_lock lock(s.mutex);
            for (const auto& [handle, feature] : s.features) {
                static_cast<void>(handle);
                if (feature.transport12) bridged.emplace_back(feature.stream, feature.transport12);
            }
            device = s.bridge_device;
            queue = s.bridge_queue;
        }
        std::uint64_t newest_publication = 0;
        for (const auto& [candidate_stream, candidate] : bridged) {
            const DlssStreamPosition candidate_position =
                dlss_motion_vector_stream_position(candidate_stream);
            if (candidate_position.known && candidate_position.publication > newest_publication) {
                newest_publication = candidate_position.publication;
                stream = candidate_stream;
                transport = candidate;
            }
        }
        if (!transport || !device || !queue) {
            skip_anchor(1);
            return;
        }
        std::scoped_lock lock(anchor_mutex());
        measure_eyes(transport, device.Get(), queue.Get(), eyes, released_at, stream);
        AnchorJob& job = anchor_job();
        if (job.pending) {
            if (job.fence->GetCompletedValue() < job.value) return;
            job.pending = false;
            if (job.samples[0].stream == job.stream && job.samples[0].epoch == job.epoch) {
                judge_anchor(job);
            }
        }
        const DlssStreamPosition position = dlss_motion_vector_stream_position(stream);
        if (!position.known || position.anchored) return;
        if (job.stream != stream || job.epoch != position.epoch) {
            job.stream = stream;
            job.epoch = position.epoch;
            job.votes = 0;
            job.last_publication = 0;
        }
        std::array<GuideTransport12::AnchorSample, 2> samples{};
        std::array<std::uint32_t, 2> slots{};
        transport->samples(&samples, &slots);
        const auto& older = samples[0];
        const auto& newer = samples[1];
        if (!older.valid || !newer.valid || older.stream != stream || newer.stream != stream ||
            older.epoch != position.epoch || newer.epoch != position.epoch ||
            newer.serial != older.serial + 1 || newer.serial != position.serial ||
            newer.publication > released_at || newer.publication <= job.last_publication ||
            older.format != newer.format || older.width != newer.width ||
            older.height != newer.height) {
            skip_anchor(older.valid && newer.valid ? 3U : 2U,
                (newer.serial << 32U) | (position.serial & 0xFFFFFFFFULL));
            return;
        }
        // The eye images: a bridged session's shared textures, read without a
        // barrier; every patch where it lies on the output, from the eye's
        // origin.
        std::array<std::uint64_t, 2> patch_pitch{};
        std::array<std::pair<std::int32_t, std::int32_t>, 2> origins{};
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            const NgxEyeImage& image = eyes[eye];
            if (image.image == nullptr || image.x < 0 || image.y < 0) {
                skip_anchor(4);
                return;
            }
            const D3D12_RESOURCE_DESC description = image.image->GetDesc();
            const std::uint32_t bytes = anchor_texel_bytes(description.Format);
            if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || bytes == 0 ||
                description.SampleDesc.Count != 1 ||
                (description.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0 ||
                static_cast<std::uint64_t>(image.x) + image.width > description.Width ||
                static_cast<std::uint64_t>(image.y) + image.height > description.Height) {
                skip_anchor(4, static_cast<std::uint64_t>(description.Format) |
                    (static_cast<std::uint64_t>(description.Flags) << 32U));
                return;
            }
            origins[eye] = eye_patch_origin(stream, eye, image);
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                const auto [x, y] = anchor_patch_origin(patch, older.width, older.height);
                if (!patch_on_eye(origins[eye], x, y, 0, image)) {
                    skip_anchor(5, (static_cast<std::uint64_t>(image.width) << 32U) | image.height);
                    return;
                }
            }
            job.eye_formats[eye] = description.Format;
            patch_pitch[eye] = align_up(static_cast<std::uint64_t>(kAnchorPatch) * bytes,
                D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        }
        if (!job.readback) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = kAnchorReadbackBytes;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(job.readback.GetAddressOf()))) ||
                FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(job.allocator.GetAddressOf()))) ||
                FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    job.allocator.Get(), nullptr, IID_PPV_ARGS(job.list.GetAddressOf()))) ||
                FAILED(job.list->Close()) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                    IID_PPV_ARGS(job.fence.GetAddressOf())))) {
                job.readback.Reset();
                skip_anchor(6);
                return;
            }
        }
        if (FAILED(job.allocator->Reset()) || FAILED(job.list->Reset(job.allocator.Get(), nullptr))) {
            return;
        }
        // The layout: both atlases, then 16 patches of each eye.
        job.atlas_pitch = align_up(static_cast<std::uint64_t>(kAnchorAtlas) *
                anchor_texel_bytes(older.format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const std::uint64_t atlas_size =
            align_up(job.atlas_pitch * kAnchorAtlas, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        job.atlas_offsets = {0, atlas_size};
        job.eye_base = 2U * atlas_size;
        job.patch_size = align_up(std::max(patch_pitch[0], patch_pitch[1]) * kAnchorPatch,
            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        job.patch_pitch_bytes[0] = patch_pitch[0];
        job.patch_pitch_bytes[1] = patch_pitch[1];
        transport->read_atlas(slots[0], job.list.Get(), job.readback.Get(), job.atlas_offsets[0],
            job.atlas_pitch);
        transport->read_atlas(slots[1], job.list.Get(), job.readback.Get(), job.atlas_offsets[1],
            job.atlas_pitch);
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            for (std::uint32_t patch = 0; patch < kAnchorPatches; ++patch) {
                const auto [x, y] = anchor_patch_origin(patch, older.width, older.height);
                const auto left = static_cast<UINT>(origins[eye].first + static_cast<std::int32_t>(x));
                const auto top = static_cast<UINT>(origins[eye].second + static_cast<std::int32_t>(y));
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = eyes[eye].image;
                from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = job.readback.Get();
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = job.eye_base +
                    (static_cast<std::uint64_t>(eye) * kAnchorPatches + patch) * job.patch_size;
                to.PlacedFootprint.Footprint = {job.eye_formats[eye], kAnchorPatch, kAnchorPatch, 1,
                    static_cast<UINT>(patch_pitch[eye])};
                const D3D12_BOX box{left, top, 0, left + kAnchorPatch, top + kAnchorPatch, 1};
                job.list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            }
        }
        if (FAILED(job.list->Close())) return;
        ID3D12CommandList* lists[]{job.list.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (FAILED(queue->Signal(job.fence.Get(), ++job.value))) return;
        transport->protect_slots();
        job.samples = samples;
        job.pending = true;
        job.last_publication = newer.publication;
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

long dlss_input_offset_within_region(
    unsigned base, unsigned output_base, unsigned extent, unsigned output_extent) noexcept {
    return input_offset_within_region(base, output_base, extent, output_extent);
}

}  // namespace xrfg
