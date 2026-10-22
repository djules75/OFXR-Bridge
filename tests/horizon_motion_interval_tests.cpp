#include "fixtures/horizon_steamvr_motion.hpp"
#include "fixtures/horizon_vdxr_motion.hpp"
#include "fixtures/horizon_v633_capture_budget.hpp"
#include "xrfg/two_frame_capture_budget.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {try {
    const auto check_receipt=[](const auto& frames,double width,double minimum_opposite,const char* runtime) {
    for(const auto& frame:frames) {
        std::array<double,25> opposite_error{},same_error{};
        unsigned index{};
        for(const auto& s:frame.samples) {
            const double u=(s.x+.5)/width,v=(s.y+.5)/width;
            const double clip[]{2*u-1,1-2*v,s.depth,1};
            const auto error=[&](const double* matrix) {
                double q[4]{};
                for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)q[r]+=matrix[c*4+r]*clip[c];
                if(!std::isfinite(q[3])||q[3]<=0)throw std::runtime_error("invalid captured projection");
                const double dx=s.motion_x*(-width*.5)-((q[0]/q[3]*.5+.5)-u)*width;
                const double dy=s.motion_y*(width*.5)-((.5-q[1]/q[3]*.5)-v)*width;
                return std::hypot(dx,dy);
            };
            opposite_error[index]=error(frame.to_opposite);same_error[index]=error(frame.to_same);++index;
        }
        std::sort(opposite_error.begin(),opposite_error.end());std::sort(same_error.begin(),same_error.end());
        std::cout<<"Horizon "<<runtime<<" native receipt: median N-1="<<opposite_error[12]<<"px N-2="<<same_error[12]<<"px\n";
        if(opposite_error[12]<minimum_opposite || same_error[12]>.25 || same_error[12]*10>=opposite_error[12])
            throw std::runtime_error("captured field no longer establishes the same-eye temporal endpoint");
    }
    };
    check_receipt(horizon_receipt::frames,1704,2,"SteamVR");
    check_receipt(horizon_vdxr_receipt::frames,1672,1,"VDXR");
    xrfg::TwoFrameCaptureStageBudget budget;
    std::uint64_t requested{},admitted{};
    unsigned accepted{};
    for(const auto& shot:horizon_capture_budget::shots) {
        requested+=shot.bytes;
        const bool keep=budget.accept(shot.stage);
        if(std::string_view(shot.stage).starts_with("afw_") && !keep)
            throw std::runtime_error("AFW capture lost an exact consumed input/output");
        if(keep){admitted+=shot.bytes;++accepted;}
    }
    if(requested<=1280ULL*1024*1024 || admitted>1280ULL*1024*1024 || accepted!=32)
        throw std::runtime_error("original Horizon second evaluation no longer fits its unchanged capture budget");
    xrfg::TwoFrameCaptureStageBudget ordinary;
    for(const auto stage:{"ngx_native_motion","ngx_native_depth","ngx_input_color","ngx_completed_color"})
        if(!ordinary.accept(stage))throw std::runtime_error("non-AFW capture was filtered");
    std::cout<<"Horizon F8 complete AFW pair: "<<admitted/(1024*1024)<<" MiB, "<<accepted<<" images\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
