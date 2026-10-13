#include "xrfg/native_ingress.hpp"
#include "xrfg/d3d12_native_binding.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/neural_rendering.hpp"
#include "xrfg/two_frame_capture.hpp"
#include "xrfg/native_ingress/D3D_DeviceOwner.h"
#include "xrfg/native_ingress/LukeNativeSceneCamera.h"
#include "xrfg/native_ingress/streamline_camera.hpp"
#include <safetyhook.hpp>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>
#include <atomic>

namespace xrfg::native_ingress {
namespace {
using Microsoft::WRL::ComPtr;
struct Hooks {
    std::mutex mutex;
    SafetyHookInline execute, create_queue;
    using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
    using CreateQueue=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    Execute submit{};
    CreateQueue make_queue{};
    std::vector<ComPtr<ID3D12CommandQueue>> queues;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> luke_lists;
    ComPtr<ID3D12Device> luke_device;
    ComPtr<ID3D12CommandQueue> luke_queue;
};
Hooks& hooks(){static auto* value=new Hooks;return *value;}
std::atomic_uint64_t camera_accepted{},camera_rejected{},submissions{};
std::atomic_uint32_t handles{};
std::atomic_uint pending_luke_lists{};
std::atomic_bool has_luke_context{};
template<std::size_t N> bool finite(const std::array<float,N>& values) {
    for(float v:values)if(!std::isfinite(v))return false;
    return true;
}
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists) {
    ++submissions;
    std::vector<ComPtr<ID3D12CommandList>> owners;
    std::vector<ID3D12CommandList*> native_lists;
    owners.reserve(count);native_lists.reserve(count);
    for(UINT i=0;lists&&i<count;++i) {
        IUnknown* native{};ComPtr<ID3D12CommandList> owner;
        if(d3d_native::get_borrowed(lists[i],&native)&&SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&owner)))) {
            native_lists.push_back(owner.Get());owners.push_back(std::move(owner));
        }
    }
    auto receipts=neural_rendering::take_submission_receipts(
        static_cast<unsigned>(native_lists.size()),native_lists.data());
    hooks().submit(queue,count,lists);
    IUnknown* unwrapped{};ComPtr<ID3D12CommandQueue> actual;
    if(d3d_native::get_borrowed(queue,&unwrapped))unwrapped->QueryInterface(IID_PPV_ARGS(&actual));
    if(actual) {
        // The original Execute has accepted these exact lists. Resolve original
        // ownership before publishing its context; never substitute our queue.
        if(pending_luke_lists.load(std::memory_order_acquire)) {
            auto& h=hooks();std::scoped_lock lock(h.mutex);
            for(auto* list:native_lists) {
                auto it=std::find_if(h.luke_lists.begin(),h.luke_lists.end(),
                    [&](const auto& pending){return pending.Get()==list;});
                if(it==h.luke_lists.end())continue;
                ComPtr<ID3D12Device> owner,reported,canonical;
                IUnknown* native_device{};
                if(SUCCEEDED((*it)->GetDevice(IID_PPV_ARGS(&owner))) &&
                    actual->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT &&
                    SUCCEEDED(actual->GetDevice(IID_PPV_ARGS(&reported))) &&
                    d3d_native::get_borrowed(reported.Get(),&native_device) &&
                    SUCCEEDED(native_device->QueryInterface(IID_PPV_ARGS(&canonical))) &&
                    SUCCEEDED(d3d_native::allocation_owner(reported.Get(),canonical)) &&
                    d3d_owner::same(owner.Get(),canonical.Get())) {
                    // Accepted V30063 backing-owner challenge handles native
                    // device aliases; adapter equality alone never proves it.
                    static std::atomic_uint owner_reports{};
                    if(owner_reports.fetch_add(1,std::memory_order_relaxed)<16)
                        bridge_flight_logger().event(BridgeFlightOperation::luke_afw_stage,
                            S_OK,960,reinterpret_cast<std::uintptr_t>(reported.Get()),
                            reinterpret_cast<std::uintptr_t>(canonical.Get()));
                    h.luke_device=std::move(canonical);h.luke_queue=actual;
                    has_luke_context.store(true,std::memory_order_release);
                }
                h.luke_lists.erase(it);
            }
            pending_luke_lists.store(static_cast<unsigned>(h.luke_lists.size()),std::memory_order_release);
        }
        report_dlss_motion_vector_submission(actual.Get(),static_cast<unsigned>(native_lists.size()),native_lists.data());
        two_frame_capture_submission(actual.Get(),static_cast<unsigned>(native_lists.size()),native_lists.data());
        neural_rendering::complete_submission(actual.Get(),receipts);
    }
}
HRESULT STDMETHODCALLTYPE create_queue(ID3D12Device* device,const D3D12_COMMAND_QUEUE_DESC* desc,
    REFIID iid,void** output) {
    const auto result=hooks().make_queue(device,desc,iid,output);
    if(SUCCEEDED(result)&&output&&*output&&desc&&desc->Type==D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ComPtr<ID3D12CommandQueue> queue;
        static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&queue));
        if(queue)configure_queue(queue.Get());
    }
    return result;
}
bool kcd() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    auto* basename=wcsrchr(path,L'\\');basename=basename?basename+1:path;
    return _wcsicmp(basename,L"KingdomCome.exe")==0;
}
}
bool luke_detected() noexcept {
    // basename lookup alone misses RealVR loaded by an absolute custom path.
    const auto marked=[](HMODULE module){return module&&GetProcAddress(module,"RealVRVersion");};
    if(marked(GetModuleHandleW(L"dxgi.dll"))||marked(GetModuleHandleW(L"RealVR64.dll")))return true;
    HMODULE modules[1024]{};DWORD bytes{};
    using Enum=BOOL(WINAPI*)(HANDLE,HMODULE*,DWORD,LPDWORD);
    const auto kernel=GetModuleHandleW(L"kernel32.dll");
    const auto enumerate=reinterpret_cast<Enum>(GetProcAddress(kernel,"K32EnumProcessModules"));
    if(!enumerate||!enumerate(GetCurrentProcess(),modules,sizeof(modules),&bytes))return false;
    for(unsigned i=0;i<std::min<DWORD>(bytes/sizeof(HMODULE),1024);++i) {
        wchar_t path[32768]{};if(!GetModuleFileNameW(modules[i],path,32768))continue;
        auto* filename=wcsrchr(path,L'\\');filename=filename?filename+1:path;
        if(marked(modules[i]))return true;
    }
    return false;
}
void configure_queue(ID3D12CommandQueue* input) noexcept {
    try {
        IUnknown* native{};ComPtr<ID3D12CommandQueue> queue;
        if(!d3d_native::get_borrowed(input,&native)||FAILED(native->QueryInterface(IID_PPV_ARGS(&queue))))return;
        auto& s=hooks();std::scoped_lock lock(s.mutex);
        if(std::none_of(s.queues.begin(),s.queues.end(),[&](auto& q){return q.Get()==queue.Get();}))s.queues.push_back(queue);
        if(!s.execute) {
            auto** table=*reinterpret_cast<void***>(queue.Get());
            s.execute=safetyhook::create_inline(table[10],reinterpret_cast<void*>(&execute),SafetyHookInline::StartDisabled);
            if(s.execute){s.submit=s.execute.original<Hooks::Execute>();(void)s.execute.enable();}
        }
        ComPtr<ID3D12Device> device;if(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device)))&&kcd())
            luke_scene_camera::install(device.Get());
    }catch(...) {}
}
void install_device(ID3D12Device* input) noexcept {
    try {
        IUnknown* native{};ComPtr<ID3D12Device> device;
        if(!d3d_native::get_borrowed(input,&native)||FAILED(native->QueryInterface(IID_PPV_ARGS(&device))))return;
        auto& s=hooks();std::scoped_lock lock(s.mutex);
        if(!s.create_queue) {
            auto** table=*reinterpret_cast<void***>(device.Get());
            s.create_queue=safetyhook::create_inline(table[8],reinterpret_cast<void*>(&create_queue),SafetyHookInline::StartDisabled);
            if(s.create_queue){s.make_queue=s.create_queue.original<Hooks::CreateQueue>();(void)s.create_queue.enable();}
        }
        if(kcd())luke_scene_camera::install(device.Get());
    }catch(...) {}
}
void observe_luke_production(ID3D12GraphicsCommandList* input) noexcept {
    if(!input || !luke_detected())return;
    try {
        IUnknown* borrowed{};ComPtr<ID3D12GraphicsCommandList> list;
        if(!d3d_native::get_borrowed(input,&borrowed) ||
            FAILED(borrowed->QueryInterface(IID_PPV_ARGS(&list))))return;
        ComPtr<ID3D12Device> device;
        if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return;
        install_device(device.Get());
        auto& h=hooks();std::scoped_lock lock(h.mutex);
        if(std::none_of(h.luke_lists.begin(),h.luke_lists.end(),
            [&](const auto& pending){return pending.Get()==list.Get();})) {
            if(h.luke_lists.size()==32)h.luke_lists.erase(h.luke_lists.begin());
            h.luke_lists.push_back(std::move(list));
            pending_luke_lists.store(static_cast<unsigned>(h.luke_lists.size()),std::memory_order_release);
        }
    }catch(...) {}
}
bool query_luke_context(ID3D12Device** device,ID3D12CommandQueue** queue) noexcept {
    if(device)*device=nullptr;if(queue)*queue=nullptr;
    if(!device || !queue || !has_luke_context.load(std::memory_order_acquire) || !luke_detected())return false;
    try {
        auto& h=hooks();std::scoped_lock lock(h.mutex);
        if(!h.luke_device || !h.luke_queue)return false;
        if(FAILED(h.luke_device.CopyTo(device)))return false;
        if(FAILED(h.luke_queue.CopyTo(queue))){(*device)->Release();*device=nullptr;return false;}
        return true;
    }catch(...) {return false;}
}
void stop_queue(ID3D12CommandQueue* queue) noexcept {
    auto& s=hooks();std::scoped_lock lock(s.mutex);
    std::erase_if(s.queues,[&](const auto& owner){return owner.Get()==queue;});
    if(s.luke_queue.Get()==queue){
        s.luke_queue.Reset();s.luke_device.Reset();s.luke_lists.clear();
        has_luke_context.store(false,std::memory_order_release);
        pending_luke_lists.store(0,std::memory_order_release);
    }
}
bool apply_camera(DlssMotionVectorPublication& g,ID3D12GraphicsCommandList* list) noexcept {
    const bool is_luke=luke_detected();g.protect_snapshot_lifetime=is_luke;
    if(is_luke&&kcd()) {
        const auto packet=luke_scene_camera::take(list,g.motion_width,g.motion_height);
        if(!packet.valid){++camera_rejected;return false;}
        const auto& c=packet.camera;
        g.camera_view_to_clip=c.projection;g.camera_clip_to_view=c.inverse_projection;
        g.camera_position=c.position;g.camera_up=c.up;g.camera_right=c.right;g.camera_forward=c.forward;
        g.camera_near=c.near_plane;g.camera_far=c.far_plane;g.camera_fov_vertical=c.fov;
        g.depth_inverted=true;g.depth_infinite=c.far_plane==0;
        g.camera_frame_token=packet.token;g.camera_frame_token_valid=true;
        g.camera_data_valid=g.camera_matrices_valid=true;++camera_accepted;return true;
    }
    const auto c=streamline_camera::current();if(!c.valid){++camera_rejected;return false;}
    g.camera_view_to_clip=c.projection;g.camera_clip_to_view=c.inverse;
    g.camera_position=c.position;g.camera_up=c.up;g.camera_right=c.right;g.camera_forward=c.forward;
    g.camera_near=c.near_z;g.camera_far=c.far_z;g.camera_fov_vertical=c.fov;
    g.depth_inverted=c.inverted;g.depth_infinite=c.far_z==0;
    g.camera_frame_token=c.frame;g.camera_frame_token_valid=true;
    g.raw_camera_motion_included=c.camera_motion;
    if(finite(c.previous)&&finite(c.to_current)){g.raw_clip_to_previous_clip=c.previous;g.raw_previous_clip_to_clip=c.to_current;}
    g.camera_data_valid=g.camera_matrices_valid=true;++camera_accepted;return true;
}
Statistics statistics() noexcept {
    auto& s=hooks();std::scoped_lock lock(s.mutex);
    return {luke_detected(),streamline_camera::hooked(),camera_accepted.load(),camera_rejected.load(),submissions.load()};
}
std::uint32_t allocate_handle_key() noexcept {
    auto prior=handles.load();
    while(prior!=UINT32_MAX)if(handles.compare_exchange_weak(prior,prior+1))return prior+1;
    return 0;
}
}

extern "C" __declspec(dllexport) int OFXR_GetLukeRossD3D12ContextV1(
    ID3D12Device** device,ID3D12CommandQueue** queue) noexcept {
    return xrfg::native_ingress::query_luke_context(device,queue)?1:0;
}
