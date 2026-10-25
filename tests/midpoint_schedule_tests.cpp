#include "xrfg/midpoint_schedule.hpp"
#include <stdexcept>
#include <iostream>
int main() {
    using namespace std::chrono;
    const auto check=[](bool ok){if(!ok)throw std::runtime_error("midpoint schedule");};
    const auto period=nanoseconds(11'111'111);
    auto now=xrfg::MidpointSchedule::Clock::time_point{}+seconds(1);
    xrfg::MidpointSchedule schedule;
    try {
        auto slot=schedule.claim(now,period,false);check(slot==now);
        schedule.submitted(slot,period);
        for(int i=1;i<=100;++i) {
            auto due=now+i*period;
            slot=schedule.claim(due+microseconds(500),period,i%2!=0);
            check(slot==due); // Neither wake jitter nor alternation loses Hz.
            schedule.submitted(slot,period);
        }
        auto due=now+101*period;
        auto late=due+milliseconds(3);
        slot=schedule.claim(late,period,true);
        check(slot==late); // No extra delay for an already-ready S.
        schedule.submitted(slot,period);
        slot=schedule.claim(late+milliseconds(1),period,false);
        check(slot==late+period); // B cannot catch up to the stale grid.
        schedule.submitted(slot,period);
        due=late+2*period;
        slot=schedule.claim(due+microseconds(500),period,true);
        check(slot==due); // Immediately resumes full-refresh fixed grid.
        schedule.submitted(slot,period);
        now+=seconds(10);
        check(schedule.claim(now,period,true)==now); // No catch-up after starvation.
        std::cout<<"Late S keeps full endpoint interval; jitter/full Hz/starvation pass\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
