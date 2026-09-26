#pragma once
#include "xrfg/fps_overlay_model.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace xrfg::standalone {

inline constexpr wchar_t kLayerName[] =
    L"XR_APILAYER_XRFrameBridge_diagnostic";

enum class FlowBackend {
    fidelity_fx,
    nvidia,
};

enum class NvidiaPerformancePreset {
    slow,
    medium,
    fast,
};

enum class NvidiaInputScale {
    full,
    three_quarter,
    half,
};

struct LauncherSettings {
    FlowBackend backend{FlowBackend::fidelity_fx};
    NvidiaPerformancePreset nvidia_preset{NvidiaPerformancePreset::medium};
    NvidiaInputScale nvidia_input_scale{NvidiaInputScale::half};
    bool nvidia_bidirectional{};
    // "Prefer FPS over latency": the layer's deeper pipeline. On by default.
    bool deep_pipeline{true};
    // "Vulkan support": generate for Vulkan sessions and register the
    // queue-serialising Vulkan layer while armed. Off by default: an
    // implicit Vulkan layer loads into every Vulkan process on the machine.
    bool vulkan_support{};
    // "D3D11 bridge": a D3D11 game's session is given to the runtime as a
    // D3D12 one on the layer's own device, so the runtime never works the
    // game's D3D11 device from the presenter thread. Off by default while
    // it is a first try; the ini key is what the layer reads.
    bool d3d11_bridge{};
    bool diagnostics{};
    FpsOverlayPosition overlay_position{FpsOverlayPosition::upper_right};
};

[[nodiscard]] std::string backend_ini_value(FlowBackend backend);
[[nodiscard]] std::string nvidia_preset_ini_value(
    NvidiaPerformancePreset preset);
[[nodiscard]] std::string nvidia_input_scale_ini_value(
    NvidiaInputScale scale);
[[nodiscard]] LauncherSettings parse_settings(std::string_view text);
[[nodiscard]] std::string serialize_settings(const LauncherSettings& settings);

// The Vulkan implicit layer's manifest, for the loader's ImplicitLayers key.
[[nodiscard]] std::string build_vulkan_layer_manifest(
    const std::filesystem::path& layer_dll,
    std::uint32_t implementation_version);

[[nodiscard]] std::string build_implicit_layer_manifest(
    const std::filesystem::path& layer_dll,
    std::uint32_t implementation_version);

// Diagnostics knobs the tray has no UI for. The runtime INI is their source of
// truth: arming used to regenerate the whole file from LauncherSettings with
// these two hardcoded, so a hand-edited value survived until the next arm and
// no further. write_runtime_configuration reads them back out of the existing
// file and passes them here.
//
// 32 MB is the default because a capture that size covers the usual case and
// costs nothing to leave enabled. At the usual record density it wraps at about
// ninety seconds, keeping only the tail - so a long session that needs its
// early records raises this by hand, which now survives arming.
constexpr unsigned kDefaultMaxFileMb = 32;

[[nodiscard]] std::string build_runtime_ini(
    const LauncherSettings& settings,
    unsigned max_file_mb = kDefaultMaxFileMb,
    bool flush_each_event = false);

[[nodiscard]] std::filesystem::path runtime_version_directory(
    const std::filesystem::path& local_directory,
    std::uint32_t implementation_version);

// Replaces an existing cached layer instead of silently retaining it.
[[nodiscard]] bool install_runtime_layer_dll(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) noexcept;

[[nodiscard]] std::wstring quote_windows_argument(std::wstring_view argument);

} // namespace xrfg::standalone
