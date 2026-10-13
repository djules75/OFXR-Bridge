#include "xrfg/vulkan_native_ingress.hpp"

namespace xrfg::native_vulkan {
// V457 owner scope: suspend Vulkan SR/RR guide capture entirely. The shipping
// layer installs no NGX Vulkan, device, image or command-stream hooks and creates
// no private guide-transport queue. XR color transport remains independent.
// Preserve the previous implementation for future work and isolated fixtures.
void configure_owner(const VulkanSessionBinding&,ID3D12Device*,ID3D12CommandQueue*) noexcept {}
void stop_owner(VkDevice) noexcept {}
void poll_capture() noexcept {}
VkDevice configure_luke_owner(ID3D12Device*,ID3D12CommandQueue*) noexcept {return {};}
bool query_luke_context(ID3D12Device** device,ID3D12CommandQueue** queue) noexcept {
    if(device)*device=nullptr;if(queue)*queue=nullptr;return false;
}
}
