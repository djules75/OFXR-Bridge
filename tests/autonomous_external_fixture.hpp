#pragma once
#include "xrfg/autonomous_control.hpp"
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrfg_fixture {
// Fake-runtime reference fixtures use FidelityFX optical flow. Seed the real
// tray channel before loading the DLL, preserving each fixture's diagnostics
// and overlay choices. All publications stay in one unique test profile.
class ExternalControlProfile {
    std::filesystem::path directory_;
    std::filesystem::path runtime_ini_;
    std::optional<std::string> original_ini_;
    std::optional<std::wstring> original_profile_;
public:
    explicit ExternalControlProfile(const std::filesystem::path& runtime_ini) : runtime_ini_(runtime_ini) {
        std::array<wchar_t, 32768> previous{};
        const auto length = GetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR", previous.data(),
            static_cast<DWORD>(previous.size()));
        if (length && length < previous.size()) original_profile_ = previous.data();
        directory_ = std::filesystem::temp_directory_path() /
            (L"ofxr-autonomous-external-test-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(directory_);
        if (!SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR", directory_.c_str()))
            throw std::runtime_error("cannot isolate fake-runtime control profile");
        std::ifstream original(runtime_ini_, std::ios::binary);
        if (original) original_ini_ = std::string(std::istreambuf_iterator<char>(original), {});
        xrfg::autonomous::Settings desired;
        desired.backend = 0;
        desired.frame_generation = 0;
        desired.motion_vectors = 0;
        desired.afw_mode = 0;
        desired.prefer_fps = GetPrivateProfileIntW(L"ofxr", L"deep_pipeline", 1, runtime_ini_.c_str()) != 0;
        desired.triple_frame_gen = false;
        desired.lower_vram = 0;
        desired.diagnostics = GetPrivateProfileIntW(L"diagnostics", L"logging_enabled", 0, runtime_ini_.c_str()) != 0;
        wchar_t position[32]{};
        GetPrivateProfileStringW(L"overlay", L"position", L"upper_right", position, 32, runtime_ini_.c_str());
        constexpr const wchar_t* positions[]{L"off", L"upper_left", L"upper_right", L"lower_left", L"lower_right"};
        for (int index = 0; index < 5; ++index)
            if (_wcsicmp(position, positions[index]) == 0) desired.overlay_position = index;
        if (!xrfg::autonomous::request(desired))
            throw std::runtime_error("cannot publish explicit fake-runtime settings");
    }
    ~ExternalControlProfile() {
        // The DLL updates its own INI through production settings persistence.
        // Restore this build fixture's original text after unloading the layer.
        if (original_ini_) {
            std::ofstream output(runtime_ini_, std::ios::binary | std::ios::trunc);
            output << *original_ini_;
        } else {
            std::error_code ignored;
            std::filesystem::remove(runtime_ini_, ignored);
        }
        SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR", original_profile_ ? original_profile_->c_str() : nullptr);
        std::error_code ignored;
        std::filesystem::remove(directory_ / L"ofxr-bridge.ini", ignored);
        std::filesystem::remove(directory_, ignored);
    }
};
} // namespace xrfg_fixture
