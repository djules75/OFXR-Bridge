// Exercise the distributed API hooks and their actual pending-frame ownership.
void interface_cycle_layer_test() {
    const auto original=xrfg::embedded::snapshot().desired;
    auto disabled=original;disabled.enabled=false;
    check(xrfg_fixture::request_control(disabled), "cycle fixture disabled generation");
    auto s=session();s->presenter_active=false;s->graphics_binding=SessionGraphicsBinding::none;
    s->menu_enabled=false;
    std::atomic<std::int64_t> accepted{};
    static std::atomic<std::int64_t>* acceptance;
    acceptance=&accepted;
    s->dispatch->end_frame=+[](XrSession,const XrFrameEndInfo*) -> XrResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        acceptance->store(xrfg::interface_clock_ns());return XR_SUCCESS;
    };
    s->dispatch->acquire_swapchain_image=+[](XrSwapchain,const XrSwapchainImageAcquireInfo*,std::uint32_t* index) -> XrResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));*index=0;return XR_SUCCESS;
    };
    s->dispatch->wait_swapchain_image=+[](XrSwapchain,const XrSwapchainImageWaitInfo*) -> XrResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));return XR_SUCCESS;
    };
    s->dispatch->release_swapchain_image=+[](XrSwapchain,const XrSwapchainImageReleaseInfo*) -> XrResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));return XR_SUCCESS;
    };
    XrSwapchainCreateInfo creation{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    creation.width=creation.height=32;creation.arraySize=creation.faceCount=creation.mipCount=creation.sampleCount=1;
    auto chain=std::make_shared<SwapchainState>(s,creation);
    chain->handle=reinterpret_cast<XrSwapchain>(0x4650);
    {
        std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;g_swapchains[chain->handle]=chain;
    }
    struct Cleanup {
        std::shared_ptr<SessionState> s;std::shared_ptr<SwapchainState> chain;xrfg::embedded::Settings previous;
        ~Cleanup() {
            xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,false);
            static_cast<void>(xrfg_fixture::request_control(previous));
            std::scoped_lock lock(g_state_mutex);g_sessions.erase(s->handle);g_swapchains.erase(chain->handle);
        }
    } cleanup{s,chain,original};
    xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,true);
    XrFrameState prediction{XR_TYPE_FRAME_STATE};XrResult waited=XR_ERROR_RUNTIME_FAILURE;
    // A real entry-point lock wait must be inside A, not omitted by a timestamp after locking.
    std::unique_lock hold(s->frame_call_mutex);
    std::atomic<bool> started{};
    std::thread waiting([&]{started=true;waited=layer_wait_frame(s->handle,nullptr,&prediction);});
    while(!started.load())std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));hold.unlock();waiting.join();
    check(XR_SUCCEEDED(waited), "cycle app Wait");
    auto cycle=s->pending_frames.back().interface_cycle;
    check(bool(cycle), "visible Wait owns timing ticket");
    check(XR_SUCCEEDED(layer_begin_frame(s->handle,nullptr)), "cycle app Begin");
    check(s->interface_active_cycle.load()==cycle, "Begin owns its exact Wait ticket");
    std::uint32_t image{};
    check(XR_SUCCEEDED(layer_acquire_swapchain_image(chain->handle,nullptr,&image)), "cycle app Acquire");
    check(XR_SUCCEEDED(layer_wait_swapchain_image(chain->handle,nullptr)), "cycle app image Wait");
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // actual game production gap
    check(XR_SUCCEEDED(layer_release_swapchain_image(chain->handle,nullptr)), "cycle app Release");
    XrFrameState next{XR_TYPE_FRAME_STATE};
    check(XR_SUCCEEDED(layer_wait_frame(s->handle,nullptr,&next)), "cycle overlapping next Wait");
    auto next_cycle=s->pending_frames.back().interface_cycle;
    check(next_cycle && next_cycle!=cycle && s->interface_active_cycle.load()==cycle, "overlap does not replace active frame");
    auto real=frame(1)->frame;real->info.displayTime=prediction.predictedDisplayTime;
    check(XR_SUCCEEDED(layer_end_frame(s->handle,&real->info)), "cycle real accepted by runtime");
    float ofxr{},total{};
    check(cycle->read(accepted.load(),ofxr,total), "producer return completed same frame");
    check(ofxr>=30 && total>=60 && total-ofxr>=25, "full cycle contains game production plus call blocking including entry lock wait");
    check(!s->interface_active_cycle.load() && s->pending_frames.size()==1 &&
        s->pending_frames.front().interface_cycle==next_cycle, "End consumes old ticket and preserves overlapping next ticket");
    check(!next_cycle->read(accepted.load(),ofxr,total), "unended next frame cannot become previous latency sample");
    check(XR_SUCCEEDED(layer_begin_frame(s->handle,nullptr)), "next cycle Begin");
    real->info.displayTime=next.predictedDisplayTime;
    check(XR_SUCCEEDED(layer_end_frame(s->handle,&real->info)), "next cycle End");
    check(next_cycle->read(accepted.load(),ofxr,total), "next frame completes independently");
    xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,false);
    check(XR_SUCCEEDED(layer_wait_frame(s->handle,nullptr,&prediction)) &&
        !s->pending_frames.back().interface_cycle, "hidden UI creates no cycle allocation");
    check(XR_SUCCEEDED(layer_begin_frame(s->handle,nullptr)), "hidden Begin");
    real->info.displayTime=prediction.predictedDisplayTime;
    check(XR_SUCCEEDED(layer_end_frame(s->handle,&real->info)), "hidden End behavior retained");
    // Engine work can start BEFORE the app's late Wait. The submitted poses
    // must carry that exact tick, even though another tick is already running.
    xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,true);
    auto engine=reinterpret_cast<UEVR_UGameEngineHandle>(0x1050);
    uevr_production_timing::begin(engine,0.F);
    const auto engine_start=uevr_production_timing::active.at;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(XR_SUCCEEDED(layer_wait_frame(s->handle,nullptr,&prediction)),"late app Wait after engine production starts");
    auto engine_cycle=s->pending_frames.back().interface_cycle;
    check(engine_cycle && engine_cycle->started_ns-engine_start>=4'000'000,"engine work precedes XR ticket");
    s->dispatch->locate_views=+[](XrSession,const XrViewLocateInfo*,XrViewState* state,std::uint32_t capacity,std::uint32_t* count,XrView* views)->XrResult {
        if(capacity<2)return XR_ERROR_SIZE_INSUFFICIENT;
        *count=2;state->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        for(int i=0;i<2;++i) {views[i]={XR_TYPE_VIEW};views[i].pose.orientation.w=1;views[i].pose.position.x=i?.03F:-.03F;}
        return XR_SUCCESS;
    };
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.displayTime=prediction.predictedDisplayTime;locate.space=reinterpret_cast<XrSpace>(77);
    XrViewState view_state{XR_TYPE_VIEW_STATE};XrView located[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};std::uint32_t count{};
    check(XR_SUCCEEDED(layer_locate_views(s->handle,&locate,&view_state,2,&count,located)),"real locate hook captures engine production identity");
    uevr_production_timing::end(engine,0.F);
    check(!uevr_production_timing::active,"engine post callback clears thread-local production");
    check(XR_SUCCEEDED(layer_begin_frame(s->handle,nullptr)),"engine ticket Begin");
    XrCompositionLayerProjectionView rendered[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(int i=0;i<2;++i)rendered[i].pose=located[i].pose;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=locate.space;projection.viewCount=2;projection.views=rendered;
    const auto* header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=prediction.predictedDisplayTime;end.layerCount=1;end.layers=&header;
    const auto handoff=xrfg::interface_clock_ns();
    check(XR_SUCCEEDED(layer_end_frame(s->handle,&end)),"engine source handed to actual End hook");
    float game_stage{},estimated{};
    check(engine_cycle->production_cycle(handoff,100.F,game_stage,estimated) && game_stage>=4.F,
        "actual engine callback, locate record and End handoff retain pre-Wait production");
    std::cout<<"V465 actual Wait/Begin/Acquire/Wait/Release/End cycle and overlapping identity passed\n";
}
