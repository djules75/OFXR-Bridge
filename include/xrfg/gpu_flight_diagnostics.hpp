#pragma once
#include "xrfg/bridge_flight_logger.hpp"
#include <d3d12.h>

namespace xrfg {
// V072: CPU observations only; never insert GPU commands or wait for progress.
// result identifies stage; a=fence identity, b=target, c=observed completed.
inline void log_gpu_fence(std::int64_t stage, ID3D12Fence* fence,
                          std::uint64_t target) noexcept {
    auto& log = bridge_flight_logger();
    if (!log.enabled() || fence == nullptr) return;
    const auto completed = fence->GetCompletedValue();
    log.event(BridgeFlightOperation::gpu_fence_snapshot, stage,
        reinterpret_cast<std::uintptr_t>(fence), target, completed);
}
inline void log_gpu_failure(ID3D12Device* device, HRESULT failure,
                            std::uint64_t owner) noexcept {
    auto& log = bridge_flight_logger();
    if (!log.enabled() || SUCCEEDED(failure) || device == nullptr) return;
    log.event(BridgeFlightOperation::gpu_device_status,
        device->GetDeviceRemovedReason(), reinterpret_cast<std::uintptr_t>(device),
        static_cast<std::uint32_t>(failure), owner);
}
}
