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

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// Distinct compositor frames per second, timed on SteamVR's own clock.
//
// The cumulative counters cannot give this. m_nNumNumReprojectedFrames counts
// routine reprojection, not lost frames: MSFS 2024 runs with prediction two
// frames ahead (flags 0x024) and every frame comes back marked reprojected and
// mispresented while the headset is receiving all 90, so presents minus dropped
// minus reprojected collapses to zero through a healthy session.
//
// The frame index alone is not enough either: it advances once per distinct
// frame where the compositor ran out of content and repeated one - MSFS's
// repeat held its index and stretched the gap to 22.227 ms - but it keeps
// advancing at the refresh rate where the loss is on our side, two submissions
// landing in one scanout window and the compositor keeping one. Callisto does
// the second, and the index rate reads 90 while the headset shows 45.
//
// What separates a lost frame from a routine one is m_nNumMisPresented against
// how far ahead the compositor is predicting. Presenting on a vsync other than
// the one first predicted is expected when it predicts ahead, and a fault when
// it does not:
//
//   Callisto healthy   mispres   0%   predicted 0   ->  90, matches fpsVR
//   Callisto dropping  mispres  49%   predicted 0   ->  46, matches fpsVR 45-40
//   MSFS 2024          mispres 100%   predicted 2   ->  90, matches fpsVR
//
// With one exception, which the MSFS capture also shows: the frame after a
// repeat is mispresented *because of* the repeat. It arrives a period late, so
// it lands on a vsync other than the one predicted, and the MSFS repeat's
// successor duly carried mispres 2 against flags 0x014 predicting one ahead.
// But the index rate has already subtracted that scanout - the repeat held its
// index rather than advancing it - so charging the successor as well counts the
// same lost frame twice. Where every frame is repeated, as MSFS is when it
// holds 45, that double count takes the whole rate to zero.
//
// So the mispresent only counts when the previous distinct frame was scanned
// out once. Beyond that the index rate has it covered.
//
// Timed on m_flSystemTimeInSeconds rather than the wall clock: it is the
// compositor's own vsync-aligned reference, so it needs no correction for when
// we happened to poll.
struct FrameRate {
    std::uint32_t first_index{};
    double first_time{};
    std::uint32_t last_index{};
    double last_time{};
    std::uint32_t seen{};
    std::uint32_t lost{};
    // Scanouts the previous distinct frame occupied. One means this frame is
    // as early as it could be, so a mispresent here is a real displacement.
    std::uint32_t previous_presents{1};
    bool started{};

    void observe(const vr::Compositor_FrameTiming& timing) {
        if (!started || timing.m_nFrameIndex < last_index) {
            first_index = last_index = timing.m_nFrameIndex;
            first_time = last_time = timing.m_flSystemTimeInSeconds;
            seen = lost = 0;
            previous_presents = timing.m_nNumFramePresents;
            started = true;
            return;
        }
        if (timing.m_nFrameIndex <= last_index) {
            return;
        }
        last_index = timing.m_nFrameIndex;
        last_time = timing.m_flSystemTimeInSeconds;
        ++seen;
        const std::uint32_t predicted =
            (timing.m_nReprojectionFlags & vr::VRCompositor_PredictionMask) >> 4;
        if (previous_presents <= 1 && timing.m_nNumMisPresented > predicted) {
            ++lost;
        }
        previous_presents = timing.m_nNumFramePresents;
    }
    // Scanouts per second, less the share of them that carried nothing new.
    [[nodiscard]] double per_second() const {
        const double span = last_time - first_time;
        if (span <= 0.05 || seen == 0) {
            return -1.0;
        }
        const double scanouts =
            static_cast<double>(last_index - first_index) / span;
        return scanouts *
            (1.0 - static_cast<double>(lost) / static_cast<double>(seen));
    }
    [[nodiscard]] double lost_share() const {
        return seen == 0 ? 0.0
                         : 100.0 * static_cast<double>(lost) /
                               static_cast<double>(seen);
    }
    void restart() { started = false; }
};

// Folds whatever settled since the last look into the rate. The newest entry
// is left for the next poll: m_nNumFramePresents is still rising while a frame
// is on screen, and the rule above reads it, so a frame is only counted once a
// later one exists and its scanout count is final.
void accumulate(vr::IVRCompositor* compositor, FrameRate* rate) {
    std::array<vr::Compositor_FrameTiming, 64> window{};
    window[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
    const std::uint32_t filled = compositor->GetFrameTimings(window.data(), 64);
    for (std::uint32_t i = 0; i + 1 < filled; ++i) {
        if (window[i].m_nNumFramePresents != 0) {
            rate->observe(window[i]);
        }
    }
}

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
    int seconds = 300;
    bool raw = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "raw") == 0) {
            raw = true;
        } else {
            const int value = std::atoi(argv[i]);
            if (value > 0) {
                seconds = value;
            }
        }
    }

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

    Sample previous = take(compositor);

    // Three delivery formulas have now been fitted to captures with nothing to
    // check them against, and each was wrong somewhere: presents minus dropped
    // minus reprojected reads zero wherever the compositor reprojects as a
    // matter of course, counting frame indices returns the refresh rate by
    // construction, and subtracting per-frame repeats produced 104 scanouts a
    // second on a 90 Hz display. So dump what the compositor actually reports,
    // per frame, and derive the formula from that rather than guessing at the
    // counter semantics a fourth time.
    //
    // Polled at 100 ms over a 64-frame window - the compositor produces about
    // nine frames in that time, so nothing is missed - and deduped on
    // m_nFrameIndex. Pass "raw" on the command line.
    if (raw) {
        emit("\nRAW  index  presents  mispres  dropped  flags  sinceLastMs\n");
        std::uint32_t last_index = 0;
        bool have_last = false;
        double last_system = 0.0;
        const ULONGLONG raw_until =
            GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
        while (GetTickCount64() < raw_until) {
            std::array<vr::Compositor_FrameTiming, 64> window{};
            window[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
            const std::uint32_t filled =
                compositor->GetFrameTimings(window.data(), 64);
            for (std::uint32_t i = 0; i < filled; ++i) {
                const auto& t = window[i];
                if (have_last && t.m_nFrameIndex <= last_index) {
                    continue;
                }
                emit("RAW  %6u  %8u  %7u  %7u  0x%03X  %11.3f\n",
                    t.m_nFrameIndex,
                    t.m_nNumFramePresents,
                    t.m_nNumMisPresented,
                    t.m_nNumDroppedFrames,
                    t.m_nReprojectionFlags,
                    have_last
                        ? (t.m_flSystemTimeInSeconds - last_system) * 1000.0
                        : 0.0);
                last_system = t.m_flSystemTimeInSeconds;
                last_index = t.m_nFrameIndex;
                have_last = true;
            }
            Sleep(100);
        }
        // The cumulative counters over the same window, so the per-frame data
        // can be checked against what the current formula would have said.
        const Sample after = take(compositor);
        emit("\nRAW-TOTALS presents %u  dropped %u  reprojected %u  over %d s\n",
            after.presents - previous.presents,
            after.dropped - previous.dropped,
            after.reprojected - previous.reprojected,
            seconds);
        shutdown();
        if (g_log != nullptr) {
            fclose(g_log);
        }
        return 0;
    }

    emit("      clock   sec    pid  presents  dropped  reproj  delivered   lost%\n");
    FrameRate rate;
    std::uint32_t total_presents = 0;
    std::uint32_t total_lost = 0;
    for (int second = 1; second <= seconds; ++second) {
        // Sampled often enough that the 64-frame window cannot overflow at
        // 90 Hz, so no distinct frame goes unseen.
        for (int slice = 0; slice < 10; ++slice) {
            accumulate(compositor, &rate);
            Sleep(100);
        }
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
        const double delivered = rate.per_second();
        emit("  %s  %4d  %5u  %8u  %7u  %6u  %9.1f  %5.1f\n",
            wall_clock().c_str(), second, current.pid, presents, dropped,
            reprojected, delivered, rate.lost_share());
        rate.restart();
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
