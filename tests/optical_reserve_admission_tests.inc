void optical_reserve_scope_test() {
    auto& s=*session_state;
    s.dispatch->virtual_desktop_runtime=true;s.vdxr_legacy=true;s.queued_nvidia_presenter=true;
    s.graphics_binding=SessionGraphicsBinding::d3d12;s.nvidia_new_route=true;
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    s.optical_flow_backend=xrfg::D3D12OpticalFlowBackend::nvidia;
    require(adaptive_optical_reserve_route(s),"VDXR optical Normal did not select adaptive reserve");
    s.menu_enabled=false;require(!adaptive_optical_reserve_route(s),"FG off trained/admitted a reserve");s.menu_enabled=true;
    s.dispatch->virtual_desktop_runtime=false;require(!adaptive_optical_reserve_route(s),"other runtime changed");s.dispatch->virtual_desktop_runtime=true;
    s.vdxr_legacy=false;require(!adaptive_optical_reserve_route(s),"private comparison changed");s.vdxr_legacy=true;
    s.lower_vram=1;require(!adaptive_optical_reserve_route(s),"one-ring outputs admitted overlapping writes");s.lower_vram=0;
    s.luke_afw_enabled=true;require(!adaptive_optical_reserve_route(s),"Luke changed");s.luke_afw_enabled=false;
    s.nvidia_new_route=false;require(!adaptive_optical_reserve_route(s),"reference ring contract changed");s.nvidia_new_route=true;
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::fsr3;require(!adaptive_optical_reserve_route(s),"FSR changed");
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::native_dlss;require(!adaptive_optical_reserve_route(s),"DLSS-G changed");
    s.frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;s.hybrid_requested=true;
    require(!adaptive_optical_reserve_route(s),"Hybrid changed");s.hybrid_requested=false;
    s.three_slot_director=true;require(!adaptive_optical_reserve_route(s),"different director changed");
    std::cout<<"Adaptive reserve scope: VDXR native D12 optical two-ring queued only PASS\n";
}

void optical_reserve_admission_test(bool old_control=false) {
    auto s=session_state;
    s->dispatch->virtual_desktop_runtime=true;s->vdxr_legacy=true;s->queued_nvidia_presenter=true;
    s->nvidia_new_route=true;s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    s->presenter_active=true;s->presenter_display_period=20'000'000;s->optical_reserve_ns=3'000'000;
    s->optical_reserve_pair=1;s->optical_reserve_served=0;
    auto current=std::make_shared<PresenterSubmission>();current->sequence=2;
    s->presenter_submissions.push_back(current);s->outstanding_presenter_submissions=1;
    const auto entered=std::chrono::steady_clock::now();
    s->optical_reserve_current_due=entered+std::chrono::milliseconds(60);
    ComPtr<ID3D12Fence> retired;
    hr(s->d3d12_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&retired)),"retirement fence");
    std::atomic_bool early{};
    std::jthread retirement([&] {
        std::this_thread::sleep_until(entered+std::chrono::milliseconds(65));
        retired->Signal(1);
        {std::scoped_lock lock(s->presenter_mutex);s->presenter_submissions.clear();s->outstanding_presenter_submissions=0;}
        s->presenter_condition.notify_all();
    });
    const auto result=old_control?wait_for_presenter_capacity(s,0):wait_for_optical_reserve_admission(s);
    const auto admitted=std::chrono::steady_clock::now();
    early=retired->GetCompletedValue()==0;
    retirement.join();
    require(XR_SUCCEEDED(result),"adaptive preparation admission failed");
    require(early,"production still waits for complete prior-pair retirement");
    require(admitted>=entered+std::chrono::milliseconds(56),"fractional reserve admitted a full frame early");
    require(s->optical_reserve_served==1,"one synthetic receipt was not consumed exactly once");
    // A third pair cannot reuse that same admission. Failure/stop must wake it.
    s->presenter_submissions.push_back(current);s->outstanding_presenter_submissions=1;
    std::atomic_bool escaped{};
    std::jthread blocked([&]{const auto r=wait_for_optical_reserve_admission(s);escaped=r==XR_ERROR_SESSION_NOT_RUNNING;});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {std::scoped_lock lock(s->presenter_mutex);s->presenter_stop_requested=true;}
    s->presenter_condition.notify_all();blocked.join();
    require(escaped,"reused same pair receipt or stop did not wake admission");
    s->presenter_submissions.clear();
    s->presenter_stop_requested=false;s->presenter_failure=XR_ERROR_RUNTIME_FAILURE;
    require(wait_for_optical_reserve_admission(s)==XR_ERROR_RUNTIME_FAILURE,"runtime failure was swallowed");
    std::cout<<"Actual admission: 3ms lead, controlled retirement receipt still pending, one admission per S, stop/failure escapes PASS\n";
}

void optical_reserve_consumer_test(bool old_control=false) {
    auto s=session_state;
    s->dispatch->virtual_desktop_runtime=true;s->vdxr_legacy=true;s->queued_nvidia_presenter=true;
    s->nvidia_new_route=true;s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    s->presenter_active=true;s->presenter_display_period=10'000'000;
    D3D12_COMMAND_QUEUE_DESC description{};description.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr(s->d3d12_device->CreateCommandQueue(&description,IID_PPV_ARGS(&s->d3d12_queue)),"reserve consumer queue");
    auto frame=[](unsigned id) {
        auto f=std::make_shared<GeneratedFrameEndInfo>();f->info.layerCount=1;f->synthetic=id%2!=0;
        XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};q.pose.position.x=static_cast<float>(id);
        f->composition_layers.emplace_back(q);f->layer_pointers.push_back(composition_layer_header(f->composition_layers.back()));
        f->info.layers=f->layer_pointers.data();return f;
    };
    s->dispatch->wait_frame=[](XrSession,const XrFrameWaitInfo*,XrFrameState* f)->XrResult {
        runtime_time+=10'000'000;f->predictedDisplayTime=runtime_time;f->predictedDisplayPeriod=10'000'000;f->shouldRender=XR_TRUE;return XR_SUCCESS;
    };
    s->dispatch->begin_frame=[](XrSession,const XrFrameBeginInfo*)->XrResult{return XR_SUCCESS;};
    static std::function<XrResult(const XrFrameEndInfo*)> observe;
    std::vector<unsigned> outputs;
    std::vector<std::chrono::steady_clock::time_point> times;
    observe=[&](const XrFrameEndInfo* end)->XrResult {
        if(!end || !end->layerCount)return XR_ERROR_LAYER_INVALID;
        outputs.push_back(static_cast<unsigned>(reinterpret_cast<const XrCompositionLayerQuad*>(end->layers[0])->pose.position.x));
        times.push_back(std::chrono::steady_clock::now());
        return XR_SUCCESS;
    };
    s->dispatch->end_frame=[](XrSession,const XrFrameEndInfo* f)->XrResult{return observe(f);};
    {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;}
    std::jthread consumer([&]{three_slot_director_main(s);});
    HANDLE production_timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    XrResult result=enqueue_presenter_pair(s,frame(1),frame(2));
    std::int64_t previous_budget{};bool monotonic=true;unsigned maximum_queued=2;
    for(unsigned pair=1;pair<190 && XR_SUCCEEDED(result);++pair) {
        const auto budget=s->optical_reserve_ns.load();monotonic &= budget>=previous_budget;previous_budget=budget;
        result=old_control ? wait_for_presenter_capacity(s,0) : wait_for_native_preparation_admission(s,true);
        if(XR_FAILED(result))break;
        if(budget<=0 || old_control) {
            std::scoped_lock lock(s->presenter_mutex);
            s->optical_reserve_served=s->optical_reserve_pair;
        }
        // Controlled production cost slightly longer than one display slot.
        // The real consumer must learn/retain a fractional preparation lead.
        LARGE_INTEGER production_delay{};production_delay.QuadPart=-125'000;
        if(!production_timer || !SetWaitableTimer(production_timer,&production_delay,0,nullptr,nullptr,FALSE) ||
            WaitForSingleObject(production_timer,1000)!=WAIT_OBJECT_0) {result=XR_ERROR_RUNTIME_FAILURE;break;}
        result=old_control ? enqueue_presenter_pair(s,frame(2*pair+1),frame(2*pair+2)) : publish_native_generated_pair(s,frame(2*pair+1),frame(2*pair+2));
        {std::scoped_lock lock(s->presenter_mutex);
         maximum_queued=(std::max)(maximum_queued,static_cast<unsigned>(s->outstanding_presenter_submissions));}
    }
    if(XR_SUCCEEDED(result))result=wait_for_presenter_capacity(s,0);
    const auto learned=s->optical_reserve_ns.load();
    {std::scoped_lock lock(s->presenter_mutex);s->presenter_stop_requested=true;}
    s->presenter_condition.notify_all();consumer.join();observe={};
    if(production_timer)CloseHandle(production_timer);
    {std::scoped_lock lock(g_state_mutex);g_sessions.erase(s->handle);}
    require(XR_SUCCEEDED(result),"actual optical reserve consumer failed");
    require(learned>0 && learned<=7'500'000 && monotonic,"actual consumer failed to learn/retain sub-frame reserve");
    require(outputs.size()==380,"reserve consumer emitted empty/repeated/missing outputs");
    require(maximum_queued<=3,"reserve consumer accumulated a whole additional pair");
    for(unsigned i=0;i<outputs.size();++i)require(outputs[i]==i+1,"reserve consumer reordered output identities");
    const auto jitter=[&](unsigned first,unsigned last) {
        std::vector<double> error;
        for(unsigned i=first;i<last;++i)error.push_back(std::abs(
            std::chrono::duration<double,std::milli>(times[i]-times[i-1]).count()-10.0));
        std::sort(error.begin(),error.end());return error[error.size()*9/10];
    };
    const auto cold=jitter(1,64),settled=jitter(260,380);
    std::cout<<"Reserve consumer diagnostics: budget="<<learned<<"ns, p90 error "<<cold<<" -> "<<settled<<"ms\n";
    require(settled<cold*.75,"learned headroom did not reduce actual delivery jitter");
    std::cout<<"Actual VDXR queued consumer: auto-trained "<<learned<<"ns, p90 interval error "<<cold<<" -> "<<settled<<"ms, 380 exact FIFO outputs PASS\n";
}

void optical_reserve_activation_test() {
    auto s=session_state;
    s->dispatch->virtual_desktop_runtime=true;s->vdxr_legacy=true;s->queued_nvidia_presenter=true;
    s->nvidia_new_route=true;s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    s->presenter_active=true;s->presenter_display_period=11'111'111;s->optical_reserve_ns=500'000;
    // The controller just activated. S1's B has already retired; S3/B4 are
    // queued by the preceding, unreserved admission. S1 is NOT a free credit
    // to append another whole pair. This is the V669 activation regression.
    s->optical_reserve_pair=1;s->optical_reserve_served=0;
    s->next_presenter_sequence=3;
    s->optical_reserve_current_due=std::chrono::steady_clock::now()-std::chrono::milliseconds(20);
    auto frame=[](unsigned id) {
        auto f=std::make_shared<GeneratedFrameEndInfo>();f->synthetic=id%2!=0;
        return f;
    };
    require(XR_SUCCEEDED(enqueue_presenter_pair(s,frame(3),frame(4))),"activation initial pair");
    std::atomic_bool admitted{};XrResult result{};
    std::chrono::steady_clock::time_point returned;
    std::jthread producer([&]{result=wait_for_optical_reserve_admission(s);returned=std::chrono::steady_clock::now();admitted=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool stale_credit_consumed=admitted.load();
    std::chrono::steady_clock::time_point current_due;
    {std::scoped_lock lock(s->presenter_mutex);
     s->presenter_submissions.pop_front();s->outstanding_presenter_submissions=1;
     s->optical_reserve_pair=3;
     current_due=s->optical_reserve_current_due=std::chrono::steady_clock::now()+std::chrono::milliseconds(12);}
    s->presenter_condition.notify_all();producer.join();
    require(!stale_credit_consumed,"stale synthetic receipt admitted a whole additional pair at activation");
    require(XR_SUCCEEDED(result) && returned>=current_due-std::chrono::microseconds(800),"new reserve admitted more than its fractional lead");
    require(s->optical_reserve_served==3,"activation consumed wrong synthetic receipt");
    require(XR_SUCCEEDED(enqueue_presenter_pair(s,frame(5),frame(6))) && s->outstanding_presenter_submissions==3,
        "one old B plus one new S/B must be the maximum transient queue");
    s->presenter_submissions.clear();s->outstanding_presenter_submissions=2;
    const auto rejected=enqueue_presenter_pair(s,frame(7),frame(8));
    s->presenter_submissions.clear();s->outstanding_presenter_submissions=0;
    require(XR_FAILED(rejected),"adaptive queue admitted two complete outstanding pairs");
    // Stop must also escape the timed fractional wait, rather than waiting
    // for a distant deadline after dropping the presenter mutex.
    s->presenter_submissions.push_back(std::make_shared<PresenterSubmission>());
    s->outstanding_presenter_submissions=1;s->optical_reserve_pair=5;
    s->optical_reserve_current_due=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    std::jthread stopped([&]{result=wait_for_optical_reserve_admission(s);});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {std::scoped_lock lock(s->presenter_mutex);s->presenter_stop_requested=true;}
    s->presenter_condition.notify_all();stopped.join();
    require(result==XR_ERROR_SESSION_NOT_RUNNING,"timed reserve wait did not escape on stop");
    s->presenter_submissions.clear();s->outstanding_presenter_submissions=0;
    std::cout<<"Reserve activation: stale credits blocked, fractional lead only, max B/S/B (three outputs), never two full pairs PASS\n";
}
