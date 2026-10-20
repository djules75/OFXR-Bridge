// Public UEVR SDK callbacks, used only as observation. No renderer hook or GPU
// synchronization is added. A per-thread engine tick is associated with the
// poses produced on that same thread; End must match the exact stereo record.
namespace uevr_production_timing {
thread_local xrfg::FrameProductionStart active;
std::atomic<std::uint64_t> serial{};
void begin(UEVR_UGameEngineHandle engine,float) {
    active={};
    if(xrfg::interface_measurement_requested())
        active={reinterpret_cast<std::uintptr_t>(engine),++serial,xrfg::interface_clock_ns()};
}
void end(UEVR_UGameEngineHandle,float) {active={};}
struct Registration;
std::mutex registration_mutex;
std::weak_ptr<Registration> current_registration;
const UEVR_PluginFunctions* registered_functions{};
struct Registration {
    bool owns{};
    ~Registration() {
        if(!owns)return;
        std::scoped_lock lock(registration_mutex);
        // A replacement session can acquire observation while an old last
        // reference is retiring. In that case transfer the registration.
        if(!current_registration.expired())return;
        if(registered_functions && GetModuleHandleW(L"UEVRBackend.dll")) {
            registered_functions->remove_callback(reinterpret_cast<void*>(&begin));
            registered_functions->remove_callback(reinterpret_cast<void*>(&end));
        }
        registered_functions=nullptr;
    }
};
std::shared_ptr<Registration> acquire() {
    std::scoped_lock lock(registration_mutex);
    if(auto live=current_registration.lock())return live;
    const auto module=GetModuleHandleW(L"UEVRBackend.dll");
    if(!module)return {};
    const auto* api=reinterpret_cast<const UEVR_PluginInitializeParam*>(GetProcAddress(module,"g_plugin_initialize_param"));
    if(!api || !api->version || api->version->major!=2 || !api->sdk || !api->sdk->callbacks ||
       !api->functions || !api->functions->remove_callback)return {};
    const auto* cb=api->sdk->callbacks;
    if(!cb->on_pre_engine_tick || !cb->on_post_engine_tick)return {};
    auto registration=std::make_shared<Registration>();
    if(registered_functions==api->functions) {
        registration->owns=true;
        current_registration=registration;return registration;
    }
    // Allocate first, but never destroy a Registration while holding this
    // mutex on a failed SDK call: its destructor uses the same mutex.
    if(!cb->on_pre_engine_tick(&begin))return {};
    if(!cb->on_post_engine_tick(&end)) {
        api->functions->remove_callback(reinterpret_cast<void*>(&begin));return {};
    }
    registered_functions=api->functions;
    registration->owns=true;
    current_registration=registration;return registration;
}
}
