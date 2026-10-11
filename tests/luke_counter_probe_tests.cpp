#include "xrfg/luke_counter_probe.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uintptr_t module=0x180000000, object=0x220000000;
struct Memory {
    std::map<std::uintptr_t, std::vector<unsigned char>> blocks;
    std::uintptr_t fail{};
    unsigned render_reads{};
    bool race{};
    template<class T> void put(std::uintptr_t p, T v) {
        auto& b=blocks[p]; b.resize(sizeof(v)); std::memcpy(b.data(), &v, sizeof(v));
    }
    bool operator()(std::uintptr_t p, void* out, std::size_t n) noexcept {
        if (p == fail) return false;
        auto i=blocks.upper_bound(p);
        if (i==blocks.begin()) return false;
        --i;
        if (p-i->first > i->second.size() || n > i->second.size()-(p-i->first)) return false;
        std::memcpy(out, i->second.data()+(p-i->first), n);
        if (p==object+0x841870 && ++render_reads==2 && race) {
            std::uint64_t changed=43; std::memcpy(out, &changed, sizeof(changed));
        }
        return true;
    }
    Memory() {
        auto& h=blocks[module]; h.resize(4096); h[0]='M'; h[1]='Z';
        auto word=[&](unsigned p, std::uint32_t v){std::memcpy(h.data()+p,&v,4);};
        word(0x3c,0x100);word(0x100,0x4550);word(0x108,1789728589);word(0x150,11407360);
        blocks[module+0x2b0a80]={0xf3,0x0f,0x10,0x89,0xb0,0x1f,0x84,0x00,0x0f,0x57,0xc0,0x0f,0x2f,0xc8,0x77,
            0x0f,0x48,0x8b,0x81,0x38,0x03,0x00,0x00,0xf3,0x0f,0x10,0x88,0xa4,0x03,0x00};
        blocks[module+0x2aca91]={0x48,0x8b,0xc8,0x83,0xe1,0x07,0x49,0x8b,0x9c,0xce,0xc0,0x0f,0x84,0x00};
        put(module+xrfg::kLukeRuntimePointer, std::uint64_t{object});
        put(object, std::uint64_t{module+xrfg::kLukeCrimsonVtable});
        put(object+0x841870,std::uint64_t{42});
    }
};
void check(bool value, const char* why) {if(!value)throw std::runtime_error(why);}
}
int main() {
    try {
        Memory m;
        check(xrfg::luke_layout_matches(module,m),"supported signatures");
        const auto c=xrfg::read_luke_identity(module,m);
        check(c && c->render==42 && c->object==object,"valid identity");
        check(xrfg::read_luke_identity(module,m)==c,"duplicate identity");
        m=Memory{}; m.race=true;
        check(!xrfg::read_luke_identity(module,m),"race rejected");
        m=Memory{};m.fail=object+0x841870;
        check(!xrfg::read_luke_identity(module,m),"fault rejected");
        m=Memory{};m.put(object,std::uint64_t{module+1});
        check(!xrfg::read_luke_identity(module,m),"other game rejected");
        m=Memory{};m.put(module+xrfg::kLukeRuntimePointer,std::uint64_t{0});
        check(!xrfg::read_luke_identity(module,m),"missing runtime rejected");
        m=Memory{};m.blocks[module][0x108]^=1;
        check(!xrfg::luke_layout_matches(module,m),"wrong build rejected");
        m=Memory{};m.blocks[module+0x2b0a80][0]^=1;
        check(!xrfg::luke_layout_matches(module,m),"wrong HUD signature rejected");
        m=Memory{};m.blocks[module+0x2aca91][0]^=1;
        check(!xrfg::luke_layout_matches(module,m),"wrong mapping signature rejected");
        m=Memory{};std::uint32_t oversized=0xffffffff;std::memcpy(m.blocks[module].data()+0x3c,&oversized,4);
        check(!xrfg::luke_layout_matches(module,m),"invalid PE offset rejected");
        m=Memory{};m.put(object+0x841870,~std::uint64_t{0});
        check(!xrfg::read_luke_identity(module,m),"negative sentinel rejected");
        m=Memory{};
        m.blocks[module+0x20788b]={0x48,0x89,0x83,0x70,0x18,0x84,0x00,0xc6,0x84,0x19,0x10,0x14,0x84,0x00,0x01,0x48};
        m.put(module+xrfg::kLukeHfwVtable+30*8,std::uint64_t{module+0x204e80});
        m.put(object,std::uint64_t{module+xrfg::kLukeHfwVtable});
        check(xrfg::luke_hfw_layout_matches(module,m),"HFW source-selection signature");
        check(!xrfg::read_luke_identity(module,m),"HFW requires explicit layout authorization");
        check(xrfg::read_luke_identity(module,m,true)==c,"HFW selected source identity");
        m.blocks[module+0x20788b][0]^=1;
        check(!xrfg::luke_hfw_layout_matches(module,m),"HFW wrong selection signature rejected");
        m.blocks[module+0x20788b][0]^=1;
        m.put(module+xrfg::kLukeHfwVtable+30*8,std::uint64_t{module+1});
        check(!xrfg::luke_hfw_layout_matches(module,m),"HFW wrong vtable method rejected");
        std::cout << "Luke probe layout/identity/race/fault tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
