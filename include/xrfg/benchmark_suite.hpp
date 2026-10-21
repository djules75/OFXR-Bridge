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
    int pacing{-1}; // -1 preserves the pre-V614 runtime's global choice.
};
inline std::vector<BenchmarkStep> legacy_benchmark_plan(bool full,bool luke=false) {
    std::vector<BenchmarkStep> steps;
    for(const int quality : (luke ? std::vector<int>{0,4,8} : std::vector<int>{-1})) {
        BenchmarkStep off{0,0,0,0,false,false,true,luke?"AFW only":"Disabled"};
        off.pacing=0;if(luke){off.afw=3;off.shading=quality;}steps.push_back(off);
        const auto route=[&](const char* name,int generation,int backend,bool triple,bool fast) {
            BenchmarkStep s{generation,backend,luke?0:int(fast),luke?0:int(fast),triple,triple,false,
                std::string(name)+(luke?" 00":fast?" Lower VRAM":" Normal")};
            s.pacing=0;if(luke){s.afw=3;s.shading=quality;}steps.push_back(std::move(s));
        };
        route("Fidelity",0,0,false,true);route("0.2.1",0,1,false,true);route("Djules 2X",0,3,false,true);
        if(!luke){route("Djules 3X",0,3,true,false);route("Djules 3X",0,3,true,true);}
        route("0.3.0",0,2,false,true);
        if(full&&!luke)for(auto [name,mode]:{std::pair{"Hybrid",3},std::pair{"FSR",1},std::pair{"DLSS-G",2}}) {
            route(name,mode,0,false,false);route(name,mode,0,false,true);
        }
    }
    return steps;
}
inline const char* benchmark_pacing_label(int mode) {
    return mode==0 ? "Framework" : mode==1 ? "Director" : "Framework+director";
}
inline std::vector<BenchmarkStep> benchmark_plan(bool full,bool luke=false,bool steamvr=true) {
    // The session-latched transport selects the new matrix or Legacy VDXR plan.
    if(!steamvr)return legacy_benchmark_plan(full,luke);
    std::vector<BenchmarkStep> steps;
    const auto route=[&](const char* name,int generation,int backend,bool triple,int quality) {
        for(int pacing=0;pacing<3;++pacing) {
            for(int fast=0;fast<(luke ? 1 : 2);++fast) {
                BenchmarkStep step{generation,backend,fast,fast,triple,triple,false,
                    std::string(name)+" "+benchmark_pacing_label(pacing)+(luke ? "" : fast ? " Fast" : " Normal")};
                step.pacing=pacing;
                if(luke) {step.afw=3;step.shading=quality;}
                steps.push_back(std::move(step));
            }
        }
    };
    for(const int quality : (luke ? std::vector<int>{0,4,8} : std::vector<int>{-1})) {
        BenchmarkStep off{0,0,0,0,false,false,true,luke ? "AFW only" : "Disabled"};
        off.pacing=0;
        if(luke) {off.afw=3;off.shading=quality;}
        steps.push_back(std::move(off));
        route("Fidelity",0,0,false,quality);route("0.2.1",0,1,false,quality);
        route("Djules 2X",0,3,false,quality);
        if(!luke)route("Djules 3X",0,3,true,quality);
        route("0.3.0",0,2,false,quality);
        if(full && !luke) {route("Hybrid",3,0,false,quality);route("FSR",1,0,false,quality);route("DLSS-G",2,0,false,quality);}
    }
    return steps;
}
inline std::vector<BenchmarkStep> benchmark_export_plan(bool full,bool luke,bool steamvr) {
    auto steps=benchmark_plan(full,luke,steamvr);
    if(luke) {
        // Execution minimizes AFW resets with whole-quality passes. Export groups
        // every route/pacing's three qualities without relying on a fixed route count.
        const auto stride=steps.size()/3;
        std::vector<BenchmarkStep> ordered;
        for(std::size_t trial=0;trial<stride;++trial)
            for(std::size_t quality=0;quality<3;++quality)ordered.push_back(steps[quality*stride+trial]);
        return ordered;
    }
    return steps;
}
inline Settings benchmark_settings(Settings saved,const BenchmarkStep& step) {
    select_generation(saved,step.generation,step.backend);
    saved.enabled=!step.disabled;
    saved.motion_vectors=step.generation ? 1 : 0;
    saved.separate_synthesis=step.a;saved.lower_vram=step.b;
    saved.triple_frame_gen=step.triple;saved.prefer_fps=step.prefer;
    if(step.pacing>=0)saved.pacing_mode=step.pacing;
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
            m.output_source==InterfaceSource::accepted_scene_submissions ||
            m.output_source==InterfaceSource::runtime_accepted_delivery) && m.rates_valid &&
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
