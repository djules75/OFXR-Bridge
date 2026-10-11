#pragma once
#include <chrono>

namespace xrfg {
// A pending output is eligible immediately on startup or after starvation.
// Normal wake-up lateness must not move every later slot by the same amount.
class TwoOutputSchedule {
public:
    using Clock = std::chrono::steady_clock;
    Clock::time_point claim(Clock::time_point now, std::chrono::nanoseconds period) noexcept {
        if (!valid_ || period <= std::chrono::nanoseconds::zero() || now >= next_ + period)
            next_ = now;
        valid_ = period > std::chrono::nanoseconds::zero();
        return next_;
    }
    void submitted(Clock::time_point slot, std::chrono::nanoseconds period,
                   Clock::time_point completed = {}) noexcept {
        next_ = slot + period;
        // A runtime call that spans an entire slot has consumed the margin of
        // the next one. Do not hand B over immediately after a stalled S.
        // Normal call cost / wake jitter leave the fixed grid unchanged.
        if (completed >= next_) next_ = completed + period;
        valid_ = period > std::chrono::nanoseconds::zero();
    }
private:
    Clock::time_point next_{};
    bool valid_{};
};
}
