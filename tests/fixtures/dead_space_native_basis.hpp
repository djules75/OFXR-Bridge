#pragma once
#include "xrfg/luke_pose_camera.hpp"
// V680/PID30460 F8 publications2436/2437. The verified original builder
// source stores native view-back; our recovered camera supplies its negative
// as physical forward. Keep that exact convention at the AFW scene boundary.
namespace xrfg::tests {
struct DeadSpaceBasisFixture { LukeCameraBasis producer; std::array<float,3> native_back; float perspective_w; };
inline const auto dead_space_basis_fixtures=[] {
    std::array<DeadSpaceBasisFixture,2> samples{};
    samples[0].producer={{{0.0274704136F,0.0154228173F,0.999503791F},{0.114170372F,0.993289769F,-0.0184648F},{0.99308151F,-0.114620939F,-0.0255252682F}}};
    samples[0].native_back={-0.99308151F,0.114620939F,0.0255252682F};
    samples[0].perspective_w=-1.0F;
    samples[1].producer={{{0.0269729029F,0.015283674F,0.999519587F},{0.117121577F,0.992948413F,-0.0183438174F},{0.992751479F,-0.117560059F,-0.0249926914F}}};
    samples[1].native_back={-0.992751479F,0.117560059F,0.0249926914F};
    samples[1].perspective_w=-1.0F;
    return samples;
}();
} // namespace xrfg::tests
