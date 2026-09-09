#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace xrfg::implicit_layer {

inline constexpr wchar_t kRegistrySubkey[] =
    L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
inline constexpr wchar_t kManifestPrefix[] =
    L"XR_APILAYER_XRFrameBridge_manual-";
inline constexpr wchar_t kManifestSuffix[] = L".json";

enum class ConfiguredFlowBackend {
    fidelity_fx,
    nvidia,
};

enum class ConfiguredNvidiaPerformancePreset {
    slow,
    medium,
    fast,
};

enum class ConfiguredNvidiaInputScale {
    full,
    three_quarter,
    half,
};

struct ConfiguredNvidiaOptions {
    ConfiguredNvidiaPerformancePreset preset{
        ConfiguredNvidiaPerformancePreset::medium};
    ConfiguredNvidiaInputScale input_scale{
        ConfiguredNvidiaInputScale::half};
    bool bidirectional{};
};

struct ConfiguredFidelityFxOptions {
    // Full by default: the FidelityFX path has always run the flow at full
    // resolution, and lowering it trades motion-vector detail for a
    // materially cheaper pack and flow at high headset resolutions.
    ConfiguredNvidiaInputScale input_scale{
        ConfiguredNvidiaInputScale::full};
};

[[nodiscard]] ConfiguredFlowBackend read_flow_backend(
    const std::filesystem::path& module_directory) noexcept;

[[nodiscard]] ConfiguredNvidiaOptions read_nvidia_options(
    const std::filesystem::path& module_directory) noexcept;

[[nodiscard]] ConfiguredFidelityFxOptions read_fidelity_fx_options(
    const std::filesystem::path& module_directory) noexcept;

[[nodiscard]] bool register_manifest(
    const std::filesystem::path& manifest,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

[[nodiscard]] bool unregister_manifest(
    const std::filesystem::path& manifest,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

[[nodiscard]] bool manifest_registered(
    const std::filesystem::path& manifest,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

[[nodiscard]] bool owned_manifest_path(
    const std::filesystem::path& manifest,
    const std::filesystem::path& runtime_directory) noexcept;

} // namespace xrfg::implicit_layer
