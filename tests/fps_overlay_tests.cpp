#include "xrfg/fps_overlay_model.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    try {
        using namespace xrfg;
        FpsCounter counter;
        require(!counter.snapshot(0).active, "startup green");
        for (int i = 0; i < 45; ++i) {
            const auto t = static_cast<std::int64_t>(i) * 1'000'000'000 / 45;
            counter.submitted(t, true);
            counter.submitted(t + 10'000'000, false);
        }
        const auto snapshot = counter.snapshot(999'999'999);
        require(std::abs(snapshot.submitted_fps - 90) < 0.1f && snapshot.active,
                "output must count both originals and synthetics");
        counter.submitted(1'100'000'000, false);
        require(counter.snapshot(1'100'000'000).active, "S/B status flicker");
        require(!counter.snapshot(1'600'000'000).active, "fallback fails to expire green");
        require(counter.snapshot(3'000'000'000).submitted_fps == 0, "stale output FPS");
        counter.reset();
        counter.submitted(4'000'000'000, false);
        require(!counter.snapshot(4'050'000'000).active, "prime/repeat must not be green");
        // A repeat is a submission and not a frame. The presenter emits one per
        // display period the application did not fill, so counting them made the
        // overlay report the headset's refresh rate: 144 on a title delivering
        // 78. Feed a second of 39 pairs padded to 144 submissions and require
        // the count to be the 78 distinct images, not the 144 handed over.
        counter.reset();
        for (int i = 0; i < 144; ++i) {
            const auto t = 5'000'000'000 + static_cast<std::int64_t>(i) * 1'000'000'000 / 144;
            if (i % 2 == 0 && i / 2 < 39) {
                counter.submitted(t, true);
            } else if (i % 2 == 1 && i / 2 < 39) {
                counter.submitted(t, false);
            } else {
                counter.submitted(t, false, false);
            }
        }
        const auto padded = counter.snapshot(5'999'999'999);
        require(std::abs(padded.submitted_fps - 78) < 0.5f,
                "repeats must not be counted as frames");
        require(padded.active, "synthetics still mark the overlay active");
        // The compositor scans every one of those 144 submissions out on time,
        // so its delivered count is the refresh rate. What reaches the eye is
        // the new-content share of it.
        require(std::abs(padded.new_content_share - 78.0f / 144.0f) < 0.01f,
                "new-content share must exclude repeats");
        require(std::abs(delivered_new_images(144, padded) - 78) < 0.5f,
                "delivered count must not credit repeats");
        // A compositor-side loss stays visible alongside the repeats: 90
        // delivered of 100 submitted with a fifth of them repeats.
        FpsSnapshot mixed;
        mixed.new_content_share = 0.8f;
        require(std::abs(delivered_new_images(90, mixed) - 72) < 0.01f,
                "repeats and compositor loss must compound");
        require(delivered_new_images(90, FpsSnapshot{}) == 90,
                "no repeats must leave the delivered count unchanged");
        // One flagged frame in the compositor's 0.7 s window reads 88.6 at
        // 90 Hz, and the counter's own second is a frame either way; the
        // overlay draws the refresh rate for both, and keeps a real loss.
        require(displayed_rate(88.6f, 90) == 90, "one frame short must read as whole");
        require(displayed_rate(89.3f, 90) == 90, "a window one frame short must read as whole");
        require(displayed_rate(91.2f, 90) == 90, "a frame over must read as whole");
        require(displayed_rate(87.0f, 90) == 87, "three frames short is a loss");
        require(displayed_rate(45.0f, 90) == 45, "half rate is not snapped");
        require(displayed_rate(89.0f, 0) == 89, "no refresh known leaves the rate");
        require(displayed_rate(118.5f, 120) == 120, "the band scales with the refresh");
        for (auto position : {FpsOverlayPosition::off, FpsOverlayPosition::upper_left,
            FpsOverlayPosition::upper_right, FpsOverlayPosition::lower_left, FpsOverlayPosition::lower_right}) {
            require(parse_overlay_position(overlay_position_name(position)) == position, "position round trip");
            if (position == FpsOverlayPosition::off) continue;
            const auto p = overlay_placement(position, -2, 2, -1.5f, 1.5f);
            const bool left = position == FpsOverlayPosition::upper_left || position == FpsOverlayPosition::lower_left;
            const bool upper = position == FpsOverlayPosition::upper_left || position == FpsOverlayPosition::upper_right;
            require((p.x < 0) == left && (p.y > 0) == upper, "wrong quadrant");
            require(std::abs(p.x) + p.width / 2 < 2 && std::abs(p.y) + p.height / 2 < 2,
                    "not inset in wide FOV");
            require(std::abs(std::abs(p.x) - 0.88f) < 0.0001f &&
                    std::abs(std::abs(p.y) - 0.78f) < 0.0001f,
                    "number must use the new inward position in every corner");
            require(std::abs(p.width - 0.192f) < 0.0001f &&
                    std::abs(p.height - 0.096f) < 0.0001f && p.z == -2.0f,
                    "number must be 20 percent smaller without changing depth");
            const auto asymmetric = overlay_placement(position, -0.65f, 0.9f, -0.75f, 0.8f);
            constexpr float span = 1.55f, anchor_width = span * 0.06f;
            constexpr float anchor_height = anchor_width / 2.0f;
            const float expected_x = 2.0f * (left ? -0.65f + span * 0.25f + anchor_width * 0.5f
                                                  : 0.9f - span * 0.25f - anchor_width * 0.5f);
            const float expected_y = 2.0f * (upper ? 0.8f - span * 0.29f - anchor_height * 0.5f
                                                  : -0.75f + span * 0.29f + anchor_height * 0.5f);
            require(std::abs(asymmetric.x - expected_x) < 0.0001f &&
                    std::abs(asymmetric.y - expected_y) < 0.0001f &&
                    std::abs(asymmetric.width - anchor_width * 1.6f) < 0.0001f &&
                    std::abs(asymmetric.height - anchor_height * 1.6f) < 0.0001f,
                    "smaller number must retain the inward asymmetric-FOV centre");
        }
        require(overlay_texture_width(4000) > overlay_texture_width(1600), "resolution adaptation");
        require(overlay_texture_width(1) == 96 && overlay_texture_width(UINT32_MAX) == 336, "texture bounds");
        const auto green = rasterize_fps_overlay(240, 120, snapshot, false);
        auto inactive = snapshot; inactive.active = false;
        const auto red = rasterize_fps_overlay(240, 120, inactive, false);
        require(std::count(green.begin(), green.end(), 0xff70eb40u) > 100, "green glyphs");
        require(std::count(red.begin(), red.end(), 0xff5050ffu) > 100, "red glyphs");
        for (std::size_t i = 0; i < green.size(); ++i) {
            require(green[i] == 0 || green[i] == 0xff70eb40u, "extra text or opaque background");
            require(red[i] == (green[i] ? 0xff5050ffu : 0), "active/inactive changes more than digit color");
        }
        require(std::count(green.begin(), green.end(), 0u) > green.size() / 2,
                "background must be fully transparent");
        const auto bgra = rasterize_fps_overlay(240, 120, inactive, true);
        require(std::count(bgra.begin(), bgra.end(), 0xffff5050u) > 100, "BGRA color order");
        // Exact oracle: the whole minimal image must contain only the digits 90.
        const auto minimal = rasterize_fps_overlay(24, 12, snapshot, false);
        const unsigned rows[2][7]{{14,17,17,15,1,1,14}, {14,17,19,21,25,17,14}};
        for (unsigned y = 0; y < 12; ++y) for (unsigned x = 0; x < 24; ++x) {
            bool stroke = false;
            for (unsigned digit = 0; digit < 2; ++digit) {
                const auto left = 6 + digit * 6;
                if (x >= left && x < left + 5 && y >= 2 && y < 9)
                    stroke = stroke || (rows[digit][y - 2] & (1u << (4 - (x - left))));
            }
            require(minimal[y * 24 + x] == (stroke ? 0xff70eb40u : 0u), "must render only output FPS number");
        }
        require(rasterize_fps_overlay(24, 12, {90.49f, true}, false) == minimal, "FPS rounding down");
        require(rasterize_fps_overlay(24, 12, {90.6f, true}, false) ==
                rasterize_fps_overlay(24, 12, {91, true}, false), "FPS rounding up");
        for (float value : {-1.0f, std::numeric_limits<float>::quiet_NaN()})
            require(rasterize_fps_overlay(24, 12, {value, false}, false) ==
                    rasterize_fps_overlay(24, 12, {0, false}, false), "invalid FPS clamp");
        require(rasterize_fps_overlay(24, 12, {1000, true}, false) ==
                rasterize_fps_overlay(24, 12, {999, true}, false), "three-digit FPS clamp");
        // Paused: two orange bars, two columns each, before an unchanged
        // number. "p45" starts at column 3 of the minimal image.
        const auto paused_image =
            rasterize_fps_overlay(24, 12, FpsSnapshot{45, false, 1.0f, true}, false);
        const auto unpaused_image = rasterize_fps_overlay(24, 12, {45, false}, false);
        require(std::count(paused_image.begin(), paused_image.end(), 0xff0096ffu) == 28,
                "pause symbol is two 2x7 bars");
        for (unsigned y = 2; y < 9; ++y)
            require(paused_image[y * 24 + 3] == 0xff0096ffu && paused_image[y * 24 + 4] == 0xff0096ffu &&
                    paused_image[y * 24 + 5] == 0u &&
                    paused_image[y * 24 + 6] == 0xff0096ffu && paused_image[y * 24 + 7] == 0xff0096ffu,
                    "pause bars stand left of the number");
        require(std::count(paused_image.begin(), paused_image.end(), 0xff5050ffu) ==
                std::count(unpaused_image.begin(), unpaused_image.end(), 0xff5050ffu),
                "pause symbol leaves the number as it was");
        require(!rasterize_fps_overlay(24, 12, FpsSnapshot{999, false, 1.0f, true}, false).empty(),
                "pause symbol fits beside three digits");
        // Medium and small: the same digits at half and a third of the
        // size, centred where big puts them.
        require(rasterize_fps_overlay(240, 120, snapshot, false, FpsOverlaySize::big) == green,
                "big is the original size");
        // Ink count, and the centre of the ink's bounding box: "90" fills its
        // text box, so that centre is the text's.
        const auto ink = [](const std::vector<std::uint32_t>& image, double& cx, double& cy) {
            std::size_t count = 0, x0 = 240, x1 = 0, y0 = 120, y1 = 0;
            for (std::size_t i = 0; i < image.size(); ++i) if (image[i]) {
                ++count;
                x0 = std::min(x0, i % 240); x1 = std::max(x1, i % 240);
                y0 = std::min(y0, i / 240); y1 = std::max(y1, i / 240);
            }
            cx = (static_cast<double>(x0) + static_cast<double>(x1)) / 2;
            cy = (static_cast<double>(y0) + static_cast<double>(y1)) / 2;
            return count;
        };
        double big_x{}, big_y{}, medium_x{}, medium_y{}, small_x{}, small_y{};
        const auto big_ink = ink(green, big_x, big_y);
        const auto medium_ink = ink(
            rasterize_fps_overlay(240, 120, snapshot, false, FpsOverlaySize::medium), medium_x, medium_y);
        const auto small_ink = ink(
            rasterize_fps_overlay(240, 120, snapshot, false, FpsOverlaySize::little), small_x, small_y);
        require(medium_ink * 4 == big_ink, "medium is half the size per axis");
        require(small_ink * 9 > big_ink * 8 / 10 && small_ink * 9 < big_ink * 12 / 10,
                "small is a third of the size per axis");
        require(std::abs(medium_x - big_x) <= 1 && std::abs(medium_y - big_y) <= 1 &&
                std::abs(small_x - big_x) <= 1 && std::abs(small_y - big_y) <= 1,
                "smaller text keeps the counter's centre");
        require(!rasterize_fps_overlay(24, 12, FpsSnapshot{999, false, 1.0f, true}, false,
                                       FpsOverlaySize::little).empty(),
                "small fits the minimal image");
        for (auto size : {FpsOverlaySize::big, FpsOverlaySize::medium, FpsOverlaySize::little})
            require(parse_overlay_size(overlay_size_name(size)) == size, "size name round trip");
        require(parse_overlay_size("") == FpsOverlaySize::big, "size defaults to big");
        // The latency line: a 24x16 image keeps the number exactly where the
        // 24x12 one has it and draws "11ms" at half size in the rows below.
        auto with_latency = snapshot;
        with_latency.latency_ms = 11;
        const auto tall_plain = rasterize_fps_overlay(240, 160, snapshot, false);
        const auto tall_latency = rasterize_fps_overlay(240, 160, with_latency, false);
        require(std::equal(green.begin(), green.end(), tall_plain.begin()) &&
                std::all_of(tall_plain.begin() + 240 * 120, tall_plain.end(),
                            [](std::uint32_t p) { return p == 0; }),
                "taller image keeps the number in place");
        require(std::equal(green.begin(), green.begin() + 240 * 100, tall_latency.begin()),
                "latency leaves the number as it was");
        std::size_t latency_ink = 0;
        for (std::size_t i = 240 * 100; i < tall_latency.size(); ++i) {
            require(tall_latency[i] == 0 || tall_latency[i] == 0xff70eb40u, "latency in the number's colour");
            latency_ink += tall_latency[i] != 0;
        }
        require(latency_ink > 0 && std::all_of(tall_latency.begin() + 240 * 136, tall_latency.end(),
                    [](std::uint32_t p) { return p == 0; }),
                "latency drawn below the number, at half its size");
        require(rasterize_fps_overlay(240, 120, with_latency, false) == green,
                "no room, no latency");
        // V506: Hybrid's state just above the number of a 20-row image - green
        // OK, red KO. The number sits in rows 4-16 of the grid; its glyphs
        // start at texture row 65 here.
        constexpr std::size_t number_glyphs_top = 65;
        const auto rows_above = [&](const std::vector<std::uint32_t>& image, std::uint32_t colour) {
            std::size_t ink = 0;
            for (std::size_t i = 0; i < 240 * number_glyphs_top; ++i) {
                require(image[i] == 0 || image[i] == colour, "Hybrid status in one colour");
                ink += image[i] != 0;
            }
            return ink;
        };
        FpsSnapshot hybrid_ok = snapshot, hybrid_ko = snapshot;
        hybrid_ok.hybrid = 0;
        hybrid_ko.hybrid = 821;
        const auto tall_none = rasterize_fps_overlay(240, 200, snapshot, false);
        const auto tall_ok = rasterize_fps_overlay(240, 200, hybrid_ok, false);
        const auto tall_ko = rasterize_fps_overlay(240, 200, hybrid_ko, false);
        require(rows_above(tall_none, 0) == 0, "no Hybrid status, nothing above");
        require(rows_above(tall_ok, 0xff70eb40u) > 0, "HYBRID OK in green above the number");
        require(rows_above(tall_ko, 0xff5050ffu) > 0, "HYBRID KO in red above the number");
        require(std::equal(tall_none.begin() + 240 * 40, tall_none.begin() + 240 * 160, green.begin()),
                "a 20-row image keeps the number in rows 4-16");
        require(std::equal(tall_ok.begin() + 240 * number_glyphs_top, tall_ok.end(),
                    tall_none.begin() + 240 * number_glyphs_top) &&
                std::equal(tall_ko.begin() + 240 * number_glyphs_top, tall_ko.end(),
                    tall_none.begin() + 240 * number_glyphs_top),
                "Hybrid status leaves the number as it was");
        require(rasterize_fps_overlay(240, 160, hybrid_ok, false) == tall_plain,
                "a 16-row image has no room for Hybrid status");
        // Optional visual artifact, not a live image or runtime dependency.
        if (argc > 1) {
            std::ofstream file(argv[1], std::ios::binary);
            file << "P6\n240 640\n255\n";
            // Checkerboard is preview-only, to visualize transparent pixels.
            // The two numbers, then HYBRID OK and HYBRID KO above one (V506).
            const std::pair<const std::vector<std::uint32_t>*, unsigned> previews[]{
                {&green, 120u}, {&red, 120u}, {&tall_ok, 200u}, {&tall_ko, 200u}};
            for (const auto& [pixels, rows] : previews) for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < 240; ++x) {
                const auto pixel = (*pixels)[y * 240 + x];
                const char checker = ((x / 12 + y / 12) % 2) ? 48 : 32;
                const char rgb[]{pixel ? static_cast<char>(pixel) : checker,
                    pixel ? static_cast<char>(pixel >> 8) : checker,
                    pixel ? static_cast<char>(pixel >> 16) : checker};
                file.write(rgb, 3);
            }
        }
        std::cout << "Output FPS, active/inactive hysteresis, positions, number-only and transparent raster tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
