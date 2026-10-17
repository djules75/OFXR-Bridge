void native_input_readiness_test(std::string_view mode) {
    auto s=session_state;
    const bool ready=mode=="input-ready";
    const bool missing=mode=="input-missing";
    const bool acquire_pending=mode=="input-acquire-pending";
    auto device=s->d3d12_device;
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&s->d3d12_queue)),"proof producer queue");
    hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&s->binding_queue)),"proof binding queue");
    ComPtr<ID3D12Fence> output,input,acquire;
    hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fsr_gpu_gate)),"proof gate");
    hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&output)),"proof output");
    hr(device->CreateFence(1,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&input)),"proof input");
    hr(device->CreateFence(acquire_pending?0:1,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&acquire)),"proof acquire");
    hr(device->CreateFence(1,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s->app_release_fence)),"proof app tail");
    hr(s->d3d12_queue->Wait(fsr_gpu_gate.Get(),1),"hold exact output");
    if(acquire_pending)hr(s->d3d12_queue->Signal(acquire.Get(),1),"pending acquire receipt");
    hr(s->d3d12_queue->Signal(output.Get(),1),"queued exact output");
    hr(s->d3d12_queue->Signal(s->app_release_fence.Get(),2),"app tail behind output");
    auto frame=std::make_shared<GeneratedFrameEndInfo>();
    frame->info.displayTime=1'000'000'000;frame->synthetic=true;
    frame->app_release_value=2;frame->input_release_value=1;
    GeneratedFrameEndInfo::Readiness receipt{output,1,true};
    receipt.inputs.captured=!missing;
    receipt.inputs.dependencies[0]={input,1};
    receipt.inputs.dependencies[2]={acquire,1};
    if(mode=="input-source-pending")frame->input_release_value=2;
    frame->reference_readiness.push_back(receipt);
    auto request=std::make_shared<PresenterSubmission>();request->owned_frame=frame;request->sequence=1;
    s->presenter_submissions.push_back(request);s->presenter_display_period=8'333'333;
    s->dispatch->steamvr_runtime=true;s->dispatch->wait_frame=fake_wait;
    s->dispatch->begin_frame=fake_begin;s->dispatch->end_frame=fake_end;
    reference_readiness_test=true;fsr_queue_gpu_pending=!ready;
    std::jthread watchdog([gate=fsr_gpu_gate]{std::this_thread::sleep_for(std::chrono::milliseconds(300));gate->Signal(1);});
    {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;}
    struct Registration {XrSession handle;~Registration(){std::scoped_lock lock(g_state_mutex);g_sessions.erase(handle);}} registration{s->handle};
    std::jthread consumer([s]{three_slot_director_main(s);});
    if(ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        hr(fsr_gpu_gate->Signal(1),"release autonomous output");
    }
    bool completed=false;
    {
        std::unique_lock lock(s->presenter_mutex);
        completed=s->presenter_condition.wait_for(lock,std::chrono::seconds(1),[&]{return request->completed;});
        s->presenter_stop_requested=true;
    }
    s->presenter_condition.notify_all();consumer.join();
    require(completed && XR_SUCCEEDED(request->result),"proof consumer did not complete");
    require(reference_wait_before_gpu_complete!=ready,
        "autonomous output skipped readiness, or pending input blocked XR progress");
    hr(wait_composition_fence(output.Get(),1,1000),"output fence completion");
    std::cout<<mode<<": actual consumer, independent input/acquire proof and pending app tail PASS\n";
}
