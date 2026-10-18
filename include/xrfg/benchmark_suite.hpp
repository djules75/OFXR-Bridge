#pragma once
#include "xrfg/autonomous_control.hpp"
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

namespace xrfg::autonomous {
struct BenchmarkStep {
    int generation{}, backend{}, a{}, b{};
    bool triple{}, prefer{}, disabled{};
    std::string label;
    int afw{-1},shading{-1};
};
inline std::vector<BenchmarkStep> benchmark_plan(bool full,bool luke=false) {
    if(luke) {
        std::vector<BenchmarkStep> result;
        for(const int quality:{0,4,8}) {
            for(auto step:benchmark_plan(false)) {
                if(step.a || step.b || (step.prefer && !step.triple))continue;
                step.afw=3;step.shading=quality;
                if(step.disabled)step.label="AFW only";
                else if(step.label.ends_with(" Normal"))step.label.replace(step.label.size()-7,7," 00");
                result.push_back(std::move(step));
            }
        }
        return result;
    }
    std::vector<BenchmarkStep> steps{{0,0,0,0,false,false,true,"Disabled"}};
    const auto route=[&](const char* name,int generation,int backend,bool triple=false) {
        // Normal and combined Lower VRAM only. Both retain their requested multiplier.
        for (const auto flags : {0,3}) {
            const int a=flags&1,b=(flags>>1)&1;
            steps.push_back({generation,backend,a,b,triple,triple,false,
                std::string(name)+(flags ? " Lower VRAM" : " Normal")});
        }
    };
    route("Fidelity",0,0);route("0.2.1",0,1);route("Djules 2X",0,3);
    route("Djules 3X",0,3,true);route("0.3.0",0,2);
    if(full) {route("Hybrid",3,0);route("FSR",1,0);route("DLSS-G",2,0);}
    return steps;
}
inline Settings benchmark_settings(Settings saved,const BenchmarkStep& step) {
    select_generation(saved,step.generation,step.backend);
    saved.enabled=!step.disabled;
    saved.motion_vectors=step.generation ? 1 : 0;
    saved.separate_synthesis=step.a;saved.lower_vram=step.b;
    saved.triple_frame_gen=step.triple;saved.prefer_fps=step.prefer;
    if(step.afw>=0) {saved.afw_mode=step.afw;saved.afw_shading_rate=step.shading;}
    return sanitize(saved);
}
inline double benchmark_median(std::vector<double> values) {
    if(values.empty()) return 0;
    std::sort(values.begin(),values.end());
    const auto middle=values.size()/2;
    return values.size()%2 ? values[middle] : (values[middle-1]+values[middle])/2;
}
struct BenchmarkSamples {
    std::vector<double> fps,latency;
    std::uint64_t last_ms{};
    bool generated{};
    void observe(const InterfaceTelemetry& m,std::uint64_t started,std::uint64_t end) {
        if(!m.updated_ms || m.updated_ms==last_ms || m.updated_ms<started+3000 || m.updated_ms>=end) return;
        last_ms=m.updated_ms;
        if((m.output_source==InterfaceSource::steamvr_delivery ||
            m.output_source==InterfaceSource::steamvr_scene_delivery ||
            m.output_source==InterfaceSource::accepted_scene_submissions) && m.rates_valid &&
            std::isfinite(m.output_fps) && m.output_fps>=0) fps.push_back(m.output_fps);
        if(m.cycle_estimated_valid && std::isfinite(m.cycle_estimated_ms) && m.cycle_estimated_ms>=0)
            latency.push_back(m.cycle_estimated_ms);
        generated |= m.generating;
    }
};
// UI commands; the tray/desktop timer drives the suite while menus are hidden.
bool start_benchmark(bool full,bool luke=false) noexcept;
void stop_benchmark() noexcept;
void tick_benchmark(std::uint64_t now_ms=0) noexcept;
} // namespace xrfg::autonomous
