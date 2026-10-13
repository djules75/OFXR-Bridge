#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace xrfg {
// Publications must come from CRC-validated tokens in these exact two colors.
// Release order and DLSS preset do not identify the new AER source. Equal fresh
// publications in different eye colors are ambiguous and must not select an eye.
inline std::optional<std::size_t> select_luke_afw_color_source(
    const std::array<std::uint64_t,2>& publications,std::uint64_t last) noexcept {
    const bool a=publications[0]>last,b=publications[1]>last;
    if(!a&&!b)return {};
    if(a&&b&&publications[0]==publications[1])return {};
    return a&&(!b||publications[0]>publications[1])?0u:1u;
}
}
