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
// 5: edge ghosting reduction and the repeating-pattern lock (V478). 4 was
// V477's (edge ghosting reduction only); 3 was V475's, which also carried the
// background fill and the hardware bilinear switch; 1 and 2 were the V374
// flow-history window, which never reached main.
inline constexpr std::uint32_t kVersion = 5U;

// Edge ghosting reduction: the search for the vector of what an in-between
// pixel actually shows, beside a motion boundary. Fractions of the colour
// range are in 255ths, pixel distances in hundredths of a pixel.
struct Block {
    std::uint32_t magic;
    std::uint32_t version;
    // Incremented by the tray after every change, so the layer can record
    // each new set of values exactly once.
    std::uint32_t sequence;
    std::uint32_t edge_ghosting_reduction;  // 0 off, 1 on
    // Rings of cells searched around the pixel: 1 = neighbours, 2 = + two
    // cells, 3 = + four, 4 = + six, 5 = + eight.
    std::uint32_t search_rings;
    // Candidates compared on the full-resolution frames, 1-3.
    std::uint32_t survivor_limit;
    // A pixel searches when the four flow cells it blends differ by more
    // than this, or its two endpoints by more than the error trigger.
    std::uint32_t spread_trigger_centipixels;
    std::uint32_t error_trigger_255ths;
    // How much better than the vector at the pixel a survivor must match.
    std::uint32_t survivor_margin_255ths;
    // A candidate lands on itself when the vector at its origin is within
    // base + speed * |candidate|.
    std::uint32_t fixed_point_base_centipixels;
    std::uint32_t fixed_point_speed_permille;
    // Repeating-pattern lock: where the pose alone ("nothing moved beyond the
    // head") matches within this margin of the flow's motion, the pose wins.
    std::uint32_t pattern_lock;  // 0 off, 1 on
    std::uint32_t pattern_lock_margin_255ths;
};

// The composition's built-in values (frame_synthesis.hlsl), so the window
// opens on exactly what runs without it: the search values chosen in the
// headset on V475 and the pattern lock margin chosen on V478 (2026-10-17).
inline constexpr Block kDefaults{
    kMagic, kVersion, 0U,
    1U,
    3U, 1U,
    800U, 24U, 2U,
    100U, 100U,
    1U, 1U,
};

}  // namespace xrfg::live_tuning
