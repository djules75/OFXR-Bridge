#include "xrfg/optical_pacing_reserve.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() try {
    constexpr std::int64_t p=11'111'111;
    xrfg::OpticalPacingReserve c;
    std::int64_t old{}, t=1'000'000'000;
    // Closed loop: an extra 3ms arrives on S. Early preparation buys that
    // margin; the observed short/long cadence disappears once it is covered.
    for(int i=0;i<1600;++i) {
        const auto due=t+i*p;
        const auto late=(i%2==0) ? (std::max)(std::int64_t{},3'000'000-c.budget()) : 0;
        const auto budget=c.observe(due+late,due,p);
        check(budget>=old && budget<=p*3/4,"margin decayed or exceeded sub-frame cap");old=budget;
    }
    check(c.budget()>=2'500'000 && c.budget()<=3'500'000,"bounded jitter did not converge to a few milliseconds");
    const auto learned=c.budget();
    for(int i=0;i<600;++i) {const auto due=t+(1600+i)*p;c.observe(due,due,p);}
    check(c.budget()==learned,"quiet success consumed the learned safety margin");
    c.observe(t+2200*p,t+2200*p,8'333'333);
    check(c.budget()==0,"physical refresh change retained an old reserve");
    xrfg::OpticalPacingReserve slow;
    for(int i=0;i<900;++i) {const auto due=t+i*p;slow.observe(t+i*p*5/4+(i%2)*2'000'000,due,p);}
    check(slow.budget()==0,"sustained 80-percent throughput trained a pacing reserve");
    xrfg::OpticalPacingReserve stable;
    for(int i=0;i<900;++i) {const auto due=t+i*p;stable.observe(due+100'000,due,p);}
    check(stable.budget()==0,"already regular full-rate output acquired latency");
    xrfg::OpticalPacingReserve almost_full;
    std::int64_t missed{};
    for(int i=0;i<900;++i) {
        if(i && i%32==0)missed+=p;
        const auto due=t+i*p+missed;
        almost_full.observe(due+(i%2)*2'000'000,due,p);
    }
    check(almost_full.budget()>0,"occasional missed slot near refresh perpetually reset learning");
    xrfg::OpticalPacingReserve capped;
    for(int i=0;i<4000;++i) {const auto due=t+i*p;capped.observe(due+(i%2)*10'000'000,due,p);}
    check(capped.budget()>0 && capped.budget()<=p*3/4,"near-frame jitter escaped the fractional cap");
    const auto before=capped.budget();
    capped.observe(t+5000*p,t+5000*p,p);
    check(capped.budget()==before,"one large hitch changed the learned floor");
    std::cout<<"Adaptive optical reserve: closed-loop convergence, no decay after recovery, sub-frame cap, refresh reset, stable/underload exclusions PASS\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
