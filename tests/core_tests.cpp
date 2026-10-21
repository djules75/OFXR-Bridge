#include "xrfg/image.hpp"
#include "xrfg/generation_backpressure.hpp"
#include "xrfg/pose.hpp"
#include "xrfg/release_spacing.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

[[nodiscard]] bool near(float left, float right, float epsilon = 1.0e-4F) {
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] xrfg::Quaternion yaw(float degrees) {
    const float half_angle = degrees * std::numbers::pi_v<float> / 360.0F;
    return {0.0F, std::sin(half_angle), 0.0F, std::cos(half_angle)};
}

}  // namespace

int main() {
    const xrfg::Pose start{yaw(0.0F), {0.0F, 1.0F, 2.0F}};
    const xrfg::Pose end{yaw(90.0F), {2.0F, 3.0F, 6.0F}};
    const xrfg::Pose middle = xrfg::interpolate_pose(start, end, 0.5F);

    expect(near(middle.position.x, 1.0F), "position x midpoint");
    expect(near(middle.position.y, 2.0F), "position y midpoint");
    expect(near(middle.position.z, 4.0F), "position z midpoint");
    expect(near(middle.orientation.y, std::sin(std::numbers::pi_v<float> / 8.0F)), "45-degree yaw sine");
    expect(near(middle.orientation.w, std::cos(std::numbers::pi_v<float> / 8.0F)), "45-degree yaw cosine");

    const auto same_rotation_other_sign = xrfg::slerp_shortest(yaw(30.0F), {
        -yaw(30.0F).x,
        -yaw(30.0F).y,
        -yaw(30.0F).z,
        -yaw(30.0F).w,
    }, 0.5F);
    expect(near(std::abs(same_rotation_other_sign.w), std::cos(std::numbers::pi_v<float> / 12.0F)),
           "quaternion sign uses shortest path");

    const xrfg::TimedPose timed_start{100, start};
    const xrfg::TimedPose timed_end{200, end};
    const auto timed_middle = xrfg::midpoint(timed_start, timed_end);
    expect(timed_middle.has_value() && timed_middle->time_ns == 150, "timestamp midpoint");
    expect(!xrfg::midpoint(timed_end, timed_start).has_value(), "reject non-monotonic timestamps");

    const xrfg::Rgba8 black{0, 0, 0, 0};
    const xrfg::Rgba8 white{255, 255, 255, 255};
    const auto gray = xrfg::interpolate_pixel(black, white, 0.5F);
    expect(gray.r == 128 && gray.g == 128 && gray.b == 128 && gray.a == 128, "pixel midpoint rounds correctly");

    using namespace std::chrono_literals;
    expect(!xrfg::should_enter_generation_cooldown(false, 200ms, 8'333'333),
            "FidelityFX ignores compositor end spikes");
    expect(!xrfg::should_enter_generation_cooldown(true, 50ms, 8'333'333),
           "NVIDIA tolerates low-frame-rate compositor pacing");
    expect(!xrfg::should_enter_generation_cooldown(
               true, 75ms, 8'333'333),
           "NVIDIA cooldown has a strict 75 ms minimum threshold");
    expect(xrfg::should_enter_generation_cooldown(true, 75'000'001ns, 8'333'333),
           "NVIDIA retains recovery above the 75 ms minimum threshold");
    expect(!xrfg::should_enter_generation_cooldown(true, 83ms, 13'888'889),
           "NVIDIA also requires more than six display periods");
    expect(xrfg::should_enter_generation_cooldown(true, 84ms, 13'888'889),
           "NVIDIA retains recovery beyond six display periods");
    expect(!xrfg::should_enter_generation_cooldown(true, 68ms, 0),
           "invalid display period cannot start cooldown");

    constexpr std::array uevr_slow_ends{
        68'522us, 18'226us, 22'262us, 19'471us, 19'330us,
        19'863us, 19'893us, 19'440us, 22'551us};
    std::size_t uevr_nvidia_cooldowns = 0;
    for (const auto elapsed : uevr_slow_ends) {
        uevr_nvidia_cooldowns += xrfg::should_enter_generation_cooldown(
            true, elapsed, 8'333'333);
    }
    expect(uevr_nvidia_cooldowns == 0,
           "retained low-rate UEVR timing replay causes no cooldowns");

    // The 3X floor under the once-per-frame hold (release_serial_floor).
    expect(xrfg::release_serial_floor(2, true, false, false, 10) == 0,
           "2X: a prime takes no floor");
    expect(xrfg::release_serial_floor(2, true, true, true, 10) == 0,
           "2X: the pair after a prime takes no floor");
    expect(xrfg::release_serial_floor(3, true, true, false, 10) == 0,
           "3X: a pair with the floor disarmed keeps its pop phase");
    expect(xrfg::release_serial_floor(3, true, false, false, 10) == 13,
           "3X: a prime is held to the 3X cadence");
    expect(xrfg::release_serial_floor(3, true, true, true, 13) == 16,
           "3X: a pair with the floor armed is held to the 3X cadence");
    expect(xrfg::release_serial_floor(3, true, false, true, 13) == 16,
           "3X: a prime after a prime is held to the 3X cadence");
    expect(!xrfg::release_floor_armed_after(2, true, false, false, false),
           "2X: a prime does not arm the floor");
    expect(xrfg::release_floor_armed_after(3, true, false, false, false),
           "3X: a prime arms the floor");
    expect(xrfg::release_floor_armed_after(3, true, true, true, true),
           "3X: a pair the floor held back keeps it armed");
    expect(!xrfg::release_floor_armed_after(3, true, true, true, false),
           "3X: a pair whose own target met the floor disarms it");
    expect(!xrfg::release_floor_armed_after(3, true, true, false, false),
           "3X: a pair never arms the floor");
    // Where the hold waits for a pop there is no floor (issue #43).
    expect(xrfg::release_serial_floor(3, false, false, false, 10) == 0,
           "3X pop hold: a prime takes no floor");
    expect(xrfg::release_serial_floor(3, false, true, true, 13) == 0,
           "3X pop hold: a pair takes no floor even with it armed");
    expect(!xrfg::release_floor_armed_after(3, false, false, false, false),
           "3X pop hold: a prime does not arm the floor");
    expect(!xrfg::release_floor_armed_after(3, false, true, true, true),
           "3X pop hold: a pair the floor held back does not keep it armed");
    // Issue #43: at depth 2 a pair's pop target is one pop after a prime's
    // release and two before the floor; armed, the floor held every pair
    // one pop late and re-armed itself for the rest of the session.
    {
        std::uint64_t served = 100;
        bool armed = xrfg::release_floor_armed_after(3, false, false, false, false);
        std::uint64_t held = 0;
        for (int frame = 0; frame < 30; ++frame) {
            const std::uint64_t floor =
                xrfg::release_serial_floor(3, false, true, armed, served);
            const std::uint64_t own = served + 1 + (frame == 0 ? 0 : 2);
            const bool held_back = floor > own;
            held += held_back ? 1 : 0;
            armed = xrfg::release_floor_armed_after(3, false, true, armed, held_back);
            served = held_back ? floor : own;
        }
        expect(held == 0 && !armed,
               "3X pop hold: a prime never holds the pairs after it late");
    }
    // Alternating failures: every frame costs three presenter frames.
    {
        std::uint64_t served = 0;
        bool armed = false;
        for (int frame = 0; frame < 12; ++frame) {
            const bool pairable = frame % 2 == 1;
            const std::uint64_t floor =
                xrfg::release_serial_floor(3, true, pairable, armed, served);
            // A pair's own count is three; a prime pops after one.
            const std::uint64_t own = served + (pairable ? 3 : 1);
            armed = xrfg::release_floor_armed_after(3, true, pairable, armed, floor > own);
            served = floor > own ? floor : own;
        }
        expect(served == 36,
               "3X: twelve frames alternating prime and pair take 36 presenter frames");
    }
    // The ResetXPDR dip (issue #33, V495 logs): after a prime the pairs'
    // own pop phase came round 1, 2, 2 presenter frames after the previous
    // release before it settled back to 3. Each is held to 3, and the floor
    // disarms on the first pair whose own phase is 3 away again.
    {
        const std::uint64_t own_spacing[] = {4, 1, 2, 2, 3, 3, 3};
        std::uint64_t served = 100;
        bool armed = false;
        std::uint64_t floored = 0;
        bool every_release_three_apart = true;
        for (std::size_t frame = 0; frame < std::size(own_spacing); ++frame) {
            const bool pairable = frame != 0;
            const std::uint64_t floor =
                xrfg::release_serial_floor(3, true, pairable, armed, served);
            const std::uint64_t own = served + own_spacing[frame];
            const std::uint64_t released = floor > own ? floor : own;
            if (released - served < 3) {
                every_release_three_apart = false;
            }
            if (floor != 0) {
                ++floored;
            }
            armed = xrfg::release_floor_armed_after(3, true, pairable, armed, floor > own);
            served = released;
        }
        expect(every_release_three_apart,
               "3X: after a prime no release comes within three presenter frames");
        expect(floored == 5,
               "3X: the floor covers the prime, the three early pairs and the first on time");
        expect(!armed, "3X: the floor disarms once the pop phase is back");
    }
    // Steady 3X never arms the floor.
    {
        std::uint64_t served = 0;
        bool armed = false;
        bool ever_floored = false;
        for (int frame = 0; frame < 30; ++frame) {
            const std::uint64_t floor =
                xrfg::release_serial_floor(3, true, true, armed, served);
            ever_floored = ever_floored || floor != 0;
            armed = xrfg::release_floor_armed_after(3, true, true, armed, false);
            served += 3;
        }
        expect(!ever_floored, "3X: steady pairs never take the floor");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All XR Frame Bridge core tests passed\n";
    return EXIT_SUCCESS;
}
