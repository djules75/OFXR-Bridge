// Actual Locate/Wait/Begin and downstream inline/queued handover paths.
void blackbar_timing_layer_test() {
    auto s=session();s->presenter_active=false;s->three_slot_director=false;
    s->nvidia_new_route=false;s->djules_route=false;s->menu_enabled=true;
    s->minimum_runtime_display_period=2000000;
    s->dispatch->locate_views=+[](XrSession,const XrViewLocateInfo* info,XrViewState* flags,
        std::uint32_t capacity,std::uint32_t* count,XrView* views)->XrResult {
        *count=2;if(capacity<2)return XR_ERROR_SIZE_INSUFFICIENT;
        flags->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        for(unsigned i=0;i<2;++i) {views[i].pose.orientation={0,0,0,1};
            views[i].pose.position={float(info->displayTime%1000000000)/1000000000.F,float(i),0};}
        return XR_SUCCESS;
    };
    {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;}
    struct Cleanup {XrSession session;~Cleanup(){xrfg::autonomous::r19_camera_enabled=false;
        std::scoped_lock lock(g_state_mutex);g_sessions.erase(session);}} cleanup{s->handle};
    auto on=[&](bool enabled){xrfg::autonomous::r19_camera_enabled=enabled;blackbar_gate(*s);};
    on(false);
    check(blackbar_promise_shift(*s,next_time,2000000,false)==0,"OFF promise unchanged");
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.space=reinterpret_cast<XrSpace>(0x4720);
    locate.displayTime=next_time;XrViewState flags{XR_TYPE_VIEW_STATE};std::uint32_t count=0;
    XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    check(XR_SUCCEEDED(layer_locate_views(s->handle,&locate,&flags,2,&count,views)),"real locate hook");
    check(s->blackbar_timing.located_next==0,"OFF does not collect poses");
    const auto run_inline=[&](bool lag,unsigned outputs) {
        on(false);on(true);next_time=1000000000;s->pending_frames.clear();
        s->application_wait_pending_begin=false;s->application_frame_in_progress=false;
        BlackBarPoseTime previous{};XrPosef previous_pose{};
        for(unsigned i=0;i<360;++i) {
            XrFrameState prediction{XR_TYPE_FRAME_STATE};
            check(XR_SUCCEEDED(layer_wait_frame_impl(s->handle,nullptr,&prediction)),"production wait");
            check(XR_SUCCEEDED(layer_begin_frame_impl(s->handle,nullptr)),"production begin");
            check(s->blackbar_inline_slot==s->last_inline_frame_state.predictedDisplayTime,"physical slot unchanged");
            if(i>240) check(prediction.predictedDisplayTime-s->blackbar_inline_slot==s->blackbar_inline_shift,
                "Begin freezes the correction of the current physical slot");
            locate.displayTime=prediction.predictedDisplayTime;
            check(XR_SUCCEEDED(layer_locate_views(s->handle,&locate,&flags,2,&count,views)),"production locate");
            auto real=frame(i+1)->frame;auto& p=real->projections[0];p.layer.space=locate.space;
            p.views.resize(2);p.layer.viewCount=2;p.layer.views=p.views.data();
            for(unsigned eye=0;eye<2;++eye)p.views[eye].pose=views[eye].pose;
            real->info.displayTime=prediction.predictedDisplayTime;
            auto current=blackbar_pose_time(*s,real->info);
            if(lag && i) {p.views[0].pose=previous_pose;
                check(blackbar_pose_time(*s,real->info).time==previous.time,"R21 matches prior pose instead of frame stamp");}
            real->blackbar_pose=blackbar_pose_time(*s,real->info);real->blackbar_pose.paired=true;
            previous=current;previous_pose=views[0].pose;
            check(XR_SUCCEEDED(s->dispatch->end_frame(s->handle,&real->info)),"first physical slot");
            // Generated slots already consumed; x3 adds one intermediate.
            for(unsigned output=1;output<outputs;++output) {
                const bool synthetic=output+1<outputs;
                auto result=submit_current_cycle(s,real->info,0,nullptr,std::nullopt,real.get(),synthetic);
                check(result.completed,"inline production handover");
                if(i>240 && !synthetic) check(result.display_time==real->blackbar_pose.time,
                    "real handed over at located pose time after correction");
            }
            // This fixture consumes the app cycle at its already-forwarded physical End.
            s->application_frame_in_progress=false;s->pending_frames.clear();
        }
        check(s->blackbar_timing.inline_promise.correction==int((outputs-1)+(lag?outputs:0)),
            "inline learned exact x2/x3 and previous-game-frame pose delay");
    };
    run_inline(false,2);run_inline(true,2);run_inline(false,3);
    const auto epoch=s->blackbar_timing.epoch;
    BlackBarPoseTime stale{next_time,0,epoch,true};on(false);on(true);
    blackbar_observe(*s,stale,next_time+10000000,2000000,2000000,true);
    check(!s->blackbar_timing.presenter.count,"toggle discards immutable old-epoch feedback");
    // R21 freezes matching time BEFORE queue residence; overwriting locate ring cannot mutate it.
    locate.displayTime=next_time;
    check(XR_SUCCEEDED(layer_locate_views(s->handle,&locate,&flags,2,&count,views)),"queue source locate");
    auto real=frame(1)->frame;auto& projection=real->projections[0];projection.layer.space=locate.space;
    projection.views.resize(1);projection.views[0].pose=views[0].pose;projection.views[0].subImage.swapchain=reinterpret_cast<XrSwapchain>(0x4721);
    projection.layer.viewCount=1;projection.layer.views=projection.views.data();real->info.displayTime=next_time;
    ProjectionSnapshot snapshot;check(capture_projection_snapshot(&real->info,&snapshot),"capture source");
    snapshot.blackbar_pose=blackbar_pose_time(*s,real->info);
    std::array destinations{ProjectionResourceDestination{reinterpret_cast<XrSwapchain>(0x4721),reinterpret_cast<XrSwapchain>(0x4721),false}};
    GeneratedFrameEndInfo owned;
    check(build_generated_frame_end_info(&real->info,snapshot,false,destinations,&owned),"immutable owned metadata");
    for(unsigned i=0;i<70;++i){locate.displayTime+=2000000;layer_locate_views(s->handle,&locate,&flags,2,&count,views);}
    check(owned.blackbar_pose.time==real->info.displayTime,"queued pose time survives locate ring eviction");
    // Exercise actual presenter consumer, successful real handover only.
    s->three_slot_director=true;s->presenter_active=true;s->presenter_display_period=2000000;
    s->presenter_schedule_valid=false;owned.blackbar_pose.paired=true;
    auto queued=std::make_shared<GeneratedFrameEndInfo>(std::move(owned));
    auto request=enqueue_presenter_submission(s,queued,nullptr);
    std::thread worker([&]{three_slot_director_main(s);});
    check(XR_SUCCEEDED(wait_for_presenter_submission(s,request)),"actual queued consumer handover");
    {std::scoped_lock lock(s->presenter_mutex);s->presenter_stop_requested=true;}
    s->presenter_condition.notify_all();worker.join();
    check(s->blackbar_timing.presenter.count==1 && s->blackbar_timing.presenter.running,
        "queued consumer feeds existing pose controller once");
    on(false);check(blackbar_promise_shift(*s,next_time,2000000,true)==0,"live OFF removes correction");
    check(!s->blackbar_timing.presenter.count && !s->blackbar_timing.inline_promise.correction,
        "live OFF clears both learned routes");
    // Distributed EndFrame branch must use the begun physical slot when Wait
    // advanced its promise, even when this frame falls back to a real-only output.
    const auto original=xrfg::autonomous::snapshot().desired;
    auto desired=original;desired.enabled=true;desired.backend=0;desired.r19_camera=true;
    struct Restore {xrfg::autonomous::Settings settings;~Restore(){
        static_cast<void>(xrfg::autonomous::request(settings));xrfg::autonomous::apply_runtime_settings();}} restore{original};
    check(xrfg::autonomous::request(desired),"real live checkbox request");
    xrfg::autonomous::apply_runtime_settings();
    auto live=session();live->presenter_active=false;live->three_slot_director=false;
    live->graphics_binding=SessionGraphicsBinding::none;live->nvidia_new_route=false;live->djules_route=false;
    apply_embedded_control(live);blackbar_gate(*live);
    live->blackbar_timing.inline_promise.running=true;live->blackbar_timing.inline_promise.correction=1;
    static XrTime received{};
    live->dispatch->end_frame=+[](XrSession,const XrFrameEndInfo* info)->XrResult {received=info->displayTime;return XR_SUCCESS;};
    {std::scoped_lock lock(g_state_mutex);g_sessions[live->handle]=live;}
    XrFrameState advanced{XR_TYPE_FRAME_STATE};
    check(XR_SUCCEEDED(layer_wait_frame_impl(live->handle,nullptr,&advanced)),"advanced live Wait");
    check(XR_SUCCEEDED(layer_begin_frame_impl(live->handle,nullptr)),"advanced live Begin");
    const auto physical=live->blackbar_inline_slot;
    check(advanced.predictedDisplayTime==physical+2000000,"Wait advanced prediction only");
    XrFrameEndInfo single{XR_TYPE_FRAME_END_INFO};single.displayTime=advanced.predictedDisplayTime;
    single.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    check(XR_SUCCEEDED(layer_end_frame_impl(live->handle,&single)),"actual EndFrame physical handover");
    check(received==physical,"real downstream EndFrame uses runtime slot, not advanced promise");
    std::cout<<"R20 inline x2/x3; R21 UEVR prior poses; immutable queue; actual consumer; live OFF passed\n";
}
