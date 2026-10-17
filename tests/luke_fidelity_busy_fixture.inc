// Real D11->D12 captures and FidelityFX. Hold the preceding submission on the
// GPU; the next raw Luke stereo input must wait/retry, not lose its anchor.
void luke_fidelity_busy_probe(const std::shared_ptr<SessionState>& state,
    const std::shared_ptr<SwapchainState>& chain) {
    state->luke_afw_enabled=true; state->nvidia_new_route=false;
    state->presenter_active=false; state->three_slot_director=false;
    state->optical_flow_backend=xrfg::D3D12OpticalFlowBackend::fidelity_fx;
    auto gen=chain->frame_generation;auto bridge=gen->d3d11_interop;
    std::array<xrfg::D3D12HistoryCaptureTicket,2> captures;
    std::array<xrfg::D3D12ReprojectionView,2> views{};
    for(unsigned eye=0;eye<2;++eye) {
        views[eye].pose.orientation.w=1;views[eye].fov={-.7F,.7F,.7F,-.7F};
        views[eye].image_rect={0,0,256,128};views[eye].array_slice=eye;
    }
    for(unsigned n=0;n<2;++n) {
        for(unsigned eye=0;eye<2;++eye) {
            std::vector<std::uint32_t> pixels(256*128,0xff200000u|((eye?160u:80u)<<8)|(32u+n*32));
            state->d3d11_context->UpdateSubresource(chain->enumerated_d3d11_images[0].Get(),eye,nullptr,pixels.data(),256*4,0);
        }
        check(SUCCEEDED(bridge->prepare_capture(0)),"Fidelity source prepare");
        check(SUCCEEDED(chain->d3d12_history->capture(0,&captures[n])) &&
            SUCCEEDED(chain->d3d12_history->commit(captures[n])) &&
            SUCCEEDED(bridge->finish_capture()) && SUCCEEDED(bridge->wait_for_idle()),"Fidelity immutable stereo source");
    }
    Microsoft::WRL::ComPtr<ID3D12Fence> gate;
    check(SUCCEEDED(state->d3d12_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))),"Fidelity gate");
    check(SUCCEEDED(state->d3d12_queue->Wait(gate.Get(),1)),"hold previous Fidelity submission");
    // Always release GPU work before any failure unwinds its resources.
    struct Release {ID3D12Fence* gate;~Release(){gate->Signal(1);}} release{gate.Get()};
    chain->last_released_capture=captures[0];
    auto prime=prepare_frame_generation(chain->handle,false,views,.5F);
    if(prime.kind!=PreparedGenerationKind::prime)gate->Signal(1);
    check(prime.kind==PreparedGenerationKind::prime,"blocked Fidelity prime submission");
    chain->last_released_capture=captures[1];
    PreparedGeneration pair;std::atomic<bool> started{},finished{};
    std::thread producer([&] {started=true;pair=prepare_frame_generation(chain->handle,true,views,.5F);finished=true;});
    for(unsigned i=0;i<200 && !started.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool waiting=!finished.load();
    const bool gpu_free=state->gpu_mutex.try_lock();if(gpu_free)state->gpu_mutex.unlock();
    const bool call_free=chain->call_mutex.try_lock();if(call_free)chain->call_mutex.unlock();
    gate->Signal(1);producer.join();
    check(waiting && gpu_free && call_free,"inline Fidelity busy did not wait outside producer locks");
    check(pair.kind==PreparedGenerationKind::pair && pair.anchor_is_current,
        "inline Fidelity busy discarded the stereo transaction");
    check(SUCCEEDED(bridge->wait_for_idle()),"Fidelity pair publication");
    D3D11_TEXTURE2D_DESC desc{};chain->enumerated_d3d11_images[0]->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
    check(SUCCEEDED(state->d3d11_device->CreateTexture2D(&desc,nullptr,&readback)),"Fidelity stereo readback");
    state->d3d11_context->CopyResource(readback.Get(),bridge->published_current_image(pair.current_destination_index).Get());
    for(unsigned eye=0;eye<2;++eye) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(state->d3d11_context->Map(readback.Get(),eye,D3D11_MAP_READ,0,&mapped)),"Fidelity map");
        const auto pixel=*static_cast<const std::uint32_t*>(mapped.pData);
        state->d3d11_context->Unmap(readback.Get(),eye);
        check(pixel==(0xff200000u|((eye?160u:80u)<<8)|64u),"Fidelity stale/cross-eye retry output");
    }
    std::cout<<"Real inline Luke/Fidelity busy: exact stereo capture retained, locks released, current pixels verified\n";
}
