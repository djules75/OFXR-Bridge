// Included after the production layer and the fixture's require helper.
void test_msfs_capture_order(SessionState& state) {
    const auto left = std::make_shared<xrfg::D3D12SwapchainHistory>();
    const auto right = std::make_shared<xrfg::D3D12SwapchainHistory>();
    ProjectionSnapshot previous, current;
    previous.interface_stamp.serial = 1;
    previous.display_time = 100;
    previous.layers.resize(1);
    previous.layers[0].space = reinterpret_cast<XrSpace>(1);
    previous.layers[0].views.resize(2);
    previous.capture_order = {{reinterpret_cast<XrSwapchain>(1), left, 5, 10},
        {reinterpret_cast<XrSwapchain>(2), right, 8, 20}};
    current = previous;
    current.interface_stamp.serial = 2;
    current.display_time = 90;
    ++current.capture_order[0].serial; ++current.capture_order[0].fence_value;
    ++current.capture_order[1].serial; ++current.capture_order[1].fence_value;
    require(consecutive_projection_captures(previous, current), "ordered stereo proof");
    require(!projection_snapshots_compatible(previous, current), "default timeline must remain strict");
    require(projection_snapshots_compatible(previous, current, true), "capture-ordered pair rejected");
    auto reject = [&](auto mutate) {
        auto changed = current; mutate(changed);
        require(!consecutive_projection_captures(previous, changed), "invalid capture proof accepted");
    };
    reject([](auto& s) { s.capture_order.clear(); });
    reject([](auto& s) { s.capture_order.pop_back(); });
    reject([](auto& s) { s.capture_order[1].serial -= 1; }); // one stale eye
    reject([](auto& s) { s.capture_order[0].serial += 1; }); // skipped release
    reject([](auto& s) { s.capture_order[0].serial = 1; });
    reject([](auto& s) { s.capture_order[1].fence_value -= 1; });
    reject([](auto& s) { s.capture_order[0].history.reset(); });
    reject([&](auto& s) { s.capture_order[0].history = right; });
    reject([](auto& s) { s.capture_order[0].swapchain = reinterpret_cast<XrSwapchain>(3); });
    reject([](auto& s) { s.interface_stamp.serial = 1; });
    auto expired = current;
    expired.capture_order[0].history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(!consecutive_projection_captures(previous, expired), "expired history accepted");
    auto reject_metadata = [&](auto mutate) {
        auto changed = current; mutate(changed);
        require(!projection_snapshots_compatible(previous, changed, true), "metadata guard bypassed");
    };
    reject_metadata([](auto& s) { s.layers[0].space = reinterpret_cast<XrSpace>(2); });
    reject_metadata([](auto& s) { s.layers[0].layer_flags ^= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; });
    reject_metadata([](auto& s) { ++s.layers[0].views[0].subImage.imageRect.extent.width; });
    reject_metadata([](auto& s) { s.layers[0].views.pop_back(); });
    state.dispatch->steamvr_runtime = true;
    require(capture_order_timeline(state), "native SteamVR scope");
    state.dispatch->steamvr_runtime = false;
    require(!capture_order_timeline(state), "other runtime scope changed");
    state.dispatch->steamvr_runtime = true;
    state.luke_afw_enabled = true;
    require(!capture_order_timeline(state), "Luke scope changed");
    state.luke_afw_enabled = false;
    state.graphics_binding = SessionGraphicsBinding::d3d11;
    require(!capture_order_timeline(state), "D11 scope changed");
    state.graphics_binding = SessionGraphicsBinding::d3d12;
    for (auto mode : {xrfg::D3D12FrameGenerationMode::fsr3, xrfg::D3D12FrameGenerationMode::native_dlss}) {
        state.frame_generation_mode = mode;
        require(capture_order_timeline(state), "guide-based native capture scope missing");
    }
}
