// Real original D12 Execute hook, COM ownership, and production layer bootstrap.
void luke_standalone_context_test() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> d11;ComPtr<ID3D11DeviceContext> c11;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d11,nullptr,&c11)),"D11 device");
    ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> private_queue,original_queue;
    check(SUCCEEDED(xrfg::create_d3d12_device_for_d3d11(d11.Get(),&d12,&private_queue)),"private bridge");
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(SUCCEEDED(d12->CreateCommandQueue(&desc,IID_PPV_ARGS(&original_queue))),"pre-existing game queue");
    xrfg::native_ingress::configure_queue(private_queue.Get());
    ComPtr<ID3D12Device> queried;ComPtr<ID3D12CommandQueue> q;
    check(!query_luke_ross_d3d12_context(&queried,&q),"ordinary host rejected");
    auto marker=LoadLibraryW((current_layer_directory()/L"luke-runtime-fixture.dll").c_str());
    check(marker && xrfg::native_ingress::luke_detected(),"Luke fixture detected");
    check(!query_luke_ross_d3d12_context(&queried,&q),"queue creation is not original production");
    auto state=session();state->presenter_active=false;state->three_slot_director=false;
    state->luke_afw_enabled=true;state->graphics_binding_capabilities=7;
    state->d3d11_device=d11;state->d3d11_context=c11;
    state->d3d12_device=d12;state->d3d12_queue=private_queue;
    initialize_late_luke_context(state);
    check(!state->luke_afw && state->d3d12_queue==private_queue,"cold bootstrap stays bypass");
    auto pending=std::make_shared<SwapchainState>(state,XrSwapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO});
    pending->generation_eligible_pending=true;
    check(ensure_frame_generation(pending) && pending->generation_eligible_pending &&
        !pending->generation_declined && !pending->frame_generation,
        "cold native handoff preserves projection eligibility without unrelated GPU allocations");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    check(SUCCEEDED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"allocator");
    check(SUCCEEDED(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))),"list");
    check(SUCCEEDED(list->Close()),"unrelated close");
    ID3D12CommandList* unrelated[]{list.Get()};original_queue->ExecuteCommandLists(1,unrelated);
    ComPtr<ID3D12Fence> fence;check(SUCCEEDED(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))),"fence");
    check(SUCCEEDED(original_queue->Signal(fence.Get(),1)),"fence signal");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(fence->GetCompletedValue()<1 && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(fence->GetCompletedValue()>=1,"unrelated list retired");
    check(!query_luke_ross_d3d12_context(&queried,&q),"unrelated execution is not SR/RR evidence");
    check(SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(),nullptr)),"list reset");
    xrfg::native_ingress::observe_luke_production(list.Get());
    check(!query_luke_ross_d3d12_context(&queried,&q),"recording alone is not submission");
    check(SUCCEEDED(list->Close()),"close");ID3D12CommandList* lists[]{list.Get()};
    original_queue->ExecuteCommandLists(1,lists);
    check(query_luke_ross_d3d12_context(&queried,&q),"actual queue context export resolves");
    check(q==original_queue && queried==d12 && q!=private_queue,"original exact native owner, not bridge queue");
    state->application_frame_in_progress=true;initialize_late_luke_context(state);
    check(!state->luke_afw && state->d3d12_queue==private_queue,"in-flight frame defers binding");
    state->application_frame_in_progress=false;initialize_late_luke_context(state);
    check(state->luke_afw && (state->graphics_binding_capabilities&8) &&
        !(state->graphics_binding_capabilities&4) && state->d3d12_queue &&
        state->d3d12_queue!=original_queue && state->d3d12_queue!=private_queue &&
        state->d3d12_device==d12,"late AFW uses separate queue on exact native device");
    queried.Reset();q.Reset();
    check(query_luke_ross_d3d12_context(&queried,&q)&&q==original_queue&&queried==d12,
        "private execution queue replaced original SR/RR provenance");
    auto execution_queue=state->d3d12_queue;
    auto initialized=state->luke_afw;initialize_late_luke_context(state);
    check(state->luke_afw==initialized&&state->d3d12_queue==execution_queue,"no repeat initialization");
    xrfg::stop_ngx_guide_capture(original_queue.Get());
    queried.Reset();q.Reset();
    check(!query_luke_ross_d3d12_context(&queried,&q),"retired producer does not survive reset");
    check(!xrfg::native_ingress::query_luke_context(nullptr,&q) && !q,"invalid output rejected");
    FreeLibrary(marker);
    std::cout<<"Luke standalone: exact submitted producer retained, separate execution queue, delayed bootstrap, real AFW provider, retirement\n";
}
