#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace xrfg {

enum class FpsOverlayPosition { off, upper_left, upper_right, lower_left, lower_right };
[[nodiscard]] FpsOverlayPosition parse_overlay_position(std::string_view value) noexcept;
[[nodiscard]] const char* overlay_position_name(FpsOverlayPosition position) noexcept;

struct FpsSnapshot {
    float submitted_fps{};
    bool active{};
};

// Caller serializes access. Fixed storage, monotonic wall-clock measurements;
// successful downstream submissions are NOT evidence of physical scanout.
//
// Counts *distinct images*, not submissions. The presenter hands the runtime
// one frame per display period whether or not the application produced one,
// resubmitting what it already holds to keep the cadence. Those repeats carry
// nothing new, and counting them made the overlay report the headset's refresh
// rate rather than the frame rate: measured on The Witcher 3 through VDXR at
// 144 Hz, 144.0 submissions a second of which 65.8 were repeats, against 78.1
// distinct images actually reaching the eye. The reporter saw 144 and a
// picture that visibly was not.
//
// On SteamVR the figure is replaced by the compositor's own delivered count
// before it is displayed, which is why this only ever showed on runtimes where
// no such source exists.
class FpsCounter {
public:
    // `new_content` is false for a repeat.
    void submitted(std::int64_t now_ns, bool synthetic, bool new_content = true) noexcept;
    [[nodiscard]] FpsSnapshot snapshot(std::int64_t now_ns) const noexcept;
    void reset() noexcept { *this = {}; }
private:
    struct Bucket {
        std::int64_t epoch{-1};
        std::uint32_t output{};
    };
    Bucket& bucket(std::int64_t now_ns) noexcept;
    std::array<Bucket, 12> buckets_{};
    std::int64_t start_ns_{-1};
    std::int64_t last_synthetic_ns_{-1};
};

struct OverlayPlacement {
    float x{}, y{}, z{-2.0f}, width{}, height{};
};
// Tangent-space bounds common to both eyes. Inset AND capped to the central
// 90 degrees so wide-FOV headsets do not push the panel into peripheral optics.
[[nodiscard]] OverlayPlacement overlay_placement(
    FpsOverlayPosition position, float left, float right, float down, float up) noexcept;
[[nodiscard]] std::uint32_t overlay_texture_width(std::uint32_t eye_width) noexcept;
[[nodiscard]] std::vector<std::uint32_t> rasterize_fps_overlay(
    std::uint32_t width, std::uint32_t height, const FpsSnapshot& snapshot, bool bgra);

} // namespace xrfg
