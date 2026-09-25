#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

#include "xrfg/swapchain_interop.hpp"

namespace xrfg {

enum class VulkanInteropInitializationStage : std::uint32_t {
    complete = 0,
    arguments = 1,
    loader = 2,
    instance_functions = 3,
    device_functions = 4,
    adapter_luid = 5,
    format = 6,
    queue = 7,
    source_create_d3d12 = 8,
    source_create_handle = 9,
    source_create_image = 10,
    source_import_memory = 11,
    source_bind_memory = 12,
    current_create_d3d12 = 13,
    current_create_handle = 14,
    current_create_image = 15,
    current_import_memory = 16,
    current_bind_memory = 17,
    synthetic_create_d3d12 = 18,
    synthetic_create_handle = 19,
    synthetic_create_image = 20,
    synthetic_import_memory = 21,
    synthetic_bind_memory = 22,
    create_fence = 23,
    create_fence_handle = 24,
    create_semaphore = 25,
    import_semaphore = 26,
    command_pool = 27,
    command_buffers = 28,
    initial_layout = 29,
    create_event = 30,
};

// The application's Vulkan session as XrGraphicsBindingVulkanKHR describes
// it. The runtime works on this queue inside the frame calls, and Vulkan
// requires a queue to be driven by one thread at a time, so every submission
// the interop makes has to come from the thread the application makes its
// OpenXR calls on.
struct VulkanSessionBinding {
    VkInstance instance{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    std::uint32_t queue_family{};
    std::uint32_t queue_index{};
};

// The shape of every image the interop moves. Vulkan has no way to ask an
// image for its description, so it comes from the XrSwapchainCreateInfo that
// made it; the private swapchains are created from the same info.
struct VulkanImageDescription {
    VkFormat format{VK_FORMAT_UNDEFINED};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t array_size{1};
    std::uint32_t mip_levels{1};
    std::uint32_t sample_count{1};
};

// The D3D12 format a shared texture takes for a Vulkan swapchain format, or
// DXGI_FORMAT_UNKNOWN where the synthesizer has nothing to say about it.
[[nodiscard]] DXGI_FORMAT dxgi_format_for_vulkan(VkFormat format) noexcept;

// Creates a private D3D12 device and direct queue on the adapter the Vulkan
// physical device reports through its LUID. The caller retains both returned
// objects.
[[nodiscard]] HRESULT create_d3d12_device_for_vulkan(
    const VulkanSessionBinding& binding,
    ID3D12Device** d3d12_device,
    ID3D12CommandQueue** d3d12_queue) noexcept;

// The Vulkan counterpart of D3D11D3D12SwapchainInterop. D3D12 owns the shared
// textures and the shared fence, as it does for D3D11; Vulkan imports them as
// images (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT) and a timeline
// semaphore (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT), so a signal
// from either API is a signal on the same object and the D3D12 fence's
// completed value says where the Vulkan side has got to. Every Vulkan entry
// point is taken from the vulkan-1.dll the application already loaded; the
// layer links nothing.
class VulkanD3D12SwapchainInterop final : public SwapchainInterop {
public:
    VulkanD3D12SwapchainInterop() noexcept;
    ~VulkanD3D12SwapchainInterop() override;

    VulkanD3D12SwapchainInterop(const VulkanD3D12SwapchainInterop&) = delete;
    VulkanD3D12SwapchainInterop& operator=(
        const VulkanD3D12SwapchainInterop&) = delete;

    [[nodiscard]] HRESULT initialize(
        const VulkanSessionBinding& binding,
        ID3D12Device* d3d12_device,
        ID3D12CommandQueue* d3d12_queue,
        const VulkanImageDescription& description,
        std::span<const VkImage> source_images,
        std::span<const VkImage> current_destination_images,
        std::span<const VkImage> synthetic_destination_images,
        VulkanInteropInitializationStage* failure_stage = nullptr) noexcept;

    [[nodiscard]] std::span<ID3D12Resource* const> source_images()
        const noexcept override;
    [[nodiscard]] std::span<ID3D12Resource* const>
    current_destination_images() const noexcept override;
    [[nodiscard]] std::span<ID3D12Resource* const>
    synthetic_destination_images() const noexcept override;
    [[nodiscard]] HRESULT prepare_capture(
        std::uint32_t source_index) noexcept override;
    [[nodiscard]] HRESULT finish_capture() noexcept override;
    [[nodiscard]] HRESULT prepare_synthesis() noexcept override;
    [[nodiscard]] HRESULT publish(
        std::uint32_t current_destination_index,
        std::optional<std::uint32_t> synthetic_destination_index) noexcept
        override;
    [[nodiscard]] HRESULT publication_fence(
        ID3D12Fence** fence,
        std::uint64_t* value) const noexcept override;
    [[nodiscard]] HRESULT wait_for_idle() noexcept override;
    [[nodiscard]] bool initialized() const noexcept override;

private:
    struct Impl;

    mutable std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xrfg
