#pragma once
#include "xrfg/autonomous_control.hpp"
#include "xrfg/embedded_control.hpp"
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace xrfg_fixture {
inline bool request_control(const xrfg::embedded::Settings& desired) {
    auto s=xrfg::autonomous::snapshot().desired;
    s.enabled=desired.enabled; s.backend=desired.backend; s.preset=desired.preset;
    s.scale=desired.scale; s.backward=desired.backward; s.lower_vram=desired.lower_vram;
    s.motion_vectors=desired.motion_vectors; s.frame_generation=desired.frame_generation;
    s.native_scale=static_cast<int>(desired.native_scale);
    const auto pipeline=xrfg::embedded::djules_pipeline_snapshot();
    s.prefer_fps=pipeline.prefer_fps; s.triple_frame_gen=pipeline.triple_frame_gen;
    if (!xrfg::autonomous::request(s)) return false;
    xrfg::autonomous::apply_runtime_settings();
    auto effective=desired;
    effective.motion_vectors=xrfg::autonomous::snapshot().desired.motion_vectors;
    return xrfg::embedded::snapshot().desired==effective;
}
class ControlProfile {
    std::filesystem::path directory_;
public:
    ControlProfile() {
        directory_=std::filesystem::temp_directory_path()/
            (L"ofxr-autonomous-layer-test-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory_);
        SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",directory_.c_str());
        if (!request_control(xrfg::embedded::snapshot().desired))
            throw std::runtime_error("autonomous fixture publication failed");
    }
    ~ControlProfile() {
        std::error_code error;
        std::filesystem::remove(directory_/L"autonomous.ini",error);
        std::filesystem::remove(directory_,error);
    }
};
}
