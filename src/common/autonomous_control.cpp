#include "xrfg/autonomous_control.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <limits>
#include <sstream>
#include <type_traits>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace xrfg::autonomous {
std::string_view overlay_limit_text(std::uint32_t mask) noexcept {
    if(mask&4) return "OpenXR swapchain limit reached: the headset F10 menu could not open. Turn off unused overlays and retry F10. Settings remain available on the desktop.";
    if(mask&2) return "OpenXR swapchain limit reached: the OFXR meter could not be created. Turn off unused overlays and select the meter again.";
    if(mask&1) return "OpenXR swapchain limit reached: the OFXR FPS overlay could not be created. Turn off unused overlays and select it again.";
    return {};
}
std::string_view generation_status_text(int mode, int motion_source, GenerationState state) noexcept {
    switch (state) {
    case GenerationState::paused: return "Frame generation paused: real frames only";
    case GenerationState::waiting: return "Waiting for compatible frames; real frames only";
    case GenerationState::recovering: return "Bypass: priming fresh stereo history; real frames only";
    case GenerationState::busy: return "Bypass: capture or GPU output busy; real frames only";
    case GenerationState::unavailable: return "DLSS-G bypass: feature unavailable on this adapter/runtime or first NGX initialization";
    case GenerationState::runtime_missing: return "FSR bypass: frame-generation runtime missing";
    case GenerationState::initialization_failed: return "Bypass: frame-generation initialization failed";
    case GenerationState::dispatch_failed: return "Bypass: provider rejected generation; real frames only";
    case GenerationState::native_declined: return "DLSS-G bypass: NGX disabled interpolation for this pair; real frames only";
    case GenerationState::multi_frame_unsupported: return "DLSS-G: 3X unsupported on this adapter; using 2X";
    default: break;
    }
    const bool fallback = (mode == 0 || mode == 3) && motion_source == 1;
    switch (state) {
    case GenerationState::missing_motion:
        return fallback ? "Fallback to optical flow: no compatible game motion vectors" :
            "Bypass: no compatible game motion vectors; real frames only";
    case GenerationState::missing_depth:
        return fallback ? "Fallback to optical flow: native depth unavailable" :
            "Bypass: native depth missing or its convention is unknown; real frames only";
    case GenerationState::invalid_guides:
        return fallback ? "Fallback to optical flow: native guides rejected" :
            "Bypass: native guides rejected (frame/device/camera identity); real frames only";
    case GenerationState::invalid_stereo:
        return fallback ? "Fallback to optical flow: distinct stereo guides unavailable" :
            "Bypass: distinct guides required for both eyes; real frames only";
    case GenerationState::temporal_mismatch:
        return fallback ? "Fallback to optical flow: guide history is discontinuous" :
            "Bypass: guide history is discontinuous; real frames only";
    case GenerationState::output_mismatch:
        return fallback ? "Fallback to optical flow: upscaler output does not match XR color" :
            "Bypass: upscaler output does not match XR color; real frames only";
    case GenerationState::queue_mismatch:
        return fallback ? "Fallback to optical flow: guide queue ownership mismatch" :
            "Bypass: guide queue ownership mismatch; real frames only";
    default:
        if (mode == 3) return motion_source == 1 ? "Hybrid: game motion vectors + FidelityFX optical flow" :
            "Hybrid fallback: FidelityFX optical flow; no complete native stereo guides";
        if (mode == 1) return "FSR: generating with native stereo motion and depth guides";
        if (mode == 2) return "DLSS-G: generating with native stereo motion and depth guides";
        return motion_source == 1 ? "OFXR: using compatible game motion vectors (optical flow bypassed)" :
            "OFXR: optical flow active";
    }
}
namespace {
constexpr std::uint32_t kMagic = 0x41465852;
constexpr std::uint32_t kAbi = 54201;
std::atomic_uint overlay_limits{};
static_assert(offsetof(RuntimeStatus, provider_error)==72); // V471 IPC layout
static_assert(offsetof(RuntimeStatus, afw_running)==69); // previously reserved padding
static_assert(offsetof(RuntimeStatus, overlay_limit_mask)==76); // previous reserved padding
struct Shared {
    std::uint32_t magic{};
    std::uint32_t abi{};
    std::uint32_t bytes{};
    Snapshot snapshot{};
    alignas(8) std::uint64_t visible_until_ms[3]{};
};
static_assert(std::is_trivially_copyable_v<Shared>);

float bounded(float value, float minimum, float maximum, float fallback) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == value.npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return std::string(value.substr(first, last - first + 1));
}
constexpr std::array<std::string_view, kGenerationProfileCount> profile_sections{
    "[route.ofxr_fidelityfx]", "[route.ofxr_reference]", "[route.ofxr_new]",
    "[route.ofxr_djules]", "[route.fsr]", "[route.dlssg]", "[route.hybrid]"};
std::size_t selected_profile(const Settings& s) noexcept {
    if (s.frame_generation==3) return 6;
    return s.frame_generation == 0 ? static_cast<std::size_t>(std::clamp(s.backend, 0, 3)) :
        static_cast<std::size_t>(std::clamp(s.frame_generation, 1, 2) + 3);
}
GenerationPreferences active_preferences(const Settings& s) noexcept {
    return {s.motion_vectors, s.preset, s.scale, s.backward, s.separate_synthesis, s.lower_vram,
        s.prefer_fps, s.triple_frame_gen, s.native_scale};
}
void restore_preferences(Settings& s) noexcept {
    const auto& p = s.generation_profiles[selected_profile(s)];
    s.motion_vectors = p.motion_vectors; s.preset = p.preset; s.scale = p.scale;
    s.backward = p.backward; s.separate_synthesis = p.separate_synthesis; s.lower_vram = p.lower_vram; s.prefer_fps = p.prefer_fps;
    s.triple_frame_gen = p.triple_frame_gen; s.native_scale = p.native_scale;
}
Settings load() {
    std::ifstream input(configuration_path(), std::ios::binary);
    std::ostringstream data;
    if (input) data << input.rdbuf();
    return parse_settings(data.str());
}
bool save(const Settings& value) {
    const auto path = configuration_path();
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
#ifdef _WIN32
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
#else
    temporary += ".tmp";
#endif
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << serialize_settings(value);
        output.close();
        if (!output) return false;
    }
#ifdef _WIN32
    const bool success = MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!success) DeleteFileW(temporary.c_str());
    return success;
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}

class Channel {
public:
#ifdef _WIN32
    HANDLE mutex{};
    HANDLE mapping{};
    Shared* data{};
    Channel() {
        // Profile identity is part of the mapping name, so fixture profiles
        // and future concurrent candidates cannot modify the user's settings.
        std::uint64_t identity = 14695981039346656037ULL;
        for (const auto character : configuration_path().wstring()) {
            identity ^= static_cast<std::uint64_t>(std::towlower(character));
            identity *= 1099511628211ULL;
        }
        const auto prefix = L"Local\\OFXRAutonomousV487-" + std::to_wstring(identity);
        mutex = CreateMutexW(nullptr, FALSE, (prefix + L"-mutex").c_str());
        if (!mutex) return;
        const DWORD waited = WaitForSingleObject(mutex, 2000);
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) return;
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(Shared), (prefix + L"-control").c_str());
        if (mapping) data = static_cast<Shared*>(MapViewOfFile(
            mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (data && (data->magic != kMagic || data->abi != kAbi || data->bytes != sizeof(Shared))) {
            // A same-name object with another ABI is never overwritten.
            if (data->magic != 0) {
                UnmapViewOfFile(data);
                data = nullptr;
            } else {
                Shared initial{};
                initial.magic = kMagic;
                initial.abi = kAbi;
                initial.bytes = sizeof(Shared);
                initial.snapshot.desired = load();
                *data = initial;
            }
        }
        ReleaseMutex(mutex);
    }
    ~Channel() {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
    }
    bool lock(unsigned milliseconds) {
        if (!mutex || !data) return false;
        const DWORD waited = WaitForSingleObject(mutex, milliseconds);
        return waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
    }
    void unlock() { ReleaseMutex(mutex); }
#else
    std::mutex mutex;
    Shared storage{kMagic, kAbi, sizeof(Shared), {load(), 1, {}, false}};
    Shared* data{&storage};
    bool lock(unsigned) { mutex.lock(); return true; }
    void unlock() { mutex.unlock(); }
#endif
};
Channel& channel() { static Channel instance; return instance; }
struct Lock {
    Channel& owner;
    bool acquired;
    explicit Lock(Channel& value, unsigned milliseconds = 0) : owner(value), acquired(value.lock(milliseconds)) {}
    ~Lock() { if (acquired) owner.unlock(); }
};
} // namespace

Settings sanitize(Settings value) noexcept {
    value.backend = std::clamp(value.backend, 0, 3);
    value.hybrid_backend = 0; // Migrate old selectable fallbacks to FidelityFX.
    value.motion_vectors = std::clamp(value.motion_vectors, 0, 1);
    value.frame_generation = std::clamp(value.frame_generation, 0, 3);
    value.preset = std::clamp(value.preset, 0, 2);
    value.scale = std::clamp(value.scale, 0, 3);
    value.separate_synthesis = std::clamp(value.separate_synthesis, 0, 1);
    value.lower_vram = std::clamp(value.lower_vram, 0, 1);
    value.native_scale = std::clamp(value.native_scale, 25, 100);
    value.afw_mode = std::clamp(value.afw_mode, 0, 3);
    value.afw_ignore_motion_threshold = bounded(value.afw_ignore_motion_threshold, 0.F, 16.F, 2.5F);
    value.afw_depth_dilate_mode = std::clamp(value.afw_depth_dilate_mode, 0, 2);
    value.afw_shading_rate = std::clamp(value.afw_shading_rate, 0, 8);
    value.nr_intensity = bounded(value.nr_intensity, 0.F, 2.F, 1.F);
    value.nr_preset = std::clamp(value.nr_preset, 0, 3);
    value.nr_style = std::clamp(value.nr_style, 0, 2);
    value.nr_local_structure = bounded(value.nr_local_structure, 0.F, 2.F, 1.F);
    value.nr_local_tone = bounded(value.nr_local_tone, 0.F, 2.F, 1.F);
    value.nr_skin_structure = bounded(value.nr_skin_structure, -1.F, 2.F, -1.F);
    value.nr_transfer_strength = bounded(value.nr_transfer_strength, 0.F, 2.F, 1.F);
    value.nr_colour_strength = bounded(value.nr_colour_strength, 0.F, 1.F, 1.F);
    value.nr_max_ratio = bounded(value.nr_max_ratio, 1.F, 8.F, 2.F);
    value.nr_working_scale = bounded(value.nr_working_scale, .25F, 1.F, 1.F);
    value.nr_white_point_scale = bounded(value.nr_white_point_scale, .25F, 4.F, 1.F);
    value.nr_debug_view = std::clamp(value.nr_debug_view, 0, 3);
    value.nr_compare = std::clamp(value.nr_compare, 0, 2);
    value.nr_compare_split = bounded(value.nr_compare_split, 0.F, 1.F, .5F);
    value.nr_compare_zoom = bounded(value.nr_compare_zoom, 1.F, 2.F, 1.F);
    value.overlay_position = std::clamp(value.overlay_position, 0, 6);
    value.menu_scale = bounded(value.menu_scale, .5F, 1.25F, 1.F);
    value.menu_offset_x = bounded(value.menu_offset_x, -1.F, 1.F, 0.F);
    value.menu_offset_y = bounded(value.menu_offset_y, -1.F, 1.F, 0.F);
    // FSR and DLSS-G require engine guides. No optical-flow substitution is
    // silently offered for a guide-only generation backend.
    if (value.frame_generation != 0) value.motion_vectors = 1;
    // Legacy profiles only know the selected route. Keep its explicit values;
    // unseen routes start from their own defaults, never from another menu.
    if (!value.profiles_initialized) value.generation_profiles = {};
    value.profiles_initialized = true;
    value.generation_profiles[selected_profile(value)] = active_preferences(value);
    for (std::size_t i = 0; i < value.generation_profiles.size(); ++i) {
        auto& p = value.generation_profiles[i];
        p.motion_vectors = i >= 4 ? 1 : std::clamp(p.motion_vectors, 0, 1);
        p.preset = std::clamp(p.preset, 0, 2); p.scale = std::clamp(p.scale, 0, 3);
        p.separate_synthesis = std::clamp(p.separate_synthesis, 0, 1);
        p.lower_vram = std::clamp(p.lower_vram, 0, 1);
        p.native_scale = std::clamp(p.native_scale, 25, 100);
    }
    restore_preferences(value);
    return value;
}

void select_generation(Settings& value, int generation, int ofxr_engine) noexcept {
    value = sanitize(value); // commit outgoing edits before restoring the target
    value.frame_generation = std::clamp(generation, 0, 3);
    value.backend = std::clamp(ofxr_engine, 0, 3);
    restore_preferences(value);
}

Settings parse_settings(std::string_view text) {
    Settings value;
    int section = -2; // -1 autonomous, 0..5 generation profile, -2 ignored
    std::array<bool, kGenerationProfileCount> profile_seen{}, separation_seen{};
    bool flat_separation_seen{};
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line == "[autonomous]" ? -1 : -2;
            for (std::size_t i = 0; i < profile_sections.size(); ++i)
                if (line == profile_sections[i]) { section = static_cast<int>(i); profile_seen[i] = true; }
            continue;
        }
        if (section == -2) continue;
        const auto equals = line.find('=');
        if (equals == line.npos) continue;
        const auto key = trim(std::string_view(line).substr(0, equals));
        const auto data = trim(std::string_view(line).substr(equals + 1));
        char* end = nullptr;
        const float number = std::strtof(data.c_str(), &end);
        if (!end || *end != '\0' || end == data.c_str() || !std::isfinite(number)) continue;
        // Bound before any float-to-integral conversion; hostile/unexpected
        // profile text must not produce an undefined out-of-range conversion.
        if (number < -1000000.F || number > 1000000.F) continue;
        if (section >= 0) {
            auto& profile = value.generation_profiles[static_cast<std::size_t>(section)];
            if(key=="separate_synthesis") separation_seen[static_cast<std::size_t>(section)]=true;
#define READ_PROFILE(name) if (key == #name) profile.name = static_cast<decltype(profile.name)>(number);
            READ_PROFILE(motion_vectors) READ_PROFILE(preset) READ_PROFILE(scale)
            READ_PROFILE(backward) READ_PROFILE(separate_synthesis) READ_PROFILE(lower_vram) READ_PROFILE(prefer_fps)
            READ_PROFILE(triple_frame_gen) READ_PROFILE(native_scale)
#undef READ_PROFILE
            continue;
        }
        if(key=="separate_synthesis") flat_separation_seen=true;
#define READ_FIELD(name) if (key == #name) value.name = static_cast<decltype(value.name)>(number);
        READ_FIELD(enabled) READ_FIELD(backend) READ_FIELD(motion_vectors)
        READ_FIELD(frame_generation) READ_FIELD(hybrid_backend) READ_FIELD(preset) READ_FIELD(scale)
        READ_FIELD(backward) READ_FIELD(separate_synthesis) READ_FIELD(lower_vram) READ_FIELD(prefer_fps)
        READ_FIELD(triple_frame_gen) READ_FIELD(native_scale) READ_FIELD(r19_camera)
        READ_FIELD(afw_mode) READ_FIELD(afw_debug) READ_FIELD(afw_clear_before_warping)
        READ_FIELD(afw_ignore_motion_threshold) READ_FIELD(afw_depth_dilate_mode) READ_FIELD(afw_shading_rate)
        READ_FIELD(nr_enabled) READ_FIELD(nr_before_sr) READ_FIELD(nr_intensity)
        READ_FIELD(nr_preset) READ_FIELD(nr_style) READ_FIELD(nr_local_structure)
        READ_FIELD(nr_local_tone) READ_FIELD(nr_skin_structure) READ_FIELD(nr_auto_mask)
        READ_FIELD(nr_transfer_strength) READ_FIELD(nr_colour_strength)
        READ_FIELD(nr_max_ratio) READ_FIELD(nr_working_scale) READ_FIELD(nr_white_point_scale)
        READ_FIELD(nr_debug_view) READ_FIELD(nr_compare) READ_FIELD(nr_compare_split)
        READ_FIELD(nr_compare_zoom) READ_FIELD(nr_compare_swap)
        READ_FIELD(diagnostics) READ_FIELD(overlay_position)
        READ_FIELD(menu_scale) READ_FIELD(menu_offset_x) READ_FIELD(menu_offset_y)
#undef READ_FIELD
    }
    if(!flat_separation_seen) value.separate_synthesis=value.lower_vram;
    for(std::size_t i=0;i<profile_seen.size();++i)
        if(!separation_seen[i]) value.generation_profiles[i].separate_synthesis=value.generation_profiles[i].lower_vram;
    if (std::any_of(profile_seen.begin(), profile_seen.end(), [](bool seen) { return seen; })) {
        value.profiles_initialized = true;
        if (profile_seen[selected_profile(value)]) restore_preferences(value);
    }
    return sanitize(value);
}

std::string serialize_settings(const Settings& settings) {
    const Settings value = sanitize(settings);
    std::ostringstream output;
    output << "[autonomous]\n" << std::setprecision(9);
#define WRITE_FIELD(name) output << #name << '=' << value.name << '\n';
    WRITE_FIELD(enabled) WRITE_FIELD(backend) WRITE_FIELD(motion_vectors)
    WRITE_FIELD(frame_generation) WRITE_FIELD(hybrid_backend) WRITE_FIELD(preset) WRITE_FIELD(scale)
    WRITE_FIELD(backward) WRITE_FIELD(separate_synthesis) WRITE_FIELD(lower_vram) WRITE_FIELD(prefer_fps)
    WRITE_FIELD(triple_frame_gen) WRITE_FIELD(native_scale) WRITE_FIELD(r19_camera)
    WRITE_FIELD(afw_mode) WRITE_FIELD(afw_debug) WRITE_FIELD(afw_clear_before_warping)
    WRITE_FIELD(afw_ignore_motion_threshold) WRITE_FIELD(afw_depth_dilate_mode) WRITE_FIELD(afw_shading_rate)
    WRITE_FIELD(nr_enabled) WRITE_FIELD(nr_before_sr) WRITE_FIELD(nr_intensity)
    WRITE_FIELD(nr_preset) WRITE_FIELD(nr_style) WRITE_FIELD(nr_local_structure)
    WRITE_FIELD(nr_local_tone) WRITE_FIELD(nr_skin_structure) WRITE_FIELD(nr_auto_mask)
    WRITE_FIELD(nr_transfer_strength) WRITE_FIELD(nr_colour_strength)
    WRITE_FIELD(nr_max_ratio) WRITE_FIELD(nr_working_scale) WRITE_FIELD(nr_white_point_scale)
    WRITE_FIELD(nr_debug_view) WRITE_FIELD(nr_compare) WRITE_FIELD(nr_compare_split)
    WRITE_FIELD(nr_compare_zoom) WRITE_FIELD(nr_compare_swap)
    WRITE_FIELD(diagnostics) WRITE_FIELD(overlay_position)
    WRITE_FIELD(menu_scale) WRITE_FIELD(menu_offset_x) WRITE_FIELD(menu_offset_y)
#undef WRITE_FIELD
    for (std::size_t i = 0; i < profile_sections.size(); ++i) {
        output << '\n' << profile_sections[i] << '\n';
        const auto& p = value.generation_profiles[i];
#define WRITE_PROFILE(name) output << #name << '=' << p.name << '\n';
        WRITE_PROFILE(motion_vectors) WRITE_PROFILE(preset) WRITE_PROFILE(scale)
        WRITE_PROFILE(backward) WRITE_PROFILE(separate_synthesis) WRITE_PROFILE(lower_vram) WRITE_PROFILE(prefer_fps)
        WRITE_PROFILE(triple_frame_gen) WRITE_PROFILE(native_scale)
#undef WRITE_PROFILE
    }
    return output.str();
}

std::filesystem::path configuration_path() {
#ifdef _WIN32
    wchar_t override_directory[32768]{};
    const auto override_length = GetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",
        override_directory, static_cast<DWORD>(std::size(override_directory)));
    if (override_length && override_length < std::size(override_directory)) {
        const std::filesystem::path override_path(override_directory);
        if (override_path.is_absolute()) return override_path / L"autonomous.ini";
    }
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)))
        throw std::runtime_error("Local application data is unavailable");
    std::filesystem::path result(local);
    CoTaskMemFree(local);
    return result / L"OFXR Autonomous" / L"autonomous.ini";
#else
    const auto* local = std::getenv("XDG_CONFIG_HOME");
    return std::filesystem::path(local ? local : ".") / "OFXR Autonomous" / "autonomous.ini";
#endif
}

Snapshot snapshot() noexcept {
    try {
        auto& connection = channel();
        // A frame thread never waits for disk publication from the panel.
        // Retain the last coherent settings while another peer holds the lock.
        thread_local Snapshot last_good{load(), 1, {}, false};
        Lock lock(connection);
        if (lock.acquired) last_good = connection.data->snapshot;
        Snapshot result = last_good;
#ifdef _WIN32
        const auto now = GetTickCount64();
        result.connected = result.runtime.sessions != 0 && result.runtime.process_id != 0 &&
            now >= result.runtime.heartbeat_ms && now - result.runtime.heartbeat_ms < 5000;
#endif
        return result;
    } catch (...) { return {}; }
}

bool request(Settings desired) noexcept {
    try {
        desired = sanitize(desired);
        auto& connection = channel();
        Lock lock(connection, 2000);
        if (!lock.acquired || !save(desired)) return false;
        if (connection.data->snapshot.desired != desired) {
            connection.data->snapshot.desired = desired;
            ++connection.data->snapshot.revision;
        }
        return true;
    } catch (...) { return false; }
}

Settings merge_changes(Settings current, const Settings& prior, const Settings& edited) noexcept {
    current = sanitize(current);
    const auto base = sanitize(prior), desired = sanitize(edited);
    for (std::size_t i = 0; i < kGenerationProfileCount; ++i) {
        auto& p = current.generation_profiles[i];
        const auto& b = base.generation_profiles[i];
        const auto& d = desired.generation_profiles[i];
#define MERGE_PROFILE(name) if (b.name != d.name) p.name = d.name;
        MERGE_PROFILE(motion_vectors) MERGE_PROFILE(preset) MERGE_PROFILE(scale)
        MERGE_PROFILE(backward) MERGE_PROFILE(separate_synthesis) MERGE_PROFILE(lower_vram) MERGE_PROFILE(prefer_fps)
        MERGE_PROFILE(triple_frame_gen) MERGE_PROFILE(native_scale)
#undef MERGE_PROFILE
    }
#define MERGE_FIELD(name) if (base.name != desired.name) current.name = desired.name;
    MERGE_FIELD(enabled) MERGE_FIELD(backend) MERGE_FIELD(frame_generation) MERGE_FIELD(hybrid_backend) MERGE_FIELD(r19_camera)
    MERGE_FIELD(afw_mode) MERGE_FIELD(afw_debug) MERGE_FIELD(afw_clear_before_warping)
    MERGE_FIELD(afw_ignore_motion_threshold) MERGE_FIELD(afw_depth_dilate_mode) MERGE_FIELD(afw_shading_rate)
    MERGE_FIELD(nr_enabled) MERGE_FIELD(nr_before_sr) MERGE_FIELD(nr_intensity)
    MERGE_FIELD(nr_preset) MERGE_FIELD(nr_style) MERGE_FIELD(nr_local_structure)
    MERGE_FIELD(nr_local_tone) MERGE_FIELD(nr_skin_structure) MERGE_FIELD(nr_auto_mask)
    MERGE_FIELD(nr_transfer_strength) MERGE_FIELD(nr_colour_strength)
    MERGE_FIELD(nr_max_ratio) MERGE_FIELD(nr_working_scale) MERGE_FIELD(nr_white_point_scale)
    MERGE_FIELD(nr_debug_view) MERGE_FIELD(nr_compare) MERGE_FIELD(nr_compare_split)
    MERGE_FIELD(nr_compare_zoom) MERGE_FIELD(nr_compare_swap)
    MERGE_FIELD(diagnostics) MERGE_FIELD(overlay_position)
    MERGE_FIELD(menu_scale) MERGE_FIELD(menu_offset_x) MERGE_FIELD(menu_offset_y)
#undef MERGE_FIELD
    restore_preferences(current);
    return sanitize(current);
}

bool request_changes(const Settings& base, const Settings& desired) noexcept {
    try {
        auto& connection = channel();
        Lock lock(connection);
        if (!lock.acquired) return false;
        const auto merged = merge_changes(connection.data->snapshot.desired, base, desired);
        if (merged == connection.data->snapshot.desired) return true;
        if (!save(merged)) return false;
        connection.data->snapshot.desired = merged;
        ++connection.data->snapshot.revision;
        return true;
    } catch (...) { return false; }
}

void report_runtime(RuntimeStatus status) noexcept {
    try {
        auto& connection = channel();
        Lock lock(connection);
        if (!lock.acquired) return;
#ifdef _WIN32
        status.process_id = GetCurrentProcessId();
        status.heartbeat_ms = GetTickCount64();
#endif
        status.overlay_limit_mask=overlay_limits.load(std::memory_order_relaxed);
        status.message[sizeof(status.message) - 1] = '\0';
        if (connection.data->snapshot.runtime.process_id == status.process_id)
            status.telemetry = connection.data->snapshot.runtime.telemetry;
        connection.data->snapshot.runtime = status;
    } catch (...) {}
}
void report_overlay_limit(OverlaySwapchain surface, bool refused) noexcept {
    const auto bit=static_cast<unsigned>(surface);
    if(refused) overlay_limits.fetch_or(bit,std::memory_order_relaxed);
    else overlay_limits.fetch_and(~bit,std::memory_order_relaxed);
    try {
        auto& connection=channel();Lock lock(connection);
        if(!lock.acquired)return;
        auto& runtime=connection.data->snapshot.runtime;
#ifdef _WIN32
        if(runtime.process_id && runtime.process_id!=GetCurrentProcessId())return;
#endif
        runtime.overlay_limit_mask=overlay_limits.load(std::memory_order_relaxed);
    } catch(...) {}
}
void set_interface_visible(InterfaceSurface surface, bool visible) noexcept {
    try {
        auto& c = channel(); if (!c.data) return;
        const auto index = static_cast<unsigned>(surface); if (index > 2) return;
        std::atomic_ref<std::uint64_t>(c.data->visible_until_ms[index]).store(
            visible ? GetTickCount64() + 1000 : 0, std::memory_order_release);
    } catch (...) {}
}
bool interface_telemetry_requested() noexcept {
    try {
        auto& c = channel(); if (!c.data) return false;
        const auto now = GetTickCount64();
        for (auto& deadline : c.data->visible_until_ms)
            if (std::atomic_ref<std::uint64_t>(deadline).load(std::memory_order_acquire) > now) return true;
    } catch (...) {}
    return false;
}
void report_interface_telemetry(InterfaceTelemetry value) noexcept {
    try {
        auto& c = channel(); Lock lock(c); if (!lock.acquired) return;
        if (c.data->snapshot.runtime.process_id && c.data->snapshot.runtime.process_id != GetCurrentProcessId()) return;
        value.updated_ms = GetTickCount64(); c.data->snapshot.runtime.telemetry = value;
    } catch (...) {}
}
std::string_view interface_vectors_text(const Snapshot& s, std::uint64_t now) noexcept {
    const auto& r = s.runtime;
    // Native SR/RR is the only typed vector ingress in V456. FSR FG is a
    // consumer, never evidence of FSR-origin vectors.
    return s.connected && r.upscaler_evaluations && r.upscaler_motion &&
        now >= r.upscaler_last_seen_ms && now - r.upscaler_last_seen_ms < 2000 ? "DLSS" : "None";
}
std::string_view interface_state_text(const Snapshot& s) noexcept {
    if (!s.connected) return "Waiting";
    const auto& r = s.runtime;
    if (r.generation_state == GenerationState::paused) return "Paused";
    if (r.pending || r.applied_revision != s.revision) return "Applying settings";
    const bool active = r.telemetry.updated_ms && GetTickCount64() - r.telemetry.updated_ms < 2000 && r.telemetry.generating;
    if (active && (r.generation_mode == 0 || r.generation_mode == 3) && r.motion_source == 1 &&
        r.generation_state != GenerationState::ready) return "Fallback";
    if (active) {
        const auto now=GetTickCount64();
        const bool fresh=r.heartbeat_ms && now>=r.heartbeat_ms && now-r.heartbeat_ms<2000;
        const bool afw=fresh && r.afw_running && s.desired.afw_mode!=0;
        const bool nr=fresh && r.nr_running && s.desired.nr_enabled;
        if (r.generation_mode==1) return afw ? (nr ? "Generating FSR + AFW + Neural" : "Generating FSR + AFW")
            : (nr ? "Generating FSR + Neural" : "Generating FSR");
        if (r.generation_mode==2) return afw ? (nr ? "Generating DLSS-G + AFW + Neural" : "Generating DLSS-G + AFW")
            : (nr ? "Generating DLSS-G + Neural" : "Generating DLSS-G");
        return afw ? (nr ? "Generating OFXR + AFW + Neural" : "Generating OFXR + AFW")
            : (nr ? "Generating OFXR + Neural" : "Generating OFXR");
    }
    return r.ready ? "Real frames only" : "Waiting";
}
std::string_view swapchain_budget_warning(const SwapchainBudgetStatus& value) noexcept {
    if (value.fallback) return "Swapchain fallback: auxiliary allocation refused; synchronous Luke delivery is active until the game restarts.";
    if (value.refused) return "Swapchain allocation refused: one or more game outputs could not obtain generation resources.";
    if (value.other) return "Unclassified app swapchain: not observed in an eye or auxiliary layer. See handle and dimensions below.";
    return {};
}
std::string interface_status_text(const Snapshot& s) {
    if (!s.connected) return "No active game";
    std::string result;
    const auto add = [&](std::string_view text) {
        if (text.empty() || result.find(text) != std::string::npos) return;
        if (!result.empty()) result += " | "; result += text;
    };
    const auto& r = s.runtime;
    switch (r.generation_state) {
    case GenerationState::missing_motion: add("Missing game vectors"); break;
    case GenerationState::missing_depth: add("Missing depth"); break;
    case GenerationState::invalid_guides: add("Invalid vectors / guide identity"); break;
    case GenerationState::invalid_stereo: add("Missing stereo pair"); break;
    case GenerationState::temporal_mismatch: add("Frame mismatch / missing history pair"); break;
    case GenerationState::output_mismatch: add("Game output does not match the eye image"); break;
    case GenerationState::queue_mismatch: add("Queue mismatch"); break;
    case GenerationState::unavailable: add("DLSS-G provider unavailable"); break;
    case GenerationState::runtime_missing: add("FSR provider missing"); break;
    case GenerationState::initialization_failed: add("Provider initialization failed"); break;
    case GenerationState::dispatch_failed: add(r.generation_mode == 2 ? "NGX rejected generation" : r.generation_mode == 1 ? "FSR rejected generation" : "OFXR rejected generation"); break;
    case GenerationState::native_declined: add("NGX refused interpolation"); break;
    case GenerationState::multi_frame_unsupported: add("Native 3X unsupported: using 2X"); break;
    case GenerationState::recovering: add("Priming fresh stereo history"); break;
    case GenerationState::busy: add("Capture / GPU output busy"); break;
    default: break;
    }
    add(std::string_view(r.message, std::find(std::begin(r.message), std::end(r.message), '\0') - std::begin(r.message)));
    if (r.provider_error) {
        char code[48]{};
        std::snprintf(code, sizeof(code), "NGX result 0x%08X", r.provider_error);
        add(code);
    }
    if (r.swapchains.fallback) add("Swapchain fallback");
    else if (r.swapchains.refused) add("Swapchain allocation refused");
    if (r.swapchains.other) add("Unclassified app swapchain");
    if (r.overlay_limit_mask) add("Overlay swapchain limit reached");
    if (r.luke_detected) add("Luke Ross detected");
    if (r.vulkan_detected) add("Vulkan detected");
    if (r.vulkan_capture_suspended) add((r.generation_mode == 0 || r.generation_mode == 3)
        ? "Vulkan game-vector capture suspended: OFXR uses optical flow"
        : "Vulkan game-vector capture suspended: FSR / DLSS-G real-frame bypass");
    const auto now=GetTickCount64();
    const bool generating=r.telemetry.updated_ms && now>=r.telemetry.updated_ms &&
        now-r.telemetry.updated_ms<2000 && r.telemetry.generating;
    const bool runtime_fresh=r.heartbeat_ms && now>=r.heartbeat_ms && now-r.heartbeat_ms<2000;
    const auto generator=r.generation_mode==1 ? "FSR" : r.generation_mode==2 ? "DLSS-G" : "OFXR";
    add(std::string(generator)+(s.desired.enabled ? (generating ? ": Active" : ": Waiting") : ": Off"));
    add(!r.luke_detected ? "AFW: N/A" : s.desired.afw_mode==0 ? "AFW: Off" :
        runtime_fresh && r.afw_running ? "AFW: Active" : "AFW: Waiting");
    add(!s.desired.nr_enabled ? "Neural Render: Off" : runtime_fresh && r.nr_running ? "Neural Render: Active" : "Neural Render: Waiting");
    return result;
}
} // namespace xrfg::autonomous
