void minimum_spacing_rules() {
    constexpr auto P=11'111'111LL;
    float value{};
    InterfaceCycle queued(100'000'000);queued.real_prediction(250'000'000);queued.producer_returned(300'000'000);
    queued.synthetic_accepted(120'250'000,P,120'000'000);
    check(queued.read_minimum_cycle(121'000'000,value) && std::abs(value-31.111111F)<.001F,
        "one-ms S/B acceptance gap still retains one90Hz refresh");
    check(queued.read_minimum_cycle(146'000'000,value) && value==46,
        "already longer observed delivery is retained, never charged another refresh");
    queued.synthetic_accepted(120'250'000,P,120'000'000);
    check(queued.read_minimum_cycle(121'000'000,value) && std::abs(value-31.111111F)<.001F,"duplicate generation receipt cannot add another slot");
    InterfaceCycle triple(100'000'000);
    triple.synthetic_accepted(120'250'000,P,120'000'000);triple.synthetic_accepted(121'250'000,P,121'000'000);
    check(triple.read_minimum_cycle(122'000'000,value) && std::abs(value-42.222222F)<.001F,"3X retains two refresh separations");
    InterfaceCycle late_second(100'000'000);
    late_second.synthetic_accepted(120'250'000,P,120'000'000);late_second.synthetic_accepted(136'250'000,P,136'000'000);
    check(late_second.read_minimum_cycle(137'000'000,value) && std::abs(value-47.111111F)<.001F,
        "late intermediate output retains one remaining refresh without inventing a phase grid");
    InterfaceCycle high_refresh(100'000'000);high_refresh.synthetic_accepted(120'250'000,8'333'333,120'000'000);
    check(high_refresh.read_minimum_cycle(121'000'000,value) && std::abs(value-28.333333F)<.001F,"120Hz uses8.333ms, not a hardcoded11ms");
    InterfaceCycle unknown(100'000'000);unknown.synthetic_accepted(120'000'000,0);
    check(!unknown.read_minimum_cycle(121'000'000,value),"unknown generated-output refresh cannot fabricate a minimum");
    InterfaceCycle changed(100'000'000);changed.synthetic_accepted(120'000'000,P);changed.synthetic_accepted(121'000'000,8'333'333);
    check(!changed.read_minimum_cycle(122'000'000,value),"mid-pair physical refresh change has no consistent bound");
    // A runtime can hold S End after S was already displayed. Entry is the
    // earliest-possible image time; using its return would add a false slot.
    InterfaceCycle native(100'000'000);native.synthetic_accepted(105'000'000,P,104'000'000);
    auto s=std::make_shared<InterfaceDeliveryReceipt>(true),b=std::make_shared<InterfaceDeliveryReceipt>(false);
    s->synthetic_confirmed=true;native.attach_native_delivery(s);native.attach_native_delivery(b);
    s->completed(true,120'000'000,135'000'000,250'000'000);
    check(!native.read_minimum_cycle(110'000'000,value),"virtual real acceptance cannot close a pending native cycle");
    b->completed(true,136'000'000,137'000'000,260'000'000);
    check(native.read_minimum_cycle(110'000'000,value) && value==37,"long S End cannot double-charge already elapsed display spacing");
    InterfaceCycle compressed(100'000'000);compressed.synthetic_accepted(105'000'000,P,104'000'000);
    auto cs=std::make_shared<InterfaceDeliveryReceipt>(true),cb=std::make_shared<InterfaceDeliveryReceipt>(false);
    cs->synthetic_confirmed=true;compressed.attach_native_delivery(cs);compressed.attach_native_delivery(cb);
    cs->completed(true,120'000'000,120'250'000,250'000'000);cb->completed(true,121'000'000,121'250'000,260'000'000);
    check(compressed.read_minimum_cycle(110'000'000,value) && std::abs(value-31.111111F)<.001F,
        "native Framework burst uses native timing, not earlier virtual acceptance or future prediction");
}
