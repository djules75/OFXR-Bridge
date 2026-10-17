void fsr_phase_scope_clock_test() {
    auto s=session_state;s->dispatch->steamvr_runtime=true;
    s->graphics_binding=SessionGraphicsBinding::d3d12;
    s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::fsr3;
    require(fsr_steamvr_phase_clock(*s) && !djules_native_scheduler(*s),"FSR clock missing or backend identity changed");
    s->presenter_display_period=22'222'222;
    require(adopt_djules_display_period(*s,std::chrono::nanoseconds(11'111'111)) &&
        s->presenter_display_period==11'111'111 && s->history_capture_wait_period==11'111'111,
        "FSR retained throttled application clock");
    require(!adopt_djules_display_period(*s,std::nullopt) &&
        !adopt_djules_display_period(*s,std::chrono::nanoseconds(1)),"missing/invalid HMD period changed FSR cadence");
    s->dispatch->steamvr_runtime=false;
    require(!fsr_steamvr_phase_clock(*s) && !adopt_djules_display_period(*s,std::chrono::nanoseconds(8'333'333)),"VDXR FSR changed");
    s->dispatch->steamvr_runtime=true;s->graphics_binding=SessionGraphicsBinding::d3d11;
    require(!fsr_steamvr_phase_clock(*s),"D11 FSR changed");s->graphics_binding=SessionGraphicsBinding::vulkan;
    require(!fsr_steamvr_phase_clock(*s),"Vulkan FSR changed");s->graphics_binding=SessionGraphicsBinding::d3d12;
    s->luke_afw_enabled=true;require(!fsr_steamvr_phase_clock(*s),"Luke FSR changed");s->luke_afw_enabled=false;
    s->lower_vram=1;require(!fsr_steamvr_phase_clock(*s),"single-ring FSR changed");s->lower_vram=0;
    s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;
    require(!fsr_steamvr_phase_clock(*s),"non-FSR clock selected by FSR predicate");
    std::cout<<"FSR phase scope and physical HMD-period recovery without backend/VDXR/bridge/one-ring changes PASS\n";
}
