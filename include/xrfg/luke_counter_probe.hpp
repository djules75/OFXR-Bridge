#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "xrfg/luke_render_identity.hpp"

namespace xrfg {
// Read-only offsets for Luke 2609.18.2.5162, Crimson Desert and pinned HFW.
// CPU render identities, not a claim of pixel-identical AER submissions.
inline constexpr std::uintptr_t kLukeRuntimePointer = 0x797408;
inline constexpr std::uintptr_t kLukeCrimsonVtable = 0x585cc0;
inline constexpr std::uintptr_t kLukeHfwVtable = 0x587250;

template<class Read>
bool luke_hfw_layout_matches(std::uintptr_t module, Read&& read) noexcept {
    // HFW's completed AER source selection, not its XR call counter.
    constexpr std::array<unsigned char,16> selected{
        0x48,0x89,0x83,0x70,0x18,0x84,0x00,0xc6,0x84,0x19,0x10,0x14,0x84,0x00,0x01,0x48};
    std::array<unsigned char,16> live{};
    std::uint64_t method{};
    return read(module+0x20788b,live.data(),live.size()) && live==selected &&
        read(module+kLukeHfwVtable+30*8,&method,sizeof(method)) && method==module+0x204e80;
}

template<class Read>
bool luke_layout_matches(std::uintptr_t module, Read&& read) noexcept {
    std::array<unsigned char, 4096> header{};
    if (!read(module, header.data(), header.size())) return false;
    auto u32 = [&](std::size_t offset) { std::uint32_t v{};
        std::memcpy(&v, header.data() + offset, sizeof(v)); return v; };
    if (header[0] != 'M' || header[1] != 'Z') return false;
    const auto nt = u32(0x3c);
    if (nt > header.size()-128 || u32(nt) != 0x4550 ||
        u32(nt+8) != 1789728589 || u32(nt+24+56) != 11407360) return false;
    constexpr std::array<unsigned char, 30> hud{
        0xf3,0x0f,0x10,0x89,0xb0,0x1f,0x84,0x00,0x0f,0x57,0xc0,0x0f,0x2f,0xc8,0x77,
        0x0f,0x48,0x8b,0x81,0x38,0x03,0x00,0x00,0xf3,0x0f,0x10,0x88,0xa4,0x03,0x00};
    constexpr std::array<unsigned char, 14> mapping{
        0x48,0x8b,0xc8,0x83,0xe1,0x07,0x49,0x8b,0x9c,0xce,0xc0,0x0f,0x84,0x00};
    std::array<unsigned char, hud.size()> live_hud{};
    std::array<unsigned char, mapping.size()> live_mapping{};
    return read(module+0x2b0a80, live_hud.data(), live_hud.size()) && live_hud == hud &&
        read(module+0x2aca91, live_mapping.data(), live_mapping.size()) && live_mapping == mapping;
}

template<class Read>
std::optional<LukeRenderIdentity> read_luke_identity(std::uintptr_t module, Read&& read,
    bool allow_hfw = false) noexcept {
    auto get = [&](std::uintptr_t address, auto& value) {
        return address >= 0x10000 && address < 0x00007fffffff0000ULL &&
            read(address, &value, sizeof(value));
    };
    std::uint64_t object{}, vt{}, render{}, render_after{}, final_object{}, final_vt{};
    if (!get(module+kLukeRuntimePointer, object) || !object || !get(object, vt))
        return {};
    if ((vt != module+kLukeCrimsonVtable && !(allow_hfw && vt==module+kLukeHfwVtable)) || !get(object+0x841870, render) ||
        !get(module+kLukeRuntimePointer, final_object) || !get(object, final_vt) ||
        !get(object+0x841870, render_after) || final_object != object || final_vt != vt ||
        render != render_after || render > 0x7fffffffffffffffULL) return {};
    return LukeRenderIdentity{object, render};
}
} // namespace xrfg
