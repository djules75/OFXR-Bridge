void luke_fresh_pacing_test(bool startup=false) {
    const auto original=xrfg::embedded::snapshot().desired;
    auto control=original; control.enabled=false;
    check(xrfg_fixture::request_control(control),"disable test profile");
    auto afw=xrfg::embedded::afw_snapshot().desired; afw.mode=0;
    check(xrfg::embedded::request_afw(afw),"AFW off");
    auto s=session();
    s->presenter_active=false; s->three_slot_director=false;
    s->luke_afw_enabled=true; s->luke_fresh_only=true;
    s->luke_source_filter_armed=!startup;
    s->menu_enabled=false; s->dlss_motion_vectors=false;
    s->control_revision=xrfg::embedded::snapshot().revision;
    struct RuntimeJournal { bool waited{},begun{};XrTime time{};unsigned errors{}; } journal;
    static RuntimeJournal* runtime;runtime=&journal;
    s->dispatch->wait_frame=+[](XrSession h,const XrFrameWaitInfo* in,XrFrameState* out) {
        if(runtime->waited || runtime->begun) {++runtime->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        const auto result=fake_wait(h,in,out);runtime->waited=true;runtime->time=out->predictedDisplayTime;return result;
    };
    s->dispatch->begin_frame=+[](XrSession h,const XrFrameBeginInfo* in) {
        if(!runtime->waited || runtime->begun) {++runtime->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        runtime->begun=true;return fake_begin(h,in);
    };
    s->dispatch->end_frame=+[](XrSession h,const XrFrameEndInfo* in) {
        if(!runtime->begun) {++runtime->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(in->layers[0]);
        if(in->layerCount!=1 || in->displayTime!=runtime->time || p->space!=reinterpret_cast<XrSpace>(20) ||
            p->viewCount!=1 || p->views[0].pose.position.x!=17 || p->views[0].pose.orientation.w!=1) ++runtime->errors;
        const auto result=fake_end(h,in);runtime->waited=runtime->begun=false;return result;
    };
    s->dispatch->release_swapchain_image=+[](XrSwapchain,const XrSwapchainImageReleaseInfo*){return XR_SUCCESS;};
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    auto chain=std::make_shared<SwapchainState>(s,create);
    chain->handle=reinterpret_cast<XrSwapchain>(0x991);
    {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;g_swapchains[chain->handle]=chain;}
    waits=begins=ends=0;presented.clear();
    XrCompositionLayerProjectionView view{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    view.subImage.swapchain=chain->handle; view.pose.orientation.w=1; view.pose.position.x=17;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space=reinterpret_cast<XrSpace>(20);projection.viewCount=1;projection.views=&view;
    const auto* base=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.layerCount=1;end.layers=&base;
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    XrTime previous{};
    auto cycle=[&](std::optional<xrfg::LukeRenderIdentity> id) {
        XrFrameState out{XR_TYPE_FRAME_STATE};
        check(XR_SUCCEEDED(layer_wait_frame_impl(s->handle,nullptr,&out)),"fresh virtual wait");
        check(out.predictedDisplayTime>previous,"monotonic virtual time"); previous=out.predictedDisplayTime;
        check(XR_SUCCEEDED(layer_begin_frame_impl(s->handle,nullptr)),"fresh virtual begin");
        chain->acquired_indices.push_back(0);chain->waited_count=1;
        check(XR_SUCCEEDED(release_swapchain_with_identity(chain,nullptr,id)),"fresh release");
        check(chain->acquired_indices.empty() && !chain->waited_count,"release ownership balanced");
        end.displayTime=out.predictedDisplayTime;
        check(XR_SUCCEEDED(layer_end_frame_impl(s->handle,&end)),"fresh end");
        check(s->pending_frames.empty(),"dropped virtual ticket not consumed");
        check(!s->presenter_active && !s->pipelined_presenter_mode,"unexpected presenter promotion");
    };
    const xrfg::LukeRenderIdentity first{0x12340000,20};
    if(startup) {
        // Real application APIs, with actual composition and strict physical
        // runtime ordering: no source receipt must still display loading.
        for(unsigned mode=0;mode<4;++mode) {
            control.enabled=(mode&2)!=0;
            check(xrfg_fixture::request_control(control),"startup OFXR mode");
            afw.mode=(mode&1)?1:0;
            check(xrfg::embedded::request_afw(afw),"startup AFW mode");
            s->control_revision=xrfg::embedded::snapshot().revision;s->menu_enabled=control.enabled;
            cycle({});
            check(ends==mode+1 && !s->luke_source_filter_armed,"loading image filtered or synthesis entered");
        }
        control.enabled=false;check(xrfg_fixture::request_control(control),"startup return off");
        afw.mode=0;check(xrfg::embedded::request_afw(afw),"startup AFW return off");
        s->control_revision=xrfg::embedded::snapshot().revision;s->menu_enabled=false;
        chain->frame_generation=std::make_shared<FrameGenerationSwapchainState>();
        chain->luke_admitted_identity=first;
        cycle(first);cycle(first);
        check(ends==6 && !s->luke_source_filter_armed,"stationary producer armed early");
        cycle(xrfg::LukeRenderIdentity{first.object,21});
        check(ends==7 && s->luke_source_filter_armed,"advancing producer not admitted/armed");
        cycle(xrfg::LukeRenderIdentity{first.object,21});cycle({});
        check(ends==7,"armed filter repeated or unknown source admitted");
        cycle(xrfg::LukeRenderIdentity{first.object,22});
        check(waits==8 && begins==8 && ends==8,"next real source lost");
        check(!journal.errors && !journal.waited && !journal.begun,"startup physical metadata/ordering failed");
        {std::scoped_lock lock(g_state_mutex);g_swapchains.erase(chain->handle);g_sessions.erase(s->handle);}
        check(xrfg_fixture::request_control(original),"restore startup profile");
        std::cout<<"HFW startup: four modes pass actual loading composition, stationary source stays unarmed, advancing source arms, gameplay duplicates remain filtered\n";
        return;
    }
    xrfg::configure_dlss_motion_vector_tracking(true);
    cycle(first);
    check(xrfg::dlss_motion_vector_statistics().status==xrfg::DlssMotionVectorStatus::disabled,"warm off leaves native tracking active");
    check(waits==1 && begins==1 && ends==1,"first physical cycle");
    check(chain->luke_released_identity==first,"off source receipt lost without capture");
    // Duplicate release must avoid synthesis waits even after a warm generator
    // exists. Exercise the actual End gate in all four control combinations.
    chain->frame_generation=std::make_shared<FrameGenerationSwapchainState>();
    for(unsigned mode=0;mode<4;++mode) {
        control.enabled=(mode&2)!=0;check(xrfg_fixture::request_control(control),"switch test OFXR");
        afw.mode=(mode&1)?1:0;check(xrfg::embedded::request_afw(afw),"switch test AFW");
        s->control_revision=xrfg::embedded::snapshot().revision;s->menu_enabled=control.enabled;
        s->synthesis_busy=true;
        cycle(first);
        s->synthesis_busy=false;
        check(waits==1 && begins==1 && ends==1,"duplicate entered physical XR cycle");
        check(!chain->last_released_capture,"duplicate became new history");
    }
    control.enabled=false;check(xrfg_fixture::request_control(control),"return off");
    afw.mode=0;check(xrfg::embedded::request_afw(afw),"return AFW off");
    s->control_revision=xrfg::embedded::snapshot().revision;s->menu_enabled=false;
    cycle({});check(ends==1,"unknown source replayed stale image");
    cycle(xrfg::LukeRenderIdentity{first.object,21});
    check(waits==2 && begins==2 && ends==2,"next source not delivered exactly once");
    check(view.pose.position.x==17 && view.pose.orientation.w==1,"source pose rewritten");
    // An internal generated/real second cycle has already Waited/Begun. The
    // common output owner must not open it a second time.
    XrFrameState physical{XR_TYPE_FRAME_STATE};
    s->dispatch->wait_frame(s->handle,nullptr,&physical);s->dispatch->begin_frame(s->handle,nullptr);
    s->luke_output_begun=true;s->luke_output_slot=physical;
    check(XR_SUCCEEDED(content_end_frame(s->handle,&end)),"adopt internal cycle");
    check(waits==3 && begins==3 && ends==3,"internal double wait/begin");
    check(!journal.errors && !journal.waited && !journal.begun,"strict physical ordering/metadata failed");
    {std::scoped_lock lock(g_state_mutex);g_swapchains.erase(chain->handle);g_sessions.erase(s->handle);}
    check(xrfg_fixture::request_control(original),"restore test profile");
    std::cout<<"HFW: actual Wait/Begin/Release/End, all four duplicate gates, warm bypass, unknown source, new source and adopted internal cycle passed\n";
}
