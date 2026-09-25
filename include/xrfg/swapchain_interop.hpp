#pragma once

#include <d3d12.h>

#include <cstdint>
#include <optional>
#include <span>

namespace xrfg {

// What the layer needs from a bridge between the application's graphics API
// and the D3D12 device synthesis runs on, when the two are not the same. The
// application's released images are moved into D3D12 textures the history
// ring reads, and the synthesizer's output is moved back into private OpenXR
// images of the application's API for the runtime to composite. Every
// transfer is GPU ordered through one shared fence; nothing here CPU-waits on
// the per-frame path.
//
// Where an interop exists the layer treats the pair as ready when the publish
// copy has run, not when synthesis has, because the runtime reads the images
// that copy writes. D3D11 and Vulkan both sit behind this.
class SwapchainInterop {
public:
    virtual ~SwapchainInterop() = default;

    [[nodiscard]] virtual std::span<ID3D12Resource* const> source_images()
        const noexcept = 0;
    [[nodiscard]] virtual std::span<ID3D12Resource* const>
    current_destination_images() const noexcept = 0;
    [[nodiscard]] virtual std::span<ID3D12Resource* const>
    synthetic_destination_images() const noexcept = 0;

    // Must be called while the application still owns the released source
    // image. Queues the copy of it into the shared source on the
    // application's queue and makes the D3D12 queue wait for that copy before
    // any history access.
    [[nodiscard]] virtual HRESULT prepare_capture(
        std::uint32_t source_index) noexcept = 0;

    // Marks the shared source safe for the application's next write once the
    // history copy submitted immediately before this call completes on the
    // D3D12 queue.
    [[nodiscard]] virtual HRESULT finish_capture() noexcept = 0;

    // Makes the D3D12 queue wait for any earlier publication copy before the
    // synthesizer can reuse a shared destination.
    [[nodiscard]] virtual HRESULT prepare_synthesis() noexcept = 0;

    // Called immediately after the synthesizer submission on the same D3D12
    // queue. Queues, on the application's queue, a wait for that submission
    // and the copies into the acquired private OpenXR images.
    [[nodiscard]] virtual HRESULT publish(
        std::uint32_t current_destination_index,
        std::optional<std::uint32_t> synthetic_destination_index) noexcept = 0;

    // The shared fence, AddRef'd into *fence, and the value the most recent
    // publish signals once its copies have run. Read it straight after
    // publish. GetCompletedValue on the returned fence is safe from any
    // thread.
    [[nodiscard]] virtual HRESULT publication_fence(
        ID3D12Fence** fence,
        std::uint64_t* value) const noexcept = 0;

    [[nodiscard]] virtual HRESULT wait_for_idle() noexcept = 0;
    [[nodiscard]] virtual bool initialized() const noexcept = 0;
};

}  // namespace xrfg
