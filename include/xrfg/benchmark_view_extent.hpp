#pragma once
#include <openxr/openxr.h>
#include <algorithm>
#include <cstdint>

namespace xrfg {
// Actual primary stereo scene rectangles, never the enclosing packed texture,
// a quad overlay, the DLSS input or the AFW working resolution. Both eyes must
// meet the threshold, so keep the smaller of their longest sides.
inline std::uint32_t benchmark_eye_long_side(const XrFrameEndInfo* frame) noexcept {
    if(!frame || !frame->layers)return 0;
    for(std::uint32_t i=0;i<frame->layerCount;++i) {
        const auto* layer=frame->layers[i];
        if(!layer || layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
        const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if(!projection->views || projection->viewCount<2)continue;
        std::uint32_t side=0;
        for(std::uint32_t eye=0;eye<projection->viewCount;++eye) {
            const auto extent=projection->views[eye].subImage.imageRect.extent;
            if(extent.width<=0 || extent.height<=0)return 0;
            const auto longest=static_cast<std::uint32_t>(std::max(extent.width,extent.height));
            side=side?std::min(side,longest):longest;
        }
        return side;
    }
    return 0;
}
}
