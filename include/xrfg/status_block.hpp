#pragma once

#include <cstdint>

// What a running game's layer publishes about itself for the tray's
// status.json (docs/STATUS_JSON.md), once a second.
//
// A named shared-memory block rather than a file: the layer runs inside the
// game, and a memory write is all it does on that path. Either side creates
// the block or opens the one that exists; the tray copies it out under the
// sequence counter (odd while the layer writes) and writes the JSON. The last
// game to publish wins, and its pid says which one it is.
namespace xrfg::status {

inline constexpr wchar_t kMappingName[] = L"Local\\OFXRBridgeStatus";
inline constexpr std::uint32_t kMagic = 0x5354584FU;  // "OXTS"
inline constexpr std::uint32_t kVersion = 1U;

struct Block {
    std::uint32_t magic;
    std::uint32_t version;
    // Odd while the layer writes; the tray retries a copy that straddled one.
    std::uint32_t sequence;
    // A session is running; cleared at xrDestroySession.
    std::uint32_t active;
    std::uint32_t pid;
    // Frames shown per game frame in the running session: 2, or 3 in 3X.
    std::uint32_t multiplier;
    std::uint32_t deep_pipeline;
    // Synthetic frames were handed to the runtime in the last second.
    std::uint32_t generating;
    // One-second averages: the game's frames, the figure the in-headset FPS
    // overlay shows (computed with the overlay off too), the refresh rate.
    float app_fps;
    float output_fps;
    float display_hz;
    std::uint32_t reserved;
    // When this was written, milliseconds since 1970-01-01 UTC.
    std::uint64_t updated_unix_ms;
    // The game's executable name and the OpenXR runtime's name, terminated.
    char process[64];
    char runtime[64];
};

}  // namespace xrfg::status
