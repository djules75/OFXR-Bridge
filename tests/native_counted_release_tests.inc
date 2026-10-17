void native_counted_scope_test() {
    auto& s=*session_state;s.dispatch->steamvr_runtime=true;
    s.graphics_binding=SessionGraphicsBinding::d3d12;s.queued_nvidia_presenter=true;s.nvidia_new_route=true;
    require(steamvr_counted_native_release(s),"native SteamVR2X not selected");
    s.dispatch->steamvr_runtime=false;require(!steamvr_counted_native_release(s),"VDXR policy changed");s.dispatch->steamvr_runtime=true;
    s.graphics_binding=SessionGraphicsBinding::d3d11;require(!steamvr_counted_native_release(s),"D11 changed");
    s.graphics_binding=SessionGraphicsBinding::vulkan;require(!steamvr_counted_native_release(s),"Vulkan changed");s.graphics_binding=SessionGraphicsBinding::d3d12;
    s.luke_afw_enabled=true;require(!steamvr_counted_native_release(s),"Luke changed");s.luke_afw_enabled=false;
    s.lower_vram=1;require(!steamvr_counted_native_release(s),"one-ring lease contract changed");s.lower_vram=0;
    s.nvidia_new_route=false;require(!steamvr_counted_native_release(s),"Reference changed");
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::fsr3;require(steamvr_counted_native_release(s),"FSR missing");
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;s.nvidia_new_route=true;
    s.three_slot_director=true;require(!steamvr_counted_native_release(s),"deep director changed");s.three_slot_director=false;
    s.djules_route=true;s.djules_deep_pipeline=true;require(!steamvr_counted_native_release(s),"deep Djules changed");
    std::cout<<"Counted native scope: SteamVR D12 fresh2X only; VDXR/bridges/Luke/Reference/deep/one-ring retained PASS\n";
}

void native_counted_starvation_test() {
    auto s=session_state;s->presenter_active=true;
    require(XR_SUCCEEDED(wait_for_native_counted_admission(s,true)),"empty presenter admission deadlocked");
    auto pending=std::make_shared<PresenterSubmission>();s->presenter_submissions.push_back(pending);
    require(XR_SUCCEEDED(wait_for_native_counted_admission(s,false)),"SteamVR prime was held");
    s->presenter_failure=XR_ERROR_RUNTIME_FAILURE;
    require(wait_for_native_counted_admission(s,true)==XR_ERROR_RUNTIME_FAILURE,"failure was swallowed");
    s->presenter_failure=XR_SUCCESS;s->presenter_stop_requested=true;
    require(wait_for_native_counted_admission(s,true)==XR_ERROR_SESSION_NOT_RUNNING,"stop did not release admission");
    s->presenter_submissions.clear();
    std::cout<<"Counted native starvation, prime, stop and failure escape PASS\n";
}

void native_counted_progress_test(bool old_control=false) {
    auto s=session_state;s->dispatch->steamvr_runtime=true;s->queued_nvidia_presenter=true;
    s->graphics_binding=SessionGraphicsBinding::d3d12;s->nvidia_new_route=true;
    s->presenter_active=true;s->presenter_display_period=20'000'000;
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr(s->d3d12_device->CreateCommandQueue(&desc,IID_PPV_ARGS(&s->d3d12_queue)),"counted producer queue");
    ComPtr<ID3D12Fence> gate;hr(s->d3d12_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"counted End gate");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"End event");
    hr(gate->SetEventOnCompletion(1,event),"End notification");
    auto frame=[](unsigned id) {
        auto f=std::make_shared<GeneratedFrameEndInfo>();f->info.displayTime=id;f->info.layerCount=1;f->synthetic=(id%2)!=0;
        XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};q.pose.position.x=static_cast<float>(id);
        f->composition_layers.emplace_back(q);f->layer_pointers.push_back(composition_layer_header(f->composition_layers.back()));
        f->info.layers=f->layer_pointers.data();return f;
    };
    s->dispatch->wait_frame=[](XrSession,const XrFrameWaitInfo*,XrFrameState* f)->XrResult {
        runtime_time+=20'000'000;f->predictedDisplayTime=runtime_time;f->predictedDisplayPeriod=20'000'000;f->shouldRender=XR_TRUE;return XR_SUCCESS;
    };
    s->dispatch->begin_frame=[](XrSession,const XrFrameBeginInfo*)->XrResult{return XR_SUCCESS;};
    std::mutex journal;std::condition_variable changed;bool end_entered{};
    std::vector<unsigned> outputs;
    static std::function<XrResult(const XrFrameEndInfo*)> observe;
    observe=[&](const XrFrameEndInfo* end)->XrResult {
        if(!end->layerCount)return XR_ERROR_LAYER_INVALID;
        const auto id=static_cast<unsigned>(reinterpret_cast<const XrCompositionLayerQuad*>(end->layers[0])->pose.position.x);
        {std::scoped_lock lock(journal);outputs.push_back(id);if(id==2)end_entered=true;}
        changed.notify_all();
        if(id==2 && WaitForSingleObject(event,1500)!=WAIT_OBJECT_0)return XR_ERROR_RUNTIME_FAILURE;
        return XR_SUCCESS;
    };
    s->dispatch->end_frame=[](XrSession,const XrFrameEndInfo* f)->XrResult{return observe(f);};
    {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;}
    require(XR_SUCCEEDED(wait_for_native_counted_admission(s,true)),"bootstrap admission");
    require(XR_SUCCEEDED(enqueue_presenter_pair(s,frame(1),frame(2))),"first actual pair");
    std::atomic_bool admitted_before_retirement{};
    std::jthread producer([&] {
        const auto release=old_control?wait_for_presenter_capacity(s,0):wait_for_native_counted_admission(s,true);
        if(XR_SUCCEEDED(release) && XR_SUCCEEDED(wait_for_presenter_capacity(s,2))) {
            std::unique_lock lock(journal);const bool entered=changed.wait_for(lock,std::chrono::seconds(1),[&]{return end_entered;});lock.unlock();
            if(entered && gate->GetCompletedValue()==0) {
                admitted_before_retirement=XR_SUCCEEDED(enqueue_presenter_pair(s,frame(3),frame(4)));
            }
        }
        // Actual producer GPU queue makes the blocked consumer End progress.
        s->d3d12_queue->Signal(gate.Get(),1);
    });
    std::jthread consumer([&]{three_slot_director_main(s);});
    bool drained{};
    {std::unique_lock lock(s->presenter_mutex);drained=s->presenter_condition.wait_for(lock,std::chrono::seconds(3),[&]{return admitted_before_retirement && s->outstanding_presenter_submissions==0;});s->presenter_stop_requested=true;}
    s->presenter_condition.notify_all();consumer.join();producer.join();observe={};CloseHandle(event);
    {std::scoped_lock lock(g_state_mutex);g_sessions.erase(s->handle);}
    require(admitted_before_retirement,"producer still serialized on prior output retirement");
    require(drained && outputs==std::vector<unsigned>({1,2,3,4}),"counted release lost/reordered output or stalled XR");
    std::cout<<"Actual queued consumer: producer admitted while previous End awaits producer GPU, bounded second pair, FIFO1/2/3/4 PASS\n";
}
