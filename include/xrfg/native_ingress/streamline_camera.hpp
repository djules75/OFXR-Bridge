#pragma once
#include <array>
#include <cstdint>
namespace xrfg::native_ingress::streamline_camera {
struct Camera {
    std::uint32_t frame{}, viewport{};
    bool valid{}, inverted{}, ambiguous{};
    std::array<float,16> projection{}, inverse{}, previous{}, to_current{};
    std::array<float,3> position{}, right{}, up{}, forward{};
    float near_z{}, far_z{}, fov{};
    int camera_motion{-1};
};
Camera current() noexcept;
bool hooked() noexcept;
}
