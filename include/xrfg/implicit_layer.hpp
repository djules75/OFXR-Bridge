#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace xrfg::implicit_layer {

// One manual-reset kernel event per arm. Old readers retain their own event;
// re-arming cannot accidentally reactivate a session that was already stopped.
[[nodiscard]] std::wstring arm_signal_name(const std::filesystem::path& manifest);
[[nodiscard]] void* create_arm_signal(const std::filesystem::path& manifest,
                                      std::wstring* error = nullptr) noexcept;
[[nodiscard]] bool signal_arm_stop(const std::filesystem::path& manifest,
                                    std::wstring* error = nullptr) noexcept;
class ManualArmControl {
public:
    explicit ManualArmControl(const std::filesystem::path& module_directory) noexcept;
    ~ManualArmControl();
    ManualArmControl(const ManualArmControl&) = delete;
    ManualArmControl& operator=(const ManualArmControl&) = delete;
    [[nodiscard]] bool stop_requested() const noexcept;
private:
    void* event_{};
    bool managed_{};
};

inline constexpr wchar_t kRegistrySubkey[] =
    L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
inline constexpr wchar_t kManifestPrefix[] =
    L"XR_APILAYER_XRFrameBridge_manual-";
inline constexpr wchar_t kManifestSuffix[] = L".json";
// The Vulkan implicit layer that serialises queue submissions, registered
// beside the OpenXR one while the bridge is armed with Vulkan support on.
// Same value scheme: the manifest path as the value name, DWORD 0.
inline constexpr wchar_t kVulkanRegistrySubkey[] =
    L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers";
inline constexpr wchar_t kVulkanManifestPrefix[] =
    L"OFXR_vulkan_queue_manual-";
inline constexpr wchar_t kVulkanLayerName[] = L"VK_LAYER_OFXR_queue_serialize";
inline constexpr wchar_t kVulkanLayerDll[] = L"OFXR_vulkan_queue_layer.dll";

enum class RegistryScope {
    current_user,
    local_machine,
};

[[nodiscard]] RegistryScope registry_scope_for_integrity_rid(
    std::uint32_t integrity_rid) noexcept;
[[nodiscard]] RegistryScope preferred_registry_scope() noexcept;
[[nodiscard]] std::wstring_view registry_scope_name(RegistryScope scope) noexcept;

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

[[nodiscard]] ConfiguredFlowBackend read_flow_backend(
    const std::filesystem::path& module_directory) noexcept;

// `[ofxr] deep_pipeline`: one display period of extra depth, bought with one
// display period of latency. On unless set to 0. Read once per session, at
// xrCreateSession; the tray calls it "Prefer FPS over latency".
[[nodiscard]] bool read_deep_pipeline(
    const std::filesystem::path& module_directory) noexcept;

// `[ofxr] vulkan_support`: whether the layer generates for Vulkan sessions.
// Off unless set to 1: a Vulkan session needs the queue-serialising Vulkan
// layer registered beside this one, which the tray does only with its
// "Vulkan support" option on, and without it the presenter's submissions
// race the game's on its queue.
[[nodiscard]] bool read_vulkan_support(
    const std::filesystem::path& module_directory) noexcept;

[[nodiscard]] ConfiguredNvidiaOptions read_nvidia_options(
    const std::filesystem::path& module_directory) noexcept;

[[nodiscard]] bool register_manifest(
    const std::filesystem::path& manifest,
    RegistryScope scope,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

[[nodiscard]] bool unregister_manifest(
    const std::filesystem::path& manifest,
    RegistryScope scope,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

[[nodiscard]] bool manifest_registered(
    const std::filesystem::path& manifest,
    RegistryScope scope,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

// prefix says which of the tray's manifests: the OpenXR layer's
// (kManifestPrefix) or the Vulkan layer's (kVulkanManifestPrefix).
[[nodiscard]] bool owned_manifest_path(
    const std::filesystem::path& manifest,
    const std::filesystem::path& runtime_directory,
    std::wstring_view prefix = kManifestPrefix) noexcept;

// Recognizes only tray-owned manual manifests in RuntimeLayer (legacy) or
// RuntimeLayer/vNNN. Does not require the manifest or DLL to still exist.
[[nodiscard]] bool owned_registration_path(
    const std::filesystem::path& manifest,
    const std::filesystem::path& local_directory,
    std::wstring_view prefix = kManifestPrefix) noexcept;

// Disable before deleting; read back registry state, and retain the JSON when
// deregistration fails. The caller must report a false result, never claim Off.
[[nodiscard]] bool retire_manifest(
    const std::filesystem::path& manifest,
    RegistryScope scope,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey) noexcept;

// Registry-first recovery across ALL versions, including missing JSON files.
// Removes only owned registration values and owned JSONs, never DLLs/settings.
[[nodiscard]] bool cleanup_owned_registrations(
    const std::filesystem::path& local_directory,
    RegistryScope scope,
    std::wstring* error = nullptr,
    std::wstring_view registry_subkey = kRegistrySubkey,
    std::wstring_view prefix = kManifestPrefix) noexcept;

} // namespace xrfg::implicit_layer
