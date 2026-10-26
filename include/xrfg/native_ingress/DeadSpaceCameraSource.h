#pragma once
#include "streamline_camera.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

// Native source of the observed Dead Space SL1 constants builder. The original
// input has three independent axes; the builder computes its SDK forward from
// the wrong pair. Never alter the application's constants or derive orientation
// from XR, old frames, or the two collinear SDK directions.
namespace dead_space_camera_source {
using Camera=xrfg::native_ingress::streamline_camera::Camera;
constexpr std::size_t packet_bytes=0x1a0;
constexpr std::uintptr_t builder_rva=0x22ac5f0;
constexpr std::size_t builder_bytes=1303;
constexpr std::uint64_t builder_fingerprint=0x8923bd98c1599d2bULL;
constexpr DWORD game_timestamp=0x63ec4522,game_image_bytes=0x18d10000;
struct Source {std::array<unsigned char,packet_bytes> bytes{};std::uint32_t frame{};bool active{},read{};};
inline thread_local Source source;
struct Statistics {unsigned status{},failure{};std::uint64_t observed{},matched{},corrected{},rejected{};};
namespace detail {
inline std::atomic_uint status{},failure{};
inline std::atomic_bool terminal{};
inline std::atomic_uint64_t observed{},matched{},corrected{},rejected{};
using Builder=void(*)(const void*,std::uint32_t);
struct Hooks {std::mutex mutex;bool attempted{},deferred{};SafetyHookInline hook;Builder original{};};
inline Hooks& hooks(){static auto* s=new Hooks;return *s;}
inline std::uint64_t fingerprint(const unsigned char* data,std::size_t bytes) {
    std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<bytes;++i){h^=data[i];h*=1099511628211ULL;}return h;
}
template<std::size_t N>inline std::array<float,N> field(const Source& s,std::size_t offset) {
    std::array<float,N> out{};std::memcpy(out.data(),s.bytes.data()+offset,sizeof(out));return out;
}
template<std::size_t N>inline bool exact(const std::array<float,N>& a,const std::array<float,N>& b) {
    return !std::memcmp(a.data(),b.data(),sizeof(a));
}
inline float dot(const std::array<float,3>& a,const std::array<float,3>& b) {
    float v{};for(unsigned i=0;i<3;++i)v+=a[i]*b[i];return v;
}
inline std::array<float,3> cross(const std::array<float,3>& a,const std::array<float,3>& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline bool unit(const std::array<float,3>& a) {
    for(auto v:a)if(!std::isfinite(v))return false;
    return std::abs(dot(a,a)-1)<.001F;
}
inline bool projection(const Camera& c) {
    const auto& p=c.projection;const auto& inv=c.inverse;
    if(!(p[0]>0&&p[5]>0)||std::abs(p[11]+1)>.0001F||std::abs(p[15])>.0001F)return false;
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j) {
        double a{},b{};for(unsigned k=0;k<4;++k){a+=double(p[i*4+k])*inv[k*4+j];b+=double(inv[i*4+k])*p[k*4+j];}
        if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-(i==j?1.:0.))>.002||std::abs(b-(i==j?1.:0.))>.002)return false;
    }return true;
}
}
struct Scope {
    Source previous{source};
    Scope(const void* packet,std::uint32_t frame) {
        source={};source.active=true;source.frame=frame;SIZE_T copied{};
        source.read=packet&&ReadProcessMemory(GetCurrentProcess(),packet,source.bytes.data(),source.bytes.size(),&copied)&&copied==source.bytes.size();
        ++detail::observed;
    }
    ~Scope(){source=previous;}
};
inline bool correct(Camera& c) {
    if(!source.active)return false;
    const auto reject=[](unsigned reason){detail::failure=reason;++detail::rejected;return false;};
    if(!source.read)return reject(1);
    if(source.frame!=c.frame||c.viewport!=0)return reject(2);
    if(!c.observed||c.valid||c.ambiguous||c.rejection!=8)return reject(4);
    const auto position=detail::field<3>(source,0x70),up=detail::field<3>(source,0x7c);
    const auto native_back=detail::field<3>(source,0x88),right=detail::field<3>(source,0x94);
    if(!detail::exact(position,c.position)||!detail::exact(up,c.up)||!detail::exact(right,c.right)||
        !detail::exact(detail::field<16>(source,0xa0),c.projection)||
        !detail::exact(detail::field<16>(source,0xe0),c.inverse)||
        !detail::exact(detail::field<16>(source,0x120),c.previous)||
        !detail::exact(detail::field<16>(source,0x160),c.to_current))return reject(8);
    if(!detail::unit(up)||!detail::unit(right)||!detail::unit(native_back)||
        std::abs(detail::dot(up,right))>.0002F||std::abs(detail::dot(up,native_back))>.0002F||
        std::abs(detail::dot(right,native_back))>.0002F||
        std::abs(detail::dot(detail::cross(right,up),native_back)-1)>.0002F)return reject(16);
    if(!detail::projection(c))return reject(32);
    // Confirm the exact measured game conversion: SDK forward is the
    // normalized native_back x up, opposite its directly copied SDK right.
    auto emitted=detail::cross(native_back,up);const auto length=std::sqrt(detail::dot(emitted,emitted));
    for(unsigned i=0;i<3;++i)if(!std::isfinite(c.forward[i])||std::abs(emitted[i]/length-c.forward[i])>.0002F||
        std::abs(c.forward[i]+right[i])>.0002F)return reject(64);
    ++detail::matched;
    // Negative-Z projection: use the missing original source axis, with the
    // sign verified by the proper native [right,up,back] orientation above.
    // Right/up/position and every other SDK field remain bit-exact.
    for(unsigned i=0;i<3;++i)c.forward[i]=-native_back[i];
    c.rejection=0;c.valid=true;detail::failure=0;++detail::corrected;return true;
}
namespace detail {
inline void builder(const void* packet,std::uint32_t frame) {
    Scope scope(packet,frame);hooks().original(packet,frame);
}
inline bool attach(void* target,std::size_t bytes,std::uint64_t expected) {
    if(!target||!bytes||bytes>2048)return false;
    std::array<unsigned char,2048> code{};SIZE_T copied{};
    if(!ReadProcessMemory(GetCurrentProcess(),target,code.data(),bytes,&copied)||copied!=bytes||fingerprint(code.data(),bytes)!=expected)return false;
    auto& s=hooks();s.hook=safetyhook::create_inline(target,reinterpret_cast<void*>(&builder),SafetyHookInline::StartDisabled);
    if(!s.hook)return false;s.original=s.hook.original<Builder>();
    if(!s.hook.enable()){s.hook={};s.original=nullptr;return false;}return true;
}
}
inline Statistics statistics() {
    return {detail::status.load(),detail::failure.load(),detail::observed.load(),detail::matched.load(),detail::corrected.load(),detail::rejected.load()};
}
inline void poll(bool at_legacy_constants=false) noexcept {
    static const bool selected=[] {wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);
        const auto* base=wcsrchr(p,L'\\');return _wcsicmp(base?base+1:p,L"Dead Space.exe")==0;}();
    if(!selected||detail::terminal.load(std::memory_order_relaxed))return;
    try {
        auto& s=detail::hooks();std::scoped_lock lock(s.mutex);if(s.attempted||(s.deferred&&!at_legacy_constants))return;
        const auto vr=GetModuleHandleW(L"dxgi.dll");
        if(!vr||!GetProcAddress(vr,"RealVRVersion"))return;
        s.attempted=true;detail::terminal=true;detail::status=1;
        const auto game=GetModuleHandleW(nullptr);const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const unsigned char*>(game)+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.TimeDateStamp!=game_timestamp||nt->OptionalHeader.SizeOfImage!=game_image_bytes)return;
        detail::status=3;
        HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&poll),&pinned))return;
        if(detail::attach(reinterpret_cast<unsigned char*>(game)+builder_rva,builder_bytes,builder_fingerprint))detail::status=7;
        else {
            detail::status=11;
            // The retail image's code differs from its decoded resident body.
            // One deferred probe at the first real SL1 SetConstants boundary
            // handles lazy decoding; never hash code every game frame.
            if(!at_legacy_constants){s.attempted=false;s.deferred=true;detail::terminal=false;}
        }
    }catch(...){detail::status=17;}
}
}
