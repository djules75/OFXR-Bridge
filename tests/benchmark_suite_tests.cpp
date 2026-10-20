#include "xrfg/benchmark_suite.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace xrfg::autonomous;
void check(bool condition,const char* text) {if(!condition)throw std::runtime_error(text);}
int main() {
    const auto folder=std::filesystem::temp_directory_path()/(L"ofxr-suite-"+std::to_wstring(GetCurrentProcessId()));
    SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",folder.c_str());
    try {
        const auto optical=benchmark_plan(false),full=benchmark_plan(true);
        check(optical.size()==11 && full.size()==17,"Normal and combined Lower VRAM only; no standalone Prefer FPS test");
        for(const auto& step:optical)check(step.generation==0,"Optical suite must never request vector-only generators");
        unsigned dlssg_steps=0,prefer=0;
        Settings baseline;baseline.diagnostics=false;baseline.nr_enabled=true;baseline.afw_mode=2;baseline.single_director=true;
        for(const auto& step:full) {
            auto s=benchmark_settings(baseline,step);
            check(!s.diagnostics && s.nr_enabled && s.afw_mode==2,"Keep diagnostics, Neural and AFW choices");
            check(s.single_director,"automatic route/Lower VRAM changes keep the global director choice");
            check(!s.triple_frame_gen || s.separate_synthesis || !s.lower_vram,"Never silently test 2X as 3X");
            if(step.generation==2){
                ++dlssg_steps;
                check(step.label.starts_with("DLSS-G ") && !step.label.starts_with("DLSS-G C"),"Route labels must not invent a preset");
                for(int prior=0;prior<4;++prior) {
                    auto manual=baseline;manual.backend=prior;
                    select_generation(manual,2,prior);
                    manual.lower_vram=manual.separate_synthesis=step.b?1:0;
                    manual=sanitize(manual);
                    check(manual.backend==s.backend && manual.frame_generation==s.frame_generation &&
                        manual.lower_vram==s.lower_vram && manual.native_scale==s.native_scale,
                        "manual DLSS-G and benchmark select identical presenter, memory variant and native resolution");
                }
            }
            if(step.prefer && !step.triple)++prefer;
        }
        check(dlssg_steps==2 && prefer==0,"Two DLSS-G memory variants and no Prefer FPS trial");
        BenchmarkSamples samples;xrfg::InterfaceTelemetry m{};
        m.rates_valid=true;m.output_source=xrfg::InterfaceSource::steamvr_delivery;
        m.cycle_estimated_valid=true;m.generating=true;
        for(const auto time:{3999ULL,4000ULL,5000ULL,6000ULL,10999ULL,11000ULL}) {
            m.updated_ms=time;m.output_fps=time==3999 ? 999.F : time==4000 ? 40.F : time==5000 ? 60.F : time==6000 ? 80.F : 100.F;
            m.cycle_estimated_ms=m.output_fps/2;
            samples.observe(m,1000,11000);
        }
        check(samples.fps.size()==4 && benchmark_median(samples.fps)==70 && benchmark_median(samples.latency)==35,"Discard leading 3s only; include final samples and correct even median");
        m.updated_ms=10999;samples.observe(m,1000,11000);check(samples.fps.size()==4,"Repeated telemetry cannot overweight the median");
        BenchmarkSamples unknown;m.updated_ms=5000;m.output_source=xrfg::InterfaceSource::accepted_submissions;
        unknown.observe(m,1000,11000);check(unknown.fps.empty(),"Submitted rate is never a presentation measurement");
        m.output_source=xrfg::InterfaceSource::steamvr_scene_delivery;
        ++m.updated_ms;
        unknown.observe(m,1000,11000);check(unknown.fps.size()==1,"Measured distinct Luke scene FPS enters benchmark");
        ++m.updated_ms;m.output_source=xrfg::InterfaceSource::accepted_scene_submissions;
        unknown.observe(m,1000,11000);check(unknown.fps.size()==2,"Distinct accepted Luke total enters benchmark with explicit source");
        baseline.benchmark_csv=true;check(request(baseline),"Save isolated test profile");baseline=snapshot().desired;
        RuntimeStatus runtime{};runtime.sessions=1;runtime.dlssg_preset_request=1;
        runtime.applied_revision=snapshot().revision;report_runtime(runtime);
        check(!benchmark_enabled() && !start_benchmark(true),"INI flag defaults off and blocks suite commands");
        check(WritePrivateProfileStringW(L"benchmark",L"enabled",L"1",configuration_path().c_str()),"enable isolated benchmarks in the single INI");
        check(start_benchmark(true),"Start full suite with A without changing profile or requesting restart");
        auto state=snapshot();check(!start_benchmark(false),"A second UI cannot start a competing suite");
        const auto first_requested=state.benchmark.requested_ms;
        tick_benchmark(first_requested+250);
        check(snapshot().benchmark.phase==BenchmarkPhase::applying,"Do not start the ten-second clock before settings apply");
        runtime.applied_revision=state.revision;report_runtime(runtime);
        auto clock=first_requested+500;
        for(std::size_t i=0;i<full.size();++i) {
            state=snapshot();runtime.applied_revision=state.revision;report_runtime(runtime);
            tick_benchmark(clock);state=snapshot();
            check(state.benchmark.phase==BenchmarkPhase::running && state.benchmark.index==i,"Advance after actual application without an open menu");
            check(interface_telemetry_requested(),"Hidden suite requests metrics even with diagnostics off");
            tick_benchmark(clock+9999);
            const auto request_revision=take_benchmark_memory_request();
            check(request_revision==state.benchmark.revision,"End-of-test memory request targets this applied revision");
            check(!take_benchmark_memory_request(),"Memory request is claimed only once");
            BenchmarkMemory memory{};memory.revision=request_revision;memory.updated_ms=clock+9999;
            memory.vram_valid=true;memory.vram_bytes=1048576ULL*(i+100);
            memory.ram_valid=true;memory.ram_working_set_bytes=2097152;
            report_benchmark_memory(memory);
            check(snapshot().benchmark.index==i,"Never shorten a configuration below ten seconds");
            tick_benchmark(clock+10000);clock+=10200;
        }
        state=snapshot();check(state.benchmark.phase==BenchmarkPhase::complete && state.desired==baseline,"Completion restores every original setting/profile");
        std::ifstream csv(std::filesystem::u8path(state.benchmark.csv_path));std::stringstream text;text<<csv.rdbuf();
        check(text.str().find("Output FPS median")!=std::string::npos && text.str().find("Cycle estimated median ms")!=std::string::npos,"CSV has both medians");
        const auto csv_text=text.str();
        const auto details_path=std::filesystem::u8path(state.benchmark.csv_path);
        std::ifstream details(details_path.parent_path()/(details_path.stem().wstring()+L"-details.csv"));
        std::stringstream details_text;details_text<<details.rdbuf();
        check(details_text.str().find("Status,SingleDirector\n")!=std::string::npos,
            "details CSV identifies the global experiment without changing the sheet layout");
        check(details_text.str().find(",A startup request,")!=std::string::npos &&
            details_text.str().find("C startup request")==std::string::npos,"CSV records A receipt instead of inventing C");
        check(csv_text.find("VRAM end MiB,100.000,101.000")!=std::string::npos,"Memory belongs to the trial that just finished, before the next switch");
        check(!take_benchmark_memory_request(),"Completed suite leaves no outstanding sample request");
        check(std::count(csv_text.begin(),csv_text.end(),'\n')==11,"Layout CSV: headers, six statistics rows and four memory rows");
        // A manual edit wins instead of being overwritten at the next timed tick.
        runtime.applied_revision=state.revision;report_runtime(runtime);check(start_benchmark(false),"Start optical suite");
        state=snapshot();auto edited=state.desired;edited.nr_enabled=false;
        check(request_changes(state.desired,edited),"Manual edit");tick_benchmark();
        check(snapshot().benchmark.phase==BenchmarkPhase::stopped && !snapshot().desired.nr_enabled,"Manual changes cancel automation and are kept");
        runtime.applied_revision=snapshot().revision;report_runtime(runtime);check(start_benchmark(false),"Restart optical suite");stop_benchmark();tick_benchmark();
        check(snapshot().benchmark.phase==BenchmarkPhase::stopped,"Explicit Stop");
        const auto luke=benchmark_plan(false,true);check(luke.size()==18,"Luke has six routes times three AFW qualities");
        unsigned afw_only=0;
        for(const auto& step:luke) {
            check(step.afw==3,"Luke never tests AFW disabled");
            check(step.generation==0 && !step.a && !step.b,"Luke ends at 0.3.0 without vector generators or A/B permutations");
            if(step.disabled && step.afw==3)++afw_only;
        }
        check(afw_only==3,"AFW-only Full/Half/Quarter trials");
        runtime.luke_detected=true;runtime.applied_revision=snapshot().revision;report_runtime(runtime);
        check(start_benchmark(false,true),"Start Luke suite");clock=snapshot().benchmark.requested_ms+500;
        for(std::size_t i=0;i<luke.size();++i) {
            state=snapshot();runtime.applied_revision=state.revision;
            runtime.afw_shading_rate=state.desired.afw_shading_rate;
            runtime.afw_running=state.desired.afw_mode!=0;report_runtime(runtime);
            tick_benchmark(clock);check(snapshot().benchmark.phase==BenchmarkPhase::running,"Luke quality applied before timing");
            tick_benchmark(clock+10000);clock+=10200;
        }
        state=snapshot();check(state.benchmark.phase==BenchmarkPhase::complete,"Luke completes");
        std::ifstream layout(std::filesystem::u8path(state.benchmark.csv_path));std::string header;std::getline(layout,header);
        const auto blank=header.find("Fidelity 00 / blank"),one=header.find("Fidelity 00 / 1x1"),two=header.find("Fidelity 00 / 2x2"),four=header.find("Fidelity 00 / 4x4");
        check(blank==std::string::npos && one<two && two<four && four!=std::string::npos,"Luke CSV groups only1x1/2x2/4x4; no disabled AFW tests");
        runtime.applied_revision=snapshot().revision;report_runtime(runtime);
        check(start_benchmark(false),"start trial before disabling flag");
        check(WritePrivateProfileStringW(L"benchmark",L"enabled",L"0",configuration_path().c_str()),"disable active benchmark");
        tick_benchmark();
        check(snapshot().benchmark.phase==BenchmarkPhase::stopped && !take_benchmark_memory_request(),"disabling INI stops trial and clears memory requests");
        check(WritePrivateProfileStringW(L"benchmark",L"enabled",L"1",configuration_path().c_str()),"re-enable for preset compatibility checks");
        for(const auto preset:{0U,3U}) {
            runtime.dlssg_preset_request=preset;runtime.applied_revision=snapshot().revision;report_runtime(runtime);
            check(start_benchmark(true),"Full suite accepts default/unverified or explicit C without enforcing another preset");
            check(snapshot().runtime.dlssg_preset_request==preset,"Suite does not change the startup receipt");
            clock=snapshot().benchmark.requested_ms+500;
            for(std::size_t i=0;i<full.size();++i) {
                runtime.applied_revision=snapshot().revision;report_runtime(runtime);tick_benchmark(clock);
                check(snapshot().benchmark.phase==BenchmarkPhase::running,"A/C/default use the same uninterrupted suite");
                tick_benchmark(clock+10000);clock+=10200;
            }
            check(snapshot().benchmark.phase==BenchmarkPhase::complete,"Full suite completes without a forced preset restart");
            const auto path=std::filesystem::u8path(snapshot().benchmark.csv_path);
            std::ifstream preset_details(path.parent_path()/(path.stem().wstring()+L"-details.csv"));
            std::stringstream recorded;recorded<<preset_details.rdbuf();
            check(recorded.str().find(preset==3 ? ",C startup request," : ",Game default / unverified,")!=std::string::npos,
                "CSV preserves the actual startup receipt including an unverified default");
        }
        std::cout<<"Automatic suite: full/optical with A/C/default, no preset enforcement, truthful A CSV, hidden sampling without F9, timing, median, restore/manual cancellation PASS\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
