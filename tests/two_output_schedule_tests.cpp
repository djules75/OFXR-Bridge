#include "xrfg/two_output_schedule.hpp"
#include <stdexcept>
#include <iostream>
int main() {
    using namespace std::chrono;
    xrfg::TwoOutputSchedule schedule;
    auto now = xrfg::TwoOutputSchedule::Clock::time_point{} + seconds(1);
    const auto period = nanoseconds(11'111'111);
    const auto check = [](bool value) { if (!value) throw std::runtime_error("two-output schedule"); };
    try {
        auto slot = schedule.claim(now, period);
        check(slot == now); // No prefill or mandatory initial latency.
        schedule.submitted(slot, period);
        check(schedule.claim(now + milliseconds(2), period) == now + period);
        for (int i=1; i<=100; ++i) {
            auto deadline = now + i*period;
            slot = schedule.claim(deadline + microseconds(500), period);
            check(slot == deadline); // Wake lateness must not accumulate.
            schedule.submitted(slot, period);
        }
        now += seconds(5);
        check(schedule.claim(now, period) == now); // Starvation: no catch-up burst.
        schedule.submitted(now, period);
        check(schedule.claim(now + microseconds(1), period) == now + period);
        // NVIDIA charges a blocking End to the elapsed interval; it must not
        // add a fresh whole interval after End returns. Small overruns retain
        // the grid, while starvation reanchors without a catch-up burst.
        schedule.submitted(now, period);
        check(schedule.claim(now + period + microseconds(500), period) == now + period);
        check(schedule.claim(now + 3*period, period) == now + 3*period);
        // FSR's already validated long-End recovery remains unchanged.
        schedule.submitted(now, period, now + milliseconds(25));
        check(schedule.claim(now + milliseconds(25), period) == now + milliseconds(25) + period);
        std::cout << "Immediate first output, stable grid, bounded starvation recovery\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
