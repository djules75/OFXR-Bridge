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
// Only where the hold counts presenter frames (presenter_holds_by_count:
// SteamVR, or at admission). There a pair's own target is the same count the
// floor asks for, so the floor can only ever hold a prime, and it disarms on
// the first pair. Where the hold waits for a pop (release_phase_depth) the
// pair's target is `depth` pops before its real frame, always earlier than
// `frames` after a release made at a real frame's pop, so a floor armed by
// one prime held every release after it one pop late, re-armed itself each
// time, and pinned the release at the real frame's pop for the rest of the
// session: the phase that hands every synthetic over unwritten, issue #27
// again, on Pimax OpenXR and Virtual Desktop (issue #43, 0.2.14.1). On
// those runtimes the floor is off, and the prime hold is the pop hold's
// own (k = 0, see wait_for_presenter_pair). The steady-state check that
// cleared V522 - no two releases closer than 2.5 presenter frames across
// six 3X logs - could not see this, because the floor itself spaces them
// at exactly three.
//
// 2X is left as it is: the deeper pipeline has a period of slack the
// shallow 3X does not, and its prime hold (V438) was measured on its own.
[[nodiscard]] constexpr std::uint64_t release_serial_floor(
    std::uint64_t frames,
    bool by_count,
    bool pairable,
    bool floor_armed,
    std::uint64_t served_serial) noexcept {
    if (frames <= 2 || !by_count || (pairable && !floor_armed)) {
        return 0;
    }
    return served_serial + frames;
}

// Whether the floor is armed for the release after this one. A prime arms
// it; a pair keeps it only when the floor held that pair back past its own
// target. Never where the hold waits for a pop.
[[nodiscard]] constexpr bool release_floor_armed_after(
    std::uint64_t frames,
    bool by_count,
    bool pairable,
    bool floor_armed,
    bool floor_held_back) noexcept {
    if (frames <= 2 || !by_count) {
        return false;
    }
    return !pairable || (floor_armed && floor_held_back);
}

}  // namespace xrfg
