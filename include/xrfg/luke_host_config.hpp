#pragma once
#include <Windows.h>
#include <filesystem>

namespace xrfg {
inline std::filesystem::path luke_host_game_directory() {
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768)) return {};
    return std::filesystem::path(executable).parent_path();
}
// In the combined host these files live beside OptiScaler, in the game.
// Preserve that game's provider and phase calibration in standalone as well.
inline std::filesystem::path luke_host_provider(const std::filesystem::path& game,
    const std::filesystem::path& bundled) {
    const auto local = game / L"PDAFWPlugin.dll";
    std::error_code error;
    return std::filesystem::is_regular_file(local, error) ? local : bundled;
}
inline bool luke_host_odd_serial_is_left(const std::filesystem::path& game,
    const std::filesystem::path& runtime_ini) noexcept {
    const auto local = game / L"ofxr_bridge.ini";
    const auto phase = GetPrivateProfileIntW(L"experimental",
        L"luke_afw_odd_serial_is_left", 2, local.c_str());
    return phase <= 1 ? phase != 0 : GetPrivateProfileIntW(L"experimental",
        L"luke_afw_odd_serial_is_left", 1, runtime_ini.c_str()) != 0;
}
}
