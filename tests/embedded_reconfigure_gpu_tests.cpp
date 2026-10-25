#define main existing_history_main
#include "d3d12_history_tests.cpp"
#undef main

int main() {
    try {
        D3D12WarpFixture fixture(true); // real hardware, debug layer
        constexpr UINT width = 1024, height = 512;
        auto a = create_source_texture(fixture, width, height);
        auto b = create_source_texture(fixture, width, height);
        auto real = create_source_texture(fixture, width, height);
        auto synthetic = create_source_texture(fixture, width, height);
        const auto pattern_a = translated_motion_pattern(width, height, {0, 0});
        const auto pattern_b = translated_motion_pattern(width, height, {16, -16});
        upload_pattern(fixture, a.Get(), pattern_a);
        upload_pattern(fixture, b.Get(), pattern_b);
        ID3D12Resource* sources[]{a.Get(), b.Get()};
        ID3D12Resource* currents[]{real.Get()};
        ID3D12Resource* synthetics[]{synthetic.Get()};
        auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
        require_hresult(history->initialize(fixture.device(), fixture.queue(), sources,
            D3D12_RESOURCE_STATE_RENDER_TARGET), "history initialize");
        xrfg::D3D12FrameSynthesizer flow;
        require_hresult(flow.initialize(fixture.device(), fixture.queue(), history, currents, synthetics,
            kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET), "flow initialize");
        const auto views = make_reprojection_views();
        // Reuse the same XR resources/history while replacing backend contexts.
        for (int route = 0; route < 8; ++route) {
            const auto backend = route == 0 || route == 7 ? xrfg::D3D12OpticalFlowBackend::fidelity_fx
                : xrfg::D3D12OpticalFlowBackend::nvidia;
            xrfg::D3D12NvidiaOpticalFlowOptions options{
                static_cast<xrfg::D3D12NvidiaPerformancePreset>((route - 1 + 3) % 3),
                static_cast<xrfg::D3D12NvidiaInputScale>((route - 1 + 3) % 3), route > 3};
            require_hresult(flow.reconfigure(backend, options), "live backend switch");
            xrfg::D3D12HistoryCaptureTicket ca{}, cb{};
            require_hresult(history->capture(0, &ca), "capture A");
            require_hresult(history->commit(ca), "commit A");
            xrfg::D3D12FrameSynthesisTicket prime{}, pair{};
            require_hresult(flow.submit_prime(ca, views, 0, &prime), "fresh prime");
            fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
            require_hresult(history->capture(1, &cb), "capture B");
            require_hresult(history->commit(cb), "commit B");
            require_hresult(flow.submit_pair(cb, views, views, 0, 0, &pair), "pair after switch");
            require(pair.previous_serial == ca.serial && pair.current_serial == cb.serial, "old history leaked across switch");
            require(readback_pattern(fixture, real.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == pattern_b,
                "real output changed after switch");
            auto output = readback_pattern(fixture, synthetic.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            require(output != pattern_a && output != pattern_b, "synthetic output duplicated original");
            std::cout << "route " << route << " completed\n";
        }
        require_hresult(flow.wait_for_idle(), "final retirement");
        fixture.require_no_debug_errors();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
