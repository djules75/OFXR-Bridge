#include "xrfg/provider_api.hpp"

#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/embedded_control.hpp"

#include <algorithm>
#include <cstddef>

namespace {

bool valid_size(std::uint32_t actual, std::size_t expected) noexcept {
    return actual >= expected;
}


}  // namespace


extern "C" __declspec(dllexport) std::uint32_t
OFXR_GetProviderApiVersionV1() noexcept {
    return OFXR_PROVIDER_API_VERSION_V1;
}

extern "C" __declspec(dllexport) int OFXR_GetControlSnapshotV1(
    OFXR_ControlSnapshotV1* output) noexcept {
    try {
        if (output == nullptr ||
            !valid_size(output->struct_size, sizeof(OFXR_ControlSnapshotV1))) {
            return 0;
        }
        const auto source = xrfg::embedded::snapshot();
        OFXR_ControlSnapshotV1 value{};
        value.desired.enabled = source.desired.enabled ? 1 : 0;
        value.desired.backend = source.desired.backend;
        value.desired.preset = source.desired.preset;
        value.desired.scale = source.desired.scale;
        value.desired.backward = source.desired.backward ? 1 : 0;
        value.desired.motion_vectors = source.desired.motion_vectors;
        value.revision = source.revision;
        value.sessions = source.sessions;
        value.pending = source.pending;
        value.bypass = source.bypass;
        value.ready = source.ready;
        value.errors = source.errors;
        value.last_error = source.last_error;
        *output = value;
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_RequestControlV1(
    const OFXR_ControlSettingsV1* input) noexcept {
    try {
        if (input == nullptr ||
            !valid_size(input->struct_size, sizeof(OFXR_ControlSettingsV1))) {
            return 0;
        }
        auto settings = xrfg::embedded::snapshot().desired;
        settings.enabled = input->enabled != 0;
        settings.backend = input->backend;
        settings.preset = input->preset;
        settings.scale = input->scale;
        settings.backward = input->backward != 0;
        settings.motion_vectors = input->motion_vectors;
        return xrfg::embedded::request(settings) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_GetLoggingSettingV1() noexcept {
    try {
        return xrfg::embedded::logging_setting() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_SetLoggingSettingV1(
    int enabled) noexcept {
    try {
        return xrfg::embedded::set_logging_setting(enabled != 0) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_GetOverlaySettingV1() noexcept {
    try {
        return xrfg::embedded::overlay_setting();
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_SetOverlaySettingV1(
    int position) noexcept {
    try {
        return xrfg::embedded::set_overlay_setting(position) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) void OFXR_ConfigureDlssMotionVectorsV1(
    int enabled) noexcept {
    xrfg::configure_dlss_motion_vector_tracking(enabled != 0);
}

extern "C" __declspec(dllexport) int OFXR_PublishDlssMotionVectorsV1(
    const OFXR_DlssMotionVectorPublicationV1* input) noexcept {
    try {
        if (input == nullptr ||
            input->api_version != OFXR_PROVIDER_API_VERSION_V1 ||
            !valid_size(
                input->struct_size,
                sizeof(OFXR_DlssMotionVectorPublicationV1))) {
            return 0;
        }
        xrfg::DlssMotionVectorPublication pub{};
        pub.stream = input->stream;
        pub.output = input->output;
        pub.motion_vectors = input->motion_vectors;
        pub.producer_queue = input->producer_queue;
        pub.producer_command_list = input->producer_command_list;
        pub.output_x = input->output_x;
        pub.output_y = input->output_y;
        pub.output_width = input->output_width;
        pub.output_height = input->output_height;
        pub.motion_x = input->motion_x;
        pub.motion_y = input->motion_y;
        pub.motion_width = input->motion_width;
        pub.motion_height = input->motion_height;
        pub.scale_x = input->scale_x;
        pub.scale_y = input->scale_y;
        pub.jitter_x = input->jitter_x;
        pub.jitter_y = input->jitter_y;
        pub.resource_state = static_cast<D3D12_RESOURCE_STATES>(input->resource_state);
        pub.jittered = input->jittered != 0;
        pub.reset = input->reset != 0;
        return xrfg::publish_dlss_motion_vectors(pub) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) void OFXR_RetireDlssMotionVectorStreamV1(
    std::uint64_t stream) noexcept {
    xrfg::retire_dlss_motion_vector_stream(stream);
}

extern "C" __declspec(dllexport) int OFXR_GetDlssMotionVectorStatisticsV1(
    OFXR_DlssMotionVectorStatisticsV1* output) noexcept {
    try {
        if (output == nullptr ||
            !valid_size(
                output->struct_size,
                sizeof(OFXR_DlssMotionVectorStatisticsV1))) {
            return 0;
        }
        const auto source = xrfg::dlss_motion_vector_statistics();
        OFXR_DlssMotionVectorStatisticsV1 value{};
        value.published = source.published;
        value.invalid_publications = source.invalid_publications;
        value.snapshot_copies = source.snapshot_copies;
        value.snapshot_failures = source.snapshot_failures;
        value.resolve_calls = source.resolve_calls;
        value.resolve_missing_streams = source.resolve_missing_streams;
        value.resolve_stale_pairs = source.resolve_stale_pairs;
        value.resolve_queue_mismatches = source.resolve_queue_mismatches;
        value.matched = source.matched;
        value.temporal_rejections = source.temporal_rejections;
        value.invalid_rejections = source.invalid_rejections;
        value.used = source.used;
        value.last_used_publication = source.last_used_publication;
        value.status = static_cast<std::uint32_t>(source.status);
        *output = value;
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) std::uint32_t
OFXR_GetProviderApiVersionV2() noexcept {
    return OFXR_PROVIDER_API_VERSION_V2;
}





extern "C" __declspec(dllexport) int OFXR_GetControlSnapshotV2(
    OFXR_ControlSnapshotV2* output) noexcept {
    try {
        if (output == nullptr ||
            !valid_size(output->struct_size, sizeof(OFXR_ControlSnapshotV2))) {
            return 0;
        }
        const auto source = xrfg::embedded::snapshot();
        OFXR_ControlSnapshotV2 value{};
        value.desired.enabled = source.desired.enabled ? 1 : 0;
        value.desired.backend = source.desired.backend;
        value.desired.preset = source.desired.preset;
        value.desired.scale = source.desired.scale;
        value.desired.backward = source.desired.backward ? 1 : 0;
        value.desired.motion_vectors = source.desired.motion_vectors;
        value.desired.frame_generation = source.desired.frame_generation;
        value.revision = source.revision;
        value.sessions = source.sessions;
        value.pending = source.pending;
        value.bypass = source.bypass;
        value.ready = source.ready;
        value.errors = source.errors;
        value.last_error = source.last_error;
        value.providers = 0;
        *output = value;
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_RequestControlV2(
    const OFXR_ControlSettingsV2* input) noexcept {
    try {
        if (input == nullptr ||
            !valid_size(input->struct_size, sizeof(OFXR_ControlSettingsV2))) {
            return 0;
        }
        auto settings = xrfg::embedded::snapshot().desired;
        settings.enabled = input->enabled != 0;
        settings.backend = input->backend;
        settings.preset = input->preset;
        settings.scale = input->scale;
        settings.backward = input->backward != 0;
        settings.motion_vectors = input->motion_vectors;
        settings.frame_generation = input->frame_generation;
        return xrfg::embedded::request(settings) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

// Native DLSS Frame Generation's resolution, in percent of each eye's. Kept
// apart from OFXR_ControlSettingsV2, whose size callers already compile in.
extern "C" __declspec(dllexport) int OFXR_GetNativeDlssgScaleV2() noexcept {
    try {
        return xrfg::embedded::snapshot().desired.native_scale;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) int OFXR_RequestNativeDlssgScaleV2(int percent) noexcept {
    try {
        auto settings = xrfg::embedded::snapshot().desired;
        settings.native_scale = percent;
        return xrfg::embedded::request(settings) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) void OFXR_ConfigureDlssGuidesV2(
    int enabled) noexcept {
    xrfg::configure_dlss_motion_vector_tracking(enabled != 0);
}

extern "C" __declspec(dllexport) int OFXR_PublishDlssGuidesV2(
    const OFXR_DlssGuidePublicationV2* input) noexcept {
    try {
        if (!input || input->api_version != OFXR_PROVIDER_API_VERSION_V2 ||
            !valid_size(input->struct_size, sizeof(*input))) return 0;
        xrfg::DlssMotionVectorPublication pub{};
                pub.stream = input->stream;
        pub.output = input->output;
        pub.motion_vectors = input->motion_vectors;
        pub.producer_queue = input->producer_queue;
        pub.producer_command_list = input->producer_command_list;
        pub.output_x = input->output_x;
        pub.output_y = input->output_y;
        pub.output_width = input->output_width;
        pub.output_height = input->output_height;
        pub.motion_x = input->motion_x;
        pub.motion_y = input->motion_y;
        pub.motion_width = input->motion_width;
        pub.motion_height = input->motion_height;
        pub.scale_x = input->scale_x;
        pub.scale_y = input->scale_y;
        pub.jitter_x = input->jitter_x;
        pub.jitter_y = input->jitter_y;
        pub.resource_state = static_cast<D3D12_RESOURCE_STATES>(input->resource_state);
        pub.jittered = input->jittered != 0;
        pub.reset = input->reset != 0;
        pub.depth = input->depth;
        pub.depth_x = input->depth_x;
        pub.depth_y = input->depth_y;
        pub.depth_width = input->depth_width;
        pub.depth_height = input->depth_height;
        pub.depth_resource_state = static_cast<D3D12_RESOURCE_STATES>(input->depth_resource_state);
        pub.frame_time_delta_ms = input->frame_time_delta_ms;
        pub.camera_near = input->camera_near;
        pub.camera_far = input->camera_far;
        pub.depth_inverted = input->depth_inverted != 0;
        pub.depth_infinite = input->depth_infinite != 0;
        pub.verified_producer_device = input->verified_native_device;
        return xrfg::publish_dlss_motion_vectors(pub) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" __declspec(dllexport) void OFXR_RetireDlssGuideStreamV2(
    std::uint64_t stream) noexcept {
    xrfg::retire_dlss_motion_vector_stream(stream);
}

extern "C" __declspec(dllexport) int OFXR_GetDlssGuideStatisticsV2(
    OFXR_DlssGuideStatisticsV2* output) noexcept {
    try {
        if (output == nullptr ||
            !valid_size(output->struct_size,
                        sizeof(OFXR_DlssGuideStatisticsV2))) {
            return 0;
        }
        const auto vectors = xrfg::dlss_motion_vector_statistics();
        OFXR_DlssGuideStatisticsV2 value{};
        value.published = vectors.published;
        value.depth_publications = vectors.depth_publications;
        value.invalid_publications = vectors.invalid_publications;
        value.snapshot_copies = vectors.snapshot_copies;
        value.snapshot_failures = vectors.snapshot_failures;
        value.resolve_calls = vectors.resolve_calls;
        value.resolve_missing_streams = vectors.resolve_missing_streams;
        value.resolve_stale_pairs = vectors.resolve_stale_pairs;
        value.resolve_queue_mismatches = vectors.resolve_queue_mismatches;
        value.matched = vectors.matched;
        value.temporal_rejections = vectors.temporal_rejections;
        value.invalid_rejections = vectors.invalid_rejections;
        value.used = vectors.used;
        value.last_used_publication = vectors.last_used_publication;
        value.status = static_cast<std::uint32_t>(vectors.status);
        *output = value;
        return 1;
    } catch (...) {
        return 0;
    }
}
