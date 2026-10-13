#include "xrfg/ngx_guide_capture.hpp"
#include "xrfg/upscaler_observation.hpp"
#ifdef XRFG_NATIVE_DLSSG
#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/third_party/uevr_api.h"
#include "xrfg/native_ingress.hpp"
#include "xrfg/native_ingress/D3D_DeviceOwner.h"
#include "xrfg/luke_color_origin.hpp"
#include "xrfg/embedded_control.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/two_frame_capture.hpp"
#include "xrfg/neural_rendering.hpp"
#include "xrfg/d3d12_native_binding.hpp"
#include "xrfg/vulkan_native_ingress.hpp"
#include "xrfg/ngx_feature_metadata.hpp"
#include "xrfg/native_export_address.hpp"
#include "xrfg/luke_dispatch_module.hpp"
#include "xrfg/ngx_feature_paths.hpp"
#include "xrfg/ngx_evaluation_boundary.hpp"
#include <windows.h>
#include <wrl/client.h>
#include <nvsdk_ngx_params.h>
#include <safetyhook.hpp>
#include <d3d12sdklayers.h>
#include <array>
#include <algorithm>
#include <climits>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <share.h>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef XRFG_NATIVE_INGRESS_TESTS
#include <stdexcept>
#include <iostream>
#include <dxgi1_6.h>
#include "xrfg/native_ingress/streamline1/sl1.h"
#include "xrfg/native_ingress/streamline_camera.hpp"
#endif

namespace xrfg { namespace {
using Microsoft::WRL::ComPtr;
using Create = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using Release = NVSDK_NGX_Result(*)(NVSDK_NGX_Handle*);
using Evaluate = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*, void*);
using Feature = NgxFeatureMetadata;
struct Module {
    SafetyHookInline create_hook, evaluate_hook, evaluate_c_hook, release_hook;
    Create create{}; Evaluate evaluate{}, evaluate_c{}; Release release{};
    HMODULE retained{};
    bool typed_implementation{};
};
struct Capture {
    std::mutex mutex, log_mutex;
    ComPtr<ID3D12CommandQueue> queue;
    std::array<Module,5> modules;
    std::unordered_map<const NVSDK_NGX_Handle*,Feature> features;
    std::unordered_map<const NVSDK_NGX_Handle*,std::uint64_t> native_production_sequences;
    std::atomic_bool active{false}, started{false}, camera_ready{false};
    std::atomic_uint64_t calls{0};
    FILE* log{};
    ComPtr<ID3D12InfoQueue1> debug_queue;
    DWORD debug_cookie{};
    float camera_near{}, camera_far{};
    bool depth_inverted{}, depth_infinite{}, camera_valid{}, camera_from_xr{};
};
// Hooks retain their trampolines and pin this module for process lifetime.
// No CRT static destructor tries to unhook under the Windows loader lock.
Capture& state() { static auto* s=new Capture; return *s; }
void log(const char* message, UINT64 a=0, UINT64 b=0, UINT64 c=0, UINT64 d=0) {
    static const bool forced=GetEnvironmentVariableW(L"XRFG_TEST_NATIVE_DLSSG_DRED",nullptr,0)!=0;
    if(!forced && !bridge_flight_logger().enabled() && !bridge_flight_logger().ram_enabled())return;
    auto& s=state(); std::scoped_lock lock(s.log_mutex);
    if(!s.log) {
        wchar_t dir[MAX_PATH]{}; GetModuleFileNameW(
            reinterpret_cast<HMODULE>([] { HMODULE m{}; GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&state),&m); return m; }()),dir,MAX_PATH);
        auto* slash=wcsrchr(dir,L'\\'); if(!slash)return; *slash=0;
        wchar_t path[MAX_PATH]{}; swprintf_s(path,L"%s\\ofxr-ngx-capture-pid%lu.log",dir,GetCurrentProcessId());
        s.log=_wfsopen(path,L"wb",_SH_DENYWR);
    }
    if(s.log) { fprintf(s.log,"%llu %s a=%llu b=%llu c=%llu d=%llu\n",
        GetTickCount64(),message,a,b,c,d); fflush(s.log); }
}
template<class T> T get(const NVSDK_NGX_Parameter* p,const char* name,T fallback={}) {
    T value=fallback;
    if(!p||NVSDK_NGX_FAILED(p->Get(name,&value)))return fallback;
    return value;
}
#ifdef XRFG_NATIVE_INGRESS_TESTS
bool test_kcd_production_identity{};
#endif
bool kingdom_come_process() {
    static const bool selected=[] {
        wchar_t process[MAX_PATH]{};GetModuleFileNameW(nullptr,process,MAX_PATH);
        const auto* basename=wcsrchr(process,L'\\');basename=basename?basename+1:process;
        return !_wcsicmp(basename,L"KingdomCome.exe");
    }();
#ifdef XRFG_NATIVE_INGRESS_TESTS
    return selected || test_kcd_production_identity;
#else
    return selected;
#endif
}
template<class T> ComPtr<T> native(T* object) {
    ComPtr<T> result; IUnknown* borrowed{};
    if(d3d_native::get_borrowed(object,&borrowed))borrowed->QueryInterface(IID_PPV_ARGS(&result));
    return result;
}
bool same(IUnknown* a,IUnknown* b) {
    ComPtr<IUnknown> x,y;
    return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;
}
void update_uevr_projection() {
    // UEVR's public SDK exposes the projection actually used by the renderer.
    // This also works when the application does not attach OpenXR depth metadata.
    auto module=GetModuleHandleW(L"UEVRBackend.dll");
    if(!module)return;
    auto* api=reinterpret_cast<const UEVR_PluginInitializeParam*>(
        GetProcAddress(module,"g_plugin_initialize_param"));
    if(!api||!api->version||api->version->major!=2||!api->vr||
       !api->vr->is_runtime_ready||!api->vr->is_runtime_ready()||
       !api->vr->get_ue_projection_matrix)return;
    UEVR_Matrix4x4f eyes[2]{};
    api->vr->get_ue_projection_matrix(0,&eyes[0]);
    api->vr->get_ue_projection_matrix(1,&eyes[1]);
    for(const auto& eye:eyes) {
        for(const auto& row:eye.m)for(float value:row)if(!std::isfinite(value))return;
        // Accept only a validated reversed, infinite perspective projection.
        if(eye.m[2][2]!=0.F||eye.m[2][3]!=1.F||eye.m[3][3]!=0.F||
           eye.m[3][2]<=0.F||eye.m[0][0]<=0.F||eye.m[1][1]<=0.F)return;
    }
    if(eyes[0].m[3][2]!=eyes[1].m[3][2])return;
    auto& s=state();std::scoped_lock lock(s.mutex);
    if(s.camera_from_xr)return;
    const bool first=!s.camera_valid;
    // Keep renderer units throughout the depth/projection math. No assumed UE
    // world-to-meter constant is needed for the rotational camera alignment.
    s.camera_near=eyes[0].m[3][2];
    s.camera_far=s.camera_near*10000.F; // unused by infinite-Z unprojection
    s.depth_inverted=true;s.depth_infinite=true;s.camera_valid=true;s.camera_ready=true;
    if(first)log("uevr_projection",UINT64(s.camera_near*1000000.F),api->version->minor);
}
std::shared_ptr<const DlssMotionVectorFrame> capture(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* p,std::uint64_t production_sequence) {
    auto& s=state(); if(!s.active.load())return {};
    const auto call=++s.calls;
    if(!list||!p)return {};
    // The projection rarely changes: refresh it every 32 evaluations, and on
    // every one until it is known. OpenXR depth metadata takes precedence.
    if(!s.camera_ready.load()||call%32==0)update_uevr_projection();
    ComPtr<ID3D12CommandQueue> queue; Feature feature{}; bool known{};
    float camera_near{},camera_far{}; bool inverted{},infinite{},camera_valid{};
    { std::scoped_lock lock(s.mutex); queue=s.queue;
      auto it=s.features.find(handle); if(it!=s.features.end()){feature=it->second;known=true;}
      camera_near=s.camera_near;camera_far=s.camera_far;
      inverted=s.depth_inverted;infinite=s.depth_infinite;camera_valid=s.camera_valid; }
    const bool luke=native_ingress::luke_detected();
    if(luke) {
        ComPtr<ID3D12Device> owner;ComPtr<ID3D12CommandQueue> producer;
        if(!native_ingress::query_luke_context(owner.GetAddressOf(),producer.GetAddressOf()))return {};
        queue=std::move(producer);
    }
    if(!queue)return {};
    // Only creation-observed SR/RR types may enter guide capture or NR. NGX
    // evaluation maps alone cannot distinguish SR, DLSS-G and neural rendering.
    if(!known || (feature.type!=NVSDK_NGX_Feature_SuperSampling&&
        feature.type!=NVSDK_NGX_Feature_RayReconstruction))return {};
    auto output_owner=native(get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_Output));
    auto* output=output_owner.Get();
    auto motion=native(get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_MotionVectors));
    auto depth=native(get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_Depth));
    if(!output||!motion||!depth) { if(call<8)log("missing_guides",call); return {}; }
    auto cmd=native(list); auto nq=native(queue.Get());if(!cmd||!nq)return {};
    ComPtr<ID3D12Device> md,dd,cd,qd;
    if(FAILED(motion->GetDevice(IID_PPV_ARGS(&md)))||FAILED(depth->GetDevice(IID_PPV_ARGS(&dd)))||
       FAILED(cmd->GetDevice(IID_PPV_ARGS(&cd)))||FAILED(nq->GetDevice(IID_PPV_ARGS(&qd))))return {};
    if(luke) {
        // Keep the real game resources/list/queue intact. Only the proven
        // allocation owner bypasses an injector's private-queue interception.
        // WRL operator& converts through ComPtrRef and clears the owner before
        // returning ComPtr*. Take the storage addresses without that overload.
        std::array<ComPtr<ID3D12Device>*,4> owners{
            std::addressof(md),std::addressof(dd),std::addressof(cd),std::addressof(qd)};
        for(auto* owner:owners) {
            ComPtr<ID3D12Device> canonical;
            const auto result=d3d_native::allocation_owner(owner->Get(),canonical);
            if(FAILED(result)) {
                static std::atomic_uint failures{};
                if(failures.fetch_add(1,std::memory_order_relaxed)<8)
                    log("native_allocation_owner_failed",static_cast<UINT>(result),
                        reinterpret_cast<UINT64>(owner->Get()));
                return {};
            }
            *owner=std::move(canonical);
        }
    }
    if(!d3d_owner::same(md.Get(),dd.Get())||!d3d_owner::same(md.Get(),cd.Get())||!d3d_owner::same(md.Get(),qd.Get())) {
        if(call<8)log("device_mismatch",call); return {};
    }
    const auto od=output->GetDesc(), mv=motion->GetDesc(), dz=depth->GetDesc();
    DlssMotionVectorPublication g{};
    g.stream=reinterpret_cast<UINT64>(handle); g.output=output;
    g.motion_vectors=motion.Get(); g.depth=depth.Get();
    g.producer_queue=nq.Get(); g.producer_command_list=cmd.Get(); g.verified_producer_device=qd.Get();
    g.output_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X);
    g.output_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y);
    g.output_width=ngx_display_dimension(feature.out_width,p,NVSDK_NGX_Parameter_OutWidth,
        UINT(od.Width),feature.creation_observed);
    g.output_height=ngx_display_dimension(feature.out_height,p,NVSDK_NGX_Parameter_OutHeight,
        od.Height,feature.creation_observed);
    g.motion_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X);
    g.motion_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y);
    const auto rw=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,feature.width);
    const auto rh=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,feature.height);
    // Without creation flags, assume render-resolution vectors: the usual
    // DLSS integration, and the one whose rectangle the render size gives.
    const bool full_mv=feature.flags_known&&(feature.flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)==0;
    g.motion_width=full_mv?g.output_width:(rw?rw:UINT(mv.Width));
    g.motion_height=full_mv?g.output_height:(rh?rh:mv.Height);
    g.depth_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X);
    g.depth_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y);
    g.depth_width=rw?rw:UINT(dz.Width); g.depth_height=rh?rh:dz.Height;
    if(luke) {
        // Match OptiScaler's actual native publisher. MV_Offset is a FLOAT
        // NGX parameter, distinct from the integer MV-subrect API key.
        const auto offset=[&](const char* name) {
            const float value=get<float>(p,name);
            return std::isfinite(value) && value>0 && value<float(UINT_MAX)
                ? static_cast<UINT>(value) : 0U;
        };
        g.motion_x=offset(NVSDK_NGX_Parameter_MV_Offset_X);
        g.motion_y=offset(NVSDK_NGX_Parameter_MV_Offset_Y);
        const auto available=[](UINT64 extent,UINT base) {
            return static_cast<UINT>(extent-std::min<UINT64>(base,extent));
        };
        g.output_width=std::min(g.output_width,available(od.Width,g.output_x));
        g.output_height=std::min(g.output_height,available(od.Height,g.output_y));
        g.motion_width=std::min(g.motion_width,available(mv.Width,g.motion_x));
        g.motion_height=std::min(g.motion_height,available(mv.Height,g.motion_y));
        g.depth_width=std::min(g.depth_width,available(dz.Width,g.depth_x));
        g.depth_height=std::min(g.depth_height,available(dz.Height,g.depth_y));
        if(two_frame_capture_active()) {
            auto color=native(get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_Color));
            ComPtr<ID3D12Device> color_device;
            if(color && SUCCEEDED(color->GetDevice(IID_PPV_ARGS(&color_device))) &&
                d3d_owner::same(color_device.Get(),qd.Get())) {
                const auto desc=color->GetDesc();g.input_color=color.Get();
                g.input_color_x=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X);
                g.input_color_y=get<UINT>(p,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y);
                g.input_color_width=std::min(rw?rw:UINT(mv.Width),available(desc.Width,g.input_color_x));
                g.input_color_height=std::min(rh?rh:mv.Height,available(desc.Height,g.input_color_y));
                // Parameters retain this original color through publication;
                // no color reference is added to persistent guide history.
            }
        }
    }
    g.scale_x=get<float>(p,NVSDK_NGX_Parameter_MV_Scale_X,1);
    g.scale_y=get<float>(p,NVSDK_NGX_Parameter_MV_Scale_Y,1);
    g.jitter_x=get<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_X);
    g.jitter_y=get<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_Y);
    g.jittered=(feature.flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)!=0;
    g.reset=get<int>(p,NVSDK_NGX_Parameter_Reset)!=0;
    g.resource_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    g.depth_resource_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    g.frame_time_delta_ms=get<float>(p,NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,16.6667F);
    if(luke && (!std::isfinite(g.frame_time_delta_ms) || g.frame_time_delta_ms<=0))
        g.frame_time_delta_ms=16.6667F;
    g.camera_near=get<float>(p,"DLSSG.CameraNear",camera_near);
    g.camera_far=get<float>(p,"DLSSG.CameraFar",camera_far);
    // Unknown flags defer to the camera's own depth convention.
    g.depth_inverted=feature.flags_known?(feature.flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0:inverted;
    // A game with no camera metadata - a native VR game that submits no OpenXR
    // depth, say - still states its depth direction in the feature's flags,
    // which its own DLSS relies on. That is all NGX's result depends on: it
    // measured bit-identical for near planes from 0.01 to 100, finite or
    // infinite, while a wrong direction quadrupled the error at moving edges.
    // So nominal planes stand in for the camera's.
    if(!camera_valid&&feature.flags_known) {
        camera_valid=true; inverted=g.depth_inverted; infinite=false;
        g.camera_near=get<float>(p,"DLSSG.CameraNear",0.1F);
        g.camera_far=get<float>(p,"DLSSG.CameraFar",1000.F);
    }
    if(!camera_valid||inverted!=g.depth_inverted) {
        // The motion alone still serves OFXR + DLSS vectors; native generation
        // finds no depth and shows the current frame.
        if(call<=8||call%300==0)log("waiting_depth_convention",call,feature.flags,camera_valid,inverted);
        g.depth=nullptr;
    }
    g.depth_infinite=infinite;
    // The exact camera is consumed BEFORE SR changes list bindings or any
    // provider mutates native motion. All snapshots are recorded now.
    if(luke&&!feature.flags_known) {
        if(call<=8||call%300==0)log("waiting_creation_flags",call,g.stream);
        return {};
    }
    const bool exact_camera=native_ingress::apply_camera(g,cmd.Get());
    g.native_production_sequence=production_sequence;
    if(exact_camera)g.depth=depth.Get();
    if(luke && !exact_camera) {
        if(embedded::afw_snapshot().desired.mode!=0)embedded::report_afw_ingress(false,false);
        return {};
    }
    auto frame=publish_dlss_motion_vectors(g);
    if(luke&&embedded::afw_snapshot().desired.mode!=0)embedded::report_afw_ingress(bool(frame),exact_camera);
    const HRESULT removed=md->GetDeviceRemovedReason();
    if(FAILED(removed))log("device_removed",static_cast<UINT>(removed),call);
    if(call<=8||call%300==0) {
        log("capture",call,g.stream,(UINT64(g.output_width)<<32)|g.output_height,feature.flags);
        log("rects",(UINT64(g.output_x)<<32)|g.output_y,(UINT64(g.motion_x)<<32)|g.motion_y,
            (UINT64(g.motion_width)<<32)|g.motion_height,(UINT64(g.depth_width)<<32)|g.depth_height);
        log("formats",mv.Format,dz.Format,od.Width,od.Height);
        log("output_format",od.Format,od.Dimension,od.DepthOrArraySize,od.SampleDesc.Count);
    }
    return frame;
}
template<UINT slot> NVSDK_NGX_Result create(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature id,
    NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    // Preserve the caller's real creation contract before an injector or the
    // native implementation mutates its parameter map. No opaque handle reads.
    Feature f{0,id,get<int>(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags),
            get<UINT>(p,NVSDK_NGX_Parameter_Width),get<UINT>(p,NVSDK_NGX_Parameter_Height),
            get<UINT>(p,NVSDK_NGX_Parameter_OutWidth),get<UINT>(p,NVSDK_NGX_Parameter_OutHeight)};
    f.flags_known=p&&NVSDK_NGX_SUCCEED(p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&f.flags));
    f.creation_observed=true;
    const auto result=state().modules[slot].create(list,id,p,out);
    if(NVSDK_NGX_SUCCEED(result)&&out&&*out&&p) {
        f.identity=native_ingress::allocate_handle_key();
        f.creation_source_identity=f.identity;
        bool replaced{};
        {std::scoped_lock lock(state().mutex);
            replaced=state().features.contains(*out);
            state().features[*out]=f;
            state().native_production_sequences.erase(*out);}
        // A new feature may reuse an opaque handle address even if its old
        // release was missed. It must never inherit that generation's history.
        if(replaced&&kingdom_come_process())
            invalidate_dlss_motion_vector_stream_history(reinterpret_cast<UINT64>(*out));
        log("feature",reinterpret_cast<UINT64>(*out),f.flags,(UINT64(f.width)<<32)|f.height,
            (UINT64(f.out_width)<<32)|f.out_height);
    }
    return result;
}
void complete_color(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Parameter* p,
    const std::shared_ptr<const DlssMotionVectorFrame>& frame,bool hdr) {
    if(!frame||!frame->color_origin_token||!native_ingress::luke_detected()||
       embedded::afw_snapshot().desired.mode==0)return;
    auto color=native(get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_Output));
    auto cmd=native(list);if(!color||!cmd||!same(color.Get(),frame->output.Get()))return;
    ComPtr<ID3D12Device> cd,ld;
    if(FAILED(color->GetDevice(IID_PPV_ARGS(&cd)))||FAILED(cmd->GetDevice(IID_PPV_ARGS(&ld)))||
       !d3d_owner::same(cd.Get(),frame->verified_producer_device.Get())||
       !d3d_owner::same(ld.Get(),frame->verified_producer_device.Get()))return;
    const auto desc=color->GetDesc();
    // NGX's actual HDR creation flag supplies the typed float view contract.
    // Typeless HDR storage alone never selects a numeric interpretation.
    const auto view=hdr&&desc.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS?DXGI_FORMAT_R16G16B16A16_FLOAT:
        hdr&&desc.Format==DXGI_FORMAT_R32G32B32A32_TYPELESS?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_UNKNOWN;
    const auto result=mark_luke_color_origin(cmd.Get(),color.Get(),frame->color_origin_token,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,frame->verified_producer_device.Get(),
        {frame->output_x,frame->output_y,frame->output_width,frame->output_height},view);
    if(FAILED(result)) {
        log("origin_marker_failed",static_cast<UINT>(result),frame->publication,frame->camera_frame_token);
        static std::atomic_uint64_t last_failure{};
        const auto now=GetTickCount64();auto previous=last_failure.load();
        if(now-previous>=1000&&last_failure.compare_exchange_strong(previous,now))
            log("origin_marker_resource",desc.Format,desc.Dimension,
                (UINT64(desc.DepthOrArraySize)<<32)|desc.SampleDesc.Count,view);
    }
}
template<UINT slot, bool c_entry = false> NVSDK_NGX_Result evaluate(ID3D12GraphicsCommandList* list,
    const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,void* callback) {
    const auto original = c_entry ? state().modules[slot].evaluate_c : state().modules[slot].evaluate;
    if(kingdom_come_process()) {
        static std::atomic_uint64_t last_entry{};
        const auto now=GetTickCount64();auto previous=last_entry.load();
        if(now-previous>=1000&&last_entry.compare_exchange_strong(previous,now)) {
            int flags{};
            const auto result=p?p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags):NVSDK_NGX_Result_Fail;
            log("native_evaluate_entry",slot,state().modules[slot].typed_implementation,h!=nullptr,
                static_cast<UINT>(result));
        }
    }
    auto native_list=native(list);
    ComPtr<IUnknown> native_list_identity;
    if(native_list)static_cast<void>(native_list.As(&native_list_identity));
    bool sr=false,hdr=false,native_available=false;unsigned identity{};ComPtr<ID3D12CommandQueue> queue;
    bool replaced_native_generation{};
    Feature evaluation_metadata{};
    {std::scoped_lock lock(state().mutex);queue=state().queue;
     if constexpr(slot<2)if(state().modules[slot].typed_implementation&&h&&p) {
        // This trampoline belongs to the SR/RR implementation module, unlike
        // the generic core dispatcher. Its provenance identifies feature type;
        // creation dimensions may already be erased or inverted in the eval
        // map. A known public ancestor supplies its immutable contract only
        // within this exact evaluation on the same native command list.
        auto existing=state().features.find(h);
        Feature recovered{};
        const auto type=slot==0?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_RayReconstruction;
        const bool direct_creation=existing!=state().features.end()&&existing->second.creation_observed&&
            existing->second.creation_source_identity!=0&&
            existing->second.creation_source_identity==existing->second.identity;
        const bool inherited=!direct_creation&&NgxCreationEvaluationScope::inherit_native(type,
            native_list_identity.Get(),recovered);
        const bool inherited_unchanged=inherited&&existing!=state().features.end()&&
            existing->second.creation_observed&&existing->second.type==recovered.type&&
            existing->second.creation_source_identity==recovered.creation_source_identity&&
            existing->second.flags_known&&recovered.flags_known&&existing->second.flags==recovered.flags&&
            existing->second.width==recovered.width&&existing->second.height==recovered.height&&
            existing->second.out_width==recovered.out_width&&existing->second.out_height==recovered.out_height;
        if(!inherited_unchanged&&(inherited||existing==state().features.end()||
            !existing->second.creation_observed||!existing->second.flags_known)) {
            bool recovered_metadata=inherited;
            if(inherited&&!recovered.flags_known) {
                Feature flags;
                if(recover_typed_ngx_metadata(true,type,p,flags)&&flags.flags_known) {
                    recovered.flags=flags.flags;recovered.flags_known=true;
                }
            }
            if(!inherited&&existing!=state().features.end()&&existing->second.creation_observed) {
                recovered=existing->second;
                Feature flags;
                if(recover_typed_ngx_metadata(true,type,p,flags)&&flags.flags_known) {
                    recovered.flags=flags.flags;recovered.flags_known=true;recovered_metadata=true;
                }
            } else if(!inherited&&(existing==state().features.end()||!existing->second.flags_known))
                recovered_metadata=recover_typed_ngx_metadata(true,type,p,recovered);
            if(recovered_metadata) {
                if(existing==state().features.end()) {
                    recovered.identity=native_ingress::allocate_handle_key();state().features.emplace(h,recovered);
                    log("typed_feature_recovered",slot,recovered.flags_known,
                        (UINT64(recovered.width)<<32)|recovered.height,(UINT64(recovered.out_width)<<32)|recovered.out_height);
                } else if(recovered.flags_known||inherited) {
                    replaced_native_generation=inherited&&
                        existing->second.creation_source_identity!=recovered.creation_source_identity;
                    recovered.identity=replaced_native_generation?native_ingress::allocate_handle_key():existing->second.identity;
                    existing->second=recovered;
                    if(replaced_native_generation)state().native_production_sequences.erase(h);
                    log(inherited?"typed_creation_contract_inherited":"typed_feature_flags_recovered",
                        slot,recovered.flags,(UINT64(recovered.width)<<32)|recovered.height,
                        (UINT64(recovered.out_width)<<32)|recovered.out_height);
                }
            }
        }
     }
     auto it=state().features.find(h);sr=it!=state().features.end()&&
       (it->second.type==NVSDK_NGX_Feature_SuperSampling||it->second.type==NVSDK_NGX_Feature_RayReconstruction);
     if(it!=state().features.end()) {identity=it->second.identity;
         evaluation_metadata=it->second;
         if(sr)native_available=state().modules[it->second.type==NVSDK_NGX_Feature_SuperSampling?0:1].typed_implementation;
         hdr=it->second.flags_known&&(it->second.flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)!=0;}}
    if(replaced_native_generation)
        invalidate_dlss_motion_vector_stream_history(reinterpret_cast<UINT64>(h));
    // An untyped core dispatch may reach the typed SR module, where recovery
    // is exact. Generic core parameters never classify a feature as SR.
    if(!sr) {
        // Known non-SR dispatch blocks an enclosing SR creation receipt.
        // Unknown generic forwarding keeps the caller's exact dynamic scope.
        if(evaluation_metadata.creation_observed) {
            NgxCreationEvaluationScope non_sr(evaluation_metadata,native_list_identity.Get(),false);
            return original(list,h,p,callback);
        }
        return original(list,h,p,callback);
    }
    NgxCreationEvaluationScope creation_scope(evaluation_metadata,native_list_identity.Get(),
        !state().modules[slot].typed_implementation);
    NgxEvaluationBoundary scope(slot,state().modules[slot].typed_implementation,native_available);
    static std::atomic_uint boundary_reports{};
    if(boundary_reports.fetch_add(1,std::memory_order_relaxed)<16)
        log("ngx_observation_boundary",slot,state().modules[slot].typed_implementation,
            native_available,scope.observes());
    if(!scope.observes())return original(list,h,p,callback);
    const bool kcd_production=kingdom_come_process();
    std::uint64_t production_sequence{};
    if(kcd_production) {
        // Allocate at the unique native SR/RR owner, before any camera/guide
        // capture can fail. A generic-only known SR call owns the same logical
        // production. Nested dispatch and unrelated features do not allocate.
        std::scoped_lock lock(state().mutex);
        auto& sequence=state().native_production_sequences[h];
        if(sequence!=UINT64_MAX)production_sequence=++sequence;
    }
    // Observe the original game list even during late XR bootstrap. The actual
    // Execute hook, not this recording call, establishes the producer queue.
    native_ingress::observe_luke_production(list);
    native_ingress::observe_upscaler_inputs(
        get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_MotionVectors) != nullptr,
        get<ID3D12Resource*>(p,NVSDK_NGX_Parameter_Depth) != nullptr);
    std::shared_ptr<const DlssMotionVectorFrame> frame;
    if(sr)try {
        if(!kcd_production || production_sequence)frame=capture(list,h,p,production_sequence);
        neural_rendering::before_sr(identity,list,p,queue.Get());
    }catch(...){log("native_pre_sr_exception");}
    const auto result=original(list,h,p,callback);
    if(sr&&NVSDK_NGX_SUCCEED(result))try {
        neural_rendering::after_sr(identity,list,p,queue.Get());
        if(scope.can_complete())complete_color(list,p,frame,hdr);
    }catch(...){log("native_completion_exception");}
    if(kcd_production) {
        if(NVSDK_NGX_FAILED(result) || !scope.can_complete() || !production_sequence)
            invalidate_dlss_motion_vector_stream_history(reinterpret_cast<UINT64>(h));
        if(production_sequence<=8 || production_sequence%300==0 || NVSDK_NGX_FAILED(result))
            log("kcd_production_identity",production_sequence,frame?frame->camera_frame_token:0,
                bool(frame),static_cast<UINT>(result));
    }
    return result;
}
template<UINT slot> NVSDK_NGX_Result release(NVSDK_NGX_Handle* handle) {
    unsigned id{};{std::scoped_lock lock(state().mutex);auto it=state().features.find(handle);if(it!=state().features.end())id=it->second.identity;}
    const auto result=state().modules[slot].release(handle);
    if(NVSDK_NGX_SUCCEED(result)&&handle) {
        {std::scoped_lock lock(state().mutex);state().features.erase(handle);
            state().native_production_sequences.erase(handle);}
        retire_dlss_motion_vector_stream(reinterpret_cast<UINT64>(handle));
        neural_rendering::retire_handle(id);
    }
    return result;
}
void install(UINT i,HMODULE mod,void* create_fn,void* eval_fn,void* release_fn,void* eval_c_fn = nullptr) {
    auto& m=state().modules[i];if(m.retained||!mod)return;
    auto c=GetProcAddress(mod,"NVSDK_NGX_D3D12_CreateFeature");
    auto e=GetProcAddress(mod,"NVSDK_NGX_D3D12_EvaluateFeature");
    auto r=GetProcAddress(mod,"NVSDK_NGX_D3D12_ReleaseFeature");if(!c||!e||!r)return;
    auto ec=GetProcAddress(mod,"NVSDK_NGX_D3D12_EvaluateFeature_C");
    // KCD embeds its public NGX SDK dispatcher in WHGame.dll. Its exports
    // and C-callback evaluation can bypass both the NVIDIA and REAL VR bodies.
    // Resolve only declared executable exports from the matching module PE.
    if(i==4) {
        c=native_export_address(mod,"NVSDK_NGX_D3D12_CreateFeature");
        e=native_export_address(mod,"NVSDK_NGX_D3D12_EvaluateFeature");
        r=native_export_address(mod,"NVSDK_NGX_D3D12_ReleaseFeature");
        ec=native_export_address(mod,"NVSDK_NGX_D3D12_EvaluateFeature_C");
        if(!c||!e||!r)return;
    }
    // KCD's existing injector can redirect exports before the XR layer loads.
    // Hook the actual typed implementation body used by cached original calls.
    if(i<2&&native_ingress::luke_detected()&&kingdom_come_process()) {
        const auto original_c=native_export_address(mod,"NVSDK_NGX_D3D12_CreateFeature");
        const auto original_e=native_export_address(mod,"NVSDK_NGX_D3D12_EvaluateFeature");
        const auto original_r=native_export_address(mod,"NVSDK_NGX_D3D12_ReleaseFeature");
        log("native_export_resolution",i,original_c&&original_e&&original_r,
            e!=original_e,(UINT64(reinterpret_cast<std::uintptr_t>(e)-reinterpret_cast<std::uintptr_t>(mod))));
        if(original_c&&original_e&&original_r){c=original_c;e=original_e;r=original_r;}
    }
    HMODULE implementation{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(e),&implementation);
    m.typed_implementation=i<2&&implementation==mod;
    HMODULE retained{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(e),&retained))return;
    m.create_hook=safetyhook::create_inline(c,create_fn,SafetyHookInline::StartDisabled);
    m.evaluate_hook=safetyhook::create_inline(e,eval_fn,SafetyHookInline::StartDisabled);
    m.release_hook=safetyhook::create_inline(r,release_fn,SafetyHookInline::StartDisabled);
    const bool separate_c = ec && ec!=e && eval_c_fn;
    if(separate_c)m.evaluate_c_hook=safetyhook::create_inline(ec,eval_c_fn,SafetyHookInline::StartDisabled);
    if(!m.create_hook||!m.evaluate_hook||!m.release_hook||(separate_c&&!m.evaluate_c_hook)){
        m.create_hook={};m.evaluate_hook={};m.evaluate_c_hook={};m.release_hook={};FreeLibrary(retained);log("hook_failed",i);return;}
    m.create=m.create_hook.original<Create>();m.evaluate=m.evaluate_hook.original<Evaluate>();
    m.evaluate_c=separate_c ? m.evaluate_c_hook.original<Evaluate>() : m.evaluate;
    m.release=m.release_hook.original<Release>();m.retained=retained;
    (void)m.create_hook.enable();(void)m.evaluate_hook.enable();(void)m.release_hook.enable();log("hook_installed",i);
    if(separate_c){(void)m.evaluate_c_hook.enable();log("hook_c_installed",i);}
}
template<UINT slot> void install_dispatch(HMODULE module) {
    install(slot,module,reinterpret_cast<void*>(&create<slot>),
        reinterpret_cast<void*>(&evaluate<slot>),reinterpret_cast<void*>(&release<slot>),
        reinterpret_cast<void*>(&evaluate<slot,true>));
}
void install_feature_hooks() {
    install_dispatch<0>(GetModuleHandleW(L"nvngx_dlss.dll"));
    install_dispatch<1>(GetModuleHandleW(L"nvngx_dlssd.dll"));
    install_dispatch<2>(GetModuleHandleW(L"_nvngx.dll"));
    if(kingdom_come_process()) {
        const auto module=luke_dispatch_module();
        install_dispatch<3>(module);
        install_dispatch<4>(GetModuleHandleW(L"WHGame.dll"));
    }
    native_ingress::poll_streamline();
    native_ingress::poll_d3d11_ngx();
    native_vulkan::poll_capture();
}
void start_hook_monitor() {
    static std::atomic_bool started{};if(started.exchange(true))return;
    HMODULE pinned{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&start_hook_monitor),&pinned);
    std::thread([]{for(;;){install_feature_hooks();Sleep(50);}}).detach();
}
// NGX looks for a feature's module only along the path list of the process's
// first initialisation, which is usually the game's own DLSS: OFXR's later
// one succeeds yet reports frame generation not found when the game ships no
// nvngx_dlssg.dll. OFXR's directory goes last on every initialisation's list,
// so a game's own copy still wins. Both core entry points take the list as
// their last argument; the SDK's static library routes every D3D12
// initialisation through one of them.
using InitExt=NVSDK_NGX_Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,
    NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
using InitProject=NVSDK_NGX_Result(*)(const char*,NVSDK_NGX_EngineType,const char*,
    const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
struct Discovery {
    SafetyHookInline ext_hook, project_hook;
    InitExt ext{}; InitProject project{};
    std::wstring directory;
};
Discovery& discovery() { static auto* d=new Discovery; return *d; }
struct DiscoveryInfo {
    std::unique_ptr<NgxFeaturePaths> paths;
    const NVSDK_NGX_FeatureCommonInfo* with_directory(const NVSDK_NGX_FeatureCommonInfo* given) {
        paths=std::make_unique<NgxFeaturePaths>(given,discovery().directory);
        log("ngx_discovery_paths",paths->get()->PathListInfo.Length);
        return paths->changed()?paths->get():given;
    }
};
NVSDK_NGX_Result init_ext(unsigned long long id,const wchar_t* data,ID3D12Device* device,
    NVSDK_NGX_Version version,const NVSDK_NGX_FeatureCommonInfo* given) {
    DiscoveryInfo extended; const NVSDK_NGX_FeatureCommonInfo* info=given;
    try { info=extended.with_directory(given); } catch(...) {}
    native_ingress::install_device(device);
    install_feature_hooks();
    const auto result=discovery().ext(id,data,device,version,info);
    log("ngx_init_ext_result",static_cast<UINT>(result),id,
        info?info->PathListInfo.Length:0);
    return result;
}
NVSDK_NGX_Result init_project(const char* project,NVSDK_NGX_EngineType engine,
    const char* engine_version,const wchar_t* data,ID3D12Device* device,
    NVSDK_NGX_Version version,const NVSDK_NGX_FeatureCommonInfo* given) {
    DiscoveryInfo extended; const NVSDK_NGX_FeatureCommonInfo* info=given;
    try { info=extended.with_directory(given); } catch(...) {}
    native_ingress::install_device(device);
    install_feature_hooks();
    const auto result=discovery().project(project,engine,engine_version,data,device,version,info);
    log("ngx_init_project_result",static_cast<UINT>(result),static_cast<UINT>(engine),
        info?info->PathListInfo.Length:0);
    return result;
}
// The driver's NGX core, as the SDK finds it when nothing ships next to the
// game.
HMODULE load_ngx_core() {
    if(auto core=GetModuleHandleW(L"_nvngx.dll")) { log("ngx_core_already_loaded"); return core; }
    wchar_t directory[MAX_PATH]{}; DWORD size=sizeof(directory);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
        L"FullPath",RRF_RT_REG_SZ,nullptr,directory,&size)!=ERROR_SUCCESS)return nullptr;
    std::wstring path=directory; path+=L"\\_nvngx.dll";
    return LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
}
} // namespace
#ifdef XRFG_NATIVE_INGRESS_TESTS
#include "ngx_dispatch_test.inc"
#endif
void prepare_ngx_feature_discovery(const wchar_t* directory) noexcept {
    try {
        auto& d=discovery(); if(d.ext_hook||d.project_hook||!directory||!*directory)return;
        const HMODULE core=load_ngx_core(); if(!core)return;
        install_feature_hooks();start_hook_monitor();
        auto ext=GetProcAddress(core,"NVSDK_NGX_D3D12_Init_Ext");
        auto project=GetProcAddress(core,"NVSDK_NGX_D3D12_Init_ProjectID");
        if(!ext||!project)return;
        HMODULE pin{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&prepare_ngx_feature_discovery),&pin);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(ext),&pin);
        d.directory=directory;
        d.ext_hook=safetyhook::create_inline(ext,reinterpret_cast<void*>(&init_ext),
            SafetyHookInline::StartDisabled);
        d.project_hook=safetyhook::create_inline(project,reinterpret_cast<void*>(&init_project),
            SafetyHookInline::StartDisabled);
        if(!d.ext_hook||!d.project_hook){d.ext_hook={};d.project_hook={};log("ngx_discovery_hook_failed");return;}
        d.ext=d.ext_hook.original<InitExt>(); d.project=d.project_hook.original<InitProject>();
        (void)d.ext_hook.enable(); (void)d.project_hook.enable(); log("ngx_discovery_hooked");
    } catch(...) {}
}
void configure_ngx_guide_capture(ID3D12CommandQueue* queue,bool enabled) noexcept {
    try {
        ComPtr<ID3D12Device> luke_device;ComPtr<ID3D12CommandQueue> luke_queue;
        if(queue && native_ingress::query_luke_context(luke_device.GetAddressOf(),luke_queue.GetAddressOf()))
            queue=luke_queue.Get();
        if(queue)native_ingress::configure_queue(queue);
        auto& s=state(); {std::scoped_lock lock(s.mutex);
            if(s.queue.Get()!=queue){s.camera_valid=false;s.camera_from_xr=false;s.camera_ready=false;}
            s.queue=queue;}
        if(queue&&!s.debug_queue) {
            auto nq=native(queue);ComPtr<ID3D12Device> device;
            if(SUCCEEDED(nq->GetDevice(IID_PPV_ARGS(&device)))&&
               SUCCEEDED(device.As(&s.debug_queue))) {
                s.debug_queue->RegisterMessageCallback(
                    [](D3D12_MESSAGE_CATEGORY category,D3D12_MESSAGE_SEVERITY severity,
                       D3D12_MESSAGE_ID id,LPCSTR description,void*) {
                        if(severity<=D3D12_MESSAGE_SEVERITY_WARNING&&id!=1424)log(description,id,category,severity);
                    },D3D12_MESSAGE_CALLBACK_FLAG_NONE,nullptr,&s.debug_cookie);
                log("debug_messages_enabled");
            }
        }
        s.active=enabled&&queue;
        if(enabled&&queue)configure_dlss_motion_vector_tracking(true);
        // Observe SR/RR input availability even when optical flow needs no
        // owned guide copies. capture() still checks s.active before GPU work.
        if(!queue||s.started.exchange(true))return;
        HMODULE pin{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&configure_ngx_guide_capture),&pin);
        if(GetEnvironmentVariableW(L"XRFG_TEST_NATIVE_DLSSG_DRED",nullptr,0)) {
            auto nq=native(queue);ComPtr<ID3D12Device> device;
            if(SUCCEEDED(nq->GetDevice(IID_PPV_ARGS(&device)))) {
                std::thread([device] {
                    for(;;) {
                        const HRESULT reason=device->GetDeviceRemovedReason();
                        if(FAILED(reason)) {
                            log("device_removed_dred",static_cast<UINT>(reason));
                            ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
                            if(SUCCEEDED(device.As(&dred))) {
                                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 data{};
                                if(SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&data))) {
                                    for(auto* node=data.pHeadAutoBreadcrumbNode;node;node=node->pNext) {
                                        const auto done=node->pLastBreadcrumbValue?*node->pLastBreadcrumbValue:0;
                                        if(done<node->BreadcrumbCount)log(node->pCommandListDebugNameA?
                                            node->pCommandListDebugNameA:"dred_list",done,node->BreadcrumbCount,
                                            done<node->BreadcrumbCount?node->pCommandHistory[done]:0);
                                    }
                                }
                            }
                            return;
                        }
                        Sleep(1);
                    }
                }).detach();
            }
        }
        start_hook_monitor();log("enabled");
    } catch(...) {}
}
void stop_ngx_guide_capture(ID3D12CommandQueue* queue) noexcept {
    native_ingress::stop_queue(queue);
    auto& s=state(); std::scoped_lock lock(s.mutex);
    if(s.queue.Get()==queue) {s.active=false;s.queue.Reset();}
}
void update_ngx_guide_depth(float near_z,float far_z,float min_depth,float max_depth) noexcept {
    if(min_depth!=0.F||max_depth!=1.F||!std::isfinite(near_z)||!std::isfinite(far_z)||
        near_z<=0||far_z<=0||near_z==far_z)return;
    auto& s=state();std::scoped_lock lock(s.mutex);
    s.depth_inverted=near_z>far_z;
    s.camera_near=std::min(near_z,far_z);
    s.camera_far=std::max(near_z,far_z);
    s.depth_infinite=s.camera_far>1e20F;
    if(s.depth_infinite)s.camera_far=1000.F;
    s.camera_valid=true;
    s.camera_from_xr=true;
    s.camera_ready=true;
}
} // namespace xrfg
#else
namespace xrfg {
void configure_ngx_guide_capture(ID3D12CommandQueue*,bool) noexcept {}
void stop_ngx_guide_capture(ID3D12CommandQueue*) noexcept {}
void update_ngx_guide_depth(float,float,float,float) noexcept {}
void prepare_ngx_feature_discovery(const wchar_t*) noexcept {}
}
#endif
