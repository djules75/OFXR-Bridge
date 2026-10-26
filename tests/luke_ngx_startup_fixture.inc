// Actual session creation must not initialize private NGX before game DLSS.
void luke_ngx_startup_test() {
    const auto root=std::filesystem::temp_directory_path()/
        (L"ofxr-luke-ngx-startup-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    {std::ofstream ini(root/"ofxr_bridge.ini");ini<<"[diagnostics]\nlogging_enabled=1\nmax_file_mb=8\n";}
    auto& logger=xrfg::bridge_flight_logger();logger.shutdown();logger.initialize(root);
    auto marker=LoadLibraryW((current_layer_directory()/L"luke-runtime-fixture.dll").c_str());
    // The cache is deliberately cold until the real session entry refreshes
    // it. Loading a marker alone no longer performs eager discovery (V667).
    check(marker&&GetProcAddress(marker,"RealVRVersion"),"startup Luke marker");
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)),"startup D11 device");
    auto dispatch=std::make_shared<Dispatch>();
    // This is a native D11/VDXR startup fixture, not the private Framework
    // endpoint (which requires a complete runtime dispatch).
    dispatch->virtual_desktop_runtime=true;dispatch->runtime_name="VirtualDesktopXR";
    dispatch->get_instance_proc_addr=+[](XrInstance,const char*,PFN_xrVoidFunction* p)->XrResult {
        *p=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
    };
    dispatch->create_session=+[](XrInstance,const XrSessionCreateInfo*,XrSession* p)->XrResult {
        *p=reinterpret_cast<XrSession>(0x535);return XR_SUCCESS;
    };
    dispatch->destroy_session=+[](XrSession)->XrResult{return XR_SUCCESS;};
    const auto instance=reinterpret_cast<XrInstance>(0x5351);
    {std::scoped_lock lock(g_state_mutex);g_instances[instance]=dispatch;}
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;info.systemId=1;
    XrSession handle{};
    check(XR_SUCCEEDED(layer_create_session_impl(instance,&info,&handle)),"actual cold Luke session");
    auto state=find_session(handle);
    check(state&&state->luke_afw_enabled&&state->d3d12_device&&!(state->graphics_binding_capabilities&8),
        "startup fixture did not exercise private bridge before original game context");
    check(!state->luke_afw,"cold fixture fabricated AFW context");
    check(XR_SUCCEEDED(layer_destroy_session_impl(handle)),"startup session destruction");
    {std::scoped_lock lock(g_state_mutex);g_instances.erase(instance);}
    logger.shutdown();
    unsigned early_queries{};
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        if(entry.path().extension()!=L".log")continue;
        std::ifstream input(entry.path());std::string line;
        while(std::getline(input,line))
            if(line.find("op=native_dlssg_provider")!=std::string::npos)++early_queries;
    }
    FreeLibrary(marker);
    check(early_queries==0,"cold Luke session initialized private NGX before game DLSS");
    std::cout<<"Actual Luke/D11 session: no private NGX first-init/capability query before original game context PASS\n";
}
