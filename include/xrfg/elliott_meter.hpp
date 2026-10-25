#pragma once
#include "xrfg/pose.hpp"
#include "xrfg/autonomous_control.hpp"
#include <string>
#include <vector>
namespace xrfg {
inline constexpr float kFlipAngleDegrees = 120.0F;
inline constexpr std::int64_t kFlipShowDelayNs = 250'000'000;
inline constexpr std::int64_t kFlipHideDelayNs = 500'000'000;

// Whether a grip pose, located in a space whose +Y is up (LOCAL or STAGE),
// is upside down: its own +Y, the controller's up, turned past the angle.
[[nodiscard]] bool grip_upside_down(
    const Quaternion& grip, float angle_degrees = kFlipAngleDegrees) noexcept;

class FlipGesture {
public:
    // One look at the controllers: whether each is tracked and upside down.
    // Returns whether the panel is shown.
    bool observe(std::int64_t now_ns, bool left_flipped, bool right_flipped) noexcept;
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    // A controller is upside down and the panel is not up yet: the caller
    // may get its content ready, so it appears with something on it.
    [[nodiscard]] bool pending() const noexcept { return !visible_ && flipped_since_ >= 0; }
    // The hand the panel sits on (0 left, 1 right, -1 none): the one that
    // was turned, kept while it stays turned even if the other one is turned
    // as well, and through the hide delay once it is not.
    [[nodiscard]] int hand() const noexcept { return hand_; }
    void reset() noexcept { *this = FlipGesture{}; }

private:
    std::int64_t flipped_since_{-1};
    std::int64_t upright_since_{-1};
    int hand_{-1};
    bool visible_{};
};

// Where the panel sits on a flipped controller, in the grip's own space
// turned into the space the grip was located in. xrFPS's placement: turned
// half a turn about the controller's length, which stands it upright facing
// back along the controller at whoever holds it, and lifted clear of the
// controller by half its height and a margin.
[[nodiscard]] Pose panel_pose_on_grip(const Pose& grip, float panel_height_m) noexcept;


enum class PanelTone : std::uint8_t { normal, dim, good, warn, bad, accent };
inline constexpr unsigned kElliottMeterWidth=1024, kElliottMeterHeight=512;
[[nodiscard]] std::string elliott_benchmark_text(const autonomous::Snapshot&,std::uint64_t now_ms);
std::vector<std::uint32_t> rasterize_elliott_meter(const autonomous::Snapshot&, std::uint64_t now_ms,
    bool bgra, bool linear, std::uint32_t* used_height=nullptr);
} // namespace xrfg
