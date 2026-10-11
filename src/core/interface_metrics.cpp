#include "xrfg/interface_metrics.hpp"
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace xrfg {
std::int64_t interface_qpc_ns(std::int64_t ticks) noexcept {
#ifdef _WIN32
    static const auto frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    return frequency > 0 ? static_cast<std::int64_t>(static_cast<long double>(ticks) * 1e9L / frequency) : 0;
#else
    return ticks;
#endif
}
std::int64_t interface_clock_ns() noexcept {
#ifdef _WIN32
    LARGE_INTEGER qpc{}; return QueryPerformanceCounter(&qpc) ? interface_qpc_ns(qpc.QuadPart) : 0;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
}
