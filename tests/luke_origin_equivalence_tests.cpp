#include "luke_origin_v648_reference.hpp"
#include <random>
#include <iostream>
#include <chrono>
#include <stdexcept>
int main() {
    try {
        // Bound the existing least-squares fit for every legal prefix subset
        // (16 edges, at most one missing) and all64 payload edges. This proves
        // the new out-of-row rejection cannot remove a CRC-valid candidate.
        for(unsigned count:{16,64})for(int missing=-1;missing<(count==16?16:0);++missing) {
            std::vector<double> x;for(unsigned i=0;i<count;++i)if(int(i)!=missing)x.push_back(2.*i);
            double mean{};for(auto v:x)mean+=v;mean/=x.size();
            double ss{};for(auto v:x)ss+=(v-mean)*(v-mean);
            double intercept_l1{},slope_l1{};
            for(auto v:x){intercept_l1+=std::abs(1./x.size()-mean*(v-mean)/ss);slope_l1+=std::abs((v-mean)/ss);}
            if(intercept_l1>2. || slope_l1>(count==16?.125:.025))throw std::runtime_error("fit bound invalid");
        }
        if(.4*2.*(120./28.+4.05)>=8. || (1.-.4*.125)*(1.-.4*.025)*126.5<=100.)
            throw std::runtime_error("full-payload extent bound invalid");
        std::mt19937 rng(0x649);
        std::vector<std::vector<float>> samples;
        for(unsigned width:{96,128,260,512,1024}) for(unsigned n=0;n<80;++n) {
            std::vector<float> row(width);
            for(unsigned x=0;x<width;++x)row[x]=n%3==0?float(rng()%256)/255.f:
                n%3==1?(.4f+float(rng()%4)/255.f):(.2f+.001f*x+float(rng()%32)/255.f);
            if(n%5==0 && width>=260) for(unsigned x=0;x<256;++x)
                row[x+2]=xrfg::luke_origin_white(n+1,x)?.65f:.35f;
            if(n%17==0)row[width/3]=std::numeric_limits<float>::quiet_NaN();
            samples.push_back(std::move(row));
        }
        std::vector<float> mixed(1024,.4f);
        for(unsigned x=0;x<256;++x){mixed[x+4]=xrfg::luke_origin_white(649,x)?.7f:.3f;
            mixed[x+520]=xrfg::luke_origin_white(650,x)?.7f:.3f;}
        samples.push_back(mixed);
        for(const auto& row:samples) {
            const auto a=xrfg::test_reference::decode_reference_row(row),b=xrfg::decode_luke_origin_row(row);
            if(a.token!=b.token || a.ambiguous!=b.ambiguous)throw std::runtime_error("V648 token/ambiguity mismatch");
        }
        if(!xrfg::decode_luke_origin_row(mixed).ambiguous)throw std::runtime_error("mixed CRC-valid tokens accepted");
        std::uint64_t witness{};
        const auto measure=[&](bool reference){const auto before=std::chrono::steady_clock::now();
            for(unsigned i=0;i<5;++i)for(const auto& row:samples){auto r=reference?
                xrfg::test_reference::decode_reference_row(row):xrfg::decode_luke_origin_row(row);witness+=r.token+r.ambiguous;}
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();};
        auto old=measure(true),now=measure(false);
        std::cout<<"PASS "<<samples.size()<<" differential rows; same tokens/ambiguity; baseline_ms="<<old<<" optimized_ms="<<now<<" witness="<<witness<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
