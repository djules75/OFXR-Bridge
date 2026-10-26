#include "xrfg/luke_detection.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>

namespace xrfg::native_ingress {
namespace {
std::atomic_bool detected{};
std::atomic_uint64_t module_epoch{1};
std::atomic_uint64_t scanned_epoch{};
std::mutex discovery_mutex;
std::once_flag notification_once;
void* notification_cookie{};
#ifdef XRFG_LUKE_DETECTION_TESTS
std::atomic_uint64_t scan_count{};
#endif
static_assert(std::atomic_uint64_t::is_always_lock_free);

void CALLBACK module_changed(ULONG reason, const void*, void*) noexcept {
    // Runs under the Windows loader lock, before module initialization.
    // Do not call another module, allocate, inspect exports or take a mutex.
    if (reason == 1 || reason == 2)
        module_epoch.fetch_add(1, std::memory_order_release);
}

bool marked_module(HMODULE module) noexcept {
    if (!module) return false;
    HMODULE retained{};
    // A snapshot module may have unloaded before inspection. Hold a temporary
    // reference while reading its export table; do not pin the producer.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(module), &retained)) return false;
    const bool marked = GetProcAddress(retained, "RealVRVersion") != nullptr;
    FreeLibrary(retained);
    return marked;
}

std::optional<bool> discover_loaded_modules() noexcept {
#ifdef XRFG_LUKE_DETECTION_TESTS
    scan_count.fetch_add(1, std::memory_order_relaxed);
#endif
    if (marked_module(GetModuleHandleW(L"dxgi.dll")) ||
        marked_module(GetModuleHandleW(L"RealVR64.dll"))) return true;
    HMODULE modules[1024]{};
    DWORD bytes{};
    using Enum = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
    const auto enumerate = reinterpret_cast<Enum>(GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
    if (!enumerate || !enumerate(GetCurrentProcess(), modules, sizeof(modules), &bytes))
        return std::nullopt;
    for (unsigned i = 0; i < std::min<DWORD>(bytes / sizeof(HMODULE), 1024); ++i)
        if (marked_module(modules[i])) return true;
    return false;
}
}

bool luke_detected() noexcept {
    return detected.load(std::memory_order_acquire);
}

void refresh_luke_detection(bool force) noexcept {
    const auto pending = module_epoch.load(std::memory_order_acquire);
    if (!force && scanned_epoch.load(std::memory_order_acquire) == pending) return;
    try {
        std::scoped_lock lock(discovery_mutex);
        const auto before = module_epoch.load(std::memory_order_acquire);
        if (!force && scanned_epoch.load(std::memory_order_relaxed) == before) return;
        const auto result = discover_loaded_modules();
        if (!result) return;
        detected.store(*result, std::memory_order_release);
        // A loader event during the scan stays dirty for the next monitor pass.
        scanned_epoch.store(before, std::memory_order_release);
    } catch (...) {}
}

void start_luke_detection_notifications() noexcept {
    try {
        std::call_once(notification_once, [] {
            using Callback = void(CALLBACK*)(ULONG, const void*, void*);
            using Register = LONG(NTAPI*)(ULONG, Callback, void*, void**);
            const auto register_notification = reinterpret_cast<Register>(GetProcAddress(
                GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
            if (register_notification &&
                register_notification(0, &module_changed, nullptr, &notification_cookie) >= 0)
                module_epoch.fetch_add(1, std::memory_order_release);
        });
        // Close the window between the pre-hook snapshot and registration.
        refresh_luke_detection();
    } catch (...) {}
}

#ifdef XRFG_LUKE_DETECTION_TESTS
std::uint64_t luke_detection_scan_count_for_test() noexcept {
    return scan_count.load(std::memory_order_relaxed);
}
bool luke_detection_notifications_ready_for_test() noexcept {
    return notification_cookie != nullptr;
}
#endif
}
