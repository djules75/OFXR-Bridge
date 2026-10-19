#pragma once

#include <cstdint>

namespace xrfg {

// The floor under the once-per-frame hold in 3X, as a presenter frame count:
// while the floor is armed the application is not released fewer than
// `frames` presenter frames after its previous release. Zero where no floor
// applies.
//
// Without it a frame that fails to pair costs the game one presenter frame
// instead of three, and the pair that follows is released on its own phase
// right behind it: two game frames in four presenter frames, 45 game frames
// a second at 90 Hz instead of 30. On a GPU that is briefly oversubscribed -
// MSFS 2024 switching from the external view to the cockpit, leaving the
// settings menu, a focus change - that extra game frame lands on top of the
// synthesis that is already behind, the next capture finds its history slot
// busy, that frame fails to pair too, and the two feed each other for as
// long as the load lasts: 44 game frames and 77 new images a second for
// about six seconds, reported through status.json (issue #33, 2026-10-18).
//
// A prime arms the floor (V495 held only the prime and the frame after it).
// The ResetXPDR flight logs of 2026-10-19 showed that one frame is not
// enough: the second pair after the prime went out on its pop phase about
// 1.5 presenter frames after the held one, the next capture found its slot
// busy again, and the loop went on at 43 game frames a second for up to
// 20 s, one busy frame in four. So the floor stays armed release after
// release for as long as it is what holds the application back, and disarms
// on the first release whose own target already came at or after it: the
// cadence is back and the hold's own phase rules again.
//
// It does not move the steady state: across six 3X logs of both reporters,
// 4,884 releases away from any reset, none came closer than 2.5 presenter
// frames to the previous one. Where the hold waits for a pop
// (release_phase_depth) a floor in force on every frame would pin the
// release to the real frame's pop, the phase that hands synthetics over
// unwritten (issue #27); a floor that disarms itself does not.
//
// 2X is left as it is: the deeper pipeline has a period of slack the
// shallow 3X does not, and its prime hold (V438) was measured on its own.
[[nodiscard]] constexpr std::uint64_t release_serial_floor(
    std::uint64_t frames,
    bool pairable,
    bool floor_armed,
    std::uint64_t served_serial) noexcept {
    if (frames <= 2 || (pairable && !floor_armed)) {
        return 0;
    }
    return served_serial + frames;
}

// Whether the floor is armed for the release after this one. A prime arms
// it; a pair keeps it only when the floor held that pair back past its own
// target.
[[nodiscard]] constexpr bool release_floor_armed_after(
    std::uint64_t frames,
    bool pairable,
    bool floor_armed,
    bool floor_held_back) noexcept {
    if (frames <= 2) {
        return false;
    }
    return !pairable || (floor_armed && floor_held_back);
}

}  // namespace xrfg
