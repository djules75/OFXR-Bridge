// Real GPU refusal at the second eye, through the shipping stereo preparation.
// The queue gate is test-owned; no HRESULT or synthesizer is mocked.
void worker_backpressure_probe(const std::shared_ptr<SessionState>& state,
    const std::shared_ptr<SwapchainState>& left,
    const std::shared_ptr<SwapchainState>& right, bool timeout) {
    fill(left->enumerated_d3d12_images[0].Get(), 41, 80); release(left);
    fill(right->enumerated_d3d12_images[0].Get(), 41, 160); release(right);
    ProjectionSnapshot snapshot;
    snapshot.layers.resize(1);
    auto& views = snapshot.layers[0].views;
    views.resize(2);
    for (unsigned eye = 0; eye < 2; ++eye) {
        auto& view = views[eye];
        view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        view.pose.orientation.w = 1;
        view.fov = {-.7F, .7F, .7F, -.7F};
        view.subImage.swapchain = eye ? right->handle : left->handle;
        view.subImage.imageRect.extent = {256, 128};
    }
    const auto mappings = build_projection_resource_mappings(snapshot).mappings;
    require(mappings.size() == 2, "split-eye worker mapping");
    const auto right_views = build_reprojection_views(snapshot, mappings[1]);
    require(right_views.has_value(), "right worker camera");
    const auto synth = right->frame_generation->synthesizer;
    ComPtr<ID3D12Fence> gate;
    hr(state->d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&gate)), "worker backpressure gate");
    hr(state->d3d12_synthesis_queue->Wait(gate.Get(), 1), "hold prior right GPU copy");
    // Match the still-running previous copy that survives a temporal re-prime.
    xrfg::D3D12FrameSynthesisTicket previous;
    hr(synth->submit_copy(*right->last_released_capture, *right_views, 0, &previous),
        "pending prior right copy");
    require(synth->previous_submission_available() == HRESULT_FROM_WIN32(ERROR_BUSY),
        "right-eye GPU refusal not reproduced");
    PreparedProjectionFrame prepared;
    std::atomic_bool finished{};
    const auto started = std::chrono::steady_clock::now();
    std::thread worker([&] {
        prepared = prepare_projection_frame(snapshot, mappings, false, .5F, true);
        finished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool pending = !finished.load(std::memory_order_acquire);
    const bool gpu_free = state->gpu_mutex.try_lock();
    if (gpu_free) state->gpu_mutex.unlock();
    const bool right_call_free = right->call_mutex.try_lock();
    if (right_call_free) right->call_mutex.unlock();
    if (!timeout) hr(gate->Signal(1), "release prior right GPU copy");
    worker.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    // Always release the test gate before asserting or tearing GPU resources down.
    hr(gate->Signal(1), "worker gate cleanup");
    require(pending && gpu_free && right_call_free,
        "busy second eye failed stereo transaction or held producer locks");
    if (timeout) {
        require(prepared.kind == PreparedGenerationKind::none &&
            prepared.reason == GenerationPrepareReason::synthesis_busy &&
            elapsed < std::chrono::seconds(2), "GPU timeout was not bounded");
    } else {
        require(prepared.kind == PreparedGenerationKind::prime &&
            prepared.resources.size() == 2, "busy eye did not preserve the stereo prime");
        require(finish_worker_outputs(state, prepared, 1), "both worker eyes completion");
        for (unsigned eye = 0; eye < 2; ++eye) {
            const auto& g = prepared.resources[eye].generation;
            const auto image = g.staging_generation
                ? g.staging_generation->current_staging[g.current_slot]->resource
                : submitted_image(g.current_handle);
            const auto color = pixel(image.Get(), 0);
            require((color & 255) == 41 && ((color >> 8) & 255) == (eye ? 160 : 80),
                "retry mixed, poisoned or swapped stereo pixels");
        }
    }
    unsigned attempts{};
    const auto fatal = submit_luke_worker_generation(state, right->frame_generation, synth, true,
        [&] { ++attempts; return DXGI_ERROR_DEVICE_REMOVED; });
    require(fatal == DXGI_ERROR_DEVICE_REMOVED && attempts == 1,
        "real GPU failure was retried or hidden");
    ComPtr<ID3D12InfoQueue> info;
    if (SUCCEEDED(state->d3d12_device.As(&info)))
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
            SIZE_T size{}; info->GetMessage(i, nullptr, &size);
            std::vector<unsigned char> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            info->GetMessage(i, message, &size);
            require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
                "GPU debug error during second-eye backpressure");
        }
    std::cout << "Real second-eye GPU backpressure: " << (timeout ? "bounded timeout" : "same stereo pixels, producer locks free")
              << "; fatal device error preserved PASS\n";
}
