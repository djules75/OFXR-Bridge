#include "xrfg/elliott_meter.hpp"
#include "xrfg/fps_overlay_model.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace xrfg;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    FlipGesture flip;
    check(grip_upside_down({0,0,1,0})&&!grip_upside_down({}),"upside down gesture");
    check(!flip.observe(0,true,false)&&!flip.observe(249'999'999,true,false),"no wrist flash");
    check(flip.observe(250'000'000,true,false)&&flip.hand()==0,"left flip opens after delay");
    check(flip.observe(260'000'000,true,true)&&flip.hand()==0,"stable hand while both flipped");
    check(flip.observe(270'000'000,false,true)&&flip.hand()==1,"other flipped hand takes panel");
    check(flip.observe(280'000'000,false,false)&&flip.observe(779'999'999,false,false),"hide delay");
    check(!flip.observe(780'000'000,false,false),"upright hides panel");
    const auto pose=panel_pose_on_grip({{0,0,1,0},{1,2,3}},.2F);
    check(std::abs(pose.position.y-2.15F)<1e-5F,"upstream grip clearance");
    check(parse_overlay_position("elliott_controller")==FpsOverlayPosition::elliott_controller&&
        std::string(overlay_position_name(FpsOverlayPosition::elliott_bottom))=="elliott_bottom","new persistent modes");
    check(autonomous::sanitize([]{autonomous::Settings s;s.overlay_position=6;return s;}()).overlay_position==6,"mode6 retained");
    autonomous::Snapshot s;s.connected=true;s.desired.enabled=true;s.runtime.sessions=1;s.runtime.ready=1;
    s.runtime.generation_state=autonomous::GenerationState::ready;
    auto& m=s.runtime.telemetry;m.updated_ms=1000;m.rates_valid=true;
    m.output_fps=90;m.game_fps=45;m.generated_fps=45;m.display_hz=90;
    m.ofxr_time_ms=9;m.cycle_length_ms=17;m.cycle_estimated_ms=28.1F;
    m.ofxr_time_valid=m.cycle_valid=m.cycle_estimated_valid=true;
    unsigned rows{};auto pixels=rasterize_elliott_meter(s,1000,false,false,&rows);
    check(pixels.size()==1024*512&&rows>250&&rows<=512,"two metric rows and diagnostics fit");
    check(pixels[0]==0&&pixels[100*1024+30]!=0,"rounded upstream canvas");
    auto changed=s;changed.runtime.telemetry.cycle_length_ms=65;
    check(rasterize_elliott_meter(changed,1000,false,false)!=pixels,"draw supplied measured counter");
    auto stale=rasterize_elliott_meter(s,4000,false,false);
    check(stale!=pixels,"stale metrics show unavailable");
    check(rasterize_elliott_meter(s,1000,false,true)!=pixels,"UNORM linear palette conversion");
    check(rasterize_elliott_meter(s,1000,true,false)!=pixels,"BGRA channels supported");
    if(argc>1){std::ofstream out(argv[1],std::ios::binary);out<<"P6\n1024 "<<rows<<"\n255\n";
        for(unsigned i=0;i<1024*rows;++i){char rgb[]{char(pixels[i]),char(pixels[i]>>8),char(pixels[i]>>16)};out.write(rgb,3);}}
    std::cout<<"Meter supplied metrics, palette, persistence, gesture and placement passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
