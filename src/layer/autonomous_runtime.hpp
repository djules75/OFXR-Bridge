#pragma once
#include "xrfg/autonomous_control.hpp"
#include "xrfg/embedded_control.hpp"
#include "xrfg/neural_rendering.hpp"
#include <mutex>
#include <cstdio>
#include <windows.h>
#include <atomic>
#include "xrfg/native_ingress.hpp"
#include "xrfg/upscaler_observation.hpp"
#include "xrfg/fsr_frame_generation.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/diagnostic_hotkeys.hpp"

namespace xrfg::autonomous {
inline std::atomic_uint64_t applied_runtime_revision{};
inline std::atomic_bool r19_camera_enabled{};
inline void poll_diagnostic_logging_key() {
    static DiagnosticKeyEdge logging;
    if (!logging.poll(kLoggingKey)) return;
    const auto base = snapshot().desired;
    auto edited = base; edited.diagnostics = !base.diagnostics;
    request_changes(base, edited);
}
inline void apply_runtime_settings() {
    static std::mutex mutex;
    static std::uint64_t applied{};
    std::scoped_lock lock(mutex);
    const auto incoming=snapshot();
    if (applied==incoming.revision) return;
    const auto& s=incoming.desired;
    embedded::Settings e{};
    e.enabled=s.enabled; e.backend=s.backend; e.preset=s.preset; e.scale=s.scale;
    e.backward=s.backward; e.lower_vram=s.lower_vram;
    e.motion_vectors=s.motion_vectors; e.frame_generation=s.frame_generation==3 ? 0 : s.frame_generation;
    e.hybrid=s.frame_generation==3;
    if(e.hybrid) e.backend=0; // Fixed FidelityFX fallback, independent of OFXR profile.
    e.native_scale=static_cast<unsigned>(s.native_scale);
    if (!embedded::request(e)) return;
    embedded::AfwSettings a{};
    a.mode=s.afw_mode; a.debug=s.afw_debug;
    a.clear_before_warping=s.afw_clear_before_warping;
    a.ignore_motion_threshold=s.afw_ignore_motion_threshold;
    a.depth_dilate_mode=s.afw_depth_dilate_mode;
    if (!embedded::request_afw(a)) return;
    if (!embedded::set_djules_triple_frame_gen(s.triple_frame_gen)) return;
    if (!embedded::set_djules_prefer_fps(s.prefer_fps || s.triple_frame_gen)) return;
    if (!embedded::set_overlay_setting(s.overlay_position)) return;
    if (!embedded::set_logging_setting(s.diagnostics)) return;
    neural_rendering::Settings n{};
    n.enabled=s.nr_enabled; n.before_sr=s.nr_before_sr;
    n.intensity=s.nr_intensity; n.preset=s.nr_preset; n.style=s.nr_style;
    n.local_structure=s.nr_local_structure; n.local_tone=s.nr_local_tone;
    n.skin_structure=s.nr_skin_structure; n.auto_mask=s.nr_auto_mask;
    n.transfer_strength=s.nr_transfer_strength; n.colour_strength=s.nr_colour_strength;
    n.max_ratio=s.nr_max_ratio; n.working_scale=s.nr_working_scale;
    n.white_point_scale=s.nr_white_point_scale; n.diagnostic_logging=s.diagnostics;
    n.debug_view=s.nr_debug_view; n.compare=s.nr_compare;
    n.compare_split=s.nr_compare_split; n.compare_zoom=s.nr_compare_zoom;
    n.compare_swap=s.nr_compare_swap;
    neural_rendering::configure(n);
    r19_camera_enabled.store(s.r19_camera,std::memory_order_release);
    applied=incoming.revision;
    applied_runtime_revision.store(applied,std::memory_order_release);
}
inline void publish_runtime_status(unsigned native_generated_frames=0,
    GenerationState generation=GenerationState::waiting, HRESULT initialization_result=S_OK,
    bool vulkan_detected=false, std::uint32_t provider_error=0) {
    const auto control=embedded::snapshot();
    const auto afw=embedded::afw_snapshot();
    const auto nr=neural_rendering::status();
    RuntimeStatus out{};
    out.process_id=GetCurrentProcessId();
    out.applied_revision=applied_runtime_revision.load(std::memory_order_acquire);
    out.sessions=control.sessions; out.ready=control.ready;
    out.pending=control.pending; out.errors=control.errors;
    out.afw_projection_frames=afw.projection_frames; out.afw_ready_frames=afw.ready_frames;
    out.nr_running=nr.running;
    out.logging_active=bridge_flight_logger().enabled();
    out.vulkan_capture_suspended=vulkan_detected;
    out.nr_evaluations=nr.evaluations;
    if (nr.enabled && !nr.reason.empty()) snprintf(out.message,sizeof(out.message),"%s",nr.reason.c_str());
    const auto ingress=native_ingress::statistics();
    out.luke_detected = ingress.luke != 0;
    out.vulkan_detected = vulkan_detected;
    out.provider_error = control.desired.frame_generation == 2 ? provider_error : 0;
    if (ingress.luke && ingress.camera_rejected && !ingress.camera_accepted)
        snprintf(out.message,sizeof(out.message),"Luke: awaiting an exact native camera packet");
    const auto desired=snapshot().desired;
    const auto observed=native_ingress::upscaler_observation();
    const auto guides=dlss_motion_vector_statistics();
    out.upscaler_evaluations=observed.evaluations;
    out.upscaler_motion_inputs=observed.motion_inputs; out.upscaler_depth_inputs=observed.depth_inputs;
    out.upscaler_last_seen_ms=observed.last_seen_ms;
    out.upscaler_motion=observed.motion; out.upscaler_depth=observed.depth;
    out.guide_publications=guides.published; out.guide_matches=guides.matched; out.guide_uses=guides.used;
    out.generation_mode=desired.frame_generation; out.motion_source=control.desired.motion_vectors;
    if (!control.desired.enabled) generation=GenerationState::paused;
    if ((generation==GenerationState::ready || generation==GenerationState::waiting) &&
        (out.motion_source==1 || out.generation_mode!=0) &&
        !(out.generation_mode==2 && generation==GenerationState::ready)) {
        switch(guides.status) {
        case DlssMotionVectorStatus::waiting_for_dlss: generation=GenerationState::missing_motion; break;
        case DlssMotionVectorStatus::waiting_for_depth: generation=GenerationState::missing_depth; break;
        case DlssMotionVectorStatus::invalid_input: generation=GenerationState::invalid_guides; break;
        case DlssMotionVectorStatus::temporal_mismatch: generation=GenerationState::temporal_mismatch; break;
        case DlssMotionVectorStatus::output_not_direct: generation=GenerationState::output_mismatch; break;
        case DlssMotionVectorStatus::queue_mismatch: generation=GenerationState::queue_mismatch; break;
        case DlssMotionVectorStatus::native_unavailable: if(out.generation_mode==2) generation=GenerationState::unavailable; break;
        case DlssMotionVectorStatus::native_evaluate_failed: if(out.generation_mode==2) generation=GenerationState::dispatch_failed; break;
        case DlssMotionVectorStatus::native_resizing: if(out.generation_mode==2) generation=GenerationState::recovering; break;
        case DlssMotionVectorStatus::multi_frame_unsupported: generation=GenerationState::multi_frame_unsupported; break;
        default: break;
        }
        if(out.generation_mode==1) switch(fsr_frame_generation_statistics().status) {
        case FsrFrameGenerationStatus::runtime_missing: generation=GenerationState::runtime_missing; break;
        case FsrFrameGenerationStatus::initialization_failed: generation=GenerationState::initialization_failed; break;
        case FsrFrameGenerationStatus::dispatch_failed: generation=GenerationState::dispatch_failed; break;
        case FsrFrameGenerationStatus::waiting_for_guides: generation=GenerationState::temporal_mismatch; break;
        case FsrFrameGenerationStatus::stereo_unavailable: generation=GenerationState::invalid_stereo; break;
        case FsrFrameGenerationStatus::stereo_recovering: generation=GenerationState::recovering; break;
        default: break;
        }
    }
    if (out.generation_mode==2 && native_generated_frames==0 &&
        generation!=GenerationState::missing_motion && generation!=GenerationState::missing_depth &&
        generation!=GenerationState::paused && generation!=GenerationState::invalid_stereo)
        generation=GenerationState::unavailable;
    if (generation==GenerationState::ready && out.generation_mode==2 && desired.triple_frame_gen && native_generated_frames<2)
        generation=GenerationState::multi_frame_unsupported;
    if (control.errors) generation=GenerationState::initialization_failed;
    const HRESULT failure=control.errors ? static_cast<HRESULT>(control.last_error) : initialization_result;
    if (control.desired.enabled && FAILED(failure)) {
        generation=out.generation_mode==2 && failure==DXGI_ERROR_UNSUPPORTED
            ? GenerationState::unavailable : GenerationState::initialization_failed;
        snprintf(out.message,sizeof(out.message),"%s initialization failed (0x%08lX). Real-frame bypass; select another generator to retry.",
            out.generation_mode==2 ? "DLSS-G" : out.generation_mode==1 ? "FSR" : "OFXR",static_cast<unsigned long>(failure));
    }
    out.generation_state=generation;
    report_runtime(out);
}
}
