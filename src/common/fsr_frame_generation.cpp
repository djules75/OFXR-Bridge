#include "xrfg/fsr_frame_generation.hpp"

#include <mutex>

namespace xrfg {
namespace {

std::mutex g_mutex;
FsrFrameGenerationStatistics g_statistics;

}  // namespace

void report_fsr_frame_generation_status(
    FsrFrameGenerationStatus status) noexcept {
    std::scoped_lock lock(g_mutex);
    g_statistics.status = status;
}

void report_fsr_frame_generation_attempt(bool used) noexcept {
    std::scoped_lock lock(g_mutex);
    ++g_statistics.attempts;
    if (used) {
        ++g_statistics.used;
        g_statistics.status = FsrFrameGenerationStatus::used;
    } else {
        ++g_statistics.failures;
    }
}

FsrFrameGenerationStatistics fsr_frame_generation_statistics() noexcept {
    std::scoped_lock lock(g_mutex);
    return g_statistics;
}

}  // namespace xrfg
