#include "xrfg/interface_metrics.hpp"
#include "xrfg/autonomous_control.hpp"
#include "xrfg/fps_overlay_model.hpp"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace xrfg;
using namespace xrfg::autonomous;
namespace {
unsigned checks{};
void check(bool pass, const char* text) { ++checks; if (!pass) throw std::runtime_error(text); }
void timing() {
    InterfaceTimingWindow window;
    const InterfaceFrameStamp frame{1, 100'000'000, 80'000'000};
    check(!window.submitted(frame, 110'000'000, false,true,false,true), "failed submit does not consume identity");
    check(!window.submitted(frame, 110'000'000, true,false,false,true), "empty real excluded");
    check(!window.submitted(frame, 110'000'000, true,true,true,true), "synthetic excluded");
    check(!window.submitted(frame, 110'000'000, true,true,false,false), "repeat/native copied output excluded");
    check(window.submitted(frame, 150'000'000, true,true,false,true, 145'000'000), "pipeline hold retained on first successful real");
    check(!window.submitted(frame, 160'000'000, true,true,false,true), "identity cannot double count");
    InterfaceTelemetry m{}; window.snapshot(160'000'000,m);
    check(m.delay_valid && m.ofxr_delay_ms == 50 && m.cpu_valid && m.cpu_render_ms == 20, "real entry and begin own their exact interval");
    check(m.presentation_valid && m.presentation_ms == -5, "late presentation retains negative sign");
    check(window.submitted({2,180'000'000,0}, 200'000'000,true,true,false,true), "missing clock converter remains eligible for OFXR time");
    window.snapshot(210'000'000,m);
    check(m.ofxr_delay_ms == 35 && m.presentation_ms == -5 && m.cpu_render_ms == 20, "median ignores unavailable sources independently");
    window.snapshot(1'210'000'001,m);
    check(!m.delay_valid && !m.presentation_valid && !m.cpu_valid, "one-second sample window expires");
    check(!window.submitted(frame, 1'220'000'000,true,true,false,true), "expired frame identity still cannot be re-counted");
    check(!window.submitted({3,300,0},299,true,true,false,true), "clock regression rejected");
    check(!window.submitted({4,0,0},400,true,true,false,true), "no entry clock yields no latency");
    LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
    const auto converted=interface_qpc_ns(qpc.QuadPart), now=interface_clock_ns();
    check(converted>0 && now>=converted && now-converted<50'000'000, "converted predicted display uses same QPC domain");
    FpsCounter counter;
    counter.submitted(100'000'000,false); counter.submitted(200'000'000,true);
    counter.submitted(300'000'000,true,false); counter.submitted(400'000'000,false,false);
    const auto fps=counter.snapshot(1'000'000'000);
    check(fps.sample_valid && fps.real_fps>0 && fps.generated_fps>0 && fps.submitted_fps==fps.real_fps+fps.generated_fps, "real/generated split excludes repeats and native bypass copies");
}
void complete_cycle() {
    auto cycle=std::make_shared<InterfaceCycle>(100'000'000);
    cycle->call(100'000'000,110'000'000); // Wait includes runtime/OFXR waits.
    cycle->call(115'000'000,117'000'000); // Begin; game gaps remain outside OFXR time.
    cycle->call(120'000'000,124'000'000);
    cycle->call(122'000'000,126'000'000); // Concurrent eyes: union, not sum.
    cycle->call(140'000'000,165'000'000); // End includes tail after actual submission.
    InterfaceTimingWindow window;
    InterfaceFrameStamp frame{10,140'000'000,117'000'000,cycle};
    check(window.submitted(frame,150'000'000,true,true,false,true,180'000'000,155'000'000), "real acceptance retains same Wait ticket");
    InterfaceTelemetry out{};window.snapshot(160'000'000,out);
    check(!out.cycle_valid && !out.ofxr_time_valid, "producer still inside End is not a completed sample");
    cycle->producer_returned(165'000'000);window.snapshot(170'000'000,out);
    check(out.cycle_valid && out.cycle_length_ms==65 && out.ofxr_time_ms==43, "production plus waits include post-submit producer tail; eye overlaps counted once");
    check(out.cycle_source==InterfaceSource::frame_cycle_qpc && out.ofxr_time_source==InterfaceSource::producer_call_qpc && !out.gpu_valid, "CPU boundary never claims GPU completion");
    auto next=std::make_shared<InterfaceCycle>(151'000'000);
    next->call(151'000'000,171'000'000);next->producer_returned(195'000'000);
    window.snapshot(180'000'000,out);
    check(out.cycle_length_ms==65 && out.ofxr_time_ms==43, "overlapping next Wait cannot overwrite retained previous frame");
    auto held=std::make_shared<InterfaceCycle>(200'000'000);
    held->call(200'000'000,205'000'000);held->call(240'000'000,245'000'000);
    held->producer_returned(245'000'000);
    InterfaceTimingWindow asynchronous;
    check(asynchronous.submitted({11,240'000'000,205'000'000,held},280'000'000,true,true,false,true,0,285'000'000), "held real accepted after producer returns");
    asynchronous.snapshot(290'000'000,out);
    check(out.cycle_length_ms==85 && out.ofxr_time_ms==10, "director retention contributes full age even outside producer calls");
    check(!asynchronous.submitted({11,240'000'000,205'000'000,held},300'000'000,true,true,false,true), "held real cannot double count");
    asynchronous.snapshot(1'290'000'001,out);
    check(!out.cycle_valid && !out.ofxr_time_valid, "new counters expire after one second");
    InterfaceCycle overflow(1);
    for(std::int64_t i=0;i<65;++i)overflow.call(1+i*2,2+i*2);
    overflow.producer_returned(200);float a{},b{};
    check(!overflow.read(201,a,b), "bounded interval overflow is N/A rather than an invented partial total");
    InterfaceCycle regression(100);regression.call(99,105);regression.producer_returned(200);
    check(!regression.read(201,a,b), "clock/identity regression has no timing");
}
void estimated_cycle() {
    auto make=[](std::int64_t returned=125'000'000) {
        auto c=std::make_shared<InterfaceCycle>(100'000'000);
        c->call(100'000'000,105'000'000);c->producer_returned(returned);return c;
    };
    float ms{};
    auto c=make();
    check(!c->read_estimated(121'000'000,ms),"without a synthetic receipt estimated cycle is N/A");
    c->synthetic_accepted(120'000'000,10'000'000);
    check(c->read_estimated(120'100'000,ms) && ms==30,"inline S/B receives one display slot, not just delivery age");
    check(c->read_estimated(130'000'000,ms) && ms==30,"real at inclusive first-slot boundary needs only one period");
    check(c->read_estimated(130'000'001,ms) && ms==40,"late real advances to next display slot");
    check(c->read_estimated(139'000'000,ms) && ms==40,"queue wait is included without adding another full period");
    auto tail=make(150'000'000);tail->synthetic_accepted(120'000'000,10'000'000);
    check(tail->read_estimated(121'000'000,ms) && ms==50,"producer tail remains part of the complete cycle");
    auto triple=make();triple->synthetic_accepted(120'100'000,10'000'000);
    triple->synthetic_accepted(120'000'000,10'000'000); // Native confirmation out of order.
    triple->synthetic_accepted(120'100'000,10'000'000); // Duplicate confirmation.
    check(triple->read_estimated(120'200'000,ms) && ms==40,"two distinct synthetics count two slots, confirmations may arrive out of order");
    auto unknown=make();unknown->synthetic_accepted(120'000'000,0);
    check(!unknown->read_estimated(125'000'000,ms),"unknown display period is N/A");
    auto changed=make();changed->synthetic_accepted(120'000'000,10'000'000);changed->synthetic_accepted(120'100'000,8'333'333);
    check(!changed->read_estimated(125'000'000,ms),"refresh change within pair cannot fabricate a slot estimate");
    check(!c->read_estimated(119'000'000,ms),"real receipt earlier than generated is invalid");
    auto unfinished=std::make_shared<InterfaceCycle>(100'000'000);unfinished->synthetic_accepted(120'000'000,10'000'000);
    check(!unfinished->read_estimated(121'000'000,ms),"estimate waits for producer completion");
    InterfaceTimingWindow window;
    check(window.submitted({50,120'000'000,0,c},120'050'000,true,true,false,true,0,120'100'000),"estimated cycle needs no XrTime conversion");
    InterfaceTelemetry m{};window.snapshot(130'000'000,m);
    check(m.cycle_valid && m.cycle_length_ms==25 && m.cycle_estimated_valid && m.cycle_estimated_ms==30 &&
        m.cycle_estimated_source==InterfaceSource::frame_cycle_slot_estimate,"measured and estimated cycles remain distinct and independently labeled");
    window.snapshot(1'130'000'001,m);
    check(!m.cycle_estimated_valid && !m.cycle_valid,"both counters expire with the same one-second sample window");
}
void wording() {
    Snapshot s{};
    check(interface_status_text(s)=="No active game" && interface_state_text(s)=="Waiting", "disconnected UI");
    s.connected=true;s.runtime.ready=1;s.runtime.applied_revision=s.revision;
    check(interface_status_text(s)=="OFXR: Waiting | AFW: N/A | Neural Render: Off", "clean non-Luke session reports AFW not applicable");
    s.desired.afw_mode=3;
    check(interface_status_text(s).find("AFW: Active")==std::string::npos, "Luke selection cannot fake AFW activity");
    s.runtime.luke_detected=s.runtime.vulkan_detected=true;
    s.runtime.generation_state=GenerationState::invalid_stereo;
    const auto text=interface_status_text(s);
    check(text.starts_with("Missing stereo pair") && text.find("Luke Ross detected")!=std::string::npos && text.find("Vulkan detected")!=std::string::npos, "simultaneous detections follow actual problem");
    std::strcpy(s.runtime.message,"Missing stereo pair");
    check(interface_status_text(s)==text, "duplicate reason omitted");
    s.runtime.vulkan_capture_suspended=true;
    s.runtime.generation_mode=0;
    check(interface_status_text(s).find("OFXR uses optical flow")!=std::string::npos, "Vulkan capture suspension reports optical flow");
    s.runtime.generation_mode=2;
    check(interface_status_text(s).find("real-frame bypass")!=std::string::npos, "Vulkan guide-dependent route reports bypass");
    s.runtime.vulkan_capture_suspended=false;
    for (auto state : {GenerationState::missing_motion,GenerationState::missing_depth,GenerationState::invalid_guides,
        GenerationState::temporal_mismatch,GenerationState::output_mismatch,GenerationState::queue_mismatch,
        GenerationState::unavailable,GenerationState::runtime_missing,GenerationState::initialization_failed,
        GenerationState::dispatch_failed,GenerationState::native_declined,GenerationState::multi_frame_unsupported,
        GenerationState::recovering,GenerationState::busy}) {
        s.runtime.message[0]=0;s.runtime.generation_state=state;
        check(!interface_status_text(s).starts_with("Luke"), "principal refusal has a distinct reason before detections");
    }
    s.runtime.upscaler_evaluations=1;s.runtime.upscaler_motion=true;s.runtime.upscaler_last_seen_ms=100;
    s.runtime.generation_mode=1;s.desired.frame_generation=1;
    check(interface_vectors_text(s,101)=="DLSS", "FSR FG never relabels actual DLSS vectors");
    check(interface_vectors_text(s,2100)=="None" && interface_vectors_text(s,99)=="None", "expired/regressing vector source");
    s.runtime.generation_state=GenerationState::paused;
    check(interface_state_text(s)=="Paused", "pause state");
    s.runtime.generation_state=GenerationState::ready;s.runtime.pending=1;
    check(interface_state_text(s)=="Applying settings", "pending state");
    s.runtime.pending=0;s.runtime.telemetry.updated_ms=GetTickCount64();s.runtime.telemetry.generating=true;
    check(interface_state_text(s)=="Generating FSR", "confirmed generated content identifies producer");
    s.runtime.heartbeat_ms=GetTickCount64();s.runtime.afw_running=true;
    check(interface_state_text(s)=="Generating FSR + AFW", "actual AFW is explicit");
    s.desired.nr_enabled=true;s.runtime.nr_running=true;
    check(interface_state_text(s)=="Generating FSR + AFW + Neural", "actual neural processing is explicit");
    check(interface_status_text(s).find("AFW: Active | Neural Render: Active")!=std::string::npos, "all active effects reported independently");
    s.runtime.afw_running=false;s.runtime.nr_running=false;
    check(interface_status_text(s).find("AFW: Waiting | Neural Render: Waiting")!=std::string::npos, "enabled without output is waiting");
    s.runtime.afw_running=true;s.runtime.nr_running=true;s.runtime.heartbeat_ms=GetTickCount64()-2000;
    check(interface_status_text(s).find("AFW: Active")==std::string::npos && interface_status_text(s).find("Neural Render: Active")==std::string::npos, "stale activity expires");
    s.desired.afw_mode=0;s.desired.nr_enabled=false;
    check(interface_status_text(s).find("AFW: Off | Neural Render: Off")!=std::string::npos, "switches off override recent activity");
    s.runtime.generation_mode=0;s.runtime.motion_source=1;s.runtime.generation_state=GenerationState::invalid_guides;
    check(interface_state_text(s)=="Fallback", "actual optical-flow fallback");
    s.runtime.telemetry.updated_ms=GetTickCount64()-2000;
    check(interface_state_text(s)=="Real frames only", "stale generation evidence expires");
}
}
int main() {
    try {
        const auto fixture=std::filesystem::temp_directory_path()/(L"ofxr-v460-timing-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(fixture);
        SetEnvironmentVariableW(L"XRFG_AUTONOMOUS_PROFILE_DIR",fixture.c_str());
        timing();complete_cycle();estimated_cycle();wording();
        const auto before=snapshot();
        check(!interface_telemetry_requested(), "hidden by default");
        set_interface_visible(InterfaceSurface::desktop,true);
        check(interface_telemetry_requested(), "desktop demand");
        set_interface_visible(InterfaceSurface::headset,true);set_interface_visible(InterfaceSurface::desktop,false);
        check(interface_telemetry_requested(), "closing desktop preserves headset demand");
        set_interface_visible(InterfaceSurface::headset,false);
        check(!interface_telemetry_requested(), "closing last UI stops new sampling immediately");
        check(snapshot().revision==before.revision && snapshot().desired==before.desired, "visibility cannot reconfigure FG");
        const auto started=std::chrono::steady_clock::now();
        unsigned requested{};
        for(unsigned i=0;i<100000;++i) requested+=interface_telemetry_requested();
        std::cout<<"hidden visibility check ns/call: "<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count()/100000<<"; requests="<<requested<<'\n';
        std::filesystem::remove(configuration_path());std::filesystem::remove(fixture);
        std::cout<<checks<<" V465 identity/source/status/cycle checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
