#include "xrfg/private_output_distributor.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
int main(){try {
    constexpr std::int64_t ms=1'000'000;
    xrfg::PrivateOutputSchedule clock;
    clock.period(11*ms);clock.ready(20*ms);
    require(clock.eligible(20*ms) && clock.delivered(20*ms),"first output");
    clock.ready(30*ms);
    require(!clock.eligible(30*ms) && clock.delivered(31*ms),"short second delivery permitted");
    clock.ready(40*ms);require(clock.delivered(42*ms),"third output");
    clock.ready(57*ms);
    require(clock.delay()==4*ms && clock.deadline()==57*ms,"late head rounded to a refresh");
    clock.ready(57*ms);require(clock.delay()==4*ms,"same lateness accumulated twice");
    require(clock.delivered(57*ms),"late head lost");
    clock.ready(60*ms);
    require(clock.delay()==4*ms && !clock.eligible(64*ms) && clock.delivered(68*ms),"late output followed by burst catchup");
    clock.ready(69*ms);
    require(clock.delivered(83*ms) && clock.delay()==8*ms,"CPU handoff delay not retained");
    clock.ready(84*ms);require(!clock.eligible(90*ms) && clock.delivered(94*ms),"post-overrun interval shorter than refresh");
    xrfg::PrivateOutputSchedule adaptive;
    adaptive.period(11*ms);require(adaptive.input_ready(20*ms,3)==0,"initial frame unnecessarily prebuffered");
    adaptive.ready(20*ms);require(adaptive.delivered(20*ms),"adaptive first output");
    require(adaptive.input_ready(35*ms,3)==4*ms && adaptive.deadline()==35*ms,"source lateness did not build an exact refill margin");
    adaptive.ready(36*ms);require(adaptive.delivered(36*ms) && adaptive.deadline()==51*ms,"refill was not retained after delivering the late frame");
    require(adaptive.input_ready(45*ms,3)==0,"early source added an unnecessary wait");
    adaptive.ready(46*ms);require(adaptive.delivered(51*ms) && adaptive.delay()==9*ms,"refill phase was lost or charged on every frame");
    // A 75fps producer on a90Hz display must keep EVERY packet, not await
    // imaginary90fps input. Refills are bounded by owned pool capacity and
    // otherwise ready input receives no fixed per-frame latency tax.
    for(int i=1;i<100;++i) {
        const auto source=51*ms+i*13'333'333LL;
        const auto before=adaptive.deadline();
        const auto added=adaptive.input_ready(source,3);
        require(added==std::min(std::max(std::int64_t{},source-before),22*ms),"adaptive margin rounded to an entire refresh");
        adaptive.ready(source);const auto at=std::max(source,adaptive.deadline());
        require(adaptive.delivered(at),"sub-refresh-rate input was dropped");
    }
    const auto paused=adaptive.deadline()+10'000*ms;
    require(adaptive.input_ready(paused,3)==22*ms,"paused producer caused an unbounded replay pause");
    for(auto period:{8'333'333LL,11'111'111LL,13'333'333LL}) {
        xrfg::PrivateOutputSchedule c;c.period(period);c.ready(1);
        auto at=std::int64_t{1};require(c.delivered(at),"fresh refresh clock");
        for(int i=0;i<200;++i){c.ready(at+1);require(!c.eligible(at+period-1),"refresh minus1ns accepted");at+=period;require(c.delivered(at),"fixed refresh cadence");}
        // Early preparation changes neither cadence nor held lateness, even
        // over many refreshes. It is not period-minus100us pacing.
        for(int i=0;i<10000;++i) {
            c.ready(at+1);
            const auto due=c.deadline(),prepare=c.preparation_deadline();
            require(due-prepare==100'000 && !c.eligible(prepare),"preparation borrowed a refresh");
            require(c.deadline()==due && c.delay()==0,"early preparation moved phase");
            at+=period;require(c.delivered(at),"early preparation changed frequency");
        }
        c.ready(at+period+250'000);
        require(c.delay()==250'000 && c.delivered(at+period+250'000),"late preparation discarded exact overrun");
        at+=period+250'000;c.ready(at+1);
        require(!c.eligible(c.preparation_deadline()) && c.delivered(at+period),"late preparation caused catch-up");
    }
    // Producer completion depends only on taking ownership of a private slot.
    // A pending consumer handoff keeps its slot until explicit retirement.
    xrfg::PrivateOutputFifo<unsigned> fifo(2);
    require(!fifo.accept(99),"unreserved acceptance");
    require(fifo.reserve(2) && fifo.accept(10) && fifo.accept(20),"private pair acceptance");
    auto a=fifo.pop();require(a && *a==10 && fifo.occupied()==2,"dequeue freed an in-use image");
    std::promise<void> entered;auto signaled=entered.get_future();
    auto producer=std::async(std::launch::async,[&]{entered.set_value();return fifo.reserve() && fifo.accept(30);});
    signaled.wait();
    require(producer.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"full pool did not backpressure producer");
    fifo.retire();require(producer.get(),"producer not released by slot retirement");
    auto b=fifo.pop();auto c=fifo.pop();require(b && c && *b==20 && *c==30,"FIFO order/old output overwritten");
    fifo.retire(2);require(fifo.occupied()==0,"retired pool leaked slots");
    require(fifo.reserve(2),"reservation reuse");fifo.cancel(2);require(fifo.occupied()==0,"failed producer reservation leaked");
    require(fifo.reserve(2),"stop control reserve");
    auto blocked=std::async(std::launch::async,[&]{return fifo.reserve();});
    fifo.stop();require(!blocked.get() && !fifo.pop(),"stop did not release producer/consumer");
    std::cout<<"Private distributor: 100us preparation without drift over10000refreshes; exact held lateness; strict90/120/75Hz; FIFO ownership; backpressure; stop PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
