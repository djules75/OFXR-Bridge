#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include "xrfg/interface_metrics.hpp"

namespace xrfg::autonomous {

// Each generation menu owns its preferences. Flat fields below are the active
// editing view; select_generation saves/restores that view without cross-talk.
struct GenerationPreferences {
    int motion_vectors{};
    int preset{1};
    int scale{2};
    bool backward{};
    int lower_vram{};
    bool prefer_fps{true};
    bool triple_frame_gen{};
    int native_scale{100};
    bool operator==(const GenerationPreferences&) const = default;
};
constexpr std::size_t kGenerationProfileCount = 7; // four OFXR engines, FSR, DLSS-G

// Plain CPU values shared between the standalone UI and the in-process layer.
// No application resource pointers cross this boundary.
struct Settings {
    bool enabled{true};
    int backend{2}; // 0 FidelityFX, 1 NVIDIA reference, 2 NVIDIA new, 3 NVIDIA djules
    int motion_vectors{}; // 0 optical flow, 1 native game guides
    int frame_generation{}; // 0 OFXR, 1 FSR 3, 2 native DLSS Frame Generation, 3 Hybrid
    int hybrid_backend{}; // legacy profile key; Hybrid fallback is always FidelityFX
    int preset{1}; // 0 slow, 1 medium, 2 fast
    int scale{2}; // 0 full, 1 three-quarter, 2 half
    bool backward{};
    int lower_vram{}; // checkbox: 0 off (default), 1 on
    bool prefer_fps{true};
    bool triple_frame_gen{};
    int native_scale{100};
    int afw_mode{3}; // 0 off, 1 alternate, 2 previous, 3 combined
    bool afw_debug{};
    bool afw_clear_before_warping{};
    float afw_ignore_motion_threshold{2.5F};
    int afw_depth_dilate_mode{1};
    bool nr_enabled{};
    bool nr_before_sr{true};
    float nr_intensity{1.F};
    int nr_preset{};
    int nr_style{};
    float nr_local_structure{1.F};
    float nr_local_tone{1.F};
    float nr_skin_structure{-1.F};
    bool nr_auto_mask{true};
    float nr_transfer_strength{1.F};
    float nr_colour_strength{1.F};
    float nr_max_ratio{2.F};
    float nr_working_scale{1.F};
    float nr_white_point_scale{1.F};
    int nr_debug_view{};
    int nr_compare{};
    float nr_compare_split{.5F};
    float nr_compare_zoom{1.F};
    bool nr_compare_swap{};
    bool r19_camera{}; // opt-in own camera for every FG route; live pair snapshot
    bool diagnostics{};
    int overlay_position{2}; // 0 off, 1 upper-left, 2 upper-right, 3 lower-left, 4 lower-right, 5 Elliott controller, 6 Elliott bottom
    float menu_scale{1.F}; // physical headset interface multiplier, 0.5 to 1.25
    float menu_offset_x{}; // common-FOV centre offset, normalized -1 to 1
    float menu_offset_y{};
    std::array<GenerationPreferences, kGenerationProfileCount> generation_profiles{};
    bool profiles_initialized{}; // legacy flat profile migration
    bool operator==(const Settings&) const = default;
};

enum class GenerationState : std::uint32_t {
    waiting, ready, paused, missing_motion, missing_depth, invalid_guides,
    invalid_stereo, recovering, busy, unavailable, runtime_missing,
    initialization_failed, dispatch_failed, temporal_mismatch, output_mismatch,
    queue_mismatch, multi_frame_unsupported, native_declined,
};
// User-facing wording shared by both settings surfaces and exercised by the
// control fixtures. State describes actual generation, not just applied settings.
std::string_view generation_status_text(int mode, int motion_source, GenerationState state) noexcept;

struct RuntimeStatus {
    std::uint32_t process_id{};
    std::uint64_t heartbeat_ms{};
    std::uint64_t applied_revision{};
    std::uint32_t sessions{};
    std::uint32_t ready{};
    std::uint32_t pending{};
    std::uint32_t errors{};
    std::uint64_t afw_ready_frames{};
    std::uint64_t afw_projection_frames{};
    std::uint64_t nr_evaluations{};
    bool nr_running{};
    bool logging_active{};
    bool vulkan_capture_suspended{};
    bool luke_detected{}, vulkan_detected{};
    bool afw_running{}; // actual recent AFW output; IPC V487 rejects older reporters
    std::uint32_t provider_error{};
    InterfaceTelemetry telemetry{};
    int generation_mode{}, motion_source{};
    GenerationState generation_state{GenerationState::waiting};
    std::uint64_t upscaler_evaluations{}, upscaler_motion_inputs{}, upscaler_depth_inputs{}, upscaler_last_seen_ms{};
    bool upscaler_motion{}, upscaler_depth{};
    std::uint64_t guide_publications{}, guide_matches{}, guide_uses{};
    char message[192]{};
};
struct Snapshot {
    Settings desired{};
    std::uint64_t revision{1};
    RuntimeStatus runtime{};
    bool connected{}; // runtime reported within the last five seconds
};

// Persistent values are independent of the original OFXR/OptiScaler profile.
// Runtime processes and the tray keep the shared mapping open; named-mutex
// publication prevents tearing and a versioned ABI rejects incompatible peers.
Snapshot snapshot() noexcept;
bool request(Settings desired) noexcept;
// Publish only fields edited since base, preserving changes from another UI.
// The shared-mapping mutex acquisition never blocks the end-frame thread.
bool request_changes(const Settings& base, const Settings& desired) noexcept;
Settings merge_changes(Settings current, const Settings& base, const Settings& desired) noexcept;
void report_runtime(RuntimeStatus status) noexcept;
// Visibility leases are independent of settings revisions and persistence.
enum class InterfaceSurface : unsigned { desktop, headset, meter };
void set_interface_visible(InterfaceSurface surface, bool visible) noexcept;
bool interface_telemetry_requested() noexcept;
void report_interface_telemetry(InterfaceTelemetry value) noexcept;
std::string interface_status_text(const Snapshot& value);
std::string_view interface_state_text(const Snapshot& value) noexcept;
std::string_view interface_vectors_text(const Snapshot& value, std::uint64_t now_ms) noexcept;
std::filesystem::path configuration_path();

constexpr int generation_ui_index(int mode) noexcept { return mode==3 ? 1 : mode==1 ? 2 : mode==2 ? 3 : 0; }
constexpr int generation_from_ui(int index) noexcept { return index==1 ? 3 : index==2 ? 1 : index==3 ? 2 : 0; }
void select_generation(Settings& value, int generation, int ofxr_engine) noexcept;
Settings sanitize(Settings value) noexcept;
Settings parse_settings(std::string_view text);
std::string serialize_settings(const Settings& value);

} // namespace xrfg::autonomous
