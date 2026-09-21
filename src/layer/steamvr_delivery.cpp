#include "xrfg/steamvr_delivery.hpp"

#include "xrfg/bridge_flight_logger.hpp"

#include <openvr.h>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace xrfg {
namespace {

using PfnInitInternal2 =
    std::uint32_t(__cdecl*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
using PfnShutdownInternal = void(__cdecl*)();
using PfnGetGenericInterface = void*(__cdecl*)(const char*, vr::EVRInitError*);
using PfnGetInitToken = std::uint32_t(__cdecl*)();

// The runtime registers itself here rather than in the registry, and this is
// where SteamVR itself looks. Parsed by hand: a JSON dependency for one string
// would be the larger cost. Deliberately duplicated from the standalone probe,
// which must keep building with no layer dependency at all.
[[nodiscard]] std::string runtime_path_from_vrpath() {
    char local[MAX_PATH]{};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH) == 0) {
        return {};
    }
    const std::string file = std::string(local) + "\\openvr\\openvrpaths.vrpath";
    FILE* handle = nullptr;
    if (fopen_s(&handle, file.c_str(), "rb") != 0 || handle == nullptr) {
        return {};
    }
    std::string text;
    char buffer[4096];
    for (;;) {
        const std::size_t read = fread(buffer, 1, sizeof(buffer), handle);
        if (read == 0) {
            break;
        }
        text.append(buffer, read);
    }
    fclose(handle);

    const std::size_t key = text.find("\"runtime\"");
    if (key == std::string::npos) {
        return {};
    }
    const std::size_t open = text.find('[', key);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t first = text.find('"', open);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find('"', first + 1);
    if (last == std::string::npos) {
        return {};
    }
    std::string raw = text.substr(first + 1, last - first - 1);
    std::string path;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
            path.push_back('\\');
            ++i;
        } else {
            path.push_back(raw[i]);
        }
    }
    return path;
}

// How the compositor interface was obtained, recorded so a capture says which
// path worked rather than only that one did.
enum class AttachRoute : std::uint64_t {
    none = 0,
    // The process already had an OpenVR context and we borrowed it. Costs
    // nothing and adds no state, so it is tried first.
    existing_token = 1,
    // A second, background connection of our own. Never Scene: this must not
    // become the application the compositor is serving, and must not start
    // SteamVR if it is not already running.
    background_init = 2,
};

constexpr std::int64_t kWindowNanoseconds = 1'000'000'000;
// Past this the last window is too old to describe what is on screen now, so
// report nothing rather than something stale.
constexpr std::int64_t kStaleNanoseconds = 5'000'000'000;

} // namespace

struct SteamVrDelivery::Impl {
    std::mutex mutex;
    bool steamvr{};
    bool attempted{};
    bool usable{};
    HMODULE module{};
    PfnShutdownInternal shutdown{};
    vr::IVRCompositor* compositor{};
    // Held separately because GetTimeSinceLastVsync is on IVRSystem, not the
    // compositor. Its absence is not fatal: the delivered rate still works.
    vr::IVRSystem* system{};
    AttachRoute route{AttachRoute::none};
    bool owns_context{};

    float delivered{};
    std::int64_t delivered_at_ns{-1};

    std::uint32_t last_frame_index{};
    bool reported_frames{};

    // Distinct frames the headset received, counted on the compositor's own
    // vsync-aligned clock.
    //
    // The cumulative counters cannot give this and neither can the frame index
    // alone. m_nNumNumReprojectedFrames counts routine reprojection, not loss:
    // MSFS 2024 predicts two frames ahead and marks every frame reprojected
    // while the headset receives all 90, so presents minus dropped minus
    // reprojected reads zero through a healthy session. The index rate fixes
    // that but misses the other shape of loss - two of our submissions landing
    // in one scanout window, where the compositor still produces a frame per
    // scanout and the index keeps advancing at 90 while the headset shows 45.
    //
    // What separates the two is m_nNumMisPresented against how far ahead the
    // compositor says it is predicting. Landing on a vsync other than the one
    // first predicted is expected when it predicts ahead and a fault when it
    // does not:
    //
    //   Callisto healthy   mispres   0%   predicted 0   ->  90, matches fpsVR
    //   Callisto dropping  mispres  49%   predicted 0   ->  46, matches 45-40
    //   MSFS 2024          mispres 100%   predicted 2   ->  90, matches fpsVR
    //
    // With one exception. The frame after a repeat is mispresented *because of*
    // the repeat: it arrives a period late and so lands on a vsync other than
    // the one predicted. The index rate has already subtracted that scanout,
    // because a repeat holds its index instead of advancing it, so charging the
    // successor as well counts one lost frame twice. Where every frame is
    // repeated - MSFS holding 45 on a 90 Hz display - that takes the rate to
    // zero. The mispresent therefore only counts when the previous distinct
    // frame was scanned out once; past that the index rate covers it.
    std::uint32_t rate_first_index{};
    double rate_first_time{};
    std::uint32_t rate_last_index{};
    double rate_last_time{};
    std::uint32_t rate_seen{};
    std::uint32_t rate_lost{};
    // Scanouts the previous distinct frame occupied. One means this frame is as
    // early as it could be, so a mispresent here is a real displacement.
    std::uint32_t rate_previous_presents{1};
    bool rate_started{};

    void observe(const vr::Compositor_FrameTiming& timing) noexcept {
        if (!rate_started || timing.m_nFrameIndex < rate_last_index) {
            rate_first_index = rate_last_index = timing.m_nFrameIndex;
            rate_first_time = rate_last_time = timing.m_flSystemTimeInSeconds;
            rate_seen = rate_lost = 0;
            rate_previous_presents = timing.m_nNumFramePresents;
            rate_started = true;
            return;
        }
        if (timing.m_nFrameIndex <= rate_last_index) {
            return;
        }
        rate_last_index = timing.m_nFrameIndex;
        rate_last_time = timing.m_flSystemTimeInSeconds;
        ++rate_seen;
        const std::uint32_t predicted =
            (timing.m_nReprojectionFlags & vr::VRCompositor_PredictionMask) >> 4;
        if (rate_previous_presents <= 1 &&
            timing.m_nNumMisPresented > predicted) {
            ++rate_lost;
        }
        rate_previous_presents = timing.m_nNumFramePresents;
    }

    void attach() noexcept {
        attempted = true;
        if (!steamvr) {
            return;
        }
        const std::string runtime = runtime_path_from_vrpath();
        if (runtime.empty()) {
            bridge_flight_logger().event(
                BridgeFlightOperation::steamvr_delivery_attach, -1);
            return;
        }
        const std::string dll = runtime + "\\bin\\win64\\openvr_api.dll";
        module = LoadLibraryA(dll.c_str());
        if (module == nullptr) {
            bridge_flight_logger().event(
                BridgeFlightOperation::steamvr_delivery_attach,
                -2,
                GetLastError());
            return;
        }
        const auto generic = reinterpret_cast<PfnGetGenericInterface>(
            reinterpret_cast<void*>(
                GetProcAddress(module, "VR_GetGenericInterface")));
        const auto token = reinterpret_cast<PfnGetInitToken>(
            reinterpret_cast<void*>(GetProcAddress(module, "VR_GetInitToken")));
        if (generic == nullptr) {
            bridge_flight_logger().event(
                BridgeFlightOperation::steamvr_delivery_attach, -3);
            return;
        }

        // Borrow an existing context before making one. A second connection
        // from a process that is already a scene application is the risk in
        // this whole path, so it is the fallback rather than the first move.
        vr::EVRInitError error = vr::VRInitError_None;
        if (token != nullptr && token() != 0) {
            compositor = static_cast<vr::IVRCompositor*>(
                generic(vr::IVRCompositor_Version, &error));
            if (compositor != nullptr) {
                route = AttachRoute::existing_token;
            }
        }
        if (compositor == nullptr) {
            const auto init = reinterpret_cast<PfnInitInternal2>(
                reinterpret_cast<void*>(
                    GetProcAddress(module, "VR_InitInternal2")));
            shutdown = reinterpret_cast<PfnShutdownInternal>(
                reinterpret_cast<void*>(
                    GetProcAddress(module, "VR_ShutdownInternal")));
            if (init == nullptr || shutdown == nullptr) {
                bridge_flight_logger().event(
                    BridgeFlightOperation::steamvr_delivery_attach, -4);
                return;
            }
            error = vr::VRInitError_None;
            init(&error, vr::VRApplication_Background, nullptr);
            if (error != vr::VRInitError_None) {
                bridge_flight_logger().event(
                    BridgeFlightOperation::steamvr_delivery_attach,
                    -5,
                    static_cast<std::uint64_t>(error));
                shutdown = nullptr;
                return;
            }
            owns_context = true;
            error = vr::VRInitError_None;
            compositor = static_cast<vr::IVRCompositor*>(
                generic(vr::IVRCompositor_Version, &error));
            route = AttachRoute::background_init;
        }
        if (compositor == nullptr) {
            bridge_flight_logger().event(
                BridgeFlightOperation::steamvr_delivery_attach,
                -6,
                static_cast<std::uint64_t>(error));
            return;
        }
        error = vr::VRInitError_None;
        system = static_cast<vr::IVRSystem*>(
            generic(vr::IVRSystem_Version, &error));
        usable = true;
        bridge_flight_logger().event(
            BridgeFlightOperation::steamvr_delivery_attach,
            0,
            static_cast<std::uint64_t>(route),
            GetCurrentProcessId(),
            system != nullptr ? 1 : 0);
    }

    // The probe's accumulate(). A 64-frame window is not a detail: observe()
    // restarts whenever it sees an index below the last one, and the oldest
    // entry of an overlapping window always is one, so the rate only ever spans
    // a single window. Sixty-four frames is 0.7 s at 90 Hz and 63 usable
    // samples; the eight-frame lookback last_presentation() needs for its own
    // record is seven, and a lost share estimated from seven frames taken
    // whenever the overlay happened to ask is why this read 90 through drops
    // the probe resolved.
    void poll_window() noexcept {
        std::array<vr::Compositor_FrameTiming, 64> window{};
        window[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
        const std::uint32_t filled = compositor->GetFrameTimings(window.data(), 64);
        if (filled > window.size()) {
            return;
        }
        for (std::uint32_t i = 0; i + 1 < filled; ++i) {
            if (window[i].m_nNumFramePresents != 0) {
                observe(window[i]);
            }
        }
    }

    // Scanouts per second, less the share of them that carried nothing new.
    void close_window(std::int64_t now) noexcept {
        if (!rate_started || rate_seen == 0) {
            return;
        }
        // The probe's threshold, and it has to be short: the span is one
        // window, about 0.7 s, never longer. A one-second requirement never
        // fires at all, which leaves the overlay showing submitted frames.
        const double span = rate_last_time - rate_first_time;
        if (span < 0.05) {
            return;
        }
        const double scanouts =
            static_cast<double>(rate_last_index - rate_first_index) / span;
        const double lost_share =
            static_cast<double>(rate_lost) / static_cast<double>(rate_seen);
        delivered = static_cast<float>(scanouts * (1.0 - lost_share));
        delivered_at_ns = now;
        bridge_flight_logger().event(
            BridgeFlightOperation::steamvr_delivery,
            0,
            static_cast<std::uint64_t>(delivered * 1000.0F),
            static_cast<std::uint64_t>(lost_share * 1000.0),
            rate_seen);
        rate_started = false;
    }
};

SteamVrDelivery::SteamVrDelivery(bool steamvr_runtime) noexcept
    : impl_(nullptr) {
    try {
        impl_ = std::make_unique<Impl>();
        impl_->steamvr = steamvr_runtime;
    } catch (...) {
    }
}

SteamVrDelivery::~SteamVrDelivery() {
    if (!impl_) {
        return;
    }
    // Only tear down a context we created. Borrowing the runtime's own and
    // then shutting it down would take the session with it.
    if (impl_->owns_context && impl_->shutdown != nullptr) {
        impl_->shutdown();
    }
    if (impl_->module != nullptr) {
        FreeLibrary(impl_->module);
    }
}

std::optional<float> SteamVrDelivery::delivered_fps(std::int64_t now) noexcept {
    try {
        if (!impl_) {
            return std::nullopt;
        }
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->attempted) {
            impl_->attach();
        }
        if (!impl_->usable) {
            return std::nullopt;
        }
        impl_->poll_window();
        impl_->close_window(now);
        if (impl_->delivered_at_ns < 0 ||
            now - impl_->delivered_at_ns > kStaleNanoseconds) {
            return std::nullopt;
        }
        return impl_->delivered;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SteamVrDelivery::VsyncAnchor>
SteamVrDelivery::vsync_anchor() noexcept {
    try {
        if (!impl_) {
            return std::nullopt;
        }
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->attempted) {
            impl_->attach();
        }
        if (!impl_->usable || impl_->system == nullptr) {
            return std::nullopt;
        }
        float seconds = 0.0F;
        std::uint64_t counter = 0;
        const auto entered = std::chrono::steady_clock::now();
        // Read the clock immediately after, not before: the value is relative
        // to the moment of the call, and anything between the two becomes
        // phase error in whatever uses it.
        const bool ok = impl_->system->GetTimeSinceLastVsync(&seconds, &counter);
        const auto now = std::chrono::steady_clock::now();
        // Documented to return false with zeroes when the runtime has no vsync
        // times. A negative or absurd value would be worse than none, since a
        // caller would treat it as a real anchor.
        if (!ok || !(seconds >= 0.0F) || seconds > 1.0F) {
            return std::nullopt;
        }
        VsyncAnchor anchor{};
        anchor.at = now - std::chrono::nanoseconds(
            static_cast<std::int64_t>(static_cast<double>(seconds) * 1e9));
        anchor.frame_counter = counter;
        anchor.cost = std::chrono::duration_cast<std::chrono::microseconds>(
            now - entered);
        return anchor;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SteamVrDelivery::FramePresentation>
SteamVrDelivery::last_presentation() noexcept {
    try {
        if (!impl_) {
            return std::nullopt;
        }
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->attempted) {
            impl_->attach();
        }
        if (!impl_->usable) {
            return std::nullopt;
        }
        // Frames ago 0 is the one still in flight: nothing has happened to it
        // yet, so it reports zero presents and zero mispresents whatever the
        // compositor ends up doing. Reading it made this record uniformly zero
        // across 6161 submissions while frames were demonstrably being lost.
        // Walk back to the newest frame that has actually been presented.
        constexpr std::uint32_t kLookback = 8;
        std::array<vr::Compositor_FrameTiming, kLookback> timings{};
        timings[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
        const std::uint32_t filled =
            impl_->compositor->GetFrameTimings(timings.data(), kLookback);
        if (filled == 0 || filled > kLookback) {
            return std::nullopt;
        }
        // Ascending, oldest to newest.
        for (std::uint32_t offset = filled; offset-- > 0;) {
            const auto& timing = timings[offset];
            if (timing.m_nNumFramePresents == 0) {
                continue;
            }
            if (impl_->reported_frames && 
                timing.m_nFrameIndex <= impl_->last_frame_index) {
                return std::nullopt; // Nothing has settled since the last call.
            }
            FramePresentation presentation{};
            presentation.frame_index = timing.m_nFrameIndex;
            presentation.mispresented = timing.m_nNumMisPresented;
            presentation.presents = timing.m_nNumFramePresents;
            presentation.dropped = timing.m_nNumDroppedFrames;
            presentation.skipped = impl_->reported_frames
                ? timing.m_nFrameIndex - impl_->last_frame_index - 1
                : 0;
            impl_->last_frame_index = timing.m_nFrameIndex;
            impl_->reported_frames = true;
            return presentation;
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace xrfg
