// Reuse the actual-GPU menu runtime fixture, with an explicit one-swapchain
// UI budget. The production overlay, renderer and shared IPC stay unchanged.
#define main existing_menu_fixture_main
#include "openxr_menu_overlay_tests.cpp"
#undef main

int main(int argc,char** argv) {
    auto fixture=std::filesystem::temp_directory_path()/(L"ofxr-ui-lifetime-"+std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(fixture);
        SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",fixture.c_str());
        d11=argc>1&&std::string(argv[1])=="d3d11";
        if(d11)check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device11,nullptr,nullptr)),"lifetime D11 device");
        else {
            ComPtr<IDXGIFactory4> f;ComPtr<IDXGIAdapter> w;
            check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))&&SUCCEEDED(f->EnumWarpAdapter(IID_PPV_ARGS(&w))),"lifetime WARP");
            check(SUCCEEDED(D3D12CreateDevice(w.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"lifetime D12 device");
            D3D12_COMMAND_QUEUE_DESC q{};check(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"lifetime queue");
        }
        auto ini=fixture/L"ofxr_bridge.ini";position(ini,"off");
        XrCompositionLayerProjectionView v{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        v.fov={-.65F,.55F,.42F,-.61F};v.subImage.imageRect.extent={2688,2784};
        XrCompositionLayerProjection p{XR_TYPE_COMPOSITION_LAYER_PROJECTION};p.viewCount=1;p.views=&v;
        app=reinterpret_cast<XrCompositionLayerBaseHeader*>(&p);
        std::array<const XrCompositionLayerBaseHeader*,1> layers{app};
        XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};frame.layerCount=1;frame.layers=layers.data();next=nullptr;
        chain_limit=1;
        {
            xrfg::OpenXrFpsOverlay overlay(handle<XrInstance>(1),handle<XrSession>(2),1,get,end,
                device.Get(),queue.Get(),device11.Get(),ini,true,poll);
            const auto tick=[&]{overlay.application_frame(&frame);check(overlay.end_frame(&frame,false)==XR_SUCCESS,"lifetime original frame preserved");};
            const auto changed=[&](const char* name){position(ini,name);std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();};
            const auto empty=[&]{for(unsigned n=0;n<100&&!chains.empty();++n){tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                check(chains.empty(),"all unused XR swapchains destroyed before session teardown");};
            input.focused=input.mouse_valid=true;tick();check(chains.empty(),"all off allocates no hidden FPS texture");
            changed("upper_right");std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();check(chains.size()==1&&fps_seen==1,"classic FPS uses one budget slot");
            input.open=true;tick();
            check(menu_seen==0&&fps_seen==1,"refused F10 preserves active FPS and app projection");
            check((xrfg::autonomous::snapshot().runtime.overlay_limit_mask&4)!=0,"F10 refusal reaches shared desktop settings");
            check(xrfg::autonomous::overlay_limit_text(4).find("desktop")!=std::string_view::npos,"warning gives desktop recovery path");
            input.open=false;changed("off");empty();
            check((xrfg::autonomous::snapshot().runtime.overlay_limit_mask&4)!=0,"warning persists after failed F10 closes");
            input.open=true;tick();tick();
            check(chains.size()==1&&menu_seen==1&&fps_seen==0,"F10 reuses freed budget with no implicit FPS chain");
            check((xrfg::autonomous::snapshot().runtime.overlay_limit_mask&4)==0,"successful F10 retry clears its warning");
            input.open=false;tick();empty();
            changed("elliott_bottom");std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();
            check(chains.size()==1&&count_seen==2,"meter alone needs one swapchain and submits a real GPU image");
            changed("off");empty();
            changed("upper_right");std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();check(fps_seen==1&&chains.size()==1,"FPS recreates after actual destruction");
            if(!d11) {
                ComPtr<ID3D12Fence> gate;
                check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))),"retirement gate");
                struct Ungate {ID3D12Fence* fence;~Ungate(){fence->Signal(1);}} ungate{gate.Get()};
                check(SUCCEEDED(queue->Wait(gate.Get(),1)),"hold actual UI GPU upload");
                std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();
                const auto before=destroyed;changed("off");
                check(destroyed==before&&!chains.empty(),"Off must not free images before the real GPU receipt retires");
                check(SUCCEEDED(gate->Signal(1)),"retire actual upload");empty();
                changed("upper_right");std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();
            }
            // A pending XR image cannot be destroyed until wait grants access.
            auto& active=chains.begin()->second;active.timeout=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(260));tick();
            active.timeout=true;const auto before=destroyed;changed("off");
            check(destroyed==before&&!chains.empty(),"Off retains an image while zero-timeout XR wait refuses ownership");
            empty();
        }
        check(made==destroyed&&chains.empty(),"every created UI handle destroyed exactly once");
        std::filesystem::remove(xrfg::autonomous::configuration_path());std::filesystem::remove(ini);std::filesystem::remove(fixture);
        std::cout<<"Unused FPS/meter/F10 swapchains retire safely; one-slot budget recovery and persistent desktop warning PASS\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
