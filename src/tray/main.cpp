#include "xrfg/implicit_layer.hpp"
#include "xrfg/live_tuning.hpp"
#include "xrfg/status_block.hpp"
#include "xrfg/standalone_launcher.hpp"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(linker,                                                     \
    "\"/manifestdependency:type='win32' "                                  \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "           \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "          \
    "language='*'\"")

namespace {

constexpr wchar_t kWindowClass[] = L"OFXRBridgeTrayWindow";
constexpr wchar_t kApplicationName[] = L"OFXR Bridge";
constexpr wchar_t kCleanupArgument[] = L"--cleanup-manual-arm";
constexpr wchar_t kArmAndExitArgument[] = L"--arm-and-exit";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTrayId = 1;
constexpr int kPauseHotkeyId = 1;
constexpr UINT kArmPollMilliseconds = 250;
// status.json is rewritten on this timer, and at once on every command, arm
// and exit (write_status).
constexpr UINT_PTR kStatusTimerId = 2;
constexpr UINT kStatusMilliseconds = 1000;
constexpr std::uint32_t kImplementationVersion = XRFG_IMPLEMENTATION_VERSION;
// One Donate entry each, creator first, as in the About box and the README.
constexpr wchar_t kDonateCreatorUrl[] = L"https://ko-fi.com/tig3rmast3r";
constexpr wchar_t kDonateMaintainerUrl[] = L"https://ko-fi.com/djules";
// The pause symbol beside the menu's "Resume frame generation", orange so a
// glance at the menu says generation is off. The FPS overlay's pause symbol
// is the same colour.
constexpr COLORREF kPausedColor = RGB(230, 115, 0);

enum MenuCommand : UINT {
    toggle_arm = 90,
    toggle_pause = 91,
    change_pause_key = 92,
    frame_gen_2x = 100,
    frame_gen_3x = 101,
    backend_fidelity_fx = 110,
    backend_nvidia_slow = 111,
    backend_nvidia_medium = 112,
    backend_nvidia_fast = 113,
    toggle_nvidia_bidirectional = 114,
    show_live_tuning = 128,
    nvidia_scale_full = 115,
    nvidia_scale_three_quarter = 116,
    nvidia_scale_half = 117,
    nvidia_scale_quarter = 129,
    toggle_deep_pipeline = 118,
    toggle_lower_vram = 126,
    toggle_hybrid = 141,
    toggle_hybrid_depth_search = 142,
    reset_defaults = 127,
    toggle_diagnostics = 120,
    overlay_off = 121,
    overlay_upper_left = 122,
    overlay_upper_right = 123,
    overlay_lower_left = 124,
    overlay_lower_right = 125,
    open_logs = 130,
    overlay_size_big = 131,
    overlay_size_medium = 132,
    overlay_size_small = 133,
    toggle_overlay_latency = 134,
    toggle_overlay_hybrid = 135,
    donate_creator = 138,
    donate_maintainer = 139,
    show_about = 140,
    exit_application = 150,
};

struct AppState {
    HWND window{};
    NOTIFYICONDATAW icon{};
    xrfg::standalone::LauncherSettings settings;
    std::filesystem::path executable_directory;
    std::filesystem::path local_directory;
    std::filesystem::path settings_path;
    std::filesystem::path armed_manifest;
    // The Vulkan layer's manifest, registered beside the OpenXR one while
    // Vulkan support is on; empty otherwise.
    std::filesystem::path armed_vulkan_manifest;
    xrfg::implicit_layer::RegistryScope armed_scope{
        xrfg::implicit_layer::RegistryScope::current_user};
    HANDLE arm_signal{};
    // Set while "Pause frame generation" is on. Made with the arm and closed
    // with it, so every arm starts resumed; null if it could not be made.
    HANDLE pause_signal{};
    bool paused{};
    // The `pause_hotkey` of tray.ini is held system-wide, while armed only.
    bool pause_hotkey_registered{};
    // Alternate namespace used by lifecycle tests, never read from user INI.
    std::wstring registry_subkey{xrfg::implicit_layer::kRegistrySubkey};
    HICON armed_icon{};
    HICON disarmed_icon{};
    UINT taskbar_created_message{};
    bool armed{};
    // The live tuning window for the hybrid composition: hidden rather than
    // destroyed on close so it keeps its position, and the shared block it
    // writes (xrfg::live_tuning). The block lives as long as the tray, so
    // values set once hold for every game started after them.
    HWND tuning_window{};
    std::array<HWND, 14> tuning_sliders{};
    std::array<HWND, 14> tuning_labels{};
    HANDLE tuning_mapping{};
    xrfg::live_tuning::Block* tuning_block{};
};

constexpr wchar_t kTuningWindowClass[] = L"OFXRBridgeLiveTuning";

[[nodiscard]] std::wstring last_error_message(std::wstring_view action) {
    const DWORD code = GetLastError();
    wchar_t* system_message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&system_message),
        0,
        nullptr);
    std::wstring output(action);
    output += L" failed (" + std::to_wstring(code) + L")";
    if (length > 0 && system_message != nullptr) {
        output += L": ";
        output.append(system_message, length);
        LocalFree(system_message);
    }
    return output;
}

[[nodiscard]] std::filesystem::path executable_directory() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(path.data()).parent_path();
}

[[nodiscard]] std::filesystem::path local_app_data() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value))) {
        throw std::runtime_error("SHGetKnownFolderPath failed");
    }
    const std::filesystem::path output(value);
    CoTaskMemFree(value);
    return output;
}

[[nodiscard]] std::filesystem::path runtime_directory(
    const std::filesystem::path& local_directory) {
    return xrfg::standalone::runtime_version_directory(
        local_directory,
        kImplementationVersion);
}

[[nodiscard]] bool write_text_atomic(
    const std::filesystem::path& path,
    std::string_view text,
    std::wstring* error) {
    try {
        std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path temporary = path.wstring() + L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream) {
                if (error) *error = L"Unable to write " + temporary.wstring();
                return false;
            }
        }
        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            if (error) *error = last_error_message(L"Saving " + path.wstring());
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        if (error) *error = L"Unable to save " + path.wstring();
        return false;
    }
}

void load_settings(AppState& state) {
    std::ifstream stream(state.settings_path, std::ios::binary);
    if (!stream) {
        return;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    state.settings = xrfg::standalone::parse_settings(text.str());
}

// The running game's status block as its layer last published it, when a
// session runs (xrfg::status): copied under its sequence counter; a game
// that stopped publishing without its xrDestroySession (a crash) counts as
// gone after three seconds.
std::optional<xrfg::status::Block> live_session_block() {
    std::optional<xrfg::status::Block> result;
    SYSTEMTIME now{};
    GetSystemTime(&now);
    FILETIME file_now{};
    SystemTimeToFileTime(&now, &file_now);
    const std::uint64_t now_ms =
        ((static_cast<std::uint64_t>(file_now.dwHighDateTime) << 32U) |
         file_now.dwLowDateTime) / 10'000ULL - 11'644'473'600'000ULL;
    if (const HANDLE mapping = OpenFileMappingW(
            FILE_MAP_READ, FALSE, xrfg::status::kMappingName)) {
        if (const auto* block = static_cast<const xrfg::status::Block*>(
                MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(xrfg::status::Block)))) {
            xrfg::status::Block copy{};
            for (int attempt = 0; attempt < 4; ++attempt) {
                const auto* sequence =
                    reinterpret_cast<const volatile LONG*>(&block->sequence);
                const LONG before = *sequence;
                MemoryBarrier();
                std::memcpy(&copy, block, sizeof(copy));
                MemoryBarrier();
                if ((before & 1) == 0 && before == *sequence) break;
            }
            if (copy.magic == xrfg::status::kMagic &&
                copy.version == xrfg::status::kVersion && copy.active != 0U &&
                now_ms >= copy.updated_unix_ms &&
                now_ms - copy.updated_unix_ms <= 3'000ULL) {
                copy.process[sizeof(copy.process) - 1] = '\0';
                copy.runtime[sizeof(copy.runtime) - 1] = '\0';
                result = copy;
            }
            UnmapViewOfFile(block);
        }
        CloseHandle(mapping);
    }
    return result;
}

// V529: a game with Luke Ross's R.E.A.L. VR runs (status block flags bit 0).
[[nodiscard]] bool realvr_session_running() {
    const auto block = live_session_block();
    return block && (block->flags & 1U) != 0U;
}

// status.json beside tray.ini (docs/STATUS_JSON.md): the tray's state, the
// running game as its layer last published it (xrfg::status), the settings.
// Written to a temporary file and moved over the old one, so a reader never
// sees half a file; a reader holding it open only delays the next write.
void write_status(const AppState& state) {
    try {
        xrfg::standalone::StatusInputs inputs;
        inputs.armed = state.armed;
        inputs.paused = state.paused;
        inputs.build = kImplementationVersion;
        inputs.settings = state.settings;
        SYSTEMTIME now{};
        GetSystemTime(&now);
        char updated[40]{};
        std::snprintf(updated, sizeof(updated), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            now.wMilliseconds);
        inputs.updated = updated;
        if (const auto copy = live_session_block()) {
            inputs.session_present = true;
            inputs.session.pid = copy->pid;
            inputs.session.process = copy->process;
            inputs.session.runtime = copy->runtime;
            inputs.session.multiplier = copy->multiplier;
            inputs.session.deep_pipeline = copy->deep_pipeline != 0U;
            inputs.session.generating = copy->generating != 0U;
            inputs.session.app_fps = copy->app_fps;
            inputs.session.output_fps = copy->output_fps;
            inputs.session.display_hz = copy->display_hz;
        }
        const std::string json = xrfg::standalone::build_status_json(inputs);
        const auto target = state.local_directory / L"status.json";
        auto temporary = target;
        temporary += L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return;
            file.write(json.data(), static_cast<std::streamsize>(json.size()));
            if (!file) return;
        }
        static_cast<void>(MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) {
    }
}

void save_settings(const AppState& state) {
    std::wstring ignored;
    static_cast<void>(write_text_atomic(
        state.settings_path,
        xrfg::standalone::serialize_settings(state.settings),
        &ignored));
}

void show_error(HWND owner, const std::wstring& error) {
    MessageBoxW(owner, error.c_str(), kApplicationName, MB_OK | MB_ICONERROR);
}

void remove_manifest_file(const std::filesystem::path& manifest) {
    std::error_code ignored;
    std::filesystem::remove(manifest, ignored);
}

void log_lifecycle(const std::filesystem::path& local_directory,
                   std::wstring_view action, std::wstring_view error = {}) noexcept {
    try {
        std::filesystem::create_directories(local_directory);
        const auto path = local_directory / L"tray-lifecycle.log";
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        std::wofstream stream(path, !size_error && size > 262144
            ? std::ios::trunc : std::ios::app);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        stream << time.wYear << L'-' << time.wMonth << L'-' << time.wDay << L' '
               << time.wHour << L':' << time.wMinute << L':' << time.wSecond
               << L" pid=" << GetCurrentProcessId() << L" V" << kImplementationVersion
               << L' ' << action << (error.empty() ? L" OK" : L" FAILED: ") << error << L'\n';
    } catch (...) { /* Cleanup must not depend on diagnostic I/O. */ }
}

// The newest ofxr_bridge.ini under any other version's cache folder.
//
// The two diagnostics knobs are carried forward from the file arming is about
// to overwrite, which keeps a hand edit across an arm - but a version bump
// creates an empty folder, so the lookup finds nothing and silently falls back
// to the defaults. A capture setting deliberately raised for an investigation
// was then reset by the next build, and the resulting logs were truncated
// without anything saying so.
[[nodiscard]] std::filesystem::path previous_runtime_configuration(
    const std::filesystem::path& local_directory,
    const std::filesystem::path& destination) {
    std::error_code code;
    const auto root = xrfg::standalone::runtime_version_directory(
        local_directory,
        kImplementationVersion).parent_path();
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_at{};
    for (const auto& entry :
         std::filesystem::directory_iterator(root, code)) {
        if (code || !entry.is_directory(code)) {
            continue;
        }
        const auto candidate = entry.path() / L"ofxr_bridge.ini";
        if (candidate == destination ||
            !std::filesystem::exists(candidate, code)) {
            continue;
        }
        const auto at = std::filesystem::last_write_time(candidate, code);
        if (code) {
            continue;
        }
        if (newest.empty() || at > newest_at) {
            newest = candidate;
            newest_at = at;
        }
    }
    return newest;
}

[[nodiscard]] bool write_runtime_configuration(
    const AppState& state,
    std::wstring* error,
    const std::filesystem::path& arm_manifest = {}) {
    const auto& manifest = arm_manifest.empty() ? state.armed_manifest : arm_manifest;
    // Carry forward the two diagnostics knobs the tray has no UI for. Arming
    // rewrites this file whole, so hardcoding them here is what made a
    // hand-edited max_file_mb survive exactly until the next arm.
    const auto destination =
        runtime_directory(state.local_directory) / L"ofxr_bridge.ini";
    // This version's file if it has one, otherwise the newest any other version
    // left behind, so the knobs survive a version bump as well as an arm.
    std::error_code exists_code;
    const auto source = std::filesystem::exists(destination, exists_code)
        ? destination
        : previous_runtime_configuration(state.local_directory, destination);
    const unsigned max_file_mb = GetPrivateProfileIntW(
        L"diagnostics",
        L"max_file_mb",
        static_cast<INT>(xrfg::standalone::kDefaultMaxFileMb),
        source.empty() ? destination.c_str() : source.c_str());
    const bool flush_each_event = GetPrivateProfileIntW(
        L"diagnostics",
        L"flush_each_event",
        0,
        source.empty() ? destination.c_str() : source.c_str()) != 0;
    std::string configuration = xrfg::standalone::build_runtime_ini(
        state.settings, max_file_mb, flush_each_event);
    if (!manifest.empty()) {
        const auto control = xrfg::implicit_layer::arm_signal_name(manifest);
        std::string ascii_control;
        for (const wchar_t c : control) ascii_control.push_back(static_cast<char>(c));
        configuration.insert(std::string("[ofxr]\r\n").size(),
            "control_event=" + ascii_control + "\r\n");
    }
    return write_text_atomic(destination, configuration, error);
}

// `tray_control` false leaves the session unmanaged: no stop or pause event
// and no control_event in the ini, so the layer runs with no tray behind it.
[[nodiscard]] bool prepare_runtime_layer(
    AppState& state,
    std::filesystem::path* manifest,
    std::filesystem::path* vulkan_manifest,
    std::wstring* error,
    bool tray_control = true) {
    if (vulkan_manifest) vulkan_manifest->clear();
    try {
        const auto source_dll = state.executable_directory / L"ofxr" /
            L"XR_APILAYER_XRFrameBridge_diagnostic.dll";
        if (!std::filesystem::is_regular_file(source_dll)) {
            if (error) {
                *error = L"The bundled OFXR layer is missing:\r\n" +
                         source_dll.wstring();
            }
            return false;
        }
        const auto directory = runtime_directory(state.local_directory);
        std::filesystem::create_directories(directory);
        const auto runtime_dll = directory /
            L"XR_APILAYER_XRFrameBridge_diagnostic.dll";
        if (!xrfg::standalone::install_runtime_layer_dll(
                source_dll, runtime_dll)) {
            if (error) *error = last_error_message(L"Installing the runtime layer");
            return false;
        }

        static std::uint64_t last_arm_id = 0;
        last_arm_id = std::max<std::uint64_t>(last_arm_id + 1, GetTickCount64());
        const std::wstring manifest_name =
            std::wstring(xrfg::implicit_layer::kManifestPrefix) +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(last_arm_id) +
            xrfg::implicit_layer::kManifestSuffix;
        const auto generated_manifest = directory / manifest_name;
        *manifest = generated_manifest;
        if (tray_control) {
            state.arm_signal = xrfg::implicit_layer::create_arm_signal(generated_manifest, error);
            if (!state.arm_signal) return false;
            // Not worth refusing the arm over: without it the menu entry is grey.
            std::wstring pause_error;
            state.pause_signal = xrfg::implicit_layer::create_pause_signal(
                generated_manifest, &pause_error);
            state.paused = false;
            if (!state.pause_signal)
                log_lifecycle(state.local_directory, L"pause-control", pause_error);
        }
        if (!write_text_atomic(
                generated_manifest,
                xrfg::standalone::build_implicit_layer_manifest(
                    runtime_dll, kImplementationVersion),
                error) ||
            !write_runtime_configuration(state, error,
                tray_control ? generated_manifest : std::filesystem::path{})) {
            remove_manifest_file(generated_manifest);
            return false;
        }
        *manifest = generated_manifest;
        if (state.settings.vulkan_support && vulkan_manifest) {
            // The queue-serialising Vulkan layer goes beside the OpenXR one,
            // in the same version folder, with a manifest of the same shape.
            const auto source_vulkan_dll = state.executable_directory / L"ofxr" /
                xrfg::implicit_layer::kVulkanLayerDll;
            if (!std::filesystem::is_regular_file(source_vulkan_dll)) {
                if (error) {
                    *error = L"The bundled OFXR Vulkan layer is missing:\r\n" +
                             source_vulkan_dll.wstring();
                }
                remove_manifest_file(generated_manifest);
                return false;
            }
            const auto runtime_vulkan_dll =
                directory / xrfg::implicit_layer::kVulkanLayerDll;
            if (!xrfg::standalone::install_runtime_layer_dll(
                    source_vulkan_dll, runtime_vulkan_dll)) {
                if (error) *error = last_error_message(L"Installing the Vulkan layer");
                remove_manifest_file(generated_manifest);
                return false;
            }
            const std::wstring vulkan_manifest_name =
                std::wstring(xrfg::implicit_layer::kVulkanManifestPrefix) +
                std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(last_arm_id) +
                xrfg::implicit_layer::kManifestSuffix;
            const auto generated_vulkan_manifest = directory / vulkan_manifest_name;
            if (!write_text_atomic(
                    generated_vulkan_manifest,
                    xrfg::standalone::build_vulkan_layer_manifest(
                        runtime_vulkan_dll, kImplementationVersion),
                    error)) {
                remove_manifest_file(generated_manifest);
                return false;
            }
            *vulkan_manifest = generated_vulkan_manifest;
        }
        return true;
    } catch (...) {
        if (error) *error = L"Unable to prepare the manual OpenXR layer.";
        return false;
    }
}

[[nodiscard]] bool spawn_cleanup_helper(
    const AppState& state,
    const std::filesystem::path& manifest,
    xrfg::implicit_layer::RegistryScope scope,
    std::wstring* error,
    const std::filesystem::path& vulkan_manifest = {}) {
    std::wstring command = xrfg::standalone::quote_windows_argument(
        (state.executable_directory / L"OFXRBridgeTray.exe").wstring());
    command += L" ";
    command += kCleanupArgument;
    command += L" ";
    command += xrfg::standalone::quote_windows_argument(manifest.wstring());
    command += L" ";
    command += std::to_wstring(GetCurrentProcessId());
    command += L" ";
    command += xrfg::implicit_layer::registry_scope_name(scope);
    if (!vulkan_manifest.empty()) {
        command += L" ";
        command += xrfg::standalone::quote_windows_argument(vulkan_manifest.wstring());
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto tray_executable =
        state.executable_directory / L"OFXRBridgeTray.exe";
    if (!CreateProcessW(
            tray_executable.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            state.executable_directory.c_str(),
            &startup,
            &process)) {
        if (error) *error = last_error_message(L"Starting the cleanup watchdog");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

[[nodiscard]] bool cleanup_all_owned_registrations(
    const AppState& state,
    std::wstring* error) {
    if (error) error->clear();
    bool success = true;
    for (const auto scope : {
             xrfg::implicit_layer::RegistryScope::current_user,
             xrfg::implicit_layer::RegistryScope::local_machine}) {
        std::wstring detail;
        if (!xrfg::implicit_layer::cleanup_owned_registrations(
                state.local_directory, scope, &detail, state.registry_subkey)) {
            success = false;
            if (error) {
                if (!error->empty()) *error += L"\r\n";
                *error += xrfg::implicit_layer::registry_scope_name(scope);
                *error += L": ";
                *error += detail;
            }
        }
        // The Vulkan layer's registrations, in the loader's own key.
        std::wstring vulkan_detail;
        if (!xrfg::implicit_layer::cleanup_owned_registrations(
                state.local_directory, scope, &vulkan_detail,
                xrfg::implicit_layer::kVulkanRegistrySubkey,
                xrfg::implicit_layer::kVulkanManifestPrefix)) {
            success = false;
            if (error) {
                if (!error->empty()) *error += L"\r\n";
                *error += xrfg::implicit_layer::registry_scope_name(scope);
                *error += L" (Vulkan): ";
                *error += vulkan_detail;
            }
        }
    }
    return success;
}

[[nodiscard]] std::wstring tray_tooltip(const AppState& state) {
    std::wstring tooltip = state.paused
        ? L"OFXR Bridge PAUSED - "
        : state.armed ? L"OFXR Bridge ARMED - " : L"OFXR Bridge - ";
    if (state.settings.backend == xrfg::standalone::FlowBackend::nvidia) {
        switch (state.settings.nvidia_preset) {
        case xrfg::standalone::NvidiaPerformancePreset::fast:
            tooltip += L"NVIDIA Fast (test)";
            break;
        case xrfg::standalone::NvidiaPerformancePreset::slow:
            tooltip += L"NVIDIA Slow";
            break;
        case xrfg::standalone::NvidiaPerformancePreset::medium:
        default:
            tooltip += L"NVIDIA Medium";
            break;
        }
        tooltip += state.settings.nvidia_bidirectional
            ? L" + bidirectional"
            : L" + forward";
        switch (state.settings.nvidia_input_scale) {
        case xrfg::standalone::NvidiaInputScale::three_quarter:
            tooltip += L" @ 75%";
            break;
        case xrfg::standalone::NvidiaInputScale::half:
            tooltip += L" @ 50%";
            break;
        case xrfg::standalone::NvidiaInputScale::quarter:
            tooltip += L" @ 25%";
            break;
        case xrfg::standalone::NvidiaInputScale::full:
        default:
            tooltip += L" @ 100%";
            break;
        }
    } else {
        tooltip += L"FidelityFX";
    }
    if (state.settings.triple_frame_gen) {
        tooltip += L" - 3X";
    } else if (state.settings.deep_pipeline) {
        tooltip += L" - prefer FPS";
    }
    if (state.settings.hybrid) {
        tooltip += L" - hybrid";
    }
    if (state.settings.vulkan_support) {
        tooltip += L" - Vulkan";
    }
    if (state.settings.diagnostics) {
        tooltip += L" - recorder on";
    }
    return tooltip;
}

void refresh_tray_icon(AppState& state) {
    const std::wstring tooltip = tray_tooltip(state);
    wcsncpy_s(state.icon.szTip, tooltip.c_str(), _TRUNCATE);
    state.icon.hIcon = state.armed ? state.armed_icon : state.disarmed_icon;
    state.icon.uFlags = NIF_ICON | NIF_TIP;
    static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &state.icon));
}

void show_balloon(
    AppState& state,
    std::wstring_view title,
    std::wstring_view message,
    DWORD flags = NIIF_INFO) {
    wcsncpy_s(state.icon.szInfoTitle, std::wstring(title).c_str(), _TRUNCATE);
    wcsncpy_s(state.icon.szInfo, std::wstring(message).c_str(), _TRUNCATE);
    state.icon.dwInfoFlags = flags;
    state.icon.uFlags = NIF_INFO;
    static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &state.icon));
}

[[nodiscard]] bool add_tray_icon(AppState& state) {
    state.icon = {};
    state.icon.cbSize = sizeof(state.icon);
    state.icon.hWnd = state.window;
    state.icon.uID = kTrayId;
    state.icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    state.icon.uCallbackMessage = kTrayMessage;
    state.icon.hIcon = state.armed ? state.armed_icon : state.disarmed_icon;
    const std::wstring tooltip = tray_tooltip(state);
    wcsncpy_s(state.icon.szTip, tooltip.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &state.icon) != FALSE;
}

// Takes tray.ini's `pause_hotkey` for as long as the bridge is armed. The key
// is the tray's and not the layer's: nothing is added to the game, and the
// same chord works in every title. A key another program already holds is
// left to it; the menu entry still works.
void register_pause_hotkey(AppState& state) {
    if (state.pause_hotkey_registered || state.window == nullptr ||
        state.pause_signal == nullptr) return;
    const auto hotkey = xrfg::standalone::parse_hotkey(state.settings.pause_hotkey);
    if (!hotkey) return;
    state.pause_hotkey_registered = RegisterHotKey(
        state.window, kPauseHotkeyId, hotkey->modifiers | MOD_NOREPEAT,
        hotkey->key) != FALSE;
    if (!state.pause_hotkey_registered)
        log_lifecycle(state.local_directory, L"pause-hotkey",
            last_error_message(L"Registering the pause key"));
}

void unregister_pause_hotkey(AppState& state) {
    if (!state.pause_hotkey_registered) return;
    UnregisterHotKey(state.window, kPauseHotkeyId);
    state.pause_hotkey_registered = false;
}

// Ends the pause with the arm it belonged to. A running session keeps its own
// handle, and by now the arm signal has stopped it for good.
void close_pause_signal(AppState& state) {
    unregister_pause_hotkey(state);
    if (state.pause_signal) {
        CloseHandle(state.pause_signal);
        state.pause_signal = nullptr;
    }
    state.paused = false;
}

[[nodiscard]] bool disarm_bridge(
    AppState& state,
    std::wstring* error) {
    // Signal the active DLL BEFORE registry/file cleanup or window teardown.
    std::wstring signal_error;
    if (state.arm_signal && !SetEvent(state.arm_signal))
        signal_error = last_error_message(L"Stopping the loaded OFXR layer");
    std::wstring detail;
    const bool cleaned = cleanup_all_owned_registrations(state, &detail);
    if (!signal_error.empty()) {
        if (!detail.empty()) detail += L"\r\n";
        detail += signal_error;
    }
    if (!cleaned || !signal_error.empty()) {
        log_lifecycle(state.local_directory, L"disarm-all", detail);
        if (error) *error = detail;
        return false;
    }
    log_lifecycle(state.local_directory, L"disarm-all");
    if (state.arm_signal) {
        CloseHandle(state.arm_signal);
        state.arm_signal = nullptr;
    }
    close_pause_signal(state);
    state.armed = false;
    state.armed_manifest.clear();
    state.armed_vulkan_manifest.clear();
    state.armed_scope = xrfg::implicit_layer::RegistryScope::current_user;
    refresh_tray_icon(state);
    return true;
}

[[nodiscard]] bool arm_bridge(AppState& state, std::wstring* error) {
    // Reconcile disk/registry state, not just this tray process's memory.
    if (!disarm_bridge(state, error)) return false;

    std::filesystem::path manifest;
    std::filesystem::path vulkan_manifest;
    const auto scope = xrfg::implicit_layer::preferred_registry_scope();
    if (!prepare_runtime_layer(state, &manifest, &vulkan_manifest, error) ||
        !xrfg::implicit_layer::register_manifest(
            manifest, scope, error, state.registry_subkey) ||
        (!vulkan_manifest.empty() &&
         !xrfg::implicit_layer::register_manifest(
             vulkan_manifest, scope, error,
             xrfg::implicit_layer::kVulkanRegistrySubkey))) {
        if (!vulkan_manifest.empty()) {
            std::wstring cleanup_error;
            if (!xrfg::implicit_layer::retire_manifest(
                    vulkan_manifest, scope, &cleanup_error,
                    xrfg::implicit_layer::kVulkanRegistrySubkey))
                log_lifecycle(state.local_directory, L"arm-failure-cleanup", cleanup_error);
        }
        if (!manifest.empty()) {
            std::wstring cleanup_error;
            if (!xrfg::implicit_layer::retire_manifest(
                    manifest, scope, &cleanup_error, state.registry_subkey))
                log_lifecycle(state.local_directory, L"arm-failure-cleanup", cleanup_error);
        }
        if (state.arm_signal) {
            SetEvent(state.arm_signal);
            CloseHandle(state.arm_signal);
            state.arm_signal = nullptr;
        }
        close_pause_signal(state);
        return false;
    }
    if (!spawn_cleanup_helper(state, manifest, scope, error, vulkan_manifest)) {
        std::wstring ignored;
        if (!vulkan_manifest.empty()) {
            static_cast<void>(xrfg::implicit_layer::retire_manifest(
                vulkan_manifest, scope, &ignored,
                xrfg::implicit_layer::kVulkanRegistrySubkey));
        }
        static_cast<void>(xrfg::implicit_layer::retire_manifest(
            manifest, scope, &ignored, state.registry_subkey));
        if (state.arm_signal) {
            SetEvent(state.arm_signal);
            CloseHandle(state.arm_signal);
            state.arm_signal = nullptr;
        }
        close_pause_signal(state);
        return false;
    }
    state.armed = true;
    register_pause_hotkey(state);
    state.armed_manifest = manifest;
    state.armed_scope = scope;
    log_lifecycle(state.local_directory,
        scope == xrfg::implicit_layer::RegistryScope::local_machine
            ? L"arm-hklm"
            : L"arm-hkcu");
    refresh_tray_icon(state);
    show_balloon(
        state,
        L"OFXR Bridge armed",
        L"The bridge will remain enabled for OpenXR applications until you disarm it or exit the tray.");
    return true;
}

// --arm-and-exit: arm with the tray's settings and leave, keeping the
// registration. For Proton (issue #34), where the tray cannot run while the
// game starts: protontricks and Steam start separate wineservers for the
// prefix and the game deadlocks. Nothing watches the arm, so it lasts until
// the tray is next started, whose start-up cleanup removes it, and the
// session is unmanaged: no Disarm, pause or live options while it runs.
[[nodiscard]] bool arm_and_exit(
    AppState& state,
    std::wstring* error,
    xrfg::implicit_layer::RegistryScope scope =
        xrfg::implicit_layer::preferred_registry_scope()) {
    // Reconcile disk/registry state as arm_bridge does; there is no window
    // or loaded session of this process's to disarm.
    if (!cleanup_all_owned_registrations(state, error)) return false;
    std::filesystem::path manifest;
    std::filesystem::path vulkan_manifest;
    if (!prepare_runtime_layer(state, &manifest, &vulkan_manifest, error, false) ||
        !xrfg::implicit_layer::register_manifest(
            manifest, scope, error, state.registry_subkey) ||
        (!vulkan_manifest.empty() &&
         !xrfg::implicit_layer::register_manifest(
             vulkan_manifest, scope, error,
             xrfg::implicit_layer::kVulkanRegistrySubkey))) {
        std::wstring ignored;
        if (!vulkan_manifest.empty()) {
            static_cast<void>(xrfg::implicit_layer::retire_manifest(
                vulkan_manifest, scope, &ignored,
                xrfg::implicit_layer::kVulkanRegistrySubkey));
        }
        if (!manifest.empty()) {
            static_cast<void>(xrfg::implicit_layer::retire_manifest(
                manifest, scope, &ignored, state.registry_subkey));
        }
        return false;
    }
    log_lifecycle(state.local_directory,
        scope == xrfg::implicit_layer::RegistryScope::local_machine
            ? L"arm-and-exit-hklm"
            : L"arm-and-exit-hkcu");
    return true;
}

// `running_message`: what the balloon says for an option a running game
// follows; without one the option is for the next session.
void update_runtime_options(
    AppState& state, const wchar_t* running_message = nullptr) {
    save_settings(state);
    if (state.armed) {
        std::wstring error;
        if (!write_runtime_configuration(state, &error)) {
            show_error(state.window, error);
        }
    }
    refresh_tray_icon(state);
    if (state.armed) {
        show_balloon(
            state,
            L"OFXR options saved",
            running_message != nullptr
                ? running_message
                : L"A running game switches to the new optical-flow settings within a "
                  L"second, after a short pause in generated frames.");
    }
}

// The pause symbol shown beside "Resume frame generation": two orange bars on
// a transparent square the size of a check mark. A 32-bit premultiplied
// bitmap set as the entry's item bitmap is the one way to colour an entry
// that leaves the popup in the system's own menu style; an owner-drawn entry
// puts the whole menu in the flat unthemed one.
[[nodiscard]] HBITMAP create_pause_bitmap() {
    const int size = std::max(GetSystemMetrics(SM_CXMENUCHECK), 8);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) return bitmap;
    auto* pixels = static_cast<std::uint32_t*>(bits);
    std::fill_n(pixels, static_cast<std::size_t>(size) * size, 0U);
    const std::uint32_t ink = 0xff000000U |
        (static_cast<std::uint32_t>(GetRValue(kPausedColor)) << 16) |
        (static_cast<std::uint32_t>(GetGValue(kPausedColor)) << 8) |
        GetBValue(kPausedColor);
    const int bar = std::max(size / 5, 2);
    const int gap = std::max(size / 5, 2);
    const int left = (size - 2 * bar - gap) / 2;
    const int top = size / 5;
    for (int y = top; y < size - top; ++y) {
        for (int x = 0; x < bar; ++x) {
            pixels[y * size + left + x] = ink;
            pixels[y * size + left + bar + gap + x] = ink;
        }
    }
    return bitmap;
}

// What the "Current key binding" entry and the dialog call a setting.
[[nodiscard]] std::wstring pause_key_display(std::string_view setting) {
    const std::string name = xrfg::standalone::hotkey_display_name(setting);
    if (name.empty()) return L"none";
    return std::wstring(name.begin(), name.end());
}

// The dialog's own state: the setting it opened on and the one it closed on.
struct PauseKeyDialog {
    std::string setting;
};

// The hotkey control's value for a chord, and back. Its modifier flags are
// not RegisterHotKey's, and it marks the navigation keys as extended.
[[nodiscard]] WORD hotkey_control_value(const xrfg::standalone::Hotkey& hotkey) {
    BYTE flags = 0;
    if (hotkey.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    if (hotkey.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (hotkey.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    switch (hotkey.key) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
    case VK_DIVIDE: case VK_NUMLOCK:
        flags |= HOTKEYF_EXT;
        break;
    default:
        break;
    }
    return MAKEWORD(static_cast<BYTE>(hotkey.key), flags);
}

[[nodiscard]] xrfg::standalone::Hotkey hotkey_from_control(WORD value) {
    xrfg::standalone::Hotkey hotkey;
    hotkey.key = LOBYTE(value);
    const BYTE flags = HIBYTE(value);
    if (flags & HOTKEYF_SHIFT) hotkey.modifiers |= MOD_SHIFT;
    if (flags & HOTKEYF_CONTROL) hotkey.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) hotkey.modifiers |= MOD_ALT;
    return hotkey;
}

INT_PTR CALLBACK pause_key_dialog_procedure(
    HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    constexpr int kAvailabilityProbeId = 0x7F01;
    const HWND box = GetDlgItem(dialog, IDC_PAUSE_KEY_HOTKEY);
    const auto show = [&](const std::string& setting) {
        const auto hotkey = xrfg::standalone::parse_hotkey(setting);
        SendMessageW(box, HKM_SETHOTKEY, hotkey ? hotkey_control_value(*hotkey) : 0, 0);
    };
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        const auto* state = reinterpret_cast<const PauseKeyDialog*>(lparam);
        // No rules on the box: any key, with or without modifiers, is the
        // user's to choose. The dialog's text says what a bare key costs.
        show(state->setting);
        SetDlgItemTextW(dialog, IDC_PAUSE_KEY_STATUS,
            (L"Current key binding: " + pause_key_display(state->setting)).c_str());
        SetForegroundWindow(dialog);
        SetFocus(box);
        return FALSE; // the focus is set
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_PAUSE_KEY_DEFAULT:
            show(xrfg::standalone::kDefaultPauseHotkey);
            SetFocus(box);
            return TRUE;
        case IDC_PAUSE_KEY_NONE:
            SendMessageW(box, HKM_SETHOTKEY, 0, 0);
            SetFocus(box);
            return TRUE;
        case IDOK: {
            auto* state = reinterpret_cast<PauseKeyDialog*>(
                GetWindowLongPtrW(dialog, DWLP_USER));
            const WORD value = LOWORD(SendMessageW(box, HKM_GETHOTKEY, 0, 0));
            if (LOBYTE(value) == 0) {
                state->setting = "off";
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            const auto hotkey = hotkey_from_control(value);
            const auto setting = xrfg::standalone::hotkey_setting(hotkey);
            if (!setting) {
                SetDlgItemTextW(dialog, IDC_PAUSE_KEY_STATUS,
                    L"That key cannot be used. Choose a different one.");
                SetFocus(box);
                return TRUE;
            }
            // Asked of the system now, while the dialog can still say so:
            // the tray's own registration is released for as long as this
            // dialog is open, so a refusal is another program's.
            if (!RegisterHotKey(dialog, kAvailabilityProbeId,
                    hotkey.modifiers | MOD_NOREPEAT, hotkey.key)) {
                SetDlgItemTextW(dialog, IDC_PAUSE_KEY_STATUS,
                    L"Another program already uses that combination. "
                    L"Choose a different one.");
                SetFocus(box);
                return TRUE;
            }
            UnregisterHotKey(dialog, kAvailabilityProbeId);
            state->setting = *setting;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    default:
        return FALSE;
    }
}

// Stores a new key and takes it at once if the bridge is armed.
void apply_pause_hotkey(AppState& state, const std::string& setting) {
    unregister_pause_hotkey(state);
    state.settings.pause_hotkey = setting;
    save_settings(state);
    if (state.armed) register_pause_hotkey(state);
    log_lifecycle(state.local_directory, L"pause-hotkey-changed");
}

void write_tuning(AppState& state);
void reset_tuning_controls(AppState& state);

// Everything tray.ini holds goes back to LauncherSettings' defaults, the
// keys with no menu entry and the pause key included, and so do the flight
// recorder's two knobs that only the layer's ini holds, and the live tuning
// window if it was opened.
void reset_to_factory_settings(AppState& state) {
    unregister_pause_hotkey(state);
    // expert_mode is a hand-set key for testers; a reset leaves it as it is.
    const bool expert_mode = state.settings.expert_mode;
    state.settings = xrfg::standalone::LauncherSettings{};
    state.settings.expert_mode = expert_mode;
    // write_runtime_configuration carries max_file_mb and flush_each_event
    // forward from this version's ini, now or at the next arm, so the
    // defaults are written there first.
    const auto runtime_ini =
        runtime_directory(state.local_directory) / L"ofxr_bridge.ini";
    std::error_code ignored;
    std::filesystem::create_directories(runtime_ini.parent_path(), ignored);
    WritePrivateProfileStringW(L"diagnostics", L"max_file_mb",
        std::to_wstring(xrfg::standalone::kDefaultMaxFileMb).c_str(),
        runtime_ini.c_str());
    WritePrivateProfileStringW(L"diagnostics", L"flush_each_event", L"0",
        runtime_ini.c_str());
    update_runtime_options(state);
    // Once opened, the window's block is what a running game follows instead
    // of the ini's switches, so its controls and the block go back too.
    if (state.tuning_window != nullptr) {
        reset_tuning_controls(state);
        write_tuning(state);
    }
    if (state.armed) register_pause_hotkey(state);
    log_lifecycle(state.local_directory, L"reset-to-default");
}

void change_pause_hotkey(AppState& state) {
    // Released while the dialog is open, or pressing the current chord in
    // the box would pause the game instead of reaching the box.
    unregister_pause_hotkey(state);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&controls);
    PauseKeyDialog dialog{state.settings.pause_hotkey};
    const INT_PTR answer = DialogBoxParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PAUSE_KEY), state.window,
        pause_key_dialog_procedure, reinterpret_cast<LPARAM>(&dialog));
    if (answer == IDOK) {
        apply_pause_hotkey(state, dialog.setting);
    } else if (state.armed) {
        register_pause_hotkey(state);
    }
}

// The live tuning window. Each slider maps its integer position to one field
// of xrfg::live_tuning::Block; the layer picks the values up on its next pair.
// It opens on the composition's built-in values (live_tuning::kDefaults), so
// nothing changes until a control moves. Every slider runs from cheaper on
// the GPU at the left to costlier at the right: a value that is cheaper when
// larger is `reversed`, its positions counted from the right.
struct TuningSlider {
    const wchar_t* name;
    int minimum;
    int maximum;
    std::uint32_t xrfg::live_tuning::Block::*field;
    std::uint32_t per_step;  // block units per slider step
    double display_scale;    // shown value per slider step
    const wchar_t* unit;
    const wchar_t* hint;
    // A heading drawn above this slider, opening a section of the window.
    const wchar_t* section;
    bool reversed;
};

constexpr std::array<TuningSlider, 14> kTuningSliders{{
    {L"Agree threshold", 0, 62,
     &xrfg::live_tuning::Block::hybrid_agree_threshold_255ths, 1U, 1.0, L"/255",
     L"the flow is asked only where the vectors' two frames disagree by more",
     L"Hybrid choice: the game's vectors against the optical flow", true},
    {L"Choice slope", 1, 15,
     &xrfg::live_tuning::Block::hybrid_choice_slope, 1U, 1.0, L"",
     L"1 soft blend, 15 hard pick of the better branch (no cost)", nullptr, false},
    {L"Vectors slope", 1, 15,
     &xrfg::live_tuning::Block::hybrid_vectors_slope, 1U, 1.0, L"",
     L"how fast a vectors mismatch falls back to the pose-only blend (no cost)",
     nullptr, false},
    {L"Flow rule slope", 1, 15,
     &xrfg::live_tuning::Block::hybrid_flow_rule_slope, 1U, 1.0, L"",
     L"how sharply the flow must beat the camera-only blend to be kept (no cost)",
     nullptr, false},
    {L"Edge reach", 100, 200,
     &xrfg::live_tuning::Block::edge_reach_percent, 1U, 0.01, L"x",
     L"edge probes and the surface search as a multiple of the motion",
     L"The vectors at motion edges (V498-V504)", false},
    {L"Edge surface solves", 0, 4,
     &xrfg::live_tuning::Block::edge_search_solves, 1U, 1.0, L"",
     L"second-surface solves per edge pixel; 0 none", nullptr, false},
    {L"Surface agreement", 0, 250,
     &xrfg::live_tuning::Block::edge_surface_agree_1000ths, 1U, 0.001, L"",
     L"a found surface's two frames must agree within this (no cost)", nullptr, false},
    {L"Revealed check", 0, 250,
     &xrfg::live_tuning::Block::revealed_error_1000ths, 1U, 0.001, L"",
     L"revealed background accepted within this; looser skips the flow more",
     nullptr, true},
    {L"Vector snap", 0, 40,
     &xrfg::live_tuning::Block::vector_snap_tenths, 1U, 0.1, L"px",
     L"nearest vector where the four texels differ by more; 0 always (no cost)",
     nullptr, false},
    {L"Near reach", 0, 16,
     &xrfg::live_tuning::Block::depth_near_reach_px, 2U, 2.0, L"px",
     L"the game's depth read every 2 px out to here, either side",
     L"Depth search for thin near surfaces (V505; on with Hybrid: depth search)", false},
    {L"Far reach", 0, 20,
     &xrfg::live_tuning::Block::depth_far_reach_px, 4U, 4.0, L"px",
     L"then every 4 px out to here", nullptr, false},
    {L"Depth solves", 1, 4,
     &xrfg::live_tuning::Block::depth_solves, 1U, 1.0, L"",
     L"solves per pixel from nearer starts", nullptr, false},
    {L"Nearer margin", 0, 50,
     &xrfg::live_tuning::Block::depth_nearer_percent, 1U, 1.0, L"%",
     L"how much nearer a start must be; smaller tries more", nullptr, true},
    {L"Depth agreement", 0, 250,
     &xrfg::live_tuning::Block::depth_agree_1000ths, 1U, 0.001, L"",
     L"a nearer surface's two frames must agree within this (no cost)", nullptr, false},
}};

// The depth search's on/off is the menu's "Hybrid: depth search" (V532);
// the block's field is left as the window found it and the layer ignores it.
static_assert(kTuningSliders.size() == std::tuple_size_v<decltype(AppState::tuning_sliders)>);
constexpr int kTuningResetId = 2120;

// A slider's position for a block value, and the value's steps for a
// position: a reversed slider counts from the right, so the costlier end is
// the right one for every slider.
[[nodiscard]] int tuning_position(const TuningSlider& slider, std::uint32_t value) {
    const int steps = static_cast<int>(value / slider.per_step);
    return slider.reversed ? slider.maximum + slider.minimum - steps : steps;
}

[[nodiscard]] int tuning_steps(const TuningSlider& slider, int position) {
    return slider.reversed ? slider.maximum + slider.minimum - position : position;
}

[[nodiscard]] std::wstring tuning_slider_text(const TuningSlider& slider, int steps) {
    wchar_t text[240]{};
    const double shown = static_cast<double>(steps) * slider.display_scale;
    if (shown == static_cast<double>(static_cast<int>(shown))) {
        swprintf_s(text, L"%s: %d%s  -  %s", slider.name, static_cast<int>(shown),
            slider.unit, slider.hint);
    } else if (slider.display_scale >= 0.01) {
        swprintf_s(text, L"%s: %.2f%s  -  %s", slider.name, shown, slider.unit,
            slider.hint);
    } else {
        swprintf_s(text, L"%s: %.3f%s  -  %s", slider.name, shown, slider.unit,
            slider.hint);
    }
    return text;
}

void write_tuning(AppState& state) {
    xrfg::live_tuning::Block* block = state.tuning_block;
    if (block == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < kTuningSliders.size(); ++index) {
        const TuningSlider& slider = kTuningSliders[index];
        const int position = static_cast<int>(
            SendMessageW(state.tuning_sliders[index], TBM_GETPOS, 0, 0));
        const int steps = tuning_steps(slider, position);
        block->*slider.field = static_cast<std::uint32_t>(steps) * slider.per_step;
        SetWindowTextW(state.tuning_labels[index],
            tuning_slider_text(slider, steps).c_str());
    }
    MemoryBarrier();
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&block->sequence));
}

void reset_tuning_controls(AppState& state) {
    constexpr xrfg::live_tuning::Block defaults = xrfg::live_tuning::kDefaults;
    for (std::size_t index = 0; index < kTuningSliders.size(); ++index) {
        const TuningSlider& slider = kTuningSliders[index];
        SendMessageW(state.tuning_sliders[index], TBM_SETPOS, TRUE,
            static_cast<LPARAM>(tuning_position(slider, defaults.*slider.field)));
    }
}

LRESULT CALLBACK tuning_procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    switch (message) {
    case WM_HSCROLL:
        if (state != nullptr) write_tuning(*state);
        return 0;
    case WM_COMMAND:
        if (state != nullptr && HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == kTuningResetId) {
                reset_tuning_controls(*state);
            }
            write_tuning(*state);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

// Creates the shared block the first time the window needs it, on the
// built-in values.
[[nodiscard]] bool ensure_tuning_block(AppState& state) {
    if (state.tuning_block != nullptr) {
        return true;
    }
    state.tuning_mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(xrfg::live_tuning::Block), xrfg::live_tuning::kMappingName);
    if (state.tuning_mapping != nullptr) {
        state.tuning_block = static_cast<xrfg::live_tuning::Block*>(
            MapViewOfFile(state.tuning_mapping, FILE_MAP_WRITE, 0, 0,
                sizeof(xrfg::live_tuning::Block)));
    }
    if (state.tuning_block == nullptr) {
        return false;
    }
    // Values first, then the header that makes the block valid.
    xrfg::live_tuning::Block values = xrfg::live_tuning::kDefaults;
    values.magic = 0;
    *state.tuning_block = values;
    MemoryBarrier();
    state.tuning_block->magic = xrfg::live_tuning::kMagic;
    return true;
}

void show_tuning_window(AppState& state) {
    if (!ensure_tuning_block(state)) {
        show_error(state.window, last_error_message(L"Creating the live tuning block"));
        return;
    }
    if (state.tuning_window == nullptr) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW tuning_class{};
        tuning_class.cbSize = sizeof(tuning_class);
        tuning_class.lpfnWndProc = tuning_procedure;
        tuning_class.hInstance = instance;
        tuning_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        tuning_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        tuning_class.lpszClassName = kTuningWindowClass;
        static_cast<void>(RegisterClassExW(&tuning_class));
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_BAR_CLASSES;
        static_cast<void>(InitCommonControlsEx(&controls));
        constexpr int kCaptionHeight = 24;
        constexpr int kRowHeight = 44;
        constexpr int kSectionHeight = 30;
        const int sliders_top = 12 + kCaptionHeight + 8;
        // Each slider's row, with a section heading's height above the
        // sliders that open one.
        std::array<int, kTuningSliders.size()> slider_tops{};
        int next_top = sliders_top;
        for (std::size_t index = 0; index < kTuningSliders.size(); ++index) {
            if (kTuningSliders[index].section != nullptr) {
                next_top += kSectionHeight;
            }
            slider_tops[index] = next_top;
            next_top += kRowHeight;
        }
        const int client_height = next_top + 44;
        RECT frame{0, 0, 600, client_height};
        AdjustWindowRectEx(&frame, WS_CAPTION | WS_SYSMENU, FALSE,
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        state.tuning_window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kTuningWindowClass,
            L"OFXR frame generation tuning (live)",
            WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT,
            frame.right - frame.left, frame.bottom - frame.top,
            nullptr, nullptr, instance, &state);
        if (state.tuning_window == nullptr) {
            show_error(state.window, last_error_message(L"Creating the tuning window"));
            return;
        }
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HWND caption = CreateWindowExW(
            0, L"STATIC",
            L"Every slider runs from cheaper on the GPU (left) to costlier (right).",
            WS_CHILD | WS_VISIBLE, 14, 12, 570, 18,
            state.tuning_window, nullptr, instance, nullptr);
        SendMessageW(caption, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (std::size_t index = 0; index < kTuningSliders.size(); ++index) {
            const int top = slider_tops[index];
            if (kTuningSliders[index].section != nullptr) {
                const HWND heading = CreateWindowExW(
                    0, L"STATIC", kTuningSliders[index].section, WS_CHILD | WS_VISIBLE,
                    14, top - kSectionHeight + 8, 570, 18,
                    state.tuning_window, nullptr, instance, nullptr);
                SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            state.tuning_labels[index] = CreateWindowExW(
                0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                14, top, 570, 18,
                state.tuning_window, nullptr, instance, nullptr);
            SendMessageW(state.tuning_labels[index], WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
            state.tuning_sliders[index] = CreateWindowExW(
                0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                8, top + 18, 580, 24,
                state.tuning_window, nullptr, instance, nullptr);
            const TuningSlider& slider = kTuningSliders[index];
            const bool fine = slider.maximum - slider.minimum > 16;
            SendMessageW(state.tuning_sliders[index], TBM_SETRANGE, TRUE,
                MAKELPARAM(slider.minimum, slider.maximum));
            SendMessageW(state.tuning_sliders[index], TBM_SETTICFREQ, fine ? 4 : 1, 0);
            SendMessageW(state.tuning_sliders[index], TBM_SETPAGESIZE, 0, fine ? 4 : 1);
        }
        reset_tuning_controls(state);
        const HWND reset = CreateWindowExW(
            0, L"BUTTON", L"Reset to defaults",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            14, client_height - 36, 170, 26,
            state.tuning_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTuningResetId)),
            instance, nullptr);
        SendMessageW(reset, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        write_tuning(state);
    }
    ShowWindow(state.tuning_window, SW_SHOWNORMAL);
    SetForegroundWindow(state.tuning_window);
}

void show_context_menu(AppState& state) {
    HMENU menu = CreatePopupMenu();
    HMENU backend_menu = CreatePopupMenu();
    HMENU nvidia_scale_menu = CreatePopupMenu();
    if (menu == nullptr || backend_menu == nullptr ||
        nvidia_scale_menu == nullptr) {
        if (nvidia_scale_menu) DestroyMenu(nvidia_scale_menu);
        if (backend_menu) DestroyMenu(backend_menu);
        if (menu) DestroyMenu(menu);
        return;
    }

    AppendMenuW(
        menu,
        MF_STRING | (state.armed ? MF_CHECKED : MF_UNCHECKED),
        toggle_arm,
        state.armed
            ? L"Disarm bridge"
            : L"Arm bridge until manual disarm");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU multiplier_menu = CreatePopupMenu();
    if (multiplier_menu) {
        AppendMenuW(
            multiplier_menu,
            MF_STRING | (state.settings.triple_frame_gen ? MF_UNCHECKED : MF_CHECKED),
            frame_gen_2x,
            L"2X");
        AppendMenuW(
            multiplier_menu,
            MF_STRING | (state.settings.triple_frame_gen ? MF_CHECKED : MF_UNCHECKED),
            frame_gen_3x,
            L"3X");
        AppendMenuW(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(multiplier_menu),
            L"Frame Gen multiplier [live change]");
    }
    AppendMenuW(
        menu,
        MF_STRING | (state.armed && state.pause_signal ? MF_ENABLED : MF_GRAYED),
        toggle_pause,
        state.paused ? L"Resume frame generation" : L"Pause frame generation");
    // Opens the dialog that changes it. While armed, a key the system would
    // not give the tray says so here, since it will not work.
    std::wstring binding_label =
        L"Current key binding: " + pause_key_display(state.settings.pause_hotkey);
    if (state.armed && state.pause_signal && !state.pause_hotkey_registered &&
        xrfg::standalone::parse_hotkey(state.settings.pause_hotkey)) {
        binding_label += L" (used by another program)";
    }
    AppendMenuW(menu, MF_STRING, change_pause_key, binding_label.c_str());
    // Must outlive the menu, which only borrows it.
    HBITMAP pause_bitmap = state.paused ? create_pause_bitmap() : nullptr;
    if (pause_bitmap) {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = pause_bitmap;
        SetMenuItemInfoW(menu, toggle_pause, FALSE, &item);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        backend_menu,
        MF_STRING |
            (state.settings.backend == xrfg::standalone::FlowBackend::fidelity_fx
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        backend_fidelity_fx,
        L"FidelityFX");
    AppendMenuW(
        backend_menu,
        MF_STRING |
            (state.settings.backend == xrfg::standalone::FlowBackend::nvidia &&
                     state.settings.nvidia_preset ==
                         xrfg::standalone::NvidiaPerformancePreset::fast
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        backend_nvidia_fast,
        L"NVIDIA Fast (test)");
    AppendMenuW(
        backend_menu,
        MF_STRING |
            (state.settings.backend == xrfg::standalone::FlowBackend::nvidia &&
                     state.settings.nvidia_preset ==
                         xrfg::standalone::NvidiaPerformancePreset::medium
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        backend_nvidia_medium,
        L"NVIDIA Medium (default)");
    AppendMenuW(
        backend_menu,
        MF_STRING |
            (state.settings.backend == xrfg::standalone::FlowBackend::nvidia &&
                     state.settings.nvidia_preset ==
                         xrfg::standalone::NvidiaPerformancePreset::slow
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        backend_nvidia_slow,
        L"NVIDIA Slow (best quality)");
    AppendMenuW(backend_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        backend_menu,
        MF_STRING |
            (state.settings.nvidia_bidirectional ? MF_CHECKED : MF_UNCHECKED),
        toggle_nvidia_bidirectional,
        L"NVIDIA bidirectional consistency");
    // Expert mode only (tray.ini expert_mode=1): a diagnostic window, not a
    // setting.
    if (state.settings.expert_mode) {
        AppendMenuW(
            backend_menu,
            MF_STRING,
            show_live_tuning,
            L"Frame generation tuning (live)...");
    }
    AppendMenuW(
        menu,
        MF_POPUP,
        reinterpret_cast<UINT_PTR>(backend_menu),
        L"Optical flow backend");
    AppendMenuW(
        nvidia_scale_menu,
        MF_STRING |
            (state.settings.nvidia_input_scale ==
                     xrfg::standalone::NvidiaInputScale::full
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        nvidia_scale_full,
        L"100% (full resolution)");
    AppendMenuW(
        nvidia_scale_menu,
        MF_STRING |
            (state.settings.nvidia_input_scale ==
                     xrfg::standalone::NvidiaInputScale::three_quarter
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        nvidia_scale_three_quarter,
        L"75%");
    AppendMenuW(
        nvidia_scale_menu,
        MF_STRING |
            (state.settings.nvidia_input_scale ==
                     xrfg::standalone::NvidiaInputScale::half
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        nvidia_scale_half,
        L"50% (default)");
    AppendMenuW(
        nvidia_scale_menu,
        MF_STRING |
            (state.settings.nvidia_input_scale ==
                     xrfg::standalone::NvidiaInputScale::quarter
                 ? MF_CHECKED
                 : MF_UNCHECKED),
        nvidia_scale_quarter,
        L"25%");
    AppendMenuW(
        menu,
        MF_POPUP,
        reinterpret_cast<UINT_PTR>(nvidia_scale_menu),
        L"Optical flow resolution");
    // 3X needs what "Prefer FPS over latency" sets up, so it holds that on.
    AppendMenuW(
        menu,
        MF_STRING |
            (state.settings.deep_pipeline || state.settings.triple_frame_gen
                 ? MF_CHECKED
                 : MF_UNCHECKED) |
            (state.settings.triple_frame_gen ? MF_GRAYED : MF_ENABLED),
        toggle_deep_pipeline,
        state.settings.triple_frame_gen
            ? L"Prefer FPS over latency (on with 3X)"
            : L"Prefer FPS over latency");
    AppendMenuW(
        menu,
        MF_STRING | (state.settings.single_swapchain_rings ? MF_CHECKED : MF_UNCHECKED),
        toggle_lower_vram,
        L"Lower VRAM (may cause stuttering)");
    AppendMenuW(
        menu,
        MF_STRING | (state.settings.hybrid ? MF_CHECKED : MF_UNCHECKED),
        toggle_hybrid,
        L"Hybrid: game motion vectors + optical flow (DLSS games)");
    // Greyed while Hybrid is off: only the hybrid composition runs it.
    AppendMenuW(
        menu,
        MF_STRING | (state.settings.hybrid_depth_search ? MF_CHECKED : MF_UNCHECKED) |
            (state.settings.hybrid ? MF_ENABLED : MF_GRAYED),
        toggle_hybrid_depth_search,
        L"Hybrid: depth search");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        menu,
        MF_STRING | (state.settings.diagnostics ? MF_CHECKED : MF_UNCHECKED),
        toggle_diagnostics,
        L"Bridge flight recorder [live change]");
    HMENU overlay_menu = CreatePopupMenu();
    if (overlay_menu) {
        const struct { UINT command; xrfg::FpsOverlaySize size; const wchar_t* text; } sizes[]{
            {overlay_size_big, xrfg::FpsOverlaySize::big, L"Big"},
            {overlay_size_medium, xrfg::FpsOverlaySize::medium, L"Medium"},
            {overlay_size_small, xrfg::FpsOverlaySize::little, L"Small"}};
        for (const auto& entry : sizes) {
            AppendMenuW(overlay_menu, MF_STRING |
                (state.settings.overlay_size == entry.size ? MF_CHECKED : MF_UNCHECKED),
                entry.command, entry.text);
        }
        AppendMenuW(overlay_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(overlay_menu, MF_STRING |
            (state.settings.overlay_latency ? MF_CHECKED : MF_UNCHECKED),
            toggle_overlay_latency, L"Show latency");
        AppendMenuW(overlay_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(overlay_menu, MF_STRING |
            (state.settings.overlay_hybrid ? MF_CHECKED : MF_UNCHECKED),
            toggle_overlay_hybrid, L"Show Hybrid status");
        AppendMenuW(overlay_menu, MF_SEPARATOR, 0, nullptr);
        const struct { UINT command; xrfg::FpsOverlayPosition position; const wchar_t* text; } entries[]{
            {overlay_upper_left, xrfg::FpsOverlayPosition::upper_left, L"Upper left"},
            {overlay_upper_right, xrfg::FpsOverlayPosition::upper_right, L"Upper right"},
            {overlay_lower_left, xrfg::FpsOverlayPosition::lower_left, L"Lower left"},
            {overlay_lower_right, xrfg::FpsOverlayPosition::lower_right, L"Lower right"},
            {overlay_off, xrfg::FpsOverlayPosition::off, L"Off"}};
        for (const auto& entry : entries) {
            if (entry.position == xrfg::FpsOverlayPosition::off) AppendMenuW(overlay_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(overlay_menu, MF_STRING |
                (state.settings.overlay_position == entry.position ? MF_CHECKED : MF_UNCHECKED),
                entry.command, entry.text);
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(overlay_menu), L"FPS overlay");
    }
    AppendMenuW(menu, MF_STRING, open_logs, L"Open bridge logs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, reset_defaults, L"Reset to default...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU donate_menu = CreatePopupMenu();
    if (donate_menu) {
        AppendMenuW(donate_menu, MF_STRING, donate_creator,
            L"tig3rmast3r, creator of OFXR");
        AppendMenuW(donate_menu, MF_STRING, donate_maintainer,
            L"Djules, maintainer of 0.2.x");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(donate_menu), L"Donate");
    }
    AppendMenuW(menu, MF_STRING, show_about, L"About");
    AppendMenuW(menu, MF_STRING, exit_application, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(state.window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, state.window, nullptr);
    PostMessageW(state.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (pause_bitmap) DeleteObject(pause_bitmap);
}

// V529: a game with Luke Ross's R.E.A.L. VR runs at 2X whatever was asked
// for (its layer says so in the status block); the menu and the saved
// setting follow, once, with a notice why.
void follow_realvr_session(AppState& state) {
    if (!state.settings.triple_frame_gen || !realvr_session_running()) return;
    state.settings.triple_frame_gen = false;
    update_runtime_options(state);
    show_balloon(state, L"Frame Gen multiplier: 2X",
        L"The running game uses Luke Ross's R.E.A.L. VR mod, which does not "
        L"work with 3X, so OFXR runs it at 2X. The multiplier is now 2X; choose "
        L"3X again for other games.");
}

void handle_command(AppState& state, UINT command) {
    switch (command) {
    case toggle_arm:
        if (state.armed) {
            std::wstring error;
            if (!disarm_bridge(state, &error)) {
                show_error(state.window, error);
            }
        } else {
            std::wstring error;
            if (!arm_bridge(state, &error)) {
                show_error(state.window, error);
            }
        }
        break;
    case toggle_pause:
        if (!state.armed || !state.pause_signal) break;
        if (state.paused ? ResetEvent(state.pause_signal)
                         : SetEvent(state.pause_signal)) {
            state.paused = !state.paused;
            log_lifecycle(state.local_directory, state.paused ? L"pause" : L"resume");
            refresh_tray_icon(state);
            if (state.paused) {
                show_balloon(
                    state,
                    L"Frame generation paused",
                    L"Running games still go through the bridge. To rule OFXR out, disarm and restart the game.");
            } else {
                show_balloon(
                    state,
                    L"Frame generation resumed",
                    L"Running games generate frames again.");
            }
        } else {
            show_error(
                state.window, last_error_message(L"Switching the frame generation pause"));
        }
        break;
    case change_pause_key:
        change_pause_hotkey(state);
        break;
    case backend_fidelity_fx:
        state.settings.backend = xrfg::standalone::FlowBackend::fidelity_fx;
        update_runtime_options(state);
        break;
    case backend_nvidia_fast:
        state.settings.backend = xrfg::standalone::FlowBackend::nvidia;
        state.settings.nvidia_preset =
            xrfg::standalone::NvidiaPerformancePreset::fast;
        update_runtime_options(state);
        break;
    case backend_nvidia_slow:
        state.settings.backend = xrfg::standalone::FlowBackend::nvidia;
        state.settings.nvidia_preset =
            xrfg::standalone::NvidiaPerformancePreset::slow;
        update_runtime_options(state);
        break;
    case backend_nvidia_medium:
        state.settings.backend = xrfg::standalone::FlowBackend::nvidia;
        state.settings.nvidia_preset =
            xrfg::standalone::NvidiaPerformancePreset::medium;
        update_runtime_options(state);
        break;
    case toggle_nvidia_bidirectional:
        state.settings.nvidia_bidirectional =
            !state.settings.nvidia_bidirectional;
        update_runtime_options(state);
        break;
    case nvidia_scale_full:
        state.settings.nvidia_input_scale =
            xrfg::standalone::NvidiaInputScale::full;
        update_runtime_options(state);
        break;
    case nvidia_scale_three_quarter:
        state.settings.nvidia_input_scale =
            xrfg::standalone::NvidiaInputScale::three_quarter;
        update_runtime_options(state);
        break;
    case nvidia_scale_half:
        state.settings.nvidia_input_scale =
            xrfg::standalone::NvidiaInputScale::half;
        update_runtime_options(state);
        break;
    case nvidia_scale_quarter:
        state.settings.nvidia_input_scale =
            xrfg::standalone::NvidiaInputScale::quarter;
        update_runtime_options(state);
        break;
    case toggle_hybrid:
        state.settings.hybrid = !state.settings.hybrid;
        update_runtime_options(state);
        show_balloon(
            state,
            state.settings.hybrid ? L"Hybrid: on" : L"Hybrid: off",
            L"In a D3D12 game that runs DLSS, the game's own motion vectors are "
            L"composed pixel by pixel with the optical flow: each pixel takes "
            L"whichever explains the two frames better. Games without DLSS, "
            L"and D3D11 and Vulkan games, are unchanged. A running game "
            L"switches within a second.");
        break;
    case toggle_hybrid_depth_search:
        if (!state.settings.hybrid) {
            break;
        }
        state.settings.hybrid_depth_search = !state.settings.hybrid_depth_search;
        update_runtime_options(state);
        show_balloon(
            state,
            state.settings.hybrid_depth_search
                ? L"Hybrid depth search: on"
                : L"Hybrid depth search: off",
            L"Hybrid also looks in the game's depth for thin near objects - "
            L"bars, struts, cables - crossing a pixel between two frames, so "
            L"they stay whole on sideways moves. It costs GPU time, a lot at "
            L"high headset resolutions; leave it off if your frame rate drops "
            L"with Hybrid. A running game switches within a second.");
        break;
    case toggle_deep_pipeline:
        if (state.settings.triple_frame_gen) {
            break;
        }
        state.settings.deep_pipeline = !state.settings.deep_pipeline;
        update_runtime_options(state);
        // A menu item cannot carry a tooltip, so the explanation goes in
        // the notification, armed or not.
        show_balloon(
            state,
            state.settings.deep_pipeline
                ? L"Prefer FPS over latency: on"
                : L"Prefer FPS over latency: off",
            L"Helps games that only just reach half your headset's refresh "
            L"rate hold full FPS more steadily. Adds one frame of latency "
            L"(about 11 ms at 90 Hz). Leave off if the game already runs "
            L"comfortably. Takes effect the next time the game starts.");
        break;
    case toggle_lower_vram:
        state.settings.single_swapchain_rings = !state.settings.single_swapchain_rings;
        update_runtime_options(state);
        show_balloon(
            state,
            state.settings.single_swapchain_rings
                ? L"Lower VRAM: on"
                : L"Lower VRAM: off",
            L"On, OFXR keeps one working image per output instead of two and "
            L"uses about half a gigabyte less VRAM at high resolutions. On "
            L"some graphics cards that showed as stutter; turn it off if you "
            L"see any and have VRAM to spare. Takes effect the next time the "
            L"game starts.");
        break;
    case reset_defaults:
        if (MessageBoxW(
                state.window,
                L"Reset every OFXR setting to its factory default?\n\n"
                L"Optical flow: FidelityFX (NVIDIA: Medium at 50%, bidirectional "
                L"consistency off). Prefer FPS over latency: on. Frame Gen "
                L"multiplier: 2X. Lower VRAM: on. FPS overlay: upper right, big, latency and Hybrid status shown. Bridge "
                L"flight recorder: off. Pause key: Ctrl + Alt + F7. Hybrid: off, "
                L"its depth search off. "
                L"Settings "
                L"only reachable in tray.ini go back to their defaults too.\n\n"
                L"The FPS overlay, the flight recorder, the Frame Gen multiplier, "
                L"the optical flow and Hybrid change in running games at once; "
                L"the rest applies the next time the game starts.",
                L"OFXR Bridge - reset to default",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND |
                    MB_TOPMOST) != IDYES) {
            break;
        }
        reset_to_factory_settings(state);
        show_balloon(
            state,
            L"Settings reset to default",
            L"Every OFXR setting is back to its factory default. Some apply "
            L"in running games at once, the rest the next time the game "
            L"starts.");
        break;
    case frame_gen_2x:
    case frame_gen_3x:
        if (state.settings.triple_frame_gen == (command == frame_gen_3x)) {
            break;
        }
        // V529: a game with R.E.A.L. VR runs at 2X; 3X is refused while it runs.
        if (command == frame_gen_3x && realvr_session_running()) {
            show_balloon(state, L"Frame Gen multiplier: 2X",
                L"The running game uses Luke Ross's R.E.A.L. VR mod, which does "
                L"not work with 3X: it stays at 2X. 3X can be chosen again for "
                L"other games.");
            break;
        }
        state.settings.triple_frame_gen = command == frame_gen_3x;
        if (state.settings.triple_frame_gen) {
            // 3X runs on what this sets up; it stays on afterwards.
            state.settings.deep_pipeline = true;
        }
        update_runtime_options(state);
        if (state.armed && xrfg::implicit_layer::running_session_needs_restart_for_3x()) {
            // A notification is easy to miss - Windows holds them back
            // while a game runs - and this one says the click did nothing,
            // so it has to be seen.
            MessageBoxW(
                state.window,
                L"Restart your game to apply this change.\n\n"
                L"The game that is running was started with \"Prefer FPS over "
                L"latency\" off, so the Frame Gen multiplier cannot be changed "
                L"while it runs. The setting is saved: the game will use it the next "
                L"time it starts.",
                L"OFXR Bridge - game restart needed",
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
        } else {
            show_balloon(
                state,
                state.settings.triple_frame_gen
                    ? L"Frame Gen multiplier: 3X"
                    : L"Frame Gen multiplier: 2X",
                state.settings.triple_frame_gen
                    ? L"Two generated frames for every frame the game "
                      L"renders: the game runs at a third of your headset's "
                      L"refresh rate (30 FPS at 90 Hz). Switches in a running "
                      L"game within a moment."
                    : L"Back to one generated frame per game frame. Switches "
                      L"in a running game within a moment.");
        }
        break;
    case overlay_off:
    case overlay_upper_left:
    case overlay_upper_right:
    case overlay_lower_left:
    case overlay_lower_right:
        state.settings.overlay_position = command == overlay_off ? xrfg::FpsOverlayPosition::off
            : command == overlay_upper_left ? xrfg::FpsOverlayPosition::upper_left
            : command == overlay_lower_left ? xrfg::FpsOverlayPosition::lower_left
            : command == overlay_lower_right ? xrfg::FpsOverlayPosition::lower_right
            : xrfg::FpsOverlayPosition::upper_right;
        update_runtime_options(
            state, L"The FPS overlay position updates in running applications.");
        break;
    case overlay_size_big:
    case overlay_size_medium:
    case overlay_size_small:
        state.settings.overlay_size = command == overlay_size_small ? xrfg::FpsOverlaySize::little
            : command == overlay_size_medium ? xrfg::FpsOverlaySize::medium
            : xrfg::FpsOverlaySize::big;
        update_runtime_options(
            state, L"The FPS overlay size updates in running applications.");
        break;
    case toggle_overlay_latency:
        state.settings.overlay_latency = !state.settings.overlay_latency;
        update_runtime_options(
            state, L"The latency below the FPS number updates in running applications.");
        break;
    case toggle_overlay_hybrid:
        state.settings.overlay_hybrid = !state.settings.overlay_hybrid;
        update_runtime_options(
            state, L"The Hybrid status above the FPS number updates in running "
                   L"applications. It shows while Hybrid is on: HYBRID OK in green, "
                   L"or HYBRID KO with the reason's number in red.");
        break;
    case toggle_diagnostics:
        state.settings.diagnostics = !state.settings.diagnostics;
        update_runtime_options(
            state,
            state.settings.diagnostics
                ? L"Recording starts within a moment, in running games too, "
                  L"with one hitch. The log begins with the session's "
                  L"start-up records."
                : L"Recording stops within a moment, with one hitch, and the "
                  L"log file is closed.");
        break;
    case show_live_tuning:
        show_tuning_window(state);
        break;
    case open_logs: {
        const auto directory = runtime_directory(state.local_directory);
        std::error_code ignored;
        std::filesystem::create_directories(directory, ignored);
        ShellExecuteW(
            state.window, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case donate_creator:
    case donate_maintainer:
        ShellExecuteW(
            state.window, L"open",
            command == donate_creator ? kDonateCreatorUrl : kDonateMaintainerUrl,
            nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case show_about: {
        wchar_t version_label[64]{};
        swprintf_s(version_label, L"OFXR Bridge V%03u", kImplementationVersion);
        TASKDIALOGCONFIG dialog{};
        dialog.cbSize = sizeof(dialog);
        dialog.hwndParent = state.window;
        dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                         TDF_ENABLE_HYPERLINKS |
                         TDF_SIZE_TO_CONTENT |
                         TDF_USE_HICON_MAIN;
        dialog.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        dialog.pszWindowTitle = kApplicationName;
        dialog.pszMainInstruction = version_label;
        dialog.pszContent =
            L"Created by tig3rmast3r, the original author of OFXR.\r\n"
            L"Support him: <a href=\"https://ko-fi.com/tig3rmast3r\">"
            L"ko-fi.com/tig3rmast3r</a>\r\n\r\n"
            L"0.2.X version maintained by Djules.\r\n"
            L"Support him: <a href=\"https://ko-fi.com/djules\">"
            L"ko-fi.com/djules</a>\r\n\r\n"
            L"Licensed under LGPL-3.0-or-later.\r\n"
            L"<a href=\"https://github.com/djules75/OFXR-Bridge\">"
            L"github.com/djules75/OFXR-Bridge</a>";
        dialog.hMainIcon = state.disarmed_icon;
        dialog.pfCallback = [](
            HWND window,
            UINT notification,
            WPARAM,
            LPARAM parameter,
            LONG_PTR) -> HRESULT {
                if (notification == TDN_HYPERLINK_CLICKED) {
                    ShellExecuteW(
                        window,
                        L"open",
                        reinterpret_cast<LPCWSTR>(parameter),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                }
                return S_OK;
            };
        if (FAILED(TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr))) {
            const std::wstring message = std::wstring(version_label) +
                L"\r\n\r\nCreated by tig3rmast3r, the original author of OFXR.\r\n"
                L"Support him: https://ko-fi.com/tig3rmast3r\r\n\r\n"
                L"0.2.X version maintained by Djules.\r\n"
                L"Support him: https://ko-fi.com/djules\r\n\r\n"
                L"License: LGPL-3.0-or-later\r\n"
                L"https://github.com/djules75/OFXR-Bridge";
            MessageBoxW(
                state.window,
                message.c_str(),
                kApplicationName,
                MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case exit_application:
        SendMessageW(state.window, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == state->taskbar_created_message) {
        static_cast<void>(add_tray_icon(*state));
        return 0;
    }
    switch (message) {
    case kTrayMessage:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            show_context_menu(*state);
        } else if (lparam == WM_LBUTTONDBLCLK) {
            handle_command(*state, toggle_arm);
        }
        return 0;
    case WM_COMMAND:
        handle_command(*state, LOWORD(wparam));
        write_status(*state);
        return 0;
    case WM_HOTKEY:
        if (wparam == kPauseHotkeyId) handle_command(*state, toggle_pause);
        write_status(*state);
        return 0;
    case WM_TIMER:
        if (wparam == kStatusTimerId) {
            follow_realvr_session(*state);
            write_status(*state);
        }
        return 0;
    case WM_CLOSE: {
        std::wstring error;
        if (!disarm_bridge(*state, &error)) {
            show_error(window, error);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    }
    case WM_QUERYENDSESSION: {
        std::wstring error;
        if (!disarm_bridge(*state, &error)) {
            ShutdownBlockReasonCreate(window, L"OFXR Bridge could not disable its OpenXR registrations.");
            return FALSE;
        }
        ShutdownBlockReasonDestroy(window);
        return TRUE;
    }
    case WM_ENDSESSION:
        if (wparam != FALSE) {
            std::wstring error;
            if (!disarm_bridge(*state, &error))
                log_lifecycle(state->local_directory, L"end-session-cleanup", error);
            DestroyWindow(window);
        } else {
            // A cancelled shutdown remains disarmed; never silently re-arm.
            ShutdownBlockReasonDestroy(window);
        }
        return 0;
    case WM_DESTROY: {
        std::wstring error;
        if (!disarm_bridge(*state, &error))
            log_lifecycle(state->local_directory, L"destroy-cleanup", error);
        KillTimer(window, kStatusTimerId);
        // The last word: disarmed, whatever a game last published.
        write_status(*state);
        Shell_NotifyIconW(NIM_DELETE, &state->icon);
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

[[nodiscard]] bool watch_registered_arm(
    HANDLE parent, const std::filesystem::path& manifest,
    const std::filesystem::path& local_directory, std::wstring* error,
    xrfg::implicit_layer::RegistryScope scope,
    std::wstring_view registry_subkey = xrfg::implicit_layer::kRegistrySubkey,
    const std::filesystem::path& vulkan_manifest = {}) {
    if (!xrfg::implicit_layer::owned_registration_path(manifest, local_directory)) {
        if (error) *error = L"Cleanup watchdog refused an unowned manifest.";
        return false;
    }
    if (!vulkan_manifest.empty() &&
        !xrfg::implicit_layer::owned_registration_path(
            vulkan_manifest, local_directory,
            xrfg::implicit_layer::kVulkanManifestPrefix)) {
        if (error) *error = L"Cleanup watchdog refused an unowned Vulkan manifest.";
        return false;
    }
    while (xrfg::implicit_layer::manifest_registered(manifest, scope, registry_subkey)) {
        if (parent == nullptr || WaitForSingleObject(parent, kArmPollMilliseconds) != WAIT_TIMEOUT)
            break;
    }
    // Scoped to this arm only: an old helper must never revoke a newer arm.
    bool retired = xrfg::implicit_layer::retire_manifest(
        manifest, scope, error, registry_subkey);
    if (!vulkan_manifest.empty()) {
        std::wstring vulkan_error;
        if (!xrfg::implicit_layer::retire_manifest(
                vulkan_manifest, scope, &vulkan_error,
                xrfg::implicit_layer::kVulkanRegistrySubkey)) {
            retired = false;
            if (error) {
                if (!error->empty()) *error += L"\r\n";
                *error += vulkan_error;
            }
        }
    }
    return retired;
}

[[nodiscard]] std::optional<int> run_cleanup_helper() {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return EXIT_FAILURE;
    }
    if (argument_count < 2 || _wcsicmp(arguments[1], kCleanupArgument) != 0) {
        LocalFree(arguments);
        return std::nullopt;
    }
    if (argument_count != 5 && argument_count != 6) {
        LocalFree(arguments);
        return EXIT_FAILURE;
    }
    const std::filesystem::path manifest(arguments[2]);
    const std::filesystem::path vulkan_manifest(
        argument_count == 6 ? arguments[5] : L"");
    wchar_t* end = nullptr;
    const unsigned long parsed_pid = std::wcstoul(arguments[3], &end, 10);
    const bool valid_pid = end != arguments[3] && end != nullptr && *end == L'\0' &&
                           parsed_pid > 0 && parsed_pid <= MAXDWORD;
    const std::wstring_view scope_argument(arguments[4]);
    const auto scope = _wcsicmp(scope_argument.data(), L"HKLM") == 0
        ? xrfg::implicit_layer::RegistryScope::local_machine
        : xrfg::implicit_layer::RegistryScope::current_user;
    const bool valid_scope = _wcsicmp(scope_argument.data(), L"HKLM") == 0 ||
                             _wcsicmp(scope_argument.data(), L"HKCU") == 0;
    LocalFree(arguments);
    if (!valid_pid || !valid_scope) {
        return EXIT_FAILURE;
    }

    std::filesystem::path directory;
    try {
        directory = runtime_directory(local_app_data() / L"OFXR Bridge");
    } catch (...) {
        return EXIT_FAILURE;
    }
    if (!xrfg::implicit_layer::owned_manifest_path(manifest, directory)) {
        return EXIT_FAILURE;
    }

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parsed_pid));
    std::wstring error;
    const bool cleaned = watch_registered_arm(parent, manifest,
        local_app_data() / L"OFXR Bridge", &error, scope,
        xrfg::implicit_layer::kRegistrySubkey, vulkan_manifest);
    if (parent != nullptr) {
        CloseHandle(parent);
    }
    log_lifecycle(local_app_data() / L"OFXR Bridge", L"watchdog-cleanup", error);
    return cleaned ? EXIT_SUCCESS : EXIT_FAILURE;
}

[[nodiscard]] bool arm_and_exit_requested() {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) return false;
    const bool requested = argument_count == 2 &&
        _wcsicmp(arguments[1], kArmAndExitArgument) == 0;
    LocalFree(arguments);
    return requested;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (const auto helper_result = run_cleanup_helper()) {
        return *helper_result;
    }
    const bool arm_only = arm_and_exit_requested();

    HANDLE single_instance = CreateMutexW(nullptr, TRUE, L"Local\\OFXRBridgeTray");
    if (single_instance == nullptr) {
        return EXIT_FAILURE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr,
            L"OFXR Bridge is already running in the notification area.",
            kApplicationName,
            MB_OK | MB_ICONINFORMATION);
        CloseHandle(single_instance);
        // The running tray owns the arm; one made here would be retired by it.
        return arm_only ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    AppState state;
    try {
        state.executable_directory = executable_directory();
        state.local_directory = local_app_data() / L"OFXR Bridge";
        state.settings_path = state.local_directory / L"tray.ini";
        std::wstring cleanup_error;
        if (!cleanup_all_owned_registrations(state, &cleanup_error)) {
            log_lifecycle(state.local_directory, L"startup-cleanup", cleanup_error);
            MessageBoxW(nullptr, cleanup_error.c_str(), kApplicationName, MB_OK | MB_ICONERROR);
            CloseHandle(single_instance);
            return EXIT_FAILURE;
        }
        log_lifecycle(state.local_directory, L"startup-cleanup");
        load_settings(state);
        if (arm_only) {
            std::wstring arm_error;
            const bool armed = arm_and_exit(state, &arm_error);
            if (!armed) {
                log_lifecycle(state.local_directory, L"arm-and-exit", arm_error);
                MessageBoxW(nullptr, arm_error.c_str(), kApplicationName, MB_OK | MB_ICONERROR);
            }
            CloseHandle(single_instance);
            return armed ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    } catch (...) {
        MessageBoxW(
            nullptr,
            L"OFXR Bridge could not initialize its local configuration.",
            kApplicationName,
            MB_OK | MB_ICONERROR);
        CloseHandle(single_instance);
        return EXIT_FAILURE;
    }
    state.taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    state.armed_icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_OFXR_ARMED));
    state.disarmed_icon = LoadIconW(
        instance, MAKEINTRESOURCEW(IDI_OFXR_DISARMED));
    if (state.armed_icon == nullptr || state.disarmed_icon == nullptr) {
        MessageBoxW(
            nullptr,
            L"OFXR Bridge could not load its notification icons.",
            kApplicationName,
            MB_OK | MB_ICONERROR);
        CloseHandle(single_instance);
        return EXIT_FAILURE;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hIcon = state.disarmed_icon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    window_class.hIconSm = state.disarmed_icon;
    if (RegisterClassExW(&window_class) == 0) {
        CloseHandle(single_instance);
        return EXIT_FAILURE;
    }

    const HWND window = CreateWindowExW(
        0,
        kWindowClass,
        kApplicationName,
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr || !add_tray_icon(state)) {
        if (window) DestroyWindow(window);
        CloseHandle(single_instance);
        return EXIT_FAILURE;
    }
    SetTimer(window, kStatusTimerId, kStatusMilliseconds, nullptr);

    // Arm straight away: launching the tray is the user asking for the
    // bridge. A failure leaves it disarmed with the reason shown, exactly as
    // the menu item would; the menu can retry.
    {
        std::wstring arm_error;
        if (!arm_bridge(state, &arm_error)) {
            show_error(window, arm_error);
        }
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    std::wstring cleanup_error;
    const bool clean_exit = disarm_bridge(state, &cleanup_error);
    if (!clean_exit) log_lifecycle(state.local_directory, L"message-loop-exit", cleanup_error);
    CloseHandle(single_instance);
    return clean_exit ? static_cast<int>(message.wParam) : EXIT_FAILURE;
}
