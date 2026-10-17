// Hardware-only laboratory probe: real shipping OFXR backend at a chosen eye
// resolution. It measures generator cost, not game/headset FPS or pacing.
#define main history_fixture_main
#include "d3d12_history_tests.cpp"
#undef main

int main(int argc,char** argv) {try {
    if(argc>1 && std::string_view(argv[1])=="quarter-quality") {
        D3D12WarpFixture fixture(true);
        for(bool pose:{false,true}) {
            xrfg::D3D12NvidiaOpticalFlowOptions options;
            options.input_scale=xrfg::D3D12NvidiaInputScale::quarter;
            options.pose_compensated_input=pose;options.grayscale_input=pose;
            options.input_encoding=xrfg::flow_encoding_for_route(pose);
            test_stereo_motion_synthesis_beats_same_pixel_blend(fixture,xrfg::D3D12OpticalFlowBackend::nvidia,true,options);
            test_double_wide_single_slice_views(fixture,xrfg::D3D12OpticalFlowBackend::nvidia,options);
            test_rotation_aware_synthesis_beats_uncompensated_flow(fixture,xrfg::D3D12OpticalFlowBackend::nvidia,options);
        }
        fixture.require_no_debug_errors();
        std::cout<<"Quarter flow: real NVIDIA stereo/crops/repeated input/rotating cameras for Reference and New-Djules PASS\n";return 0;
    }

    const UINT width=argc>1 ? unsigned(std::stoul(argv[1])) : 1024;
    const UINT height=argc>2 ? unsigned(std::stoul(argv[2])) : 1024;
    require(width>=128&&width<=4096&&height>=128&&height<=8192,"probe eye extent");
    const bool deferred=argc>3 && std::string_view(argv[3])=="deferred";
    D3D12WarpFixture fixture(true);
    ComPtr<IDXGIFactory4> memory_factory;ComPtr<IDXGIAdapter3> memory_adapter;
    require_hresult(CreateDXGIFactory2(0,IID_PPV_ARGS(&memory_factory)),"probe memory factory");
    require_hresult(memory_factory->EnumAdapterByLuid(fixture.device()->GetAdapterLuid(),IID_PPV_ARGS(&memory_adapter)),"probe memory adapter");
    const auto memory=[&]{DXGI_QUERY_VIDEO_MEMORY_INFO info{};require_hresult(memory_adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info),"probe memory usage");return info.CurrentUsage;};
    std::array<ComPtr<ID3D12Resource>,2> sources{
        create_source_texture(fixture,width,height),create_source_texture(fixture,width,height)};
    auto current=create_source_texture(fixture,width,height),synthetic=create_source_texture(fixture,width,height);
    std::array<ID3D12Resource*,2> input{sources[0].Get(),sources[1].Get()};
    std::array<ID3D12Resource*,1> current_out{current.Get()},synthetic_out{synthetic.Get()};
    const auto a=fast_control_pattern(width,height,0,1),b=fast_control_pattern(width,height,24,1),
        expected=fast_control_pattern(width,height,12,1);
    upload_pattern(fixture,sources[0].Get(),a);upload_pattern(fixture,sources[1].Get(),b);
    const auto views=make_reprojection_views();
    ComPtr<ID3D12QueryHeap> clock_heap;D3D12_QUERY_HEAP_DESC clock_desc{};
    clock_desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;clock_desc.Count=2;
    require_hresult(fixture.device()->CreateQueryHeap(&clock_desc,IID_PPV_ARGS(&clock_heap)),"probe external timestamp heap");
    auto clock_readback=create_buffer(fixture,D3D12_HEAP_TYPE_READBACK,16,D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency{};require_hresult(fixture.queue()->GetTimestampFrequency(&frequency),"probe clock frequency");
    std::array<ComPtr<ID3D12CommandAllocator>,2> clock_allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,2> clock_lists;
    for(unsigned i=0;i<2;++i) {
        require_hresult(fixture.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&clock_allocators[i])),"probe clock allocator");
        require_hresult(fixture.device()->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,clock_allocators[i].Get(),nullptr,IID_PPV_ARGS(&clock_lists[i])),"probe clock list");
        clock_lists[i]->EndQuery(clock_heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,i);
        if(i)clock_lists[i]->ResolveQueryData(clock_heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,clock_readback.Get(),0);
        require_hresult(clock_lists[i]->Close(),"probe clock close");
    }

    for(int route:{0,1,2})for(int scale:{2,3}) {
        auto history=std::make_shared<xrfg::D3D12SwapchainHistory>();
        require_hresult(history->initialize(fixture.device(),fixture.queue(),input,D3D12_RESOURCE_STATE_RENDER_TARGET),"probe history");
        xrfg::D3D12NvidiaOpticalFlowOptions options;
        options.input_scale=static_cast<xrfg::D3D12NvidiaInputScale>(scale);
        options.grayscale_input=route==2;options.pose_compensated_input=route==2;
        options.input_encoding=xrfg::flow_encoding_for_route(route==2);
        xrfg::D3D12FrameSynthesizer synth;
        const auto backend=route ? xrfg::D3D12OpticalFlowBackend::nvidia : xrfg::D3D12OpticalFlowBackend::fidelity_fx;
        const auto initial_memory=memory();
        const auto init_start=std::chrono::steady_clock::now();
        require_hresult(synth.initialize(fixture.device(),fixture.queue(),history,current_out,synthetic_out,
            kFormat,D3D12_RESOURCE_STATE_RENDER_TARGET,backend,options,true,xrfg::D3D12FrameGenerationMode::ofxr,false,deferred),"probe initialize");
        const auto init_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-init_start).count();
        const auto initialized_memory=memory();
        xrfg::D3D12HistoryCaptureTicket capture{};xrfg::D3D12FrameSynthesisTicket ticket{};
        require_hresult(history->capture(0,&capture),"probe initial capture");require_hresult(history->commit(capture),"probe initial commit");
        if(deferred) {
            require_hresult(synth.submit_copy(capture,views,0,&ticket),"probe deferred copy");
            require_hresult(synth.wait_for_previous_submission(5000),"probe copy wait");
        }
        require_hresult(synth.submit_prime(capture,views,0,&ticket),"probe prime");require_hresult(synth.wait_for_previous_submission(5000),"probe prime wait");
        std::vector<std::uint64_t> pack,flow,compose,total;std::vector<double> cpu;
        for(unsigned frame=1;frame<=24;++frame) {
            require_hresult(history->capture(frame%2,&capture),"probe capture");require_hresult(history->commit(capture),"probe commit");
            ID3D12CommandList* before[]{clock_lists[0].Get()};fixture.queue()->ExecuteCommandLists(1,before);
            const auto start=std::chrono::steady_clock::now();
            require_hresult(synth.submit_pair(capture,views,views,0,0,&ticket),"probe pair");
            const auto submit_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            ID3D12CommandList* after[]{clock_lists[1].Get()};fixture.queue()->ExecuteCommandLists(1,after);
            fixture.execute_and_wait([](ID3D12GraphicsCommandList*){});
            require_hresult(synth.wait_for_previous_submission(5000),"probe pair completion");
            void* mapped{};require_hresult(clock_readback->Map(0,nullptr,&mapped),"probe clock map");
            const auto* ticks=static_cast<const UINT64*>(mapped);const auto elapsed_us=(ticks[1]-ticks[0])*1000000/frequency;
            clock_readback->Unmap(0,nullptr);
            xrfg::D3D12NvidiaGpuTiming timing{};const auto measured=synth.consume_nvidia_gpu_timing(&timing);
            require(SUCCEEDED(measured)&&elapsed_us,"probe missing external completed GPU interval");
            if(frame>4){pack.push_back(timing.pack_microseconds);flow.push_back(timing.eye0_microseconds+timing.eye1_microseconds);
                compose.push_back(timing.composition_microseconds);total.push_back(elapsed_us);cpu.push_back(submit_us);}
        }
        const auto actual=readback_pattern(fixture,synthetic.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto current_pixels=readback_pattern(fixture,current.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        require(current_pixels==a,"probe current endpoint changed");
        const auto median=[](auto values){std::sort(values.begin(),values.end());return values[values.size()/2];};
        const auto mae=mean_absolute_rgb_error(actual,expected,width,height,0,32);
        const auto right_mae=mean_absolute_rgb_error(actual,expected,width,height,1,32);
        std::uint64_t checksum=14695981039346656037ULL;
        for(const auto& eye:actual)for(const auto value:eye){checksum^=value;checksum*=1099511628211ULL;}
        require(mae<16 && right_mae<16,"probe generated eye outside independent midpoint tolerance");
        std::cout<<"PROBE route="<<(route==0?"fidelity":route==1?"reference":"new_djules")<<" width="<<width<<" height="<<height
            <<" scale="<<(scale==2?50:25)<<" deferred="<<deferred<<" initial_vram_mib="<<double(initialized_memory-initial_memory)/(1024*1024)<<" init_ms="<<init_ms<<" submit_cpu_us="<<median(cpu)<<" pack_us="<<median(pack)
            <<" flow_us="<<median(flow)<<" compose_us="<<median(compose)<<" total_us="<<median(total)<<" synthetic_mae="<<mae<<" right_mae="<<right_mae<<" pixel_hash="<<checksum<<std::endl;
        require(actual!=a&&actual!=b,"probe generated endpoint duplicate");
    }
    fixture.require_no_debug_errors();return 0;
}catch(const std::exception& e){std::cerr<<"OFXR probe FAIL: "<<e.what()<<'\n';return 1;}}
