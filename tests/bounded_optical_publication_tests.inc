void bounded_optical_preparation_test(bool old_control=false) {
    auto s=session_state;
    s->dispatch->virtual_desktop_runtime=true;s->vdxr_legacy=true;s->queued_nvidia_presenter=true;
    s->nvidia_new_route=true;s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    s->presenter_active=true;s->optical_reserve_ns=500'000;
    s->optical_reserve_pair=1;s->optical_reserve_served=1;
    for(unsigned id:{3,4}) {
        auto p=std::make_shared<PresenterSubmission>();p->sequence=id;p->pair_sequence=3;
        s->presenter_submissions.push_back(p);
    }
    s->outstanding_presenter_submissions=2;
    const auto frame=[] {auto f=std::make_shared<GeneratedFrameEndInfo>();f->info.layerCount=1;return f;};
    D3D12_COMMAND_QUEUE_DESC desc{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> generated;
    hr(s->d3d12_device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"preparation GPU queue");
    hr(s->d3d12_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&generated)),"preparation GPU receipt");
    HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(ready!=nullptr,"preparation event");
    hr(generated->SetEventOnCompletion(1,ready),"preparation receipt event");
    std::atomic_bool published{};XrResult result=XR_SUCCESS;
    std::jthread producer([&] {
        result=old_control?wait_for_optical_reserve_admission(s):wait_for_native_preparation_admission(s,true);
        if(XR_FAILED(result))return;
        if(FAILED(queue->Signal(generated.Get(),1))){result=XR_ERROR_RUNTIME_FAILURE;return;}
        result=old_control?enqueue_presenter_pair(s,frame(),frame()):publish_native_generated_pair(s,frame(),frame());
        published=true;
    });
    // The real preparation entry must permit GPU completion while the exact
    // older S/B is still outstanding and no new S receipt exists.
    const bool generated_before_retirement=WaitForSingleObject(ready,100)==WAIT_OBJECT_0;
    const bool published_before_retirement=published.load();
    {std::scoped_lock lock(s->presenter_mutex);
     s->presenter_submissions.pop_front();s->outstanding_presenter_submissions=1;
     s->optical_reserve_pair=3;
     s->optical_reserve_current_due=std::chrono::steady_clock::now()+std::chrono::milliseconds(10);}
    s->presenter_condition.notify_all();producer.join();CloseHandle(ready);
    require(XR_SUCCEEDED(result),"bounded publication failed after genuine S retirement");
    require(generated_before_retirement,"presentation admission still blocks GPU preparation");
    require(!published_before_retirement,"new ready pair bypassed bounded publication admission");
    require(s->outstanding_presenter_submissions==3 && s->optical_reserve_served==3,
        "publication added a complete older pair or spent stale credit");
    s->presenter_submissions.clear();s->outstanding_presenter_submissions=0;
    std::cout<<"Actual preparation entry: GPU work completes before old S retirement; publication waits, max old B plus fresh S/B, no stale credit PASS\n";
}
