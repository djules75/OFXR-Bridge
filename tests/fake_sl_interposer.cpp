// A stand-in sl.interposer.dll for xrfg_ngx_guide_capture_tests (V515):
// exports the two Streamline entry points the capture detours. Its
// slEvaluateFeature runs a callback the test sets, which makes the NGX
// evaluation through the stand-in nvngx_dlss.dll, as Streamline's DLSS
// plugin does inside the real one. Built beside the test, never beside the
// tray.
#include <atomic>
#include <cstdint>

namespace {

using EvaluateCallback = void (*)(void* context);

std::atomic<unsigned> g_constants{0};
std::atomic<unsigned> g_evaluations{0};
std::atomic<EvaluateCallback> g_callback{nullptr};
std::atomic<void*> g_context{nullptr};

}  // namespace

extern "C" {

__declspec(dllexport) __declspec(noinline) int slSetConstants(
    const void* constants, const void* frame, const void* viewport) {
    if (constants == nullptr || frame == nullptr || viewport == nullptr) {
        return 1;
    }
    g_constants.fetch_add(1);
    return 0;
}

__declspec(dllexport) __declspec(noinline) int slEvaluateFeature(
    std::uint32_t feature, const void* frame, const void* const* inputs,
    std::uint32_t input_count, void* command_buffer) {
    static_cast<void>(feature);
    static_cast<void>(command_buffer);
    if (frame == nullptr || (inputs == nullptr && input_count != 0)) {
        return 1;
    }
    g_evaluations.fetch_add(1);
    if (const auto callback = g_callback.load()) {
        callback(g_context.load());
    }
    return 0;
}

__declspec(dllexport) void fake_sl_set_evaluate_callback(EvaluateCallback callback, void* context) {
    g_context.store(context);
    g_callback.store(callback);
}

__declspec(dllexport) unsigned fake_sl_constants() { return g_constants.load(); }
__declspec(dllexport) unsigned fake_sl_evaluations() { return g_evaluations.load(); }

}  // extern "C"
