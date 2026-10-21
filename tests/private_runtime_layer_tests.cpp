// Shipping API/worker/generator code, with only the downstream runtime mocked.
#include "xrfg/benchmark_suite.hpp"
#include <fstream>
#include "../src/layer/openxr_layer.cpp"
#define main private_transport_core_main
#include "private_openxr_runtime_tests.cpp"
#undef main
#include "autonomous_layer_fixture.hpp"
#include <d3d12sdklayers.h>

#include "afw_only_session_test.inc"

static bool framework_session_probe{};
int main(int argc,char** argv) {
    std::string route=argc>1?argv[1]:"new";
    if(route.starts_with("afw-only") || route=="luke-shared-session")return afw_only_session_test(route,argc>2?argv[2]:nullptr);
    const bool shared_budget=route=="luke-shared-budget",unshared_budget=route=="luke-unshared-budget";
    const bool outlaws_budget=route=="outlaws-budget";
    const bool publication_budget=outlaws_budget || shared_budget || unshared_budget;
    if(publication_budget)route="d11-ref";
    const bool benchmark_sequence=route=="benchmark-sequence";
    if(benchmark_sequence)route="solo-djules3";
    const bool off_session_probe=route=="off-pacing-session";if(off_session_probe)route="session";
    framework_session_probe=route=="framework-session";if(framework_session_probe)route="session";
    const bool measured=route.starts_with("metrics-");if(measured)route=route.substr(8);
    if(route.starts_with("no-origin-"))route=route.substr(10);
    const bool deep=route.starts_with("deep-");if(deep)route=route.substr(5);
    const bool toggling=route.starts_with("toggle-");if(toggling)route=route.substr(7);
    const bool blocked=route.starts_with("blocked-");if(blocked)route=route.substr(8);
    const bool sole=route.starts_with("solo-");if(sole)route=route.substr(5);
    const bool canonical=route.starts_with("canonical-");if(canonical)route=route.substr(10);
    const bool pooled=route.starts_with("pooled-");if(pooled)route=route.substr(7);
    const bool unpooled=route.starts_with("unpooled-");if(unpooled)route=route.substr(9);
    const bool split_eyes=pooled || unpooled;
    const bool separate_queue=route.starts_with("separate-");if(separate_queue)route=route.substr(9);
    int a{},b{};if(route.size()>3 && route[route.size()-3]=='-'){a=route[route.size()-2]-'0';b=route.back()-'0';route.resize(route.size()-3);}
    const bool switching=route=="switch";if(switching)route="new";
    const bool luke=route.starts_with("luke-");if(luke)route=route.substr(5);
    const bool native12=!route.starts_with("d11-");if(!native12)route=route.substr(4);
    const bool triple=route.ends_with('3');if(triple)route.pop_back();
    xrfg_fixture::ControlProfile profile;
    if(benchmark_sequence && GetEnvironmentVariableW(L"XRFG_BENCH_TRACE",nullptr,0)) {
        const auto folder=xrfg::autonomous::configuration_path().parent_path()/L"trace";
        std::filesystem::create_directories(folder);
        WritePrivateProfileStringW(L"diagnostics",L"logging_enabled",L"1",(folder/L"ofxr_bridge.ini").c_str());
        xrfg::bridge_flight_logger().initialize(folder);
        std::fprintf(stderr,"TRACE=%s\n",xrfg::bridge_flight_logger().log_path().string().c_str());
    }
    if(off_session_probe) {
        auto desired=xrfg::autonomous::snapshot().desired;desired.enabled=false;desired.pacing_mode=2;desired.afw_mode=3;
        check(xrfg::autonomous::request(desired),"select AFW-only with combined pacing stored before session");
        xrfg::autonomous::apply_runtime_settings();
    }
    if(framework_session_probe) {
        auto desired=xrfg::autonomous::snapshot().desired;desired.pacing_mode=0;
        check(xrfg::autonomous::request(desired),"select framework transport before session");
        xrfg::autonomous::apply_runtime_settings();
    }
    if(sole) {
        auto desired=xrfg::autonomous::snapshot().desired;
        xrfg::autonomous::select_generation(desired,route=="fsr"?1:route=="dlssg"?2:route=="hybrid"?3:0,
            route=="ref"?1:route=="new"?2:route=="djules"?3:0);
        desired.enabled=true;desired.pacing_mode=1;
        desired.lower_vram=b;desired.separate_synthesis=a;
        desired.motion_vectors=route=="fsr" || route=="hybrid" || route=="dlssg";
        desired.triple_frame_gen=triple;desired.prefer_fps=triple || deep;
        check(xrfg::autonomous::request(desired),"publish the global experiment with its actual route");
        xrfg::autonomous::apply_runtime_settings();
    }
    if(luke) {
        auto desired=xrfg::autonomous::snapshot().desired;desired.afw_mode=0;
        check(xrfg::autonomous::request(desired),"isolate Luke color-only OFXR");
        xrfg::autonomous::apply_runtime_settings();
    }
    ComPtr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    Fixture f(native12);f.transport->stop();if(route!="memory" && route!="scope" && !framework_session_probe)f.transport.reset();f.throttled=false;f.real_common=true;f.expected_views=2;f.unblock();
    f.create_state=[] {std::scoped_lock lock(g_state_mutex);return g_swapchains.empty()?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_COMMON;};
    auto dispatch=std::make_shared<Dispatch>();dispatch->instance=reinterpret_cast<XrInstance>(1);dispatch->steamvr_runtime=true;
    if(measured)dispatch->get_instance_proc_addr=+[](XrInstance,const char*,PFN_xrVoidFunction* out)->XrResult{*out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;};
    dispatch->wait_frame=Fixture::wait;dispatch->begin_frame=Fixture::begin;dispatch->end_frame=Fixture::end;
    dispatch->create_swapchain=Fixture::create;dispatch->destroy_swapchain=Fixture::destroy;dispatch->enumerate_swapchain_images=Fixture::enumerate;
    dispatch->acquire_swapchain_image=Fixture::acquire;dispatch->wait_swapchain_image=Fixture::wait_image;dispatch->release_swapchain_image=Fixture::release;dispatch->convert_time_to_qpc=Fixture::to_qpc;
    if(route=="session") {
        dispatch->get_instance_proc_addr=+[](XrInstance,const char*,PFN_xrVoidFunction* out)->XrResult{*out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;};
        dispatch->create_session=+[](XrInstance,const XrSessionCreateInfo* info,XrSession* out)->XrResult{
            auto& f=*current;auto* binding=static_cast<const XrGraphicsBindingD3D12KHR*>(info->next);
            check(binding && binding->type==XR_TYPE_GRAPHICS_BINDING_D3D12_KHR,"actual raw D12 binding");
            if(!framework_session_probe)check(binding->queue!=f.queue.Get(),"real XR must not receive producer queue");
            f.delivery_queue=binding->queue;*out=reinterpret_cast<XrSession>(2);return XR_SUCCESS;
        };
        dispatch->destroy_session=+[](XrSession)->XrResult{return XR_SUCCESS;};
        {std::scoped_lock lock(g_state_mutex);g_instances[dispatch->instance]=dispatch;}
        XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};binding.device=f.device12.Get();binding.queue=f.queue.Get();
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;info.systemId=1;
        XrSession handle{};xr_ok(layer_create_session_impl(dispatch->instance,&info,&handle),"shipping session creates private delivery binding");
        auto created=find_session(handle);
        if(off_session_probe) {
            check(created && created->private_runtime && !created->menu_enabled,"OFXR-off startup installed the stable private endpoint");
            auto transport=created->private_runtime;f.transport=transport;f.expected_views=1;
            check(!transport->director_enabled() && !single_director_pacing(*created),"OFXR-off startup ignores stored combined pacing");
            // Model the already-produced AFW pair at the delivery boundary.
            // Actual AFW image math is covered by the pinned-provider fixtures.
            created->luke_afw_enabled=true;
            ok(apply_embedded_control(created),"qualify AFW-only output ownership");
            created->luke_afw_last_evaluated_publication=77;
            xr_ok(transport->start(),"start the actual bypass workers");auto chain=f.make_chain();unsigned serial{};
            auto batch=[&](unsigned count) {
                for(unsigned i=0;i<count;++i) {++serial;f.write(chain,serial*8);f.begin_virtual();xr_ok(f.submit(chain,float(serial)),"owned AFW pair delivered");}
                f.wait_count(serial);xr_ok(transport->drain(),"retire each policy batch");
            };
            for(int mode:{0,1,2,1,0,2}) {
                auto desired=xrfg::autonomous::snapshot().desired;desired.pacing_mode=mode;
                check(xrfg::autonomous::request(desired),"store pacing preference while OFXR is OFF");xrfg::autonomous::apply_runtime_settings();
                ok(apply_embedded_control(created),"apply OFF pacing choice");
                check(created->private_runtime==transport && !transport->director_enabled() && !single_director_pacing(*created),"all OFF choices remain unpaced on the same binding");
                check(created->luke_afw_last_evaluated_publication==77,"OFF pacing change reset AFW temporal history");
                batch(3);
            }
            for(unsigned i=0;i<serial;++i)check(!f.director_policies[i] && f.positions[i]==float(i+1) && f.pixels[i]==(i+1)*8,"OFF delivery lost pixels, order or native bypass");
            auto shortest=period;for(unsigned i=1;i<serial;++i)shortest=std::min(shortest,f.handoffs[i]-f.handoffs[i-1]);
            check(shortest<period/2,"OFF path retains an artificial refresh floor");
            for(int mode:{1,2,0,2}) {
                auto desired=xrfg::autonomous::snapshot().desired;desired.enabled=true;desired.pacing_mode=mode;
                check(xrfg::autonomous::request(desired),"enable OFXR with stored policy");xrfg::autonomous::apply_runtime_settings();ok(apply_embedded_control(created),"restore enabled policy");
                check(transport->director_enabled()==(mode!=0) && single_director_pacing(*created)==(mode==1),"ON did not restore selected Framework/Director/combined policy");
                batch(2);
                desired.enabled=false;check(xrfg::autonomous::request(desired),"disable OFXR retaining AFW");xrfg::autonomous::apply_runtime_settings();ok(apply_embedded_control(created),"return to OFF bypass");
                check(!transport->director_enabled() && !single_director_pacing(*created) && created->luke_afw_last_evaluated_publication==77,"disable left pacing active or reset AFW");
                batch(3);
            }
            check(created->private_runtime==transport && f.chains.size()==1,"OFF/ON recreated session binding or allocated extra native swapchains");
            xr_ok(transport->destroy_swapchain(chain),"release policy fixture chain");xr_ok(layer_destroy_session_impl(handle),"OFF session cleanup");
            {std::scoped_lock lock(g_state_mutex);g_instances.clear();}
            std::puts("OFXR OFF/AFW-only: every stored pacing bypasses, exact FIFO GPU pixels, no refresh floor; ON restores selection and AFW history survives PASS");return 0;
        }
        if(framework_session_probe) {
            check(created && created->private_runtime && created->private_delivery_queue,"Framework keeps the stable private binding in bypass");
            auto transport=created->private_runtime;
            check(!transport->director_enabled() && !single_director_pacing(*created),"Framework startup bypasses final director pacing");
            for(int mode:{1,2,0,2,1,0}) {
                auto next=xrfg::autonomous::snapshot().desired;next.pacing_mode=mode;
                check(xrfg::autonomous::request(next),"request a live pacing policy");
                xrfg::autonomous::apply_runtime_settings();ok(apply_embedded_control(created),"apply live policy without recreating session");
                check(created->private_runtime==transport && transport->director_enabled()==(mode!=0) &&
                    single_director_pacing(*created)==(mode==1),"every pacing choice applies to the original endpoint");
            }
            f.transport=transport;
            xr_ok(layer_destroy_session_impl(handle),"framework session cleanup");
            {std::scoped_lock lock(g_state_mutex);g_instances.clear();}
            std::puts("Framework startup and live three-mode switch retain one native binding PASS");return 0;
        }
        check(created && created->private_runtime,"shipping session installed transport");
        f.transport=created->private_runtime; // fixture teardown owns a valid stopped endpoint
        check(created->private_delivery_queue.Get()==f.delivery_queue.Get() &&
              created->private_delivery_queue.Get()!=runtime_queue(*created),"raw binding and private preparation queues are distinct");
        D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY priority{D3D12_COMMAND_LIST_TYPE_DIRECT,D3D12_COMMAND_QUEUE_PRIORITY_HIGH,FALSE};
        if(SUCCEEDED(f.device12->CheckFeatureSupport(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY,&priority,sizeof(priority))) && priority.PriorityForTypeIsSupported) {
            auto desc=binding.queue->GetDesc();desc.Priority=D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
            ComPtr<ID3D12CommandQueue> probe;
            if(SUCCEEDED(f.device12->CreateCommandQueue(&desc,IID_PPV_ARGS(&probe))))
                check(f.delivery_queue->GetDesc().Priority==D3D12_COMMAND_QUEUE_PRIORITY_HIGH,"shipping final binding must use supported HIGH rather than normal priority behind synthesis");
        }
        check(binding.queue==f.queue.Get() && info.next==&binding,"application binding metadata remains owned by application");
        xr_ok(layer_destroy_session_impl(handle),"shipping private delivery session cleanup");
        {std::scoped_lock lock(g_state_mutex);g_instances.clear();}
        std::puts("Shipping xrCreateSession receives dedicated consumer queue; private producer queue retained PASS");return 0;
    }
    auto state=std::make_shared<SessionState>(dispatch);state->handle=reinterpret_cast<XrSession>(2);state->graphics_binding=native12?SessionGraphicsBinding::d3d12:SessionGraphicsBinding::d3d11;
    if(!native12)ok(xrfg::create_d3d12_device_for_d3d11(f.device11.Get(),f.device12.GetAddressOf(),f.queue.GetAddressOf()),"D11 generator interop device");
    state->d3d12_device=f.device12;state->d3d12_queue=f.queue;
    state->d3d11_device=f.device11;state->d3d11_context=f.context;
    state->single_director_requested=sole;
    state->luke_afw_enabled=luke;
    if(route=="scope") {
        state->single_director_requested=true;
        check(!single_director_pacing(*state),"a request alone cannot bypass a native runtime's pacing");
        state->private_runtime=f.transport;
        dispatch->steamvr_runtime=false;dispatch->virtual_desktop_runtime=true;
        check(!single_director_pacing(*state),"VDXR remains unchanged with the experimental flag requested");
        dispatch->steamvr_runtime=true;dispatch->virtual_desktop_runtime=false;
        check(single_director_pacing(*state),"only private SteamVR enables sole-director pacing");
        state->single_director_requested=false;
        check(!single_director_pacing(*state),"OFF restores the existing route policy");
        state->djules_route=true;state->nvidia_new_route=true;
        state->optical_flow_backend=xrfg::D3D12OpticalFlowBackend::nvidia;
        state->djules_triple_requested=true;state->lower_vram=state->separate_synthesis=1;
        check(extra_output_capacity(*state) && native_current_rings(*state)==2 && native_synthetic_rings(*state)==2,
            "SteamVR private Fast 3X keeps independent A/B/S1/S2 capacity");
        state->private_runtime.reset();dispatch->steamvr_runtime=false;dispatch->virtual_desktop_runtime=true;
        check(staged_native_outputs(*state) && extra_output_capacity(*state) &&
            native_current_rings(*state)==1 && native_synthetic_rings(*state)==1,
            "VDXR staged Fast 3X retains its earlier one-ring runtime contract");
        state->private_runtime=f.transport;dispatch->steamvr_runtime=true;dispatch->virtual_desktop_runtime=false;
        state->djules_triple_requested=false;
        check(!extra_output_capacity(*state) && native_current_rings(*state)==1 && native_synthetic_rings(*state)==1,
            "SteamVR Fast 2X still reduces both runtime rings");
        std::puts("Single director scope: OFF/private SteamVR/VDXR/nonprivate PASS");return 0;
    }
    if(route=="memory") {
        using namespace xrfg::autonomous;
        check(WritePrivateProfileStringW(L"benchmark",L"enabled",L"1",configuration_path().c_str()),"enable isolated memory benchmark");
        auto desired=snapshot().desired;desired.benchmark_csv=true;desired.diagnostics=false;
        check(request(desired),"isolated memory test settings");
        RuntimeStatus runtime{};runtime.sessions=1;runtime.applied_revision=snapshot().revision;report_runtime(runtime);
        check(start_benchmark(false),"start isolated memory trial");
        runtime.applied_revision=snapshot().revision;report_runtime(runtime);
        const auto clock=GetTickCount64();tick_benchmark(clock);
        check(snapshot().benchmark.phase==BenchmarkPhase::running,"memory trial applied");
        tick_benchmark(clock+9750);
        sample_benchmark_memory(*state);
        check(!take_benchmark_memory_request(),"game sampler consumes one request only");
        check(state->video_memory_adapter!=nullptr,"sampler finds the actual rendering adapter with F9 off");
        DXGI_QUERY_VIDEO_MEMORY_INFO expected{};
        ok(state->video_memory_adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&expected),"independent process VRAM query");
        const auto path=std::filesystem::u8path(snapshot().benchmark.csv_path);
        tick_benchmark(clock+10000);
        std::ifstream csv(path);std::string line;bool gpu{},ram{};
        while(std::getline(csv,line)) {
            for(const auto& key:{std::string("VRAM end MiB,"),std::string("RAM working set end MiB,")}) {
                const auto at=line.find(key);if(at==std::string::npos)continue;
                const double value=std::stod(line.substr(at+key.size()));
                if(key.starts_with("VRAM")) {gpu=true;check(std::abs(value-double(expected.CurrentUsage)/1048576.0)<0.01,"CSV VRAM equals current-process DXGI usage on the rendering adapter");}
                else {ram=true;check(value>0,"real game-process RAM is observed");}
            }
        }
        check(gpu&&ram,"CSV contains actual VRAM and RAM end samples");
        stop_benchmark();tick_benchmark();
        std::puts("Memory: one end-of-trial game-process DXGI/working-set snapshot, F9 off, CSV correspondence PASS");return 0;
    }
    D3D12_COMMAND_QUEUE_DESC queue_info{};queue_info.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(native12)ok(f.device12->CreateCommandQueue(&queue_info,IID_PPV_ARGS(&state->d3d12_synthesis_queue)),"separate generator queue");
    if(native12)state->private_delivery_queue=f.delivery_queue;
    if(separate_queue) {
        ok(f.device12->CreateCommandQueue(&queue_info,IID_PPV_ARGS(&state->binding_queue)),"independent real binding queue");
        ok(f.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&state->app_release_fence)),"input publication fence");
        ok(f.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&state->reacquire_fence)),"legacy binding reacquire fence");
    }
    state->optical_flow_backend=route=="fidelity"?xrfg::D3D12OpticalFlowBackend::fidelity_fx:xrfg::D3D12OpticalFlowBackend::nvidia;
    state->nvidia_new_route=route!="ref" && route!="fidelity";state->djules_route=route=="djules";
    state->separate_synthesis=a;state->lower_vram=b;
    state->djules_triple_requested=triple;state->djules_deep_pipeline=triple || deep;
    if(benchmark_sequence)state->djules_pipeline_switchable=true; // shipping xrCreateSession enables live multiplier polling
    const bool vectors=route=="fsr" || route=="hybrid" || route=="dlssg";
    if(vectors)state->dlss_motion_vectors=true;
    if(route=="fsr")state->frame_generation_mode=xrfg::D3D12FrameGenerationMode::fsr3;
    if(route=="dlssg")state->frame_generation_mode=xrfg::D3D12FrameGenerationMode::native_dlss;
    if(canonical || route=="dlssg") {
        check(route=="dlssg","canonical route fixture is DLSS-G");
        state->optical_flow_backend=xrfg::D3D12OpticalFlowBackend::fidelity_fx;
        state->nvidia_new_route=false;state->djules_route=false;
    }
    if(route=="hybrid"){state->optical_flow_backend=xrfg::D3D12OpticalFlowBackend::fidelity_fx;state->hybrid_requested=true;state->nvidia_options.hybrid=true;state->nvidia_new_route=false;}
    if(publication_budget) {
        state->luke_afw_enabled=true;state->luke_outlaws_preframe=outlaws_budget;
        state->luke_shared_swapchains=shared_budget;
        state->graphics_binding_capabilities|=8ULL;
    }
    if(sole)ok(apply_embedded_control(state),"initial settings match the real public route selection");
    else state->control_revision=xrfg::embedded::snapshot().revision;
    {std::scoped_lock lock(g_state_mutex);g_sessions[state->handle]=state;g_instances[dispatch->instance]=dispatch;}
    ok(install_private_runtime(state),"install real SteamVR private dispatch");
    f.transport=state->private_runtime;state->uevr_application=pooled;
    if(pooled)f.native_chain_limit=4; // two app chains plus two physical publication sinks
    if(publication_budget) {
        f.native_chain_limit=16;
        const unsigned source_count=unshared_budget?2:3;
        std::array<XrSwapchain,3> app{};
        for(unsigned n=0;n<source_count;++n) {
            XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.width=64;ci.height=n==2?40:32;
            ci.arraySize=ci.faceCount=ci.sampleCount=ci.mipCount=1;ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;
            ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            xr_ok(layer_create_swapchain_impl(state->handle,&ci,&app[n]),"Outlaws original application color chain");
            unsigned count{};xr_ok(layer_enumerate_swapchain_images_impl(app[n],0,&count,nullptr),"Outlaws app count");
            std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            xr_ok(layer_enumerate_swapchain_images_impl(app[n],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Outlaws initial complete enum");
            auto chain=find_swapchain(app[n]);
            if(!outlaws_budget)check(ensure_frame_generation(chain),"ordinary deferred Luke output initialization");
            std::fprintf(stderr,"OUTLAWS source%u: native=%zu generation=%d budget=%d\n",n,f.chains.size(),bool(chain->frame_generation),bool(state->generation_budget_exhausted));
            check(chain->frame_generation && chain->d3d12_history && !state->generation_budget_exhausted,
                "Outlaws eager startup exhausted16 native chains and discarded already-ready eyes");
        }
        const auto expected_native=source_count*(unshared_budget?6U:2U);
        check(f.chains.size()==expected_native,"startup flag preserves old native allocation or opts into one sink per source");
        state->next_swapchain_inventory_ms=0;
        const auto budget=luke_swapchain_inventory(state,nullptr);
        check(budget.total==f.chains.size() && budget.application==source_count &&
            budget.current+budget.synthetic==expected_native-source_count && !budget.unassigned_native,
            "inventory deduplicates private output tokens into actual native owners");
        seal_outlaws_initialization(*state);
        for(auto handle:app)if(handle)check(find_swapchain(handle)->frame_generation!=nullptr,"startup seal lost warmed eye outputs");
        xr_ok(f.transport->start(),"start Outlaws native publication consumer");
        std::vector<unsigned> expected_left,expected_right;
        for(unsigned slot:{0U,1U,2U,3U,4U,0U}) {
            const unsigned identity=static_cast<unsigned>(expected_left.size()+1);
            std::array<XrCompositionLayerProjectionView,2> views{};
            for(unsigned eye=0;eye<2;++eye) {
                const auto generation=find_swapchain(app[eye])->frame_generation;
                const auto output=slot<3?generation->current[slot].handle:generation->synthetic[slot-3].handle;
                const unsigned color=eye?140+identity*8:30+identity*8;
                f.write(output,color);
                (eye?expected_right:expected_left).push_back(color);
                auto& view=views[eye];view.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                view.pose.orientation.w=1;view.pose.position.x=float(identity);
                view.subImage.swapchain=output;view.subImage.imageRect.extent={64,32};
            }
            XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.viewCount=2;projection.views=views.data();
            const auto* header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=epoch+ns();end.layerCount=1;end.layers=&header;
            f.begin_virtual();xr_ok(f.transport->end_frame(&end),"each independent warmed output reaches native XR");
        }
        f.wait_count(6);xr_ok(f.transport->drain(),"all Outlaws warmed outputs retire");
        check(f.pixels==expected_left && f.right_pixels==expected_right && f.chains.size()==expected_native,
            "Outlaws shared sinks overwrite another eye/frame or allocate new native handles");
        release_session_generation_budget(state);
        for(auto handle:app)if(handle)xr_ok(layer_destroy_swapchain_impl(handle),"Outlaws original chain cleanup");
        check(f.chains.empty(),"Outlaws pooled publications leaked after all owners retired");
        {std::scoped_lock lock(g_state_mutex);g_sessions.clear();g_instances.clear();}
        std::puts("Outlaws actual D11/private endpoint startup under16: three app sources, six native handles, preserved independent output storage and full cleanup PASS");return 0;
    }
    xr_ok(f.transport->start(),"start common consumer");
    if(measured) {
        state->fps_overlay=std::make_unique<xrfg::OpenXrFpsOverlay>(dispatch->instance,state->handle,0,
            state->dispatch->get_instance_proc_addr,content_end_frame,f.device12.Get(),f.queue.Get(),f.device11.Get(),
            xrfg::autonomous::configuration_path(),false);
        bind_private_delivery_metrics(state);
        xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,true);
    }
    if(separate_queue) {
        ComPtr<ID3D12Fence> gate,progress;
        ok(f.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"binding dependency gate");
        ok(f.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress)),"app progress fence");
        ok(state->binding_queue->Wait(gate.Get(),1),"hold real binding work");
        carry_reacquire_to_application(state.get());
        ok(f.queue->Signal(progress.Get(),1),"app must proceed independently of real binding work");
        const auto independent=wait_composition_fence(progress.Get(),1,30);
        ok(gate->Signal(1),"release real binding independently");
        ok(wait_composition_fence(progress.Get(),1),"reacquire proof drain");
        check(SUCCEEDED(independent),"private reacquire must not join the real runtime queue");
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.width=256;info.height=128;info.arraySize=split_eyes?1:2;info.faceCount=info.mipCount=info.sampleCount=1;info.format=DXGI_FORMAT_R8G8B8A8_UNORM;info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    XrSwapchain chain{};xr_ok(layer_create_swapchain_impl(state->handle,&info,&chain),"shipping application swapchain");
    std::array<XrSwapchainImageD3D12KHR,3> images;for(auto& image:images)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR};unsigned count{};
    if(native12)xr_ok(layer_enumerate_swapchain_images_impl(chain,3,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"shipping enumerate/capture setup");
    else{std::array<XrSwapchainImageD3D11KHR,3> d11;for(auto& image:d11)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};xr_ok(layer_enumerate_swapchain_images_impl(chain,3,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(d11.data())),"shipping D11 enumerate/capture setup");}
    XrSwapchain right=chain;std::array<XrSwapchainImageD3D12KHR,3> right_images{};
    if(split_eyes) {
        auto saved=f.create_state;f.create_state=[]{return D3D12_RESOURCE_STATE_RENDER_TARGET;};
        xr_ok(layer_create_swapchain_impl(state->handle,&info,&right),"separate right-eye application chain");
        f.create_state=std::move(saved);
        for(auto& image:right_images)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR};
        xr_ok(layer_enumerate_swapchain_images_impl(right,3,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(right_images.data())),"right-eye capture setup");
    }
    ComPtr<ID3D12Resource> depth,motion;
    if(vectors) {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=256;desc.Height=128;desc.DepthOrArraySize=2;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;desc.Format=DXGI_FORMAT_R32_FLOAT;
        ok(f.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&depth)),"owned depth guide");desc.Format=DXGI_FORMAT_R16G16_FLOAT;
        ok(f.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&motion)),"owned motion guide");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;ComPtr<ID3D12DescriptorHeap> views;ok(f.device12->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&views)),"guide RTVs");
        auto d=views->GetCPUDescriptorHandleForHeapStart(),m=d;m.ptr+=f.device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);f.device12->CreateRenderTargetView(depth.Get(),nullptr,d);f.device12->CreateRenderTargetView(motion.Get(),nullptr,m);
        f.commands([&](ID3D12GraphicsCommandList* list){const float z[]={.5F,.5F,.5F,1},v[]={0,0,0,0};list->ClearRenderTargetView(d,z,0,nullptr);list->ClearRenderTargetView(m,v,0,nullptr);D3D12_RESOURCE_BARRIER b[2]{};for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;x.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;x.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}b[0].Transition.pResource=depth.Get();b[1].Transition.pResource=motion.Get();list->ResourceBarrier(2,b);});
    }
    if(split_eyes && vectors)xrfg::configure_dlss_motion_vector_tracking(true);
    f.release_callback=[&](XrSwapchain handle,unsigned index){
        auto c=find_swapchain(handle);
        if(split_eyes && vectors) {
            ComPtr<ID3D12GraphicsCommandList> recorded;
            f.commands([&](ID3D12GraphicsCommandList* list) {
                recorded=list;xrfg::DlssMotionVectorPublication p{};
                p.stream=handle==chain?100:200;p.output=c->enumerated_d3d12_images[index].Get();
                p.motion_vectors=motion.Get();p.depth=depth.Get();p.producer_queue=f.queue.Get();p.producer_command_list=list;
                p.output_width=p.motion_width=p.depth_width=256;p.output_height=p.motion_height=p.depth_height=128;
                check(bool(xrfg::publish_dlss_motion_vectors(p)),"real per-eye release-bound guide publication");
            });
            ID3D12CommandList* lists[]={recorded.Get()};xrfg::report_dlss_motion_vector_submission(f.queue.Get(),1,lists);
        }
        {std::scoped_lock lock(c->mutex);c->acquired_indices.push_back(index);++c->waited_count;}
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};return layer_release_swapchain_image_impl(handle,&release);
    };
    unsigned phase_outputs{};
    const unsigned trial_frames=16;
    const unsigned input_count=benchmark_sequence?7*trial_frames:8;
    for(unsigned n=1;n<=input_count;++n) {
        if(benchmark_sequence && n%trial_frames==1) {
            const auto phase=(n-1)/trial_frames;
            auto desired=xrfg::autonomous::snapshot().desired;
            desired.pacing_mode=phase<6?phase/2:2;
            desired.lower_vram=desired.separate_synthesis=phase<6?phase%2:1;
            desired.triple_frame_gen=phase<6;desired.prefer_fps=phase<6;
            check(xrfg::autonomous::request(desired),"actual benchmark pacing/Fast request");
            xrfg::autonomous::apply_runtime_settings();
            if(phase==6) {
                // Exercise both actual pipeline observations without a wall-clock
                // sleep: first records the candidate, second reconciles capacity.
                for(int observation=0;observation<2;++observation) {
                    state->djules_pipeline_poll={};
                    xr_ok(poll_djules_pipeline(state),"live3X->2X Fast capacity reconciliation");
                }
            }
            phase_outputs=f.real_ends.load();
        }
        if(measured)xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,true);
        if(toggling && (n==3 || n==5 || n==7)) {
            auto desired=xrfg::autonomous::snapshot().desired;
            desired.pacing_mode=n==3?0:n==5?2:1;
            check(xrfg::autonomous::request(desired),"publish only the single-director hot switch");
            xrfg::autonomous::apply_runtime_settings();
            ok(apply_embedded_control(state),"drained single-director hot switch");
            check(single_director_pacing(*state)==(desired.pacing_mode==1),"active policy matches live Framework/Director/combined choice");
            check(state->private_runtime==f.transport && f.transport->director_enabled()==(desired.pacing_mode!=0),"live bypass retains the sole physical endpoint");
        }
        if(switching && (n==3 || n==5 || n==7)) {
            auto desired=xrfg::embedded::snapshot().desired;desired.enabled=true;desired.backend=n==3?1:n==5?3:0;
            desired.frame_generation=0;desired.separate_synthesis=0;desired.lower_vram=0;
            check(xrfg_fixture::request_control(desired),"request route hot handoff");
            ok(apply_embedded_control(state),"apply route hot handoff");check(state->private_runtime==f.transport,"route switch must retain one common distributor");
        }
        std::fprintf(stderr,"frame %u write (physical=%u)\n",n,f.real_ends.load());
        const auto color=((n-1)%8+1)*20;f.write(chain,color);if(split_eyes)f.write(right,color+7);
        if(vectors && !split_eyes){auto c=find_swapchain(chain);auto guides=std::make_shared<xrfg::DlssMotionVectorSet>();guides->eye_count=2;
            for(unsigned eye=0;eye<2;++eye){auto g=std::make_shared<xrfg::DlssMotionVectorFrame>();g->stream=eye+1;g->epoch=1;g->serial=n;g->previous_serial=n-1;g->publication=2*n+eye;g->producer_queue=f.queue;g->output=images[*c->last_released_index].texture;g->output_slice=eye;g->depth=depth;g->motion_vectors=motion;g->motion_slice=eye;g->output_width=g->depth_width=g->motion_width=256;g->output_height=g->depth_height=g->motion_height=128;guides->eyes[eye]=g;}c->last_released_motion_vectors=guides;}
        XrFrameState prediction{XR_TYPE_FRAME_STATE};xr_ok(layer_wait_frame_impl(state->handle,nullptr,&prediction),"shipping virtual Wait");xr_ok(layer_begin_frame_impl(state->handle,nullptr),"shipping virtual Begin");
        std::array<XrCompositionLayerProjectionView,2> views;
        for(unsigned eye=0;eye<2;++eye){auto& v=views[eye];v={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};v.pose.orientation.w=1;v.pose.position.x=(sole?float(n):0.F)+(eye?.032F:-.032F);v.pose.position.y=float(n);v.fov={-.7F,.7F,.7F,-.7F};v.subImage.swapchain=split_eyes && eye?right:chain;v.subImage.imageArrayIndex=split_eyes?0:eye;v.subImage.imageRect.extent={256,128};}
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=reinterpret_cast<XrSpace>(9);projection.viewCount=2;projection.views=views.data();auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=prediction.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=1;end.layers=&layer;
        if(blocked && n==2) {
            f.wait_count(1); // all three endpoint credits are free before the probe
            f.block_end=true;f.end_entered=false;
            auto producer=std::async(std::launch::async,[&]{return layer_end_frame_impl(state->handle,&end);});
            const bool independent=producer.wait_for(500ms)==std::future_status::ready;
            f.unblock();
            xr_ok(producer.get(),"shipping End while native End is withheld");
            check(independent,"bridge acceptance returns to game before blocked native End with spare credits");
        } else xr_ok(layer_end_frame_impl(state->handle,&end),"shipping OFXR End handoff");
        if(sole && !benchmark_sequence && !blocked && !toggling && n==2 && state->three_slot_director) {
            xr_ok(wait_for_synthesis_handoff(state),"second real generated without a future source");
            f.wait_count(2);
            check(f.real_ends>=2,"ready real is delivered before a third application source exists");
        }
        std::fprintf(stderr,"frame %u accepted\n",n);
        if(benchmark_sequence && n%trial_frames==0) {
            xr_ok(wait_for_synthesis_handoff(state),"phase synthesis complete");
            if(continuous_presenter_active(state))xr_ok(wait_for_presenter_idle(state),"phase presenter complete");
            xr_ok(f.transport->drain(),"phase physical FIFO complete");
            const auto phase=n/trial_frames-1,outputs=f.real_ends.load()-phase_outputs;
            std::fprintf(stderr,"BENCH phase%u mode%u fast%u physical%u expected3X=%u state%u director%u queue%u extra%u\n",
                phase,phase<6?phase/2:2,phase<6?phase%2:1,outputs,unsigned(phase<6),unsigned(state->generation_status.load()),
                unsigned(state->three_slot_director),unsigned(state->queued_nvidia_presenter),unsigned(extra_output_capacity(*state)));
            xrfg::bridge_flight_logger().flush();
            auto c=find_swapchain(chain);
            std::fprintf(stderr,"CHAIN fg%u pending%u declined%u refusal%u capture%u active%u forbidden%u cooldown%u\n",
                unsigned(bool(c->frame_generation)),unsigned(c->generation_eligible_pending),unsigned(c->generation_declined),
                unsigned(c->generation_refusal),unsigned(bool(c->last_released_capture)),unsigned(state->presenter_active),
                unsigned(presenter_forbidden(*state)),unsigned(state->generation_resume_display_time!=0));
            check(c->frame_generation && c->frame_generation->current_runtime_rings==(phase<6?2U:1U) &&
                c->frame_generation->synthetic_runtime_rings==(phase<6?2U:1U) &&
                c->frame_generation->direct_extra_outputs==(phase<6),
                "Fast 3X preserves the minimum distinct A/B and S1/S2 output ownership");
            check(outputs>=(phase<6?2*trial_frames+1:trial_frames+1),"Every normal 3X phase must deliver two generated frames per real, including after Fast");
        }
    }
    xr_ok(wait_for_synthesis_handoff(state),"worker complete");
    if(continuous_presenter_active(state))xr_ok(wait_for_presenter_idle(state),"generator transport drained");
    xr_ok(f.transport->drain(),"common physical FIFO drained");
    stop_continuous_presenter(state);
    auto generated=count>0 && f.real_ends>=(switching?10:12);
    std::printf("%s physical outputs=%u generation state=%u\n",route.c_str(),f.real_ends.load(),unsigned(state->generation_status.load()));
    check(generated,"actual generator must produce beyond eight real inputs");
    if(sole) {
        check(std::is_sorted(f.positions.begin(),f.positions.end()),"all generated and real metadata leave in source order");
        for(unsigned source=1;source<=8;++source)
            check(std::any_of(f.positions.begin(),f.positions.end(),[&](float x){return std::abs(x-(float(source)-.032F))<.001F;}),
                "every accepted real source reaches native End, including final drain");
        check(!f.pixels.empty() && f.pixels.back()==160,"last real retains its own pixels through bridge/drain");
    }
    check(f.director_policies.size()==f.handoffs.size(),"native handoffs record their effective delivery policy");
    for(unsigned i=1;i<f.handoffs.size();++i) {
        if(!f.director_policies[i])continue; // Framework explicitly bypasses the final cadence.
        if(f.handoffs[i]-f.handoffs[i-1]<period)std::fprintf(stderr,"handoff interval%lldns required%lldns\n",f.handoffs[i]-f.handoffs[i-1],period);
        check(f.handoffs[i]-f.handoffs[i-1]>=period,"all director-paced route outputs obey common physical cadence");
    }
    if(measured) {
        // Refresh observations after physical FIFO drain. This is test-only CPU
        // time; no timing policy or sleep is added to the production director.
        std::this_thread::sleep_for(260ms);
        xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,true);
        XrCompositionLayerProjection probe{XR_TYPE_COMPOSITION_LAYER_PROJECTION};auto* header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&probe);
        XrFrameEndInfo sample{XR_TYPE_FRAME_END_INFO};sample.layerCount=1;sample.layers=&header;
        state->fps_overlay->application_frame(&sample);
        const auto m=xrfg::autonomous::snapshot().runtime.telemetry;
        check(m.cycle_valid && m.cycle_estimated_valid,"shipping generator publishes XR interval and predicted display independently of engine origin");
        check(!m.game_time_valid,"abandoned Game timing stays absent without hiding the corrected Excel estimate");
        check(m.cycle_source==xrfg::InterfaceSource::native_frame_cycle_qpc && m.cycle_estimated_source==xrfg::InterfaceSource::native_predicted_display_qpc,"all shipping route measurements reach native director endpoint");
        check(m.cycle_estimated_ms>=m.cycle_length_ms && m.ofxr_time_ms<=m.cycle_length_ms,"same physical frame has consistent complete-cycle values");
        check(m.stages_valid && m.game_time_ms>=0 && m.generation_time_ms>=0 && m.delivery_time_ms>=0,
            "all shipping frameworks expose same-source production, preparation and delivery stages");
        std::printf("Native measured cycle %.3fms, predicted display %.3fms, producer calls %.3fms\n",m.cycle_length_ms,m.cycle_estimated_ms,m.ofxr_time_ms);
        xrfg::autonomous::set_interface_visible(xrfg::autonomous::InterfaceSurface::desktop,false);
    }
    if(split_eyes) {
        check(f.chains.size()==(pooled?4:10),"only UEVR separate eyes share native publication sinks");
        for(auto source:{chain,right}) {
            auto c=find_swapchain(source);check(c->pooled_publication==pooled && c->frame_generation,"both source output sets obey scoped pool admission");
            check(!state->lower_vram && !state->separate_synthesis,"pool does not activate Lower VRAM");
        }
    }
    f.transport->stop();
    for(auto& c:find_swapchains(state)){drain_swapchain_gpu(c);destroy_frame_generation_swapchains(c);destroy_composition_capture(c);}
    xr_ok(layer_destroy_swapchain_impl(chain),"shipping destroy");
    if(split_eyes){xr_ok(layer_destroy_swapchain_impl(right),"shipping right destroy");check(f.chains.empty(),"shipping pool cleanup leaves no native handles");}
    ComPtr<ID3D12InfoQueue> messages;
    if(SUCCEEDED(f.device12.As(&messages)))for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T bytes{};messages->GetMessage(i,nullptr,&bytes);std::vector<char> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());messages->GetMessage(i,message,&bytes);if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D12: %s\n",message->pDescription);check(false,"actual generator resource state/ownership validation");}}
    {std::scoped_lock lock(g_state_mutex);g_sessions.clear();g_swapchains.clear();g_instances.clear();}
    std::printf("%s shipping generator -> private bridge -> common consumer PASS\n",route.c_str());return 0;
}
