void timing_sample_cohorts() {
    InterfaceTimingWindow window;
    for(unsigned i=0;i<3;++i) {
        constexpr std::int64_t ms=1'000'000;
        auto cycle=std::make_shared<InterfaceCycle>((80+i)*ms);
        auto real=std::make_shared<InterfaceDeliveryReceipt>(false);
        cycle->attach_native_delivery(real);
        const auto entered=(i==0?110:i==1?160:170)*ms;
        if(i) {
            auto generated=std::make_shared<InterfaceDeliveryReceipt>(true);
            generated->synthetic_confirmed=true;
            generated->completed(true,entered-11*ms,entered-10*ms,0);
            cycle->attach_native_delivery(generated);
            // A confirmed output without an observed physical period must
            // reject Minimum only; its real native timing remains observed.
        }
        real->completed(true,entered,entered+ms,0);
        cycle->call((100+i)*ms,(105+i)*ms);cycle->producer_returned((105+i)*ms);
        InterfaceFrameStamp stamp{1000+i,(100+i)*ms,(99+i)*ms,cycle};
        check(window.submitted(stamp,(106+i)*ms,true,true,false,true),"cohort fixture real accepted");
    }
    InterfaceTelemetry observed;window.snapshot(200'000'000,observed);
    check(observed.delay_valid && observed.cycle_estimated_valid,"partial native period metadata does not erase independent timing");
    check(std::abs(observed.ofxr_delay_ms-59.F)<.001F && std::abs(observed.cycle_estimated_ms-31.F)<.001F,
        "each metric uses its own valid same-source samples");
    check(observed.cycle_estimated_ms<observed.ofxr_delay_ms,
        "medians with different valid sample cohorts cannot be ordered as one physical frame");
}
