// Reuse the existing GPU fixture, image generator and readback assertions.
#define main xrfg_history_test_main
#include "d3d12_history_tests.cpp"
#undef main

namespace {
constexpr UINT kFovWidth = 160, kFovHeight = 96;

ReprojectionViews fov_views(unsigned inverted_mask, bool cropped, bool rotated) {
    auto views = make_reprojection_views();
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        if (inverted_mask & (1U << eye))
            std::swap(views[eye].fov.angle_up, views[eye].fov.angle_down);
        views[eye].image_rect = cropped
            ? xrfg::D3D12ImageRect{16, 16, 128, 64}
            : xrfg::D3D12ImageRect{0, 0, kFovWidth, kFovHeight};
        if (rotated) {
            const float yaw = 0.075F, pitch = 0.06F; // Half angles.
            views[eye].pose.orientation = {
                std::cos(yaw) * std::sin(pitch),
                std::sin(yaw) * std::cos(pitch),
                -std::sin(yaw) * std::sin(pitch),
                std::cos(yaw) * std::cos(pitch)};
        }
    }
    return views;
}

StereoPattern flip_views(StereoPattern image, const ReprojectionViews& views, unsigned mask) {
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        if (!(mask & (1U << eye))) continue;
        const auto& rect = views[eye].image_rect;
        for (UINT y = 0; y < rect.height / 2; ++y) {
            auto top = image[eye].begin() +
                ((rect.offset_y + y) * kFovWidth + rect.offset_x) * kBytesPerPixel;
            auto bottom = image[eye].begin() +
                ((rect.offset_y + rect.height - y - 1) * kFovWidth + rect.offset_x) * kBytesPerPixel;
            std::swap_ranges(top, top + rect.width * kBytesPerPixel, bottom);
        }
    }
    return image;
}

StereoPattern run_fov_pair(D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend, xrfg::D3D12NvidiaOpticalFlowOptions options,
    unsigned inverted_mask, bool cropped, bool rotated,
    std::optional<xrfg::OverlayPlacement> marker = std::nullopt) {
    const auto views_a = fov_views(inverted_mask, cropped, false);
    const auto views_b = fov_views(inverted_mask, cropped, rotated);
    // Re-render the same world with the signed FOV. Inverted input is actually
    // upside down in storage, not just relabelled metadata on an upright image.
    const auto previous = ray_direction_pattern(kFovWidth, kFovHeight, views_a);
    const auto current = ray_direction_pattern(kFovWidth, kFovHeight, views_b);
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, kFovWidth, kFovHeight),
        create_source_texture(fixture, kFovWidth, kFovHeight)};
    std::array<ComPtr<ID3D12Resource>, 2> originals{
        create_source_texture(fixture, kFovWidth, kFovHeight),
        create_source_texture(fixture, kFovWidth, kFovHeight)};
    auto synthetic = create_source_texture(fixture, kFovWidth, kFovHeight);
    std::array<ID3D12Resource*, 2> inputs{sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> outputs{originals[0].Get(), originals[1].Get()};
    std::array<ID3D12Resource*, 1> middle{synthetic.Get()};
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require_hresult(history->initialize(fixture.device(), fixture.queue(), inputs,
        D3D12_RESOURCE_STATE_RENDER_TARGET), "FOV history initialization");
    xrfg::D3D12FrameSynthesizer synth;
    require_hresult(synth.initialize(fixture.device(), fixture.queue(), history,
        outputs, middle, kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET, backend, options),
        "FOV synthesizer initialization");
    xrfg::D3D12HistoryCaptureTicket a{}, b{};
    require_hresult(history->capture(0, &a), "FOV capture A");
    require_hresult(history->commit(a), "FOV commit A");

    std::vector<ReprojectionViews> invalid;
    const auto add_invalid = [&](float up, float down) {
        auto bad = views_a;
        bad[0].fov.angle_up = up;
        bad[0].fov.angle_down = down;
        invalid.push_back(bad);
    };
    constexpr float half_pi = 1.57079632679489661923F;
    for (float value : {0.0F, 0.4F, -0.4F}) add_invalid(value, value);
    for (float value : {half_pi, -half_pi, 2.0F, -2.0F,
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        add_invalid(value, -0.5F);
        add_invalid(0.5F, value);
    }
    // Horizontal inversion remains outside this vertical-only change.
    auto horizontal = views_a;
    std::swap(horizontal[0].fov.angle_left, horizontal[0].fov.angle_right);
    invalid.push_back(horizontal);
    for (const auto& bad : invalid) {
        xrfg::D3D12FrameSynthesisTicket rejected{};
        require(synth.submit_prime(a, bad, 0, &rejected) == E_INVALIDARG &&
                rejected.fence_value == 0, "invalid FOV passed prime validation");
    }
    xrfg::D3D12FrameSynthesisTicket prime{};
    require_hresult(synth.submit_prime(a, views_a, 0, &prime), "valid signed FOV prime");
    // Readback waits on the fixture queue without retiring the retained A/B
    // history (the synthesizer's teardown-style wait_for_idle does retire it).
    require(readback_pattern(fixture, originals[0].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) == previous,
            "prime changed the source image's vertical orientation");
    require_hresult(history->capture(1, &b), "FOV capture B");
    require_hresult(history->commit(b), "FOV commit B");
    for (const auto& bad : invalid) {
        xrfg::D3D12FrameSynthesisTicket rejected{};
        require(synth.submit_pair(b, bad, views_b, 0, 1, &rejected) == E_INVALIDARG &&
                rejected.fence_value == 0, "invalid source FOV passed pair validation");
        require(synth.submit_pair(b, views_b, bad, 0, 1, &rejected) == E_INVALIDARG &&
                rejected.fence_value == 0, "invalid target FOV passed pair validation");
    }
    xrfg::D3D12FrameSynthesisTicket pair{};
    require_hresult(synth.submit_pair(b, views_b, views_b, 0, 1, &pair, marker), "valid signed FOV pair");
    require(pair.previous_serial == a.serial && pair.current_serial == b.serial,
            "signed FOV lost A/B history identity");
    // The real frame carries a green diagnostic marker beside the synthetic's
    // purple one whenever diagnostics are on, so it is no longer bit-exact
    // there. That is deliberate: the purple marker rides only on the
    // synthetic, so a steady purple square cannot distinguish "only synthetics
    // are shown" from "both are shown and 45 Hz reads as steady", and nothing
    // in the flight log can either - see LOW_HEADROOM_PLAN.md section 16.
    //
    // Everything outside the marked rect must still be a bit-exact copy of B,
    // which is what this assertion is for and what the mask preserves. With no
    // marker requested the readback is untouched and the check is exactly as
    // strict as it was.
    const auto mask_current_marker = [&](StereoPattern pixels) {
        if (!marker) return pixels;
        for (UINT eye = 0; eye < kEyeCount; ++eye)
            for (std::size_t i = 0; i < pixels[eye].size(); i += 4)
                if (pixels[eye][i] <= 1 && pixels[eye][i + 1] >= 228 &&
                    pixels[eye][i + 1] <= 231 && pixels[eye][i + 2] >= 49 &&
                    pixels[eye][i + 2] <= 53 && pixels[eye][i + 3] == 255)
                    std::copy_n(current[eye].begin() + i, 4, pixels[eye].begin() + i);
        return pixels;
    };
    require(mask_current_marker(readback_pattern(fixture, originals[1].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET)) == current,
            "current output changed source image orientation outside the marker");
    const auto result = readback_pattern(fixture, synthetic.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto mask_marker = [&](StereoPattern pixels) {
        if (!marker) return pixels;
        for (UINT eye = 0; eye < kEyeCount; ++eye) for (std::size_t i = 0; i < pixels[eye].size(); i += 4)
            if (pixels[eye][i] >= 190 && pixels[eye][i] <= 192 && pixels[eye][i + 1] == 0 &&
                pixels[eye][i + 2] == 255 && pixels[eye][i + 3] == 255)
                std::copy_n(current[eye].begin() + i, 4, pixels[eye].begin() + i);
        return pixels;
    };
    const auto unmarked_result = mask_marker(result);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const double error = mean_absolute_rgb_error_rect(unmarked_result, current,
            kFovWidth, eye, views_b[eye].image_rect, 12);
        std::cout << "fov mask=" << inverted_mask << " cropped=" << cropped
                  << " rotated=" << rotated << " eye=" << eye << " mae=" << error << '\n';
        require(error <= 1.0, "signed FOV rotation reprojection is inaccurate");
    }
    xrfg::D3D12FrameSynthesisTicket repeated{};
    require_hresult(synth.submit_pair(b, views_b, views_b, 0, 0, &repeated, marker), "signed FOV repeated capture");
    const auto repeat = readback_pattern(fixture, synthetic.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto unmarked_repeat = mask_marker(repeat);
    for (UINT eye = 0; eye < kEyeCount; ++eye)
        require(mean_absolute_rgb_error_rect(unmarked_repeat, current, kFovWidth, eye,
                    views_b[eye].image_rect, 0) <= 0.5, "signed FOV repeat was flipped or distorted");
    if (marker) {
        for (UINT eye = 0; eye < kEyeCount; ++eye) for (std::size_t i = 0; i < result[eye].size(); i += 4)
            if (result[eye][i] >= 190 && result[eye][i] <= 192 && result[eye][i + 1] == 0 && result[eye][i + 2] == 255)
                require(std::equal(result[eye].begin() + i, result[eye].begin() + i + 4, repeat[eye].begin() + i),
                    "repeated synthetic lost its debug marker");
        require_hresult(synth.submit_pair(b, views_b, views_b, 0, 1, &repeated), "marker Off repeated capture");
        const auto cleared = readback_pattern(fixture, synthetic.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        for (UINT eye = 0; eye < kEyeCount; ++eye)
            require(mean_absolute_rgb_error_rect(cleared, current, kFovWidth, eye,
                views_b[eye].image_rect, 0) <= 0.5, "debug marker leaked into history or remained after Off");
    }
    require_hresult(synth.wait_for_idle(), "repeat drain");
    require_source_is_render_target(fixture, sources[0].Get());
    require_source_is_render_target(fixture, sources[1].Get());
    require_hresult(history->invalidate(), "FOV history invalidate");
    return result;
}
} // namespace

#ifndef XRFG_VERTICAL_FOV_NO_MAIN
int main(int argc, char** argv) {
    try {
        const bool nvidia = argc > 1;
        xrfg::D3D12NvidiaOpticalFlowOptions options;
        if (nvidia) {
            const std::string_view preset(argv[1]);
            require(preset == "fast" || preset == "medium" || preset == "slow", "invalid test preset");
            options.preset = preset == "fast" ? xrfg::D3D12NvidiaPerformancePreset::fast :
                preset == "slow" ? xrfg::D3D12NvidiaPerformancePreset::slow :
                xrfg::D3D12NvidiaPerformancePreset::medium;
            options.bidirectional = argc > 2 && std::string_view(argv[2]) == "backward";
        }
        D3D12WarpFixture fixture(nvidia);
        const auto backend = nvidia ? xrfg::D3D12OpticalFlowBackend::nvidia : xrfg::D3D12OpticalFlowBackend::fidelity_fx;
        for (bool cropped : {false, true}) for (bool rotated : {false, true}) {
            const auto upright = run_fov_pair(fixture, backend, options, 0, cropped, rotated);
            for (unsigned mask : {1U, 3U}) {
                const auto views = fov_views(mask, cropped, rotated);
                const auto flipped = run_fov_pair(fixture, backend, options, mask, cropped, rotated);
                const auto restored = flip_views(flipped, views, mask);
                for (UINT eye = 0; eye < kEyeCount; ++eye)
                    require(mean_absolute_rgb_error_rect(restored, upright, kFovWidth, eye,
                        views[eye].image_rect, 12) <= 1.0, "vertical storage convention changed the synthesized view");
            }
        }
        fixture.require_no_debug_errors();
        std::cout << "Vertical FOV validation, image orientation and rotation tests passed: "
                  << (nvidia ? argv[1] : "FidelityFX/WARP")
                  << (options.bidirectional ? " backward" : " forward") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
