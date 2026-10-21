#include "fixtures/horizon_steamvr_motion.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {try {
    for(const auto& frame:horizon_receipt::frames) {
        std::array<double,25> opposite_error{},same_error{};
        unsigned index{};
        for(const auto& s:frame.samples) {
            const double u=(s.x+.5)/1704,v=(s.y+.5)/1704;
            const double clip[]{2*u-1,1-2*v,s.depth,1};
            const auto error=[&](const double* matrix) {
                double q[4]{};
                for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)q[r]+=matrix[c*4+r]*clip[c];
                if(!std::isfinite(q[3])||q[3]<=0)throw std::runtime_error("invalid captured projection");
                const double dx=s.motion_x*(-852)-((q[0]/q[3]*.5+.5)-u)*1704;
                const double dy=s.motion_y*852-((.5-q[1]/q[3]*.5)-v)*1704;
                return std::hypot(dx,dy);
            };
            opposite_error[index]=error(frame.to_opposite);same_error[index]=error(frame.to_same);++index;
        }
        std::sort(opposite_error.begin(),opposite_error.end());std::sort(same_error.begin(),same_error.end());
        std::cout<<"Horizon native receipt: median N-1="<<opposite_error[12]<<"px N-2="<<same_error[12]<<"px\n";
        if(opposite_error[12]<2 || same_error[12]>.25 || same_error[12]*10>=opposite_error[12])
            throw std::runtime_error("captured field no longer establishes the same-eye temporal endpoint");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
