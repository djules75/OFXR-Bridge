#pragma once
#include <chrono>

namespace xrfg {
// New NVIDIA director: a delayed midpoint must not have its exposure stolen
// by the real endpoint catching up to a deadline from before the midpoint.
// Ordinary wake jitter retains the fixed grid and full-refresh throughput.
class MidpointSchedule {
public:
    using Clock = std::chrono::steady_clock;
    Clock::time_point claim(Clock::time_point now, std::chrono::nanoseconds period,
                           bool midpoint) noexcept {
        if (!valid_ || period <= std::chrono::nanoseconds::zero() ||
            now >= next_ + period || (midpoint && now > next_ + period / 8))
            next_ = now;
        valid_ = period > std::chrono::nanoseconds::zero();
        return next_;
    }
    void submitted(Clock::time_point slot, std::chrono::nanoseconds period) noexcept {
        next_ = slot + period;
        valid_ = period > std::chrono::nanoseconds::zero();
    }
private:
    Clock::time_point next_{};
    bool valid_{};
};
}
