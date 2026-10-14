#include "xrfg/compositor_presentation_rate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Timing {
    unsigned m_nFrameIndex{}, m_nNumFramePresents{}, m_nNumMisPresented{};
    double m_flSystemTimeInSeconds{};
};
void check(const std::vector<Timing>& records, float expected) {
    const auto rate = xrfg::compositor_presentation_rate(std::span<const Timing>(records));
    if (!rate || std::abs(rate->fps - expected) > 0.01F)
        throw std::runtime_error("incorrect compositor presentation rate");
}
int main() {
    try {
        std::vector<Timing> full, repeated, dropped, irregular;
        for (unsigned i = 0; i <= 90; ++i) {
            full.push_back({i, 1, i % 5 < 2 ? 1U : 0U, i / 90.0});
            dropped.push_back({i, i % 2 ? 0U : 1U, 0, i / 90.0});
            if (i % 2 == 0) repeated.push_back({i, 2, 1, i / 90.0});
            if (i == 0 || (i * 76 / 90 > (i - 1) * 76 / 90))
                irregular.push_back({i, 1, i % 3 == 0 ? 1U : 0U, i / 90.0});
        }
        check(full, 90); check(repeated, 45); check(dropped, 45); check(irregular, 76);
        full.push_back({91, 0, 0, 91 / 90.0});
        full.push_back({92, 0, 0, 92 / 90.0});
        check(full, 90); // latest pending frame must not be counted as a loss
        for (unsigned start : {0U, 22U}) {
            std::vector<Timing> window(full.begin() + start, full.begin() + start + 64);
            check(window, 90); // overlapping 250ms polls retain full rate
        }
        full[15].m_nFrameIndex = full[14].m_nFrameIndex;
        if (xrfg::compositor_presentation_rate(std::span<const Timing>(full)))
            throw std::runtime_error("duplicate record accepted");
        std::cout << "Presented vsyncs, repeats, true omissions, 76/90 cadence and unsettled tails pass\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
