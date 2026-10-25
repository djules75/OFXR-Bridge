void bounded_delivery_credit_test(std::size_t capacity,bool directed) {
    Fixture f(false,true,{},true,true,true,capacity);f.throttled=false;
    // This test deliberately blocks native End for20ms. Its runtime prediction
    // must cover that controlled stall also in Framework bypass (which does
    // not renew/persist Director clocks). Keep the same11.111ms physical period
    // and the strict current-cycle assertion; only extend the fake XR horizon.
    f.prediction_horizon_ns=100'000'000;
    if(!directed)xr_ok(f.transport->set_director_enabled(false),"two-credit Framework bypass");
    auto chain=f.make_chain();
    unsigned image_count{};xr_ok(f.transport->enumerate_images(chain,0,&image_count,nullptr),"private image capacity");
    check(image_count==3,"delivery credit limit must retain three image slots");
    f.write(chain,31);f.begin_virtual();xr_ok(f.submit(chain,1),"first owned output");f.wait_entered();
    f.write(chain,62);f.begin_virtual();xr_ok(f.submit(chain,2),"preparing output overlaps presenting output");
    auto third=std::async(std::launch::async,[&]{f.write(chain,93);f.begin_virtual();return f.submit(chain,3);});
    const bool third_waited=third.wait_for(20ms)==std::future_status::timeout;
    f.unblock();
    check(third.wait_for(3s)==std::future_status::ready,"producer did not resume on real retirement");
    xr_ok(third.get(),"third output accepted after credit");f.wait_count(3);xr_ok(f.transport->drain(),"bounded queue drain");
    check(third_waited==(capacity==2),"two-credit admission or three-credit control broken");
    check(f.pixels==std::vector<unsigned>({31,62,93}),"bounded admission changed or dropped pixels");
    check(f.positions==std::vector<float>({1,2,3}),"bounded admission changed FIFO poses");
    if(directed)for(unsigned i=1;i<f.handoffs.size();++i)
        check(f.handoffs[i]-f.handoffs[i-1]>=period-100'000,"bounded admission shortened physical cadence");
    xr_ok(f.transport->destroy_swapchain(chain),"bounded queue cleanup");f.transport->stop();
    check(f.real_begins==f.real_ends+f.empty_ends+f.discarded_clocks,"bounded admission native protocol unbalanced");
    std::puts(capacity==2?"Two delivery credits, three images, real-retirement backpressure, exact pixels/order PASS":"Original three-credit admission control PASS");
}

void two_credit_stop_test() {
    Fixture f(false,true,{},true,true,true,2);f.throttled=false;
    auto chain=f.make_chain();f.write(chain,17);f.begin_virtual();xr_ok(f.submit(chain,1),"stop test first");f.wait_entered();
    f.write(chain,34);f.begin_virtual();xr_ok(f.submit(chain,2),"stop test second");
    auto third=std::async(std::launch::async,[&]{f.write(chain,51);f.begin_virtual();return f.submit(chain,3);});
    const bool waiting=third.wait_for(20ms)==std::future_status::timeout;
    auto stop=std::async(std::launch::async,[&]{f.transport->stop();});
    const bool woke=third.wait_for(1s)==std::future_status::ready;
    f.unblock();check(stop.wait_for(3s)==std::future_status::ready,"two-credit stop failed to join");stop.get();
    check(waiting && woke,"stop failed to wake admission waiter");
    check(XR_FAILED(third.get()),"stopped producer accepted orphan output");
    check(f.real_begins==f.real_ends+f.empty_ends+f.discarded_clocks,"two-credit stop left native cycle unbalanced");
    std::puts("Two-credit stop wakes blocked producer and balances native lifecycle PASS");
}
