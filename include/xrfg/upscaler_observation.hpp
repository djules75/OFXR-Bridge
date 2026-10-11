#pragma once
#include <atomic>
#include <cstdint>
#include <windows.h>

namespace xrfg::native_ingress {
// Passive input observation at typed SR/RR evaluates. No GPU capture, waits,
// resource retention or upscaler substitution is needed in optical-flow mode.
struct UpscalerObservation {
    std::uint64_t evaluations{}, motion_inputs{}, depth_inputs{}, last_seen_ms{};
    bool motion{}, depth{};
};
inline std::atomic_uint64_t observed_evaluations{}, observed_motion{}, observed_depth{}, observed_last{};
inline std::atomic_uint observed_inputs{};
inline void observe_upscaler_inputs(bool motion, bool depth) noexcept {
    ++observed_evaluations;
    if (motion) ++observed_motion;
    if (depth) ++observed_depth;
    observed_inputs.store((motion ? 1U : 0U) | (depth ? 2U : 0U));
    observed_last.store(GetTickCount64());
}
inline UpscalerObservation upscaler_observation() noexcept {
    const auto flags = observed_inputs.load();
    return {observed_evaluations.load(), observed_motion.load(), observed_depth.load(),
        observed_last.load(), (flags & 1U) != 0, (flags & 2U) != 0};
}
}
