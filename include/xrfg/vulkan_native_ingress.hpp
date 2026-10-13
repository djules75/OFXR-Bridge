#pragma once
#include "vulkan_session_bridge.hpp"

namespace xrfg::native_vulkan {
// Uses the same authoritative D3D12 device as the XR Vulkan bridge, with a
// separate capture queue so a deferred Vulkan producer cannot block XR startup.
void configure_owner(const VulkanSessionBinding&,ID3D12Device*,ID3D12CommandQueue*) noexcept;
void stop_owner(VkDevice device) noexcept;
void poll_capture() noexcept;
// The Vulkan loader layer passes its unchanged device and next-device resolver
// only after successful creation. Observe those actual downstream allocation
// entries as well as REAL VR's public dispatcher; cached internal allocations
// can bypass the public wrapper entirely.
void observe_device_dispatch(VkDevice, PFN_vkGetDeviceProcAddr) noexcept;
// DOOM / Indiana Jones native Vulkan producers are independent of D3D11 XR.
// Select only a creation-observed native device with the exact adapter LUID.
VkDevice configure_luke_owner(ID3D12Device*, ID3D12CommandQueue*) noexcept;
// Published only after the original Vulkan prefix accepted an exact SR/RR
// transport recording; neither device creation nor XR queue proves production.
bool query_luke_context(ID3D12Device**, ID3D12CommandQueue**) noexcept;
}
