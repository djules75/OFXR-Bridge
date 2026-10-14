void elliott_r26_release_test(bool claimed, bool overwrite_receipt=false) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> d11;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&d11,nullptr,&context)),"D11 bridge fixture device");
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;
    check(SUCCEEDED(d11.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)),"same adapter");
    ComPtr<ID3D12Device> d12;check(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12))),"D12 bridge fixture device");
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;
    check(SUCCEEDED(d12->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))),"bridge queue");
    auto s=session();s->presenter_active=false;s->three_slot_director=false;
    s->graphics_binding=SessionGraphicsBinding::d3d12;s->d3d12_device=d12;s->d3d12_queue=queue;
    s->menu_enabled=true;s->dlss_motion_vectors=true; s->frame_generation_mode=xrfg::D3D12FrameGenerationMode::ofxr;s->d3d11_device=d11;s->luke_afw_enabled=false;s->djules_history_wait_enabled=false;
    s->dispatch->release_swapchain_image=+[](XrSwapchain,const XrSwapchainImageReleaseInfo*)->XrResult {
        ++v474_runtime_releases;return v474_release_result;
    };
    s->dispatch->acquire_swapchain_image=+[](XrSwapchain,const XrSwapchainImageAcquireInfo*,std::uint32_t* index)->XrResult {*index=0;return XR_SUCCESS;};
    s->dispatch->wait_swapchain_image=+[](XrSwapchain,const XrSwapchainImageWaitInfo*)->XrResult{return XR_SUCCESS;};
    s->dispatch->destroy_swapchain=+[](XrSwapchain)->XrResult{return XR_SUCCESS;};
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.width=ci.height=8;ci.arraySize=ci.faceCount=ci.mipCount=ci.sampleCount=1;
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=8;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> runtime;
    check(SUCCEEDED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&runtime))),"runtime texture");
    auto chain=std::make_shared<SwapchainState>(s,ci);chain->handle=reinterpret_cast<XrSwapchain>(0x474002);
    chain->bridge=std::make_shared<xrfg::D3D11BridgeSwapchain>();
    ID3D12Resource* images[]{runtime.Get()};xrfg::D3D11BridgeSwapchainDescription request{};
    request.requested_format=DXGI_FORMAT_R8G8B8A8_UNORM;request.requested_sample_count=1;std::uint32_t stage{};
    check(SUCCEEDED(chain->bridge->initialize(d11.Get(),context.Get(),d12.Get(),queue.Get(),images,request,&stage)),"real D11/D12 bridge");
    chain->d3d12_history=std::make_shared<xrfg::D3D12SwapchainHistory>();
    check(SUCCEEDED(chain->d3d12_history->initialize(d12.Get(),queue.Get(),images,D3D12_RESOURCE_STATE_RENDER_TARGET)),"history initialized");
    ComPtr<ID3D11RenderTargetView> rtv;check(SUCCEEDED(d11->CreateRenderTargetView(chain->bridge->d3d11_images()[0],nullptr,&rtv)),"source RTV");
    const auto paint=[&](float red){const float color[]{red,0,0,1};context->ClearRenderTargetView(rtv.Get(),color);};
    const auto waited=[&]{chain->acquired_indices={0};chain->waited_count=1;chain->ownership_tracking_valid=true;};
    {std::scoped_lock lock(g_state_mutex);g_swapchains[chain->handle]=chain;}
    struct Remove {XrSwapchain h;~Remove(){std::scoped_lock lock(g_state_mutex);g_swapchains.erase(h);}} cleanup{chain->handle};
    chain->enumerated_d3d12_images={runtime}; chain->projection_eye=0;
    xrfg::configure_dlss_motion_vector_tracking(true);
    struct TrackingCleanup {std::uintptr_t session;~TrackingCleanup(){xrfg::unregister_d3d11_guide_owner(session);xrfg::configure_dlss_motion_vector_tracking(false);xrfg::retire_disabled_dlss_motion_vector_resources();}} guide_cleanup{reinterpret_cast<std::uintptr_t>(s.get())};
    ComPtr<ID3D12Resource> dlss_output,motion;
    check(SUCCEEDED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&dlss_output))) &&
          SUCCEEDED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&motion))),"real guide resources");
    const auto publish=[&](std::uint64_t stream) {
        ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
        check(SUCCEEDED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a))) &&
              SUCCEEDED(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l))),"guide list");
        const auto published=xrfg::publish_dlss_motion_vectors({stream,dlss_output.Get(),motion.Get(),queue.Get(),0,0,8,8,0,0,8,8,1,1,0,0,D3D12_RESOURCE_STATE_RENDER_TARGET,false,false,l.Get()});
        check(bool(published),"actual guide publication accepted");
        check(SUCCEEDED(l->Close()),"close guides");ID3D12CommandList* lists[]{l.Get()};queue->ExecuteCommandLists(1,lists);
        xrfg::report_dlss_motion_vector_submission(queue.Get(),1,lists);
        ComPtr<ID3D12Fence> fence;check(SUCCEEDED(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) && SUCCEEDED(queue->Signal(fence.Get(),1)),"guide fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event && SUCCEEDED(fence->SetEventOnCompletion(1,event)),"guide event");
        const auto ready=WaitForSingleObject(event,10000);CloseHandle(event);check(ready==WAIT_OBJECT_0,"guides complete");
    };
    publish(53126); const auto original_publication=xrfg::dlss_motion_vector_publications();
    if(claimed) {
        const auto owner=reinterpret_cast<std::uintptr_t>(s.get());
        xrfg::register_d3d11_guide_owner(owner,d11.Get(),d12.Get(),queue.Get());
        ComPtr<ID3D12Device> gd;ComPtr<ID3D12CommandQueue> gq;std::uint64_t generation{};
        check(SUCCEEDED(xrfg::acquire_d3d11_guide_owner(d11.Get(),&gd,&gq,&generation)) && xrfg::claim_d3d11_guides(d11.Get(),generation),"claim exact D11 owner");
        xrfg::set_d3d11_guide_receipt(chain->bridge->d3d11_images()[0],{generation,53126,original_publication,8,8});
    }
    observe_application_frame_thread(*s,true);v474_runtime_releases=0;
    waited();paint(.25F);XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(XR_SUCCEEDED(layer_release_swapchain_image_impl(chain->handle,&release)),"application deferred release");
    check(v474_runtime_releases==0 && chain->pending_bridge_release && !chain->last_released_capture,"copy and capture deferred together");
    publish(53127);
    check(xrfg::dlss_motion_vector_publications()>original_publication,"other eye published before flush");
    if(claimed && overwrite_receipt) {
        auto replacement=xrfg::get_d3d11_guide_receipt(chain->bridge->d3d11_images()[0]);
        replacement.stream=53127;replacement.publication=xrfg::dlss_motion_vector_publications();
        xrfg::set_d3d11_guide_receipt(chain->bridge->d3d11_images()[0],replacement);
    }
    paint(.5F);release_pending_bridge_images(s);
    check(v474_runtime_releases==1 && !chain->pending_bridge_release && chain->last_released_capture,"EndFrame flush commits capture and release");
    check(SUCCEEDED(chain->bridge->wait_for_idle()) && SUCCEEDED(chain->d3d12_history->wait_for_idle()),"bridge/history GPU retired");
    const auto red_byte=[&](ID3D12Resource* texture,D3D12_RESOURCE_STATES state) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};
        const auto td=texture->GetDesc();d12->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES rh{};rh.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> buffer;
        check(SUCCEEDED(d12->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))),"readback buffer");
        ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
        check(SUCCEEDED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))) && SUCCEEDED(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))),"readback list");
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={texture,0,state,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=buffer.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
        check(SUCCEEDED(list->Close()),"close readback");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
        ComPtr<ID3D12Fence> fence;check(SUCCEEDED(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) && SUCCEEDED(queue->Signal(fence.Get(),1)),"readback fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event && SUCCEEDED(fence->SetEventOnCompletion(1,event)),"readback event");
        const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);check(wait==WAIT_OBJECT_0,"readback completion");
        void* mapped{};D3D12_RANGE range{0,1};check(SUCCEEDED(buffer->Map(0,&range,&mapped)),"map readback");const auto red=*static_cast<unsigned char*>(mapped);buffer->Unmap(0,nullptr);return red;
    };
    const auto observed_runtime=red_byte(runtime.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    std::cerr<<"bridge runtime red="<<unsigned(observed_runtime)<<" path="<<unsigned(chain->bridge->path())<<"\n";
    check(observed_runtime>=127 && observed_runtime<=128,"runtime displays late rendering, not release-time 64");
    ID3D12Resource* captured{};ID3D12Fence* captured_fence{};xrfg::D3D12HistoryConsumerLease lease{};
    check(SUCCEEDED(chain->d3d12_history->acquire_consumer(*chain->last_released_capture,&lease,&captured,&captured_fence)),"inspect actual committed history");
    ComPtr<ID3D12Resource> owned;owned.Attach(captured);ComPtr<ID3D12Fence> owned_fence;owned_fence.Attach(captured_fence);
    check(red_byte(owned.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)==observed_runtime,"history agrees with runtime late rendering");chain->d3d12_history->cancel_consumer(lease);
    check(chain->last_released_motion_vectors && chain->last_released_motion_vectors->eyes[0],"release retained actual guides");
    const auto actual=chain->last_released_motion_vectors->eyes[0]->publication;
    std::cout<<"R26 claimed="<<claimed<<" original="<<original_publication<<" actual="<<actual<<" boundary="<<chain->release_publication<<"\n";
    check(actual==original_publication,"R26 original eye guide changed during deferred release");
    if(!claimed) check(chain->release_publication==original_publication,"R26 original publication boundary changed");
}
