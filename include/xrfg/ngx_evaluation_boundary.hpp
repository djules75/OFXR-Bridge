#pragma once

namespace xrfg {
// Generic NGX/engine/injector dispatch can surround the native SR/RR call and
// copy its output before returning. Observe at the native body when available.
// Keep generic-only implementations observable and suppress recursive aliases.
class NgxEvaluationBoundary {
    inline static thread_local NgxEvaluationBoundary* current_{};
    NgxEvaluationBoundary* parent_{};
    unsigned slot_{};
    bool owns_{}, recursive_{}, public_guides_{};
public:
    NgxEvaluationBoundary(unsigned slot, bool typed_native, bool native_available,
        bool public_guides = false) noexcept
        : parent_(current_), slot_(slot), public_guides_(public_guides) {
        bool occupied = false;
        for (auto* p = parent_; p; p = p->parent_) {
            occupied |= p->owns_;
            if (p->owns_ && p->slot_ == slot) p->recursive_ = true;
        }
        owns_ = (typed_native || !native_available) && !occupied;
        current_ = this;
    }
    ~NgxEvaluationBoundary() { current_ = parent_; }
    NgxEvaluationBoundary(const NgxEvaluationBoundary&) = delete;
    NgxEvaluationBoundary& operator=(const NgxEvaluationBoundary&) = delete;
    bool observes() const noexcept { return owns_; }
    bool outermost() const noexcept { return parent_==nullptr; }
    bool public_guide_parent() const noexcept {
        for(auto* p=parent_;p;p=p->parent_)if(p->owns_&&p->public_guides_)return true;
        return false;
    }
    bool can_complete() const noexcept { return owns_ && !recursive_; }
};
}
