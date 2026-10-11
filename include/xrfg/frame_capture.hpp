#pragma once
#include <d3d11.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace xrfg {
// Diagnostic-only. tick/source/shutdown require the caller's GPU mutex.
// record() is thread-safe and never touches graphics state or waits for disk.
class FrameCapture final {
public:
    FrameCapture(ID3D11Device*, ID3D11DeviceContext*, std::filesystem::path root);
    ~FrameCapture();
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    void tick(bool logging_enabled) noexcept;
    bool start(bool logging_enabled, double seconds = 4.0) noexcept;
    bool active() const noexcept;
    bool busy() const noexcept;
    bool start_ring() noexcept;
    void stop_ring() noexcept;
    bool ring_active() const noexcept;
    bool save_ring() noexcept;
    std::uint64_t source(std::uint64_t swapchain, ID3D11Texture2D*,
                         std::uint32_t image_index, std::uint64_t history_serial,
                         std::uint64_t history_fence) noexcept;
    void record(std::string json) noexcept;
    void shutdown() noexcept;
    static std::int64_t qpc() noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
