#pragma once
#include <cstdint>
namespace xrfg {
// Temporal AFW owns the last accepted colors, not every native NGX production.
// A missing middle color cannot be treated as the previous opposite eye.
inline bool luke_afw_previous_opposite(std::uint64_t publication,
    std::uint64_t last_publication, unsigned eye, unsigned last_eye) noexcept {
    return last_publication && publication > last_publication &&
        publication - last_publication == 1 && eye != last_eye;
}
inline int luke_afw_effective_mode(int requested, std::uint64_t publication,
    std::uint64_t last_publication, unsigned eye, unsigned last_eye,
    std::uint64_t previous_same_publication, bool clear_before_warping = false) noexcept {
    // Retained-color mode needs both exact source colors. The pinned provider's
    // Clear mode has a different priming protocol: preserve its established
    // opposite-eye-only guard (two Alternate primes otherwise clear its output).
    if ((requested == 2 || requested == 3) &&
        (!luke_afw_previous_opposite(publication,last_publication,eye,last_eye) ||
         (!clear_before_warping && (!previous_same_publication || publication < 2 ||
          previous_same_publication != publication - 2))))
        return 1; // current source/depth stereo warp, no missing temporal color
    return requested;
}
}
