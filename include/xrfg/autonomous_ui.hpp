#pragma once
#include <windows.h>
#include <filesystem>

namespace xrfg::autonomous {
// Opens only the control panel. Activation remains an explicit tray command.
bool show_settings_window(HWND owner) noexcept;
void close_settings_window() noexcept;
// Captures this panel's own D3D11 backbuffer for layout verification.
bool save_settings_preview(const std::filesystem::path& output) noexcept;
} // namespace xrfg::autonomous
