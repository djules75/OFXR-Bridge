// Exercise the production AFW handoff with a real D11 auxiliary texture and
// the precise runtime refusal seen in KCD2. No AFW math is mocked or changed.
void luke_aux_budget_test() {
    auto s=session();
    s->luke_afw_enabled=true; s->dispatch->steamvr_runtime=true;
    s->separate_synthesis=1; s->lower_vram=0;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
        &s->d3d11_device,nullptr,&s->d3d11_context)),"budget D11 device");
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.width=ci.height=16;ci.arraySize=ci.sampleCount=ci.mipCount=ci.faceCount=1;
    ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    auto aux=std::make_shared<SwapchainState>(s,ci);
    aux->handle=reinterpret_cast<XrSwapchain>(0x820);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=16;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    check(SUCCEEDED(s->d3d11_device->CreateTexture2D(&desc,nullptr,&image)),"budget auxiliary texture");
    aux->enumerated_d3d11_images.push_back(image);
    std::array<unsigned,256> color; color.fill(0xff123456);
    s->d3d11_context->UpdateSubresource(image.Get(),0,nullptr,color.data(),16*4,0);
    check(SUCCEEDED(capture_composition_image(aux,0)),"budget release snapshot");
    aux->composition_capture->valid=true;
    static unsigned attempts; static XrResult refusal;
    attempts=0;refusal=XR_ERROR_LIMIT_REACHED;
    s->dispatch->create_swapchain=+[](XrSession,const XrSwapchainCreateInfo*,XrSwapchain*) -> XrResult {
        ++attempts;return refusal;
    };
    std::array<XrCompositionLayerProjectionView,2> views;
    std::vector<PostAfwFrameInput> inputs;
    for(unsigned eye=0;eye<2;++eye) {
        auto& v=views[eye];v={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};v.pose.orientation.w=1;
        v.pose.position.x=eye ? .032f : -.032f;v.fov={-.7f,.7f,.7f,-.7f};
        v.subImage.swapchain=reinterpret_cast<XrSwapchain>(std::uintptr_t(0x301+eye));
        v.subImage.imageRect.extent={16,16};
        PostAfwFrameInput in;in.completed_swapchain=v.subImage.swapchain;
        in.generation=std::make_shared<FrameGenerationSwapchainState>();in.capture.fence_value=1;
        in.views.push_back({});inputs.push_back(in);
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space=reinterpret_cast<XrSpace>(0x100);projection.viewCount=2;projection.views=views.data();
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.subImage.swapchain=aux->handle;quad.subImage.imageRect.extent={16,16};quad.pose.orientation.w=1;
    const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection),
        reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad)};
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=1000000000;
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=2;end.layers=layers;
    ProjectionSnapshot snapshot;check(capture_projection_snapshot(&end,&snapshot),"budget projection snapshot");
    {std::scoped_lock lock(g_state_mutex);g_swapchains[aux->handle]=aux;}
    const auto cleanup=[&]{std::scoped_lock lock(g_state_mutex);g_swapchains.erase(aux->handle);};
    try {
        // A live director cannot receive borrowed pixels. Its failed packet
        // must be rejected; the existing physical owner is drained separately.
        check(!freeze_completed_afw_frame(s,&end,snapshot,inputs,1),"active presenter accepted borrowed quad");
        s->presenter_active=false;
        auto packet=freeze_completed_afw_frame(s,&end,snapshot,inputs,2);
        check(packet && packet->submission->composition_images.empty(),"inline AFW packet still blocked by XR ceiling");
        check(std::get<XrCompositionLayerQuad>(packet->submission->composition_layers[0]).subImage.swapchain==aux->handle,
            "inline quad handle/metadata changed");
        check(presenter_forbidden(*s) && !three_slot_generation_session(s),"budget fallback can promote back to failing owner");
        check(handoff_three_slot_frame(s,&end,snapshot,{},true,packet)==XR_ERROR_LAYER_INVALID,
            "borrowed inline packet reached asynchronous admission");
        const auto n=attempts;
        for(unsigned i=0;i<8;++i) {
            s->separate_synthesis=(i&1);s->lower_vram=(i>>1)&1;
            check(bool(freeze_completed_afw_frame(s,&end,snapshot,inputs,3+i)),"inline retry failed");
            check(s->separate_synthesis==int(i&1) && s->lower_vram==int((i>>1)&1),"fallback silently changes A/B");
        }
        check(attempts==n,"fallback retries refused allocation each frame");
        // Read the original runtime texture as the synchronous consumer does.
        // Both submissions finish before the application's next release.
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        check(SUCCEEDED(s->d3d11_device->CreateTexture2D(&desc,nullptr,&staging)),"budget readback");
        for(unsigned i=0;i<2;++i) {
            s->d3d11_context->CopyResource(staging.Get(),image.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            check(SUCCEEDED(s->d3d11_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"budget consumer map");
            const auto pixel=*static_cast<const unsigned*>(mapped.pData);s->d3d11_context->Unmap(staging.Get(),0);
            check(pixel==0xff123456,"borrowed quad pixels changed before synchronous consumption");
        }
        // A fresh session and other failure classes retain strict ownership.
        for(const auto other:{XR_ERROR_RUNTIME_FAILURE,XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED}) {
            auto fresh=session();fresh->presenter_active=false;fresh->luke_afw_enabled=true;
            fresh->dispatch->steamvr_runtime=true;fresh->dispatch->create_swapchain=s->dispatch->create_swapchain;
            aux->session=fresh;refusal=other;
            check(!freeze_completed_afw_frame(fresh,&end,snapshot,inputs,20),"non-budget error bypassed ownership");
            check(!presenter_forbidden(*fresh),"non-budget error latched inline fallback");
        }
        aux->session=s;
        auto unrelated=session();unrelated->presenter_active=false;unrelated->luke_afw_enabled=true;
        unrelated->dispatch->steamvr_runtime=false;unrelated->dispatch->create_swapchain=s->dispatch->create_swapchain;
        aux->session=unrelated;refusal=XR_ERROR_LIMIT_REACHED;
        check(!freeze_completed_afw_frame(unrelated,&end,snapshot,inputs,21) && !presenter_forbidden(*unrelated),
            "SteamVR budget policy leaked to VDXR");
        aux->session=s;cleanup();
    } catch(...) {aux->session=s;cleanup();throw;}
    std::cout<<"Native D11 AFW auxiliary ceiling: active owner rejected, inline original pixels retained, no retries/promotion/A-B override; other errors and VDXR excluded\n";
}
