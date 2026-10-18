#pragma once

#include <cstdint>

// Values the tray can change while a game is running, read by the layer.
//
// A named shared-memory block rather than the INI: the layer reads it once per
// synthesized pair, and a memory read belongs on that path where a file read
// does not. The tray creates and owns the block; while it does not exist, or
// holds another layout, the layer uses the composition's built-in values.
// Diagnostic tuning only - nothing here is a setting, and nothing is saved.
namespace xrfg::live_tuning {

inline constexpr wchar_t kMappingName[] = L"Local\\OFXRBridgeLiveTuning";
inline constexpr std::uint32_t kMagic = 0x4C565446U;  // "FTVL"
// 6: the repeating-pattern lock and the hybrid composition (V497). Edge
// ghosting reduction and its search values left with this version: the
// feature is off on this branch, and the constants that carried its values
// carry the DLSS guide rectangle in a hybrid draw. 5 was V478's (edge
// ghosting reduction and the pattern lock); 4 V477's; 3 V475's, which also
// carried the background fill and the hardware bilinear switch; 1 and 2 the
// V374 flow-history window, which never reached main.
inline constexpr std::uint32_t kVersion = 6U;

// Fractions of the colour range are in 255ths.
struct Block {
    std::uint32_t magic;
    std::uint32_t version;
    // Incremented by the tray after every change, so the layer can record
    // each new set of values exactly once.
    std::uint32_t sequence;
    // Repeating-pattern lock: where the pose alone ("nothing moved beyond the
    // head") matches within this margin of the flow's motion, the pose wins.
    // Reaches the flow composition only; a hybrid draw runs the built-in
    // values (lock on, margin 1/255).
    std::uint32_t pattern_lock;  // 0 off, 1 on
    std::uint32_t pattern_lock_margin_255ths;
    // Hybrid composition: the game's motion vectors and the optical flow,
    // chosen per pixel. Where the two frames warped by the vectors disagree
    // by less than the agree threshold, the vectors are taken outright and
    // the flow branch is not evaluated at that pixel. Elsewhere both run and
    // the one whose warped samples disagree less wins, as sharply as the
    // choice slope says (1 soft blend, 15 hard pick). The vectors slope is
    // the vectors branch's own fall-back toward the pose-only blend as its
    // samples disagree (3 = 0.3's value; the vectors-only composition had 6).
    std::uint32_t hybrid_agree_threshold_255ths;  // 0-62
    std::uint32_t hybrid_choice_slope;            // 1-15
    std::uint32_t hybrid_vectors_slope;           // 1-15
};

// The composition's built-in values (frame_synthesis.hlsl), so the window
// opens on exactly what runs without it.
inline constexpr Block kDefaults{
    kMagic, kVersion, 0U,
    1U, 1U,
    5U, 8U, 3U,
};

}  // namespace xrfg::live_tuning
