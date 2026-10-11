#pragma once
#include "vulkan_session_bridge.hpp"

namespace xrfg::native_vulkan {
// Uses the same authoritative D3D12 device as the XR Vulkan bridge, with a
// separate capture queue so a deferred Vulkan producer cannot block XR startup.
void configure_owner(const VulkanSessionBinding&,ID3D12Device*,ID3D12CommandQueue*) noexcept;
void stop_owner(VkDevice device) noexcept;
void poll_capture() noexcept;
}
