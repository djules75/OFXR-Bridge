#pragma once
#include <cstdint>
namespace xrfg {
// Temporal AFW owns the last accepted colors, not every native NGX production.
// A missing middle color cannot be treated as the previous opposite eye.
inline int luke_afw_effective_mode(int requested, std::uint64_t publication,
    std::uint64_t last_publication, unsigned eye, unsigned last_eye) noexcept {
    if ((requested == 2 || requested == 3) &&
        (!last_publication || last_publication + 1 != publication || eye == last_eye))
        return 1; // current source/depth stereo warp, no missing temporal color
    return requested;
}
}
