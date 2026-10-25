#include "xrfg/presented_output_rate.hpp"
#pragma once
#include "xrfg/autonomous_control.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include <cmath>

namespace xrfg {
inline bool interface_measurement_requested() noexcept {
    return autonomous::interface_telemetry_requested() || bridge_flight_logger().enabled();
}
inline std::uint32_t benchmark_milli(float value) noexcept {
    return std::isfinite(value) && value >= 0 && value < 4'000'000.F
        ? static_cast<std::uint32_t>(std::llround(value * 1000.0)) : 0;
}
inline void log_benchmark_telemetry(const InterfaceTelemetry& value) noexcept {
    auto& logger = bridge_flight_logger();
    if (!logger.enabled()) return;
    const auto control = autonomous::snapshot();
    if (control.runtime.applied_revision != control.revision) return;
    logger.event(BridgeFlightOperation::benchmark_rates,
        (output_rate_valid(value) ? 1 : 0) | (static_cast<int>(value.output_source) << 8),
        (std::uint64_t(benchmark_milli(value.output_fps)) << 32) | benchmark_milli(value.game_fps),
        (std::uint64_t(benchmark_milli(value.generated_fps)) << 32) | benchmark_milli(value.display_hz), control.revision);
    logger.event(BridgeFlightOperation::benchmark_cycles,
        (value.ofxr_time_valid ? 1 : 0) | (value.cycle_valid ? 2 : 0) | (value.cycle_estimated_valid ? 4 : 0),
        (std::uint64_t(benchmark_milli(value.ofxr_time_ms)) << 32) | benchmark_milli(value.cycle_length_ms),
        benchmark_milli(value.cycle_estimated_ms), control.revision);
    logger.event(BridgeFlightOperation::benchmark_stages,(value.stages_valid?1:0)|(value.game_time_valid?2:0),
        (std::uint64_t(benchmark_milli(value.game_time_ms))<<32)|benchmark_milli(value.generation_time_ms),
        benchmark_milli(value.delivery_time_ms),control.revision);
}
}
