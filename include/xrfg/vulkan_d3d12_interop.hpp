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
// The depth formats the Vulkan bridge hands a D3D12 runtime for a Vulkan
// depth swapchain, or DXGI_FORMAT_UNKNOWN.
[[nodiscard]] DXGI_FORMAT dxgi_depth_format_for_vulkan(VkFormat format) noexcept;
// The reverse of both, for a D3D12 runtime's format list shown to a Vulkan
// application; VK_FORMAT_UNDEFINED where there is no Vulkan equivalent.
[[nodiscard]] VkFormat vulkan_format_for_dxgi(DXGI_FORMAT format) noexcept;

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
        std::optional<std::uint32_t> synthetic_destination_index,
        std::optional<std::uint32_t> extra_synthetic_destination_index) noexcept
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

// How a Vulkan application's image reaches the runtime's on a bridged
// session. Logged in the swapchain's vulkan_bridge record.
enum class VulkanBridgePath : std::uint32_t {
    // The application renders into the shared texture, imported as its
    // swapchain image; at release it is copied into the runtime's image.
    direct = 0,
    // Depth: the application renders into a depth image of the layer's own
    // and nothing reaches the runtime's image. The layer strips the depth
    // information naming this swapchain from every submission, so the
    // runtime composes without depth. A D3D12 depth texture cannot be
    // shared, and a Vulkan depth image cannot be given a D3D12 runtime, so
    // this is the only shape depth can take on the bridge.
    depth_private = 4,
};

struct VulkanBridgeSwapchainDescription {
    // The application's create info, before translation.
    VkFormat requested_format{VK_FORMAT_UNDEFINED};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t array_size{1};
    std::uint32_t mip_levels{1};
    std::uint32_t sample_count{1};
    bool unordered_access{};
    bool depth_stencil{};
};

// One application swapchain of a bridged Vulkan session: the runtime (a
// D3D12 session on the layer's device) owns D3D12 images, the layer owns
// D3D12 shared textures of the same shape, and the application renders into
// those textures through Vulkan images imported from them - the same import
// VulkanD3D12SwapchainInterop makes, in the other role. Why a Vulkan session
// is handed a D3D12 runtime at all: the interop mirrors every image three
// times over (measured 24 copies per swapchain in No Man's Sky); bridged,
// the layer's paths are the native D3D12 ones and nothing is mirrored.
//
// Every method returns immediately; the two sides are ordered on one shared
// fence, and the only CPU waits are at initialization and teardown.
class VulkanBridgeSwapchain final {
public:
    VulkanBridgeSwapchain() noexcept;
    ~VulkanBridgeSwapchain();
    VulkanBridgeSwapchain(const VulkanBridgeSwapchain&) = delete;
    VulkanBridgeSwapchain& operator=(const VulkanBridgeSwapchain&) = delete;

    // Creates one shared texture per runtime image, in the runtime image's
    // own shape, imports each into the application's device, and makes the
    // fence both sides signal. Fails, with the stage in `failure_stage`, if
    // nothing shareable can be made for the shape.
    [[nodiscard]] HRESULT initialize(
        const VulkanSessionBinding& binding,
        ID3D12Device* d3d12_device,
        ID3D12CommandQueue* d3d12_queue,
        std::span<ID3D12Resource* const> runtime_images,
        const VulkanBridgeSwapchainDescription& description,
        std::uint32_t* failure_stage) noexcept;

    [[nodiscard]] VulkanBridgePath path() const noexcept;
    [[nodiscard]] DXGI_FORMAT shared_format() const noexcept;
    // What the application is given: the imported images on the direct
    // path, its own depth images on the other.
    [[nodiscard]] std::span<const VkImage> vulkan_images() const noexcept;
    [[nodiscard]] std::span<ID3D12Resource* const> shared_images() const noexcept;

    // The application is about to render into image `index` (its wait
    // returned): its queue waits, on the GPU, for the layer's last read of
    // the shared texture.
    [[nodiscard]] HRESULT before_write(std::uint32_t index) noexcept;

    // The application released image `index`: its queue signals once the
    // rendering is done, the layer's queue waits and copies the shared
    // texture into the runtime's image. What the layer queues after this on
    // the same queue - the history capture - reads the shared texture in
    // order.
    [[nodiscard]] HRESULT release(std::uint32_t index) noexcept;

    // Everything the layer will read from image `index` this frame has been
    // queued: signal, so the next before_write of this image waits for it.
    [[nodiscard]] HRESULT mark_read(std::uint32_t index) noexcept;

    [[nodiscard]] HRESULT wait_for_idle() noexcept;

private:
    struct Impl;
    mutable std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xrfg
