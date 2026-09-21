#include "xrfg/steamvr_delivery.hpp"

#include "xrfg/bridge_flight_logger.hpp"

#include <openvr.h>

#include <windows.h>

#include <chrono>
#include <cstdio>
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
    bool steamvr{};
    bool attempted{};
    bool usable{};
    HMODULE module{};
    PfnShutdownInternal shutdown{};
    vr::IVRCompositor* compositor{};
    AttachRoute route{AttachRoute::none};
    bool owns_context{};

    std::uint32_t pid{};
    bool baseline{};
    std::uint32_t last_presents{};
    std::uint32_t last_dropped{};
    std::uint32_t last_reprojected{};
    std::int64_t last_sample_ns{};

    float delivered{};
    std::int64_t delivered_at_ns{-1};

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
        pid = GetCurrentProcessId();
        usable = true;
        bridge_flight_logger().event(
            BridgeFlightOperation::steamvr_delivery_attach,
            0,
            static_cast<std::uint64_t>(route),
            pid);
    }

    void sample(std::int64_t now) noexcept {
        const auto entered = std::chrono::steady_clock::now();
        vr::Compositor_CumulativeStats stats{};
        compositor->GetCumulativeStats(&stats, sizeof(stats));
        const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - entered);

        // In-process these counters must describe us. If they name another
        // process the compositor is serving someone else and the number would
        // be a different application's frame rate.
        if (stats.m_nPid != pid) {
            baseline = false;
            delivered_at_ns = -1;
            return;
        }
        if (!baseline) {
            baseline = true;
        } else if (stats.m_nNumFramePresents >= last_presents &&
                   stats.m_nNumDroppedFrames >= last_dropped &&
                   stats.m_nNumReprojectedFrames >= last_reprojected) {
            const auto elapsed = now - last_sample_ns;
            if (elapsed >= kWindowNanoseconds) {
                // presents counts every scanout including the repeated ones, so
                // what is left once both kinds of repeat are removed is the
                // distinct images the headset received.
                const std::int64_t distinct =
                    static_cast<std::int64_t>(
                        stats.m_nNumFramePresents - last_presents) -
                    static_cast<std::int64_t>(
                        stats.m_nNumDroppedFrames - last_dropped) -
                    static_cast<std::int64_t>(
                        stats.m_nNumReprojectedFrames - last_reprojected);
                delivered = distinct > 0
                    ? static_cast<float>(distinct) *
                        (1e9f / static_cast<float>(elapsed))
                    : 0.0F;
                delivered_at_ns = now;
                bridge_flight_logger().event(
                    BridgeFlightOperation::steamvr_delivery,
                    0,
                    static_cast<std::uint64_t>(delivered * 1000.0F),
                    stats.m_nNumReprojectedFrames - last_reprojected,
                    static_cast<std::uint64_t>(cost.count()));
            } else {
                return; // Window still open; keep the previous baseline.
            }
        }
        last_presents = stats.m_nNumFramePresents;
        last_dropped = stats.m_nNumDroppedFrames;
        last_reprojected = stats.m_nNumReprojectedFrames;
        last_sample_ns = now;
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
        if (!impl_->attempted) {
            impl_->attach();
        }
        if (!impl_->usable) {
            return std::nullopt;
        }
        impl_->sample(now);
        if (impl_->delivered_at_ns < 0 ||
            now - impl_->delivered_at_ns > kStaleNanoseconds) {
            return std::nullopt;
        }
        return impl_->delivered;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace xrfg
