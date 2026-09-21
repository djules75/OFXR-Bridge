// Reads SteamVR's compositor delivery counters, which are the only thing on
// this stack that reports what the headset actually scanned out.
//
// Every instrument the layer owns sits upstream of xrEndFrame - fence
// readiness, pair spacing, submission counts, requested display times - and
// all of them have been measured reading green through a 40% loss: frames
// accepted, correctly spaced, aimed at distinct display slots, and discarded
// afterwards. The overlay's own number is FpsSnapshot::submitted_fps and its
// header says so: successful downstream submissions are not evidence of
// physical scanout. fpsVR is accurate because it is an OpenVR application asking the
// compositor directly, which is a channel OpenXR does not expose.
//
// This probe is a separate process on purpose. fpsVR proves a second process
// can read the running scene application's counters, so this costs the game
// nothing. Whether the same interface can be reached from *inside* a process
// that is already a SteamVR scene application through the OpenXR runtime is a
// different question, and it should not be answered in the same step as
// whether these numbers are any good.
//
// Not registered as a test: it needs a live SteamVR session with an
// application running. Run it by hand, alongside fpsVR, and compare.

#include <openvr.h>

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using PfnInitInternal2 =
    std::uint32_t(__cdecl*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
using PfnShutdownInternal = void(__cdecl*)();
using PfnGetGenericInterface = void*(__cdecl*)(const char*, vr::EVRInitError*);
using PfnGetInitErrorAsEnglish = const char*(__cdecl*)(vr::EVRInitError);

// The runtime registers itself here rather than in the registry, and this is
// the path SteamVR itself reads. Parsed by hand: pulling in a JSON library for
// one string would be the larger dependency.
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
    // The file stores Windows paths with escaped separators.
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

// Everything printed goes to a file as well as the console: this is run while
// the operator is wearing the headset and cannot read a terminal. Flushed per
// line, because the usual way this ends is Ctrl-C.
FILE* g_log = nullptr;

void emit(const char* format, ...) {
    va_list console_args;
    va_start(console_args, format);
    std::vfprintf(stdout, format, console_args);
    va_end(console_args);
    std::fflush(stdout);
    if (g_log != nullptr) {
        va_list file_args;
        va_start(file_args, format);
        std::vfprintf(g_log, format, file_args);
        va_end(file_args);
        std::fflush(g_log);
    }
}

// Beside the flight logs rather than in them, and named the same way. The
// flight log's ms= is elapsed since session start and its filename carries the
// wall clock of that start, so stamping each row with the wall clock is what
// lets the two be joined offline.
[[nodiscard]] std::string open_log() {
    char local[MAX_PATH]{};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH) == 0) {
        return {};
    }
    const std::string parent = std::string(local) + "\\OFXR Bridge";
    const std::string directory = parent + "\\DeliveryProbe";
    CreateDirectoryA(parent.c_str(), nullptr);
    CreateDirectoryA(directory.c_str(), nullptr);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char name[MAX_PATH]{};
    std::snprintf(name, sizeof(name),
        "%s\\ofxr-delivery-%04u%02u%02u-%02u%02u%02u-pid%lu.log",
        directory.c_str(), now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    // _fsopen, not fopen_s: fopen_s takes the file exclusively, so the log
    // cannot be read until the probe exits. A run is minutes long and the
    // whole point is to compare it against a flight log while the session is
    // still fresh, so readers have to be let in while this is still writing.
    g_log = _fsopen(name, "wb", _SH_DENYWR);
    if (g_log == nullptr) {
        return {};
    }
    return name;
}

[[nodiscard]] std::string wall_clock() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return stamp;
}

struct Sample {
    std::uint32_t pid{};
    std::uint32_t presents{};
    std::uint32_t dropped{};
    std::uint32_t reprojected{};
    bool valid{};
};

[[nodiscard]] Sample take(vr::IVRCompositor* compositor) {
    vr::Compositor_CumulativeStats stats{};
    compositor->GetCumulativeStats(&stats, sizeof(stats));
    Sample sample{};
    sample.pid = stats.m_nPid;
    sample.presents = stats.m_nNumFramePresents;
    sample.dropped = stats.m_nNumDroppedFrames;
    sample.reprojected = stats.m_nNumReprojectedFrames;
    sample.valid = true;
    return sample;
}

void describe_reprojection(std::uint32_t flags) {
    if (flags == 0) {
        emit("  reprojection flags: none\n");
        return;
    }
    emit("  reprojection flags: 0x%03X", flags);
    if ((flags & vr::VRCompositor_ReprojectionMotion_Enabled) != 0) {
        emit("  motion-smoothing-enabled");
    }
    if ((flags & vr::VRCompositor_ReprojectionMotion_ForcedOn) != 0) {
        emit("  motion-smoothing-forced");
    }
    if ((flags & vr::VRCompositor_ReprojectionMotion_AppThrottled) != 0) {
        emit("  app-throttled");
    }
    if ((flags & vr::VRCompositor_ReprojectionAsync) != 0) {
        emit("  async-reprojection");
    }
    if ((flags & vr::VRCompositor_ReprojectionMotion) != 0) {
        emit("  motion-smoothing-triggered");
    }
    // The two masked fields are the ones that separated a healthy capture from
    // a failing one and went unread the first time: 0x024 is async
    // reprojection plus two predicted frames, which is the compositor no
    // longer expecting this application to make its deadline.
    const std::uint32_t predicted = (flags & vr::VRCompositor_PredictionMask) >> 4;
    const std::uint32_t throttled = (flags & vr::VRCompositor_ThrottleMask) >> 8;
    if (predicted != 0) {
        emit("  predicted-ahead=%u", predicted);
    }
    if (throttled != 0) {
        emit("  throttled-frames=%u", throttled);
    }
    emit("\n");
}

} // namespace

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 300;

    const std::string runtime = runtime_path_from_vrpath();
    if (runtime.empty()) {
        std::fprintf(stderr,
            "could not read the runtime path from openvrpaths.vrpath\n");
        return 2;
    }
    const std::string dll = runtime + "\\bin\\win64\\openvr_api.dll";
    const std::string log_path = open_log();
    if (log_path.empty()) {
        std::fprintf(stderr, "warning: no log file; console only\n");
    } else {
        std::printf("log: %s\n", log_path.c_str());
    }
    emit("runtime: %s\n", runtime.c_str());

    const HMODULE module = LoadLibraryA(dll.c_str());
    if (module == nullptr) {
        std::fprintf(stderr, "LoadLibrary failed for %s (%lu)\n",
            dll.c_str(), GetLastError());
        return 2;
    }

    const auto init = reinterpret_cast<PfnInitInternal2>(
        reinterpret_cast<void*>(GetProcAddress(module, "VR_InitInternal2")));
    const auto shutdown = reinterpret_cast<PfnShutdownInternal>(
        reinterpret_cast<void*>(GetProcAddress(module, "VR_ShutdownInternal")));
    const auto generic = reinterpret_cast<PfnGetGenericInterface>(
        reinterpret_cast<void*>(GetProcAddress(module, "VR_GetGenericInterface")));
    const auto error_text = reinterpret_cast<PfnGetInitErrorAsEnglish>(
        reinterpret_cast<void*>(
            GetProcAddress(module, "VR_GetVRInitErrorAsEnglishDescription")));
    if (init == nullptr || shutdown == nullptr || generic == nullptr) {
        std::fprintf(stderr, "openvr_api.dll is missing an expected export\n");
        return 2;
    }

    // Background, never Scene: this must not become the application SteamVR
    // is compositing, and must not start SteamVR if it is not already up.
    vr::EVRInitError init_error = vr::VRInitError_None;
    init(&init_error, vr::VRApplication_Background, nullptr);
    if (init_error != vr::VRInitError_None) {
        std::fprintf(stderr, "VR_Init failed: %s\n",
            error_text ? error_text(init_error) : "unknown");
        return 2;
    }

    auto* compositor = static_cast<vr::IVRCompositor*>(
        generic(vr::IVRCompositor_Version, &init_error));
    if (compositor == nullptr || init_error != vr::VRInitError_None) {
        std::fprintf(stderr, "no %s: %s\n", vr::IVRCompositor_Version,
            error_text ? error_text(init_error) : "unknown");
        shutdown();
        return 2;
    }
    emit("interface: %s\n\n", vr::IVRCompositor_Version);

    vr::Compositor_FrameTiming timing{};
    timing.m_nSize = sizeof(timing);
    if (compositor->GetFrameTiming(&timing, 0)) {
        describe_reprojection(timing.m_nReprojectionFlags);
    }

    emit("      clock   sec    pid  presents  dropped  reproj  delivered\n");
    Sample previous = take(compositor);
    std::uint32_t total_presents = 0;
    std::uint32_t total_lost = 0;
    for (int second = 1; second <= seconds; ++second) {
        Sleep(1000);
        const Sample current = take(compositor);
        if (current.pid != previous.pid) {
            // A different scene application; the counters restarted with it.
            emit("  --- scene application changed to pid %u\n", current.pid);
            previous = current;
            continue;
        }
        const std::uint32_t presents = current.presents - previous.presents;
        const std::uint32_t dropped = current.dropped - previous.dropped;
        const std::uint32_t reprojected = current.reprojected - previous.reprojected;
        // presents counts every scanout including the repeated ones, so the
        // distinct images the headset actually received is what is left after
        // the two kinds of repeat are removed. This is the number to compare
        // against fpsVR, and against the layer's submitted_fps.
        const std::int64_t delivered =
            static_cast<std::int64_t>(presents) - dropped - reprojected;
        emit("  %s  %4d  %5u  %8u  %7u  %6u  %9lld\n",
            wall_clock().c_str(), second, current.pid, presents, dropped,
            reprojected, static_cast<long long>(delivered));
        total_presents += presents;
        total_lost += dropped + reprojected;
        previous = current;
    }

    if (total_presents != 0) {
        emit("\n  --- %u presents, %u repeated (%.1f%%)\n",
            total_presents, total_lost,
            100.0 * total_lost / total_presents);
    }
    if (compositor->GetFrameTiming(&timing, 0)) {
        describe_reprojection(timing.m_nReprojectionFlags);
    }

    shutdown();
    if (g_log != nullptr) {
        fclose(g_log);
    }
    return 0;
}
