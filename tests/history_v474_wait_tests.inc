void test_v474_bounded_history_wait(D3D12WarpFixture& fixture) {
    auto source=create_source_texture(fixture);
    ID3D12Resource* sources[]{source.Get()};xrfg::D3D12SwapchainHistory history;
    require_hresult(history.initialize(fixture.device(),fixture.queue(),sources,D3D12_RESOURCE_STATE_RENDER_TARGET),"bounded history init");
    const auto capture=[&](xrfg::D3D12HistoryCaptureWait* wait=nullptr) {
        xrfg::D3D12HistoryCaptureTicket ticket{};
        require_hresult(history.capture(0,&ticket,nullptr,wait),"bounded capture");
        require_hresult(history.commit(ticket),"bounded commit");return ticket;
    };
    xrfg::D3D12HistoryCaptureWait free_wait{20000};auto first=capture(&free_wait);
    require(!free_wait.slot_busy && free_wait.waited_us==0,"free slot must never wait");
    auto first_resources=acquire_capture_for_inspection(history,first);
    fixture.wait_for_fence(first_resources.fence.Get(),first.fence_value);
    ComPtr<ID3D12Fence> consumer;
    require_hresult(fixture.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumer)),"bounded consumer fence");
    struct Unblock {ID3D12Fence* fence;~Unblock(){fence->Signal(100);}} unblock{consumer.Get()};
    require_hresult(history.retire_consumer(first_resources.lease,consumer.Get(),1),"submitted consumer");
    for(unsigned i=1;i<history.kSlotCount;++i)capture();
    xrfg::D3D12HistoryCaptureTicket rejected{};
    require(history.capture(0,&rejected)==HRESULT_FROM_WIN32(ERROR_BUSY) && rejected.serial==0,"default shared API remains nonblocking");
    auto signal=std::async(std::launch::async,[&]{std::this_thread::sleep_for(std::chrono::milliseconds(2));return consumer->Signal(1);});
    xrfg::D3D12HistoryCaptureWait bounded{20000};auto second=capture(&bounded);
    require_hresult(signal.get(),"release consumer fence");
    require(bounded.slot_busy && bounded.waited_us>0,"busy consumer completes inside one budget");
    auto resources=acquire_capture_for_inspection(history,second);
    fixture.wait_for_fence(resources.fence.Get(),second.fence_value);
    require_hresult(history.retire_consumer(resources.lease,consumer.Get(),2),"second submitted consumer");
    for(unsigned i=1;i<history.kSlotCount;++i)capture();
    xrfg::D3D12HistoryCaptureWait timeout{3000};
    require(history.capture(0,&rejected,nullptr,&timeout)==HRESULT_FROM_WIN32(ERROR_BUSY) && rejected.serial==0,
        "budget exhaustion must not overwrite GPU-owned history");
    require(timeout.slot_busy && timeout.waited_us>=2000 && consumer->GetCompletedValue()==1,"bounded timer actually expires");
    // Trigger the old registered event, then keep a different fence value busy.
    require_hresult(consumer->Signal(2),"stale completion signal");
    resources=acquire_capture_for_inspection(history,second);
    require_hresult(history.retire_consumer(resources.lease,consumer.Get(),3),"third consumer");
    xrfg::D3D12HistoryCaptureWait stale{3000};
    require(history.capture(0,&rejected,nullptr,&stale)==HRESULT_FROM_WIN32(ERROR_BUSY) && consumer->GetCompletedValue()==2,
        "stale event cannot grant reuse for an unfinished fence");
    require(stale.waited_us>=2000,"stale event cannot shorten timeout");
    require_hresult(consumer->Signal(3),"finish third consumer");
    // An active, not-yet-submitted CPU lease has no fence capable of clearing it.
    resources=acquire_capture_for_inspection(history,second);
    xrfg::D3D12HistoryCaptureWait leased{20000};
    require(history.capture(0,&rejected,nullptr,&leased)==HRESULT_FROM_WIN32(ERROR_BUSY),"CPU lease remains exclusive");
    require(leased.waited_us<10000,"no timed wait for an unsubmitted CPU lease");
    history.cancel_consumer(resources.lease);
    capture();require_hresult(history.wait_for_idle(),"bounded history retirement");
    fixture.require_no_debug_errors();
    std::cout<<"V474 actual history fence completion/timeout/stale-event/CPU-lease PASS\n";
}
