void live_buffer_control_test(bool d12,bool directed) {
    Fixture f(d12,true,{},true,true,!d12,2);f.throttled=false;f.unblock();
    if(!directed)xr_ok(f.transport->set_director_enabled(false),"buffer test Framework bypass");
    auto chain=f.make_chain();auto original=f.transport;unsigned identity{};
    auto batch=[&](unsigned count) {
        for(unsigned i=0;i<count;++i){++identity;f.write(chain,identity*8);f.begin_virtual();xr_ok(f.submit(chain,float(identity)),"ordered2X/3X output admitted");}
        f.wait_count(identity);xr_ok(f.transport->drain(),"buffer batch retired");
    };
    check(f.transport->buffer_capacity()==2,"small buffer starts at two");batch(6); // two3-output groups
    xr_ok(f.transport->set_buffer_capacity(3),"increase bridge buffer live");
    check(f.transport->buffer_capacity()==3,"larger buffer active");batch(6);
    xr_ok(f.transport->set_buffer_capacity(2),"reduce bridge buffer live");
    check(f.transport->buffer_capacity()==2,"two-credit buffer restored");batch(6);
    check(f.transport->set_buffer_capacity(1)==XR_ERROR_VALIDATION_FAILURE &&
        f.transport->set_buffer_capacity(4)==XR_ERROR_VALIDATION_FAILURE && f.transport->buffer_capacity()==2,"unsupported limits cannot change active budget");
    unsigned count{};xr_ok(f.transport->enumerate_images(chain,0,&count,nullptr),"unchanged image storage");
    check(count==3 && f.transport==original && f.chains.size()==1,"live buffer change recreated images/session/sink");
    check(f.pixels.size()==identity && f.positions.size()==identity,"live limits dropped/duplicated output");
    for(unsigned i=0;i<identity;++i)check(f.pixels[i]==(i+1)*8 && f.positions[i]==float(i+1),"live limits overwrote/reordered exact pixels/pose");
    if(directed)for(unsigned i=1;i<f.handoffs.size();++i)
        check(f.handoffs[i]-f.handoffs[i-1]>=period-100'000,"buffer change shortened physical cadence");
    xr_ok(f.transport->destroy_swapchain(chain),"live buffer chain cleanup");f.transport->stop();
    check(f.real_begins==f.real_ends+f.empty_ends+f.discarded_clocks,"live buffer change unbalanced native cycles");
    std::puts(d12?"D12 live2->3->2 buffer, sequential3X outputs, exact pixels/order/lifecycle PASS":"D11 live2->3->2 buffer, sequential3X outputs, exact pixels/order/lifecycle PASS");
}
void queued_buffer_change_test(bool stopping) {
    Fixture f(false,true,{},true,true,true,3);f.throttled=false;auto chain=f.make_chain();
    for(unsigned i=1;i<=3;++i){f.write(chain,i*30);f.begin_virtual();xr_ok(f.submit(chain,float(i)),"fill old three-credit buffer");}
    f.wait_entered();auto changing=std::async(std::launch::async,[&]{return f.transport->set_buffer_capacity(2);});
    const bool waited=changing.wait_for(20ms)==std::future_status::timeout;
    if(stopping) {
        auto stop=std::async(std::launch::async,[&]{f.transport->stop();});
        const bool woke=changing.wait_for(1s)==std::future_status::ready;f.unblock();
        check(stop.wait_for(3s)==std::future_status::ready,"stop failed to join buffer transition");stop.get();
        check(waited && woke && XR_FAILED(changing.get()),"stopping failed to cancel queued buffer change");
    } else {
        f.unblock();check(changing.wait_for(3s)==std::future_status::ready,"buffer drain did not finish");xr_ok(changing.get(),"drained buffer reduction");
        check(waited && f.transport->buffer_capacity()==2 && f.pixels==std::vector<unsigned>({30,60,90}),"capacity changed before accepted frames retired");
        f.write(chain,120);f.begin_virtual();xr_ok(f.submit(chain,4),"first output under reduced buffer");f.wait_count(4);xr_ok(f.transport->drain(),"post-change output drain");
        check(f.pixels==std::vector<unsigned>({30,60,90,120}),"buffer transition lost accepted output");f.transport->stop();
    }
    check(f.real_begins==f.real_ends+f.empty_ends+f.discarded_clocks,"queued buffer transition unbalanced native protocol");
}
void buffer_margin_reset_test() {
    Fixture f(true,true,{},true,true,false,3);f.throttled=false;f.unblock();auto chain=f.make_chain();
    f.write(chain,71);ok(f.queue->Wait(f.gate.Get(),1),"park source readiness before its marker");
    f.begin_virtual();xr_ok(f.submit(chain,1),"old larger buffer accepts pending GPU source");
    std::this_thread::sleep_for(35ms);ok(f.gate->Signal(1),"release controlled source wait");
    f.wait_count(1);xr_ok(f.transport->drain(),"large-margin packet retired");
    check(!f.work_margins.empty() && f.work_margins.back()==2*period,"large buffer must learn its two-period work margin");
    xr_ok(f.transport->set_buffer_capacity(2),"reduce work budget without retaining old margin");
    f.write(chain,72);f.begin_virtual();xr_ok(f.submit(chain,2),"fresh small-buffer packet");
    f.wait_count(2);xr_ok(f.transport->drain(),"small-buffer packet retired");
    check(f.work_margins.back()<=period,"old three-credit work margin survived return to two");
    check(f.pixels==std::vector<unsigned>({71,72}),"work budget reset changed pixels");
    xr_ok(f.transport->destroy_swapchain(chain),"margin test cleanup");f.transport->stop();
}
