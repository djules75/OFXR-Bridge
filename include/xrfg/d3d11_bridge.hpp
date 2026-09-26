#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <d3d11_4.h>
#include <d3d12.h>
#include <windows.h>

namespace xrfg {

// One application swapchain of a bridged D3D11 session: the runtime owns
// D3D12 images, the layer owns D3D12 shared textures of the same shape, and
// the application renders into those textures through D3D11 views. See
// SessionState::d3d11_bridge in the layer for why the runtime is handed a
// D3D12 session at all.
//
// Every method returns immediately; the two sides are ordered on one shared
// fence, and the only CPU wait is wait_for_idle at teardown.
class D3D11BridgeSwapchain final {
public:
    D3D11BridgeSwapchain();
    ~D3D11BridgeSwapchain();
    D3D11BridgeSwapchain(const D3D11BridgeSwapchain&) = delete;
    D3D11BridgeSwapchain& operator=(const D3D11BridgeSwapchain&) = delete;

    // Creates one shared texture per runtime image, in the runtime image's
    // own format and shape. `extra_flags` adds ALLOW_UNORDERED_ACCESS or
    // ALLOW_DEPTH_STENCIL for swapchains that asked for those usages. Fails,
    // with the stage in `failure_stage`, if the driver refuses to share such
    // a texture.
    [[nodiscard]] HRESULT initialize(
        ID3D11Device* d3d11_device,
        ID3D11DeviceContext* d3d11_context,
        ID3D12Device* d3d12_device,
        ID3D12CommandQueue* d3d12_queue,
        std::span<ID3D12Resource* const> runtime_images,
        D3D12_RESOURCE_FLAGS extra_flags,
        std::uint32_t* failure_stage) noexcept;

    [[nodiscard]] std::span<ID3D11Texture2D* const> d3d11_images() const noexcept;
    [[nodiscard]] std::span<ID3D12Resource* const> shared_images() const noexcept;

    // The application is about to render into image `index` (its wait
    // returned): its context waits, on the GPU, for the layer's last read of
    // that texture.
    [[nodiscard]] HRESULT before_write(std::uint32_t index) noexcept;

    // The application released image `index`: its context signals, the
    // layer's queue waits and copies the shared texture into the runtime's
    // image. What the layer queues after this on the same queue - the
    // history capture - reads the shared texture in order.
    [[nodiscard]] HRESULT release(std::uint32_t index) noexcept;

    // Everything the layer will read from image `index` this frame has been
    // queued: signal, so the next before_write of this image waits for it.
    [[nodiscard]] HRESULT mark_read(std::uint32_t index) noexcept;

    [[nodiscard]] HRESULT wait_for_idle() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xrfg
