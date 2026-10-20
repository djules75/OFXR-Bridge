#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace xrfg {
struct FrameProductionStart {
    std::uint64_t owner{}, serial{};
    std::int64_t at{};
    explicit operator bool() const noexcept { return owner && serial && at>0; }
};

// A producer timestamp is useful only if the submitted stereo projection can
// identify it. Never borrow the latest engine tick from another pipeline frame.
class FrameProductionTracker {
    struct Entry {
        FrameProductionStart start;
        XrTime target{};
        XrSpace space{};
        std::array<XrPosef,2> poses{};
    };
    mutable std::mutex mutex_;
    std::array<Entry,128> entries_{};
    std::size_t next_{};
public:
    void locate(FrameProductionStart start,const XrViewLocateInfo& info,
                const XrView* views,std::uint32_t count) noexcept {
        if(!start || !views || count!=2 || info.displayTime<=0)return;
        std::scoped_lock lock(mutex_);
        entries_[next_++%entries_.size()]={start,info.displayTime,info.space,{views[0].pose,views[1].pose}};
    }
    FrameProductionStart match(const XrFrameEndInfo& frame,std::int64_t entered) const noexcept {
        if(!frame.layers || entered<=0)return {};
        const XrCompositionLayerProjection* projection{};
        for(std::uint32_t i=0;i<frame.layerCount;++i) {
            if(!frame.layers[i] || frame.layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
            if(projection)return {}; // Multiple scene projections are ambiguous.
            projection=reinterpret_cast<const XrCompositionLayerProjection*>(frame.layers[i]);
        }
        if(!projection || projection->viewCount!=2 || !projection->views)return {};
        std::scoped_lock lock(mutex_);
        FrameProductionStart found;
        for(const auto& e:entries_) {
            if(!e.start || e.target!=frame.displayTime || e.space!=projection->space ||
                e.start.at>entered || entered-e.start.at>2'000'000'000LL)continue;
            if(std::memcmp(&e.poses[0],&projection->views[0].pose,sizeof(XrPosef)) ||
                std::memcmp(&e.poses[1],&projection->views[1].pose,sizeof(XrPosef)))continue;
            if(found && (found.owner!=e.start.owner || found.serial!=e.start.serial))return {};
            found=e.start;
        }
        return found;
    }
};
} // namespace xrfg
