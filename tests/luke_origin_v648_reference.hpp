#pragma once
// Frozen V648 decoder, test-only differential oracle.
#include "xrfg/luke_color_origin.hpp"
namespace xrfg::test_reference {
inline LukeColorOriginDecode decode_reference_row(std::span<const float> row) noexcept {
    try {
        if (row.size() < 96 || row.size() > 16384) return {};
        // The physical R8 row can retain only three quantization steps of
        // marker contrast after overlay dimming. CRC/preamble still govern
        // identity; equal/erased Manchester pairs remain undecodable.
        constexpr float gradient_contrast = .001f;
        constexpr double edge_contrast = .002, bit_contrast = .001;
        struct Edge { double x; bool falling; };
        std::vector<Edge> edges; edges.reserve(row.size());
        for (std::size_t x = 0; x + 1 < row.size();) {
            const float first = row[x + 1] - row[x];
            if (!std::isfinite(first) || std::abs(first) < gradient_contrast) { ++x; continue; }
            const bool falling = first < 0;
            double weight{}, position{};
            do {
                const float difference = row[x + 1] - row[x];
                if (!std::isfinite(difference) || std::abs(difference) < gradient_contrast || (difference < 0) != falling) break;
                const double magnitude = std::abs(difference);
                weight += magnitude; position += (x + .5) * magnitude; ++x;
            } while (x + 1 < row.size());
            if (weight >= edge_contrast) edges.push_back({position / weight, falling});
        }
        const auto sample = [&](double x) {
            if (x < 0 || x > static_cast<double>(row.size() - 1)) return static_cast<double>(NAN);
            const auto a = static_cast<std::size_t>(x), b = std::min(a + 1, row.size() - 1);
            return row[a] + (row[b] - row[a]) * (x - a);
        };
        const auto prefix_matches = [&](double origin, double cell) {
            for (unsigned bit = 0; bit < 16; ++bit) {
                const double difference = sample(origin + (2. * bit - .5) * cell)
                    - sample(origin + (2. * bit + .5) * cell);
                if (!std::isfinite(difference) || std::abs(difference) < bit_contrast ||
                    (difference > 0) != (((0xd3a5U >> (15 - bit)) & 1) != 0)) return false;
            }
            return true;
        };
        const auto fit_edges = [&](double& origin, double& cell, unsigned count, bool known_prefix) {
            double n{}, sx{}, sy{}, sxx{}, sxy{};
            for (unsigned bit = 0; bit < count; ++bit) {
                const double target = origin + 2. * bit * cell;
                double distance = .4 * cell;
                const Edge* best{};
                auto it = std::lower_bound(edges.begin(), edges.end(), target - distance,
                    [](const Edge& edge, double value) { return edge.x < value; });
                for (; it != edges.end() && it->x <= target + distance; ++it) {
                    if (known_prefix && it->falling != (((0xd3a5U >> (15 - bit)) & 1) != 0)) continue;
                    const double error = std::abs(it->x - target);
                    if (error < distance) { distance = error; best = &*it; }
                }
                if (!best) continue;
                const double x = 2. * bit, y = best->x - origin;
                ++n; sx += x; sy += y; sxx += x * x; sxy += x * y;
            }
            // One blended midpoint can disappear into the overlay/background
            // gradient. Fit the remaining sync edges; every decoded sync bit
            // and the complete payload CRC are still required below.
            if (n < (known_prefix ? 15. : 64.)) return false;
            const double pitch = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            if (pitch < .7 || pitch > 4.05) return false;
            origin += (sy - pitch * sx) / n; cell = pitch;
            return true;
        };
        LukeColorOriginDecode result{};
        // Infer continuous pitch from bit0 and either final sync midpoint.
        // Bit14 rises, bit15 falls. Using only bit15 loses a valid marker when
        // that one edge is blended into the changing overlay/background.
        // No rigid scale grid, guessed publication or partial-prefix identity.
        for (std::size_t first = 0; first < edges.size(); ++first) {
            if (!edges[first].falling) continue;
            for (std::size_t last = first + 1; last < edges.size(); ++last) {
                const double length = edges[last].x - edges[first].x;
                if (length > 120.) break;
                if (length < 21.) continue;
                const unsigned anchor = edges[last].falling ? 15 : 14;
                double origin = edges[first].x, cell = length / (2. * anchor);
                if (!prefix_matches(origin, cell) || !fit_edges(origin, cell, 16, true)) continue;
                fit_edges(origin, cell, kLukeOriginBits, false);
                std::uint16_t checksum{}; std::uint32_t token{}; bool valid = true;
                for (unsigned bit = 0; bit < kLukeOriginBits; ++bit) {
                    const double difference = sample(origin + (2. * bit - .5) * cell)
                        - sample(origin + (2. * bit + .5) * cell);
                    if (!std::isfinite(difference) || std::abs(difference) < bit_contrast) { valid = false; break; }
                    const unsigned value = difference > 0 ? 1 : 0;
                    if (bit < 16) {
                        if (value != ((0xd3a5U >> (15 - bit)) & 1)) { valid = false; break; }
                    } else if (bit < 48) token = (token << 1) | value;
                    else checksum = static_cast<std::uint16_t>((checksum << 1) | value);
                }
                if (!valid || !token || checksum != luke_origin_crc(token)) continue;
                if (result.token && result.token != token) result.ambiguous = true;
                result.token = token;
            }
        }
        return result;
    } catch (...) { return {}; }
}

}
