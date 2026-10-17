// Actual Reference inline path. During the downstream XR wait, the current
// pair's NVIDIA GPU timestamps must already be resolvable with A enabled.
void luke_private_inline_probe(const std::shared_ptr<SessionState>& state,
    const std::array<std::shared_ptr<SwapchainState>,2>& chains, bool enabled) {
    state->luke_afw_enabled=true;state->separate_synthesis=enabled?1:0;
    state->nvidia_new_route=false;state->presenter_active=false;state->three_slot_director=false;
    struct Probe {
        std::shared_ptr<SessionState> state;
        std::shared_ptr<xrfg::D3D12FrameSynthesizer> active;
        PFN_xrWaitSwapchainImage original{};
        unsigned current_frame{},witnesses{};bool observed{},enabled{};
    };
    static Probe probe;
    probe.state=state;probe.original=state->dispatch->wait_swapchain_image;probe.enabled=enabled;
    struct Reset {Probe& p;~Reset(){p.state->dispatch->wait_swapchain_image=p.original;p={};}} reset{probe};
    state->dispatch->wait_swapchain_image=[](XrSwapchain,const XrSwapchainImageWaitInfo*) {
        std::unique_lock lock(probe.state->gpu_mutex,std::try_to_lock);
        check(lock.owns_lock(),"XR wait holds global GPU lock");lock.unlock();
        if(probe.current_frame>1 && !probe.observed) {
            if(probe.enabled)check(SUCCEEDED(probe.active->wait_for_previous_submission(1000)),"A GPU work did not finish during XR wait");
            xrfg::D3D12NvidiaGpuTiming timing{};
            const auto result=probe.active->consume_nvidia_gpu_timing(&timing);
            check(probe.enabled ? result==S_OK && timing.current_serial==probe.current_frame && timing.eye0_microseconds>0
                          : result==S_FALSE || (result==S_OK && timing.current_serial<probe.current_frame),
                "A did not move actual OFA dispatch before XR wait");
            ++probe.witnesses;probe.observed=true;
        }
        return XR_SUCCESS;
    };
    xrfg::D3D12ReprojectionView view{};view.fov={-.7F,.7F,.7F,-.7F};view.image_rect={0,0,256,128};
    for(unsigned n=1;n<=3;++n)for(unsigned eye=0;eye<2;++eye) {
        probe.current_frame=n;probe.observed=false;
        auto& chain=chains[eye];auto gen=chain->frame_generation;
        const auto bridge=gen->d3d11_interop;probe.active=gen->synthesizer;
        const std::uint32_t pixel=0xff200000u | ((eye?160u:80u)<<8) | (n*24);
        std::vector<std::uint32_t> pixels(256*128,pixel);
        state->d3d11_context->UpdateSubresource(chain->enumerated_d3d11_images[0].Get(),0,nullptr,pixels.data(),256*4,0);
        check(SUCCEEDED(bridge->prepare_capture(0)),"inline A source transfer");
        xrfg::D3D12HistoryCaptureTicket capture{};
        check(SUCCEEDED(chain->d3d12_history->capture(0,&capture)) && SUCCEEDED(chain->d3d12_history->commit(capture)) &&
            SUCCEEDED(bridge->finish_capture()),"inline A immutable source");
        chain->last_released_capture=capture;
        const auto prepared=prepare_frame_generation(chain->handle,n>1,std::span(&view,1),.5F);
        check(prepared.kind==(n>1?PreparedGenerationKind::pair:PreparedGenerationKind::prime),"inline A prepare");
        check(gen->current[0].phase==PrivateOwnershipPhase::idle && gen->current[1].phase==PrivateOwnershipPhase::idle &&
            gen->synthetic[0].phase==PrivateOwnershipPhase::idle && gen->synthetic[1].phase==PrivateOwnershipPhase::idle,
            "inline A retained acquired XR output");
        check(SUCCEEDED(bridge->wait_for_idle()),"inline A publication complete");
        auto image=bridge->published_current_image(prepared.current_destination_index);
        state->d3d11_context->CopyResource(readback.Get(),image.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(state->d3d11_context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"inline A readback");
        const bool identical=*static_cast<const std::uint32_t*>(mapped.pData)==pixel;
        state->d3d11_context->Unmap(readback.Get(),0);
        check(identical,"inline A wrong-eye/stale current pixels");
        if(n==1) {
            // Source index differs from runtime destination. This is required
            // when XR returns a different image from our private work image.
            const auto source=prepared.current_destination_index;
            const auto target=(source+1)%2;
            xrfg::D3D11PublicationReceipt receipt;
            check(SUCCEEDED(bridge->publish(source,std::nullopt,&receipt,
                xrfg::D3D11PublicationTargets{target,std::nullopt})) &&
                SUCCEEDED(xrfg::wait_for_d3d11_publication(receipt)),"mapped publication");
            check(receipt.current_destination_index==target && receipt.private_current_index==source,
                "mapped receipt lost private/runtime identities");
            state->d3d11_context->CopyResource(readback.Get(),bridge->published_current_image(target).Get());
            check(SUCCEEDED(state->d3d11_context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"mapped pixel readback");
            const bool mapped_identical=*static_cast<const std::uint32_t*>(mapped.pData)==pixel;
            state->d3d11_context->Unmap(readback.Get(),0);
            check(mapped_identical,"private output not copied to actual XR image");
            check(SUCCEEDED(bridge->capture_published_current(receipt)) && SUCCEEDED(bridge->finish_capture()),
                "mapped receipt cannot capture its actual published image");
        }
    }
    check(probe.witnesses==4,"incomplete Reference XR-wait GPU witnesses");
    std::cout<<"Luke Reference private A="<<enabled<<": real OFA dispatch before XR wait verified, stereo current pixels, remapped publication and no owned outputs PASS\n";
}
