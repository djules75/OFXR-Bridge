#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>

namespace xrfg {
// QPC nanoseconds on Windows. Display-time conversion uses this same domain.
std::int64_t interface_clock_ns() noexcept;
std::int64_t interface_qpc_ns(std::int64_t ticks) noexcept;

// One app Wait/Begin/End ticket. It travels with the retained real, rather
// than sampling whatever newer frame is running on the producer thread.
class InterfaceCycle {
    mutable std::mutex mutex_;
    struct Interval { std::int64_t first{}, last{}; };
    std::array<Interval, 64> intervals_{};
    std::size_t count_{};
    bool valid_{true};
    std::int64_t producer_returned_{};
    struct SyntheticReceipt { std::int64_t accepted{}, period{}; };
    std::array<SyntheticReceipt,4> synthetic_{};
    std::size_t synthetic_count_{};
    bool estimate_valid_{true};
public:
    const std::int64_t started_ns;
    explicit InterfaceCycle(std::int64_t started) noexcept : started_ns(started) {}
    void call(std::int64_t first, std::int64_t last) noexcept {
        std::scoped_lock lock(mutex_);
        if (!first || last < first || first < started_ns) { valid_=false; return; }
        // Union overlapping eye-thread calls. Their wall time is counted once.
        for (std::size_t i=0;i<count_;) {
            if (last < intervals_[i].first || first > intervals_[i].last) { ++i; continue; }
            first=std::min(first,intervals_[i].first);last=std::max(last,intervals_[i].last);
            intervals_[i]=intervals_[--count_];i=0;
        }
        if (count_==intervals_.size()) { valid_=false; return; }
        intervals_[count_++]={first,last};
    }
    void producer_returned(std::int64_t at) noexcept {
        std::scoped_lock lock(mutex_);producer_returned_=at;
    }
    // Observation only: no sleep, queue operation or GPU synchronization.
    // Pending native results can confirm receipts later and out of order.
    void synthetic_accepted(std::int64_t at, std::int64_t period) noexcept {
        std::scoped_lock lock(mutex_);
        if (at<started_ns || period<2'000'000 || period>50'000'000) return;
        for(std::size_t i=0;i<synthetic_count_;++i) if(synthetic_[i].accepted==at)return;
        if(synthetic_count_==synthetic_.size()) {estimate_valid_=false;return;}
        synthetic_[synthetic_count_++]={at,period};
        std::sort(synthetic_.begin(),synthetic_.begin()+synthetic_count_,
            [](const auto& a,const auto& b){return a.accepted<b.accepted;});
    }
    bool read_estimated(std::int64_t accepted_ns, float& estimated_ms) const noexcept {
        std::scoped_lock lock(mutex_);
        if(!valid_ || !estimate_valid_ || !synthetic_count_ || !started_ns ||
            producer_returned_<started_ns || accepted_ns<synthetic_[synthetic_count_-1].accepted)return false;
        const auto origin=synthetic_[0].accepted,period=synthetic_[0].period;
        // Do not combine observations across a refresh change.
        for(std::size_t i=1;i<synthetic_count_;++i)if(synthetic_[i].period!=period)return false;
        const auto slots=[&](std::int64_t at){const auto d=at-origin;return d/period+(d%period!=0);};
        std::int64_t slot{};
        for(std::size_t i=1;i<synthetic_count_;++i)slot=std::max(slot+1,slots(synthetic_[i].accepted));
        slot=std::max(slot+1,slots(accepted_ns));
        const auto estimated_real=origin+slot*period;
        estimated_ms=float(std::max(estimated_real,producer_returned_)-started_ns)*1e-6F;
        return std::isfinite(estimated_ms);
    }
    bool read(std::int64_t accepted_ns, float& ofxr_ms, float& cycle_ms) const noexcept {
        std::scoped_lock lock(mutex_);
        if (!valid_ || !started_ns || !producer_returned_ || accepted_ns<started_ns ||
            producer_returned_<started_ns) return false;
        std::int64_t held{};for(std::size_t i=0;i<count_;++i)held+=intervals_[i].last-intervals_[i].first;
        ofxr_ms=float(held)*1e-6F;
        cycle_ms=float(std::max(producer_returned_,accepted_ns)-started_ns)*1e-6F;
        return true;
    }
};
// Construct before call locks; destruct after them and all frame teardown.
class InterfaceCallTiming {
    std::int64_t started_{};
    bool terminal_{};
public:
    std::shared_ptr<InterfaceCycle> cycle;
    InterfaceCallTiming(std::int64_t started, bool terminal=false) noexcept : started_(started),terminal_(terminal) {}
    std::int64_t started_ns() const noexcept { return started_; }
    ~InterfaceCallTiming() {
        if (!cycle) return;
        const auto ended=interface_clock_ns();cycle->call(started_,ended);
        if(terminal_)cycle->producer_returned(ended);
    }
};

struct InterfaceFrameStamp {
    std::uint64_t serial{};
    std::int64_t entered_ns{}, begun_ns{};
    std::shared_ptr<InterfaceCycle> cycle;
    // Exact source identity, independent of XR calls/slots; zero is unknown.
    std::uint64_t scene_owner{},scene_serial{};
};
enum class InterfaceSource : std::uint32_t { none, steamvr_pre_submit_gpu, real_frame_qpc, predicted_display_qpc, application_cpu_qpc, steamvr_display, openxr_period, accepted_submissions, steamvr_delivery, producer_call_qpc, frame_cycle_qpc, frame_cycle_slot_estimate, steamvr_scene_delivery };
struct InterfaceTelemetry {
    std::uint64_t updated_ms{};
    float output_fps{}, game_fps{}, generated_fps{}, display_hz{};
    float gpu_render_ms{}, ofxr_delay_ms{}, presentation_ms{}, cpu_render_ms{};
    float ofxr_time_ms{}, cycle_length_ms{};
    bool rates_valid{}, generating{};
    bool gpu_valid{}, delay_valid{}, presentation_valid{}, cpu_valid{};
    bool ofxr_time_valid{}, cycle_valid{};
    InterfaceSource gpu_source{}, delay_source{}, presentation_source{}, cpu_source{}, display_source{}, output_source{};
    InterfaceSource ofxr_time_source{}, cycle_source{};
    float cycle_estimated_ms{};
    bool cycle_estimated_valid{};
    InterfaceSource cycle_estimated_source{};
};

// Caller serializes access. Fixed storage; only the first successful real
// submission contributes. Failed attempts do not consume identity, and delayed
// repeats cannot become another latency sample after the time window expires.
class InterfaceTimingWindow {
    struct Sample {
        std::int64_t at{};
        float delay{}, presentation{}, cpu{};
        bool display_valid{}, cpu_valid{};
        std::int64_t accepted_ns{};
        std::shared_ptr<InterfaceCycle> cycle;
    };
    std::array<Sample, 256> samples_{};
    std::array<std::uint64_t, 256> identities_{};
    std::size_t next_{}, identity_next_{};
public:
    bool submitted(InterfaceFrameStamp stamp, std::int64_t now,
        bool success, bool nonempty, bool synthetic, bool fresh,
        std::int64_t display_ns = 0, std::int64_t accepted_ns = 0) noexcept {
        if (!success || !nonempty || synthetic || !fresh || !stamp.serial ||
            !stamp.entered_ns || now < stamp.entered_ns) return false;
        if (std::find(identities_.begin(), identities_.end(), stamp.serial) != identities_.end()) return false;
        identities_[identity_next_++ % identities_.size()] = stamp.serial;
        auto& s = samples_[next_++ % samples_.size()];
        s = {now, float(now - stamp.entered_ns) * 1e-6F,
            float(display_ns - now) * 1e-6F,
            float(stamp.entered_ns - stamp.begun_ns) * 1e-6F,
            display_ns > 0, stamp.begun_ns > 0 && stamp.begun_ns <= stamp.entered_ns,
            accepted_ns ? accepted_ns : now, std::move(stamp.cycle)};
        return true;
    }
    void snapshot(std::int64_t now, InterfaceTelemetry& out) const noexcept {
        std::array<float, 256> delay{}, presentation{}, cpu{};
        std::array<float,256> ofxr{},cycle{};
        std::array<float,256> estimated{};
        std::size_t nd{}, np{}, nc{}, nt{}, ne{};
        for (const auto& s : samples_) {
            if (!s.at || now < s.at || now - s.at > 1'000'000'000) continue;
            delay[nd++] = s.delay;
            if (s.display_valid) presentation[np++] = s.presentation;
            if (s.cpu_valid) cpu[nc++] = s.cpu;
            if(s.cycle && s.cycle->read(s.accepted_ns,ofxr[nt],cycle[nt]))++nt;
            if(s.cycle && s.cycle->read_estimated(s.accepted_ns,estimated[ne]))++ne;
        }
        const auto median = [](auto& values, std::size_t count) {
            std::sort(values.begin(), values.begin() + count);
            return count % 2 ? values[count/2] : (values[count/2-1] + values[count/2]) * .5F;
        };
        out.delay_valid = nd != 0; if (nd) out.ofxr_delay_ms = median(delay, nd);
        out.presentation_valid = np != 0; if (np) out.presentation_ms = median(presentation, np);
        out.cpu_valid = nc != 0; if (nc) out.cpu_render_ms = median(cpu, nc);
        out.ofxr_time_valid=out.cycle_valid=nt!=0;
        if(nt) {out.ofxr_time_ms=median(ofxr,nt);out.cycle_length_ms=median(cycle,nt);}
        out.ofxr_time_source=nt?InterfaceSource::producer_call_qpc:InterfaceSource::none;
        out.cycle_source=nt?InterfaceSource::frame_cycle_qpc:InterfaceSource::none;
        out.cycle_estimated_valid=ne!=0;
        if(ne)out.cycle_estimated_ms=median(estimated,ne);
        out.cycle_estimated_source=ne?InterfaceSource::frame_cycle_slot_estimate:InterfaceSource::none;
        out.delay_source = nd ? InterfaceSource::real_frame_qpc : InterfaceSource::none;
        out.presentation_source = np ? InterfaceSource::predicted_display_qpc : InterfaceSource::none;
        out.cpu_source = nc ? InterfaceSource::application_cpu_qpc : InterfaceSource::none;
    }
};
} // namespace xrfg
