#pragma once
#include <dxgiformat.h>
namespace xrfg {
inline constexpr DXGI_FORMAT kLukeAfwMotionFormat = DXGI_FORMAT_R16G16_FLOAT;
// Only an original NGX SR/RR MotionVectors input supplies the float-XY contract.
// This selects the format of an owned snapshot, never an arbitrary source SRV.
[[nodiscard]] constexpr DXGI_FORMAT dlss_motion_snapshot_format(
    DXGI_FORMAT format, bool float_xy_receipt) noexcept {
    return float_xy_receipt && format == DXGI_FORMAT_R16G16_TYPELESS
        ? DXGI_FORMAT_R16G16_FLOAT : format;
}
// Motion is XY of a typed float2/float4 or signed-normalized float2 SRV.
// SNORM sampling exposes its native signed normalized values; extra ZW channels
// are not motion. Never reinterpret typeless, integer or UNORM data.
[[nodiscard]] constexpr DXGI_FORMAT luke_motion_srv_format(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return format;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
} // namespace xrfg
