#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace xrfg {

// One clock for all SteamVR outputs. Units are QPC nanoseconds. It never
// borrows time from the following refresh to catch up after a late output.
class PrivateOutputSchedule {
    std::int64_t period_{}, due_{}, last_{}, hold_{}, refill_{};
    bool primed_{};
public:
    // Prepare metadata/optional logging ahead of the handoff, without moving
    // its clock or subtracting a margin from every physical refresh.
    static constexpr std::int64_t preparation_lead=100'000;
    void period(std::int64_t value) noexcept {
        if(value<2'000'000 || value>50'000'000)return;
        period_=value;
        if(primed_)due_=std::max(due_,last_+period_);
    }
    [[nodiscard]] std::int64_t period() const noexcept {return period_;}
    [[nodiscard]] std::int64_t delay() const noexcept {return hold_;}
    [[nodiscard]] std::int64_t deadline() const noexcept {return due_;}
    [[nodiscard]] std::int64_t preparation_deadline() const noexcept {
        return std::max(std::int64_t{},due_-preparation_lead);
    }
    [[nodiscard]] bool primed() const noexcept {return primed_;}
    // A source underrun needs an actual refill window, not just an immediate
    // late handoff with no ready lead left. Add its exact deficit as margin
    // once AFTER this late frame; the following deadlines retain that phase.
    // Do not age this frame's already selected runtime cycle with a refill.
    // The bounded pool
    // limits useful refill time (a stopped/paused application must not turn
    // into an arbitrarily long replay pause). This runs only at consumption;
    // no producer or GPU operation waits here.
    std::int64_t input_ready(std::int64_t at,std::size_t capacity) noexcept {
        if(!primed_ || at<=due_ || capacity<2)return 0;
        const auto deficit=at-due_;
        const auto refill=std::min(deficit,period_*static_cast<std::int64_t>(capacity-1));
        hold_+=deficit;due_=at;refill_=std::max(refill_,refill);
        return refill;
    }
    // Invoke once the FIFO head's exact GPU completion has been observed.
    // ready is the completion-observation timestamp, not a rounded XR slot.
    void ready(std::int64_t at) noexcept {
        if(!primed_){due_=at;primed_=true;return;}
        if(at>due_){hold_+=at-due_;due_=at;}
    }
    [[nodiscard]] bool eligible(std::int64_t now) const noexcept {
        return primed_ && period_>0 && now>=due_ && (!last_ || now-last_>=period_);
    }
    // at is the actual beginning of the runtime handoff. This also accounts
    // for a late wakeup/long prior runtime call, without a short next interval.
    bool delivered(std::int64_t at) noexcept {
        if(!eligible(at))return false;
        hold_+=std::max(std::int64_t{},at-due_)+refill_;
        last_=at;due_=at+period_+refill_;refill_=0;
        return true;
    }
};

// A slot is reserved BEFORE its producer records/writes the private image.
// Accepted entries and reservations both occupy capacity. pop() transfers a
// slot to the preparation/presentation pipeline; only retire() after delivery
// makes it writable by a producer again.
// Thus dequeue never authorizes overwrite of an image still being copied.
template<class T> class PrivateOutputFifo {
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<T> ready_;
    std::size_t capacity_,occupied_{},reserved_{},inflight_{};
    bool stopped_{};
public:
    explicit PrivateOutputFifo(std::size_t capacity):capacity_(capacity){}
    PrivateOutputFifo(const PrivateOutputFifo&)=delete;
    PrivateOutputFifo& operator=(const PrivateOutputFifo&)=delete;
    bool reserve(std::size_t count=1) {
        std::unique_lock lock(mutex_);
        if(!count || count>capacity_)return false;
        changed_.wait(lock,[&]{return stopped_ || occupied_<=capacity_-count;});
        if(stopped_)return false;
        occupied_+=count;reserved_+=count;return true;
    }
    bool accept(T value) {
        {std::scoped_lock lock(mutex_);if(stopped_ || !reserved_)return false;
         ready_.push_back(std::move(value));--reserved_;}
        changed_.notify_all();return true;
    }
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        changed_.wait(lock,[&]{return stopped_ || !ready_.empty();});
        if(stopped_)return {};
        T value=std::move(ready_.front());ready_.pop_front();++inflight_;return value;
    }
    void retire(std::size_t count=1) {
        {std::scoped_lock lock(mutex_);count=std::min(count,inflight_);inflight_-=count;occupied_-=count;}
        changed_.notify_all();
    }
    void cancel(std::size_t count=1) {
        {std::scoped_lock lock(mutex_);count=std::min(count,reserved_);reserved_-=count;occupied_-=count;}
        changed_.notify_all();
    }
    void stop() {
        {std::scoped_lock lock(mutex_);stopped_=true;}
        changed_.notify_all();
    }
    [[nodiscard]] std::size_t occupied() const {std::scoped_lock lock(mutex_);return occupied_;}
    [[nodiscard]] std::size_t queued() const {std::scoped_lock lock(mutex_);return ready_.size();}
};
} // namespace xrfg
