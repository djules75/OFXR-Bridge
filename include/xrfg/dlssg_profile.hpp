#pragma once

#include <filesystem>
#include <string>

namespace xrfg::standalone {

enum class DlssgPreset { game_default = 0, a = 1, c = 3 };

struct DlssgProfile {
    std::wstring name;
    unsigned preset{};
    bool enabled{};
    bool custom_override{};
};

// The game's NVIDIA profile, never the global/base profile. No NVAPI SDK
// installation is needed; the driver exports the public DRS interfaces.
bool read_dlssg_profile(const std::filesystem::path& executable,
                       DlssgProfile& result, std::wstring& error) noexcept;
bool write_dlssg_profile(const std::filesystem::path& executable,
                        DlssgPreset preset, std::wstring& error) noexcept;

std::filesystem::path process_executable() noexcept;
// Capture once before menus can edit the next-launch driver preference. This is
// a process-start profile request, not verification of the model loaded by NGX.
unsigned process_dlssg_preset() noexcept;

} // namespace xrfg::standalone
