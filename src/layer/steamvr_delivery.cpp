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

    // Distinct compositor frames counted since the window opened, and the
    // repeats among them. The displayed rate is derived from these rather than
    // from the cumulative counters - see the note on sample().
    std::uint32_t window_frames{};
    std::uint32_t window_repeats{};
    std::int64_t window_started_ns{-1};

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

    // Closes the delivery window from frames counted by last_presentation.
    //
    // This used to be presents - dropped - reprojected, read from
    // GetCumulativeStats, and that is wrong wherever the compositor reprojects
    // as a matter of course. MSFS 2024 runs with prediction two frames ahead
    // (reprojection flags 0x024), so nearly every frame is marked reprojected
    // and mispresented by construction - 92% of them - while 91% are still
    // presented exactly once and the headset is fine. The subtraction collapsed
    // to zero and the overlay read 0 through a healthy session.
    //
    // Counting distinct frames does not care how the compositor labels its
    // timewarp. Measured against the same captures: Callisto 90.0/s either way,
    // MSFS 84.7/s against the old formula's 0.9.
    void close_window(std::int64_t now) noexcept {
        if (window_started_ns < 0) {
            window_started_ns = now;
            return;
        }
        const auto elapsed = now - window_started_ns;
        if (elapsed < kWindowNanoseconds) {
            return;
        }
        if (window_frames != 0) {
            delivered = static_cast<float>(window_frames) *
                (1e9f / static_cast<float>(elapsed));
            delivered_at_ns = now;
            bridge_flight_logger().event(
                BridgeFlightOperation::steamvr_delivery,
                0,
                static_cast<std::uint64_t>(delivered * 1000.0F),
                window_repeats,
                window_frames);
        }
        window_frames = 0;
        window_repeats = 0;
        window_started_ns = now;
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
            // Frames the compositor produced since the last look: this one plus
            // any that settled between calls. This is what the displayed rate
            // is counted from.
            impl_->window_frames += 1 + presentation.skipped;
            if (presentation.presents > 1) {
                impl_->window_repeats += presentation.presents - 1;
            }
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
