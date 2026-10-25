#pragma once
#include <cstdint>
// V633 owner F8 sequence and original padded readback allocation sizes.
// Several second-evaluation images failed only after earlier NGX copies
// exhausted 1280 MiB. All AFW inputs/outputs and XR terminals must fit.
namespace horizon_capture_budget {
struct Shot { const char* stage; std::uint64_t bytes; };
inline constexpr Shot shots[]{
    {"ngx_native_motion",45371200},
    {"ngx_native_depth",46227008},
    {"xr_final_eye",41091036},
    {"ngx_input_color",45371200},
    {"ngx_completed_color",45371200},
    {"afw_ngx_mv_before_crop",45371200},
    {"afw_ngx_depth_before_crop",46227008},
    {"afw_source_color",41091036},
    {"ngx_native_motion",45371200},
    {"afw_prior_source_eye_color",41091036},
    {"ngx_native_depth",46227008},
    {"afw_prior_peer_eye_color",41091036},
    {"ngx_input_color",45371200},
    {"afw_plugin_input_color",41091036},
    {"ngx_completed_color",45371200},
    {"afw_plugin_input_depth",10272752},
    {"afw_plugin_input_mv",10272752},
    {"afw_depth_remap_calibration",272},
    {"afw_plugin_returned_eye",41091036},
    {"afw_generated_eye",41091036},
    {"ngx_native_motion",45371200},
    {"afw_xr_terminal_eye",41091036},
    {"ngx_native_depth",46227008},
    {"afw_xr_terminal_eye",41091036},
    {"ngx_input_color",45371200},
    {"xr_final_eye",41091036},
    {"ngx_completed_color",45371200},
    {"ngx_native_motion",45371200},
    {"ngx_native_depth",46227008},
    {"ngx_input_color",45371200},
    {"ngx_completed_color",45371200},
    {"afw_ngx_mv_before_crop",45371200},
    {"afw_ngx_depth_before_crop",46227008},
    {"afw_source_color",41091036},
    {"afw_prior_source_eye_color",41091036},
    {"afw_prior_peer_eye_color",41091036},
    {"afw_plugin_input_color",41091036},
    {"afw_plugin_input_depth",10272752},
    {"afw_plugin_input_mv",10272752},
    {"afw_depth_remap_calibration",272},
    {"afw_plugin_returned_eye",41091036},
    {"afw_generated_eye",41091036},
    {"afw_xr_terminal_eye",41091036},
    {"afw_xr_terminal_eye",41091036},
};
}
