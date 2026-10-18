// The NGX guide capture against a stand-in nvngx_dlss.dll (fake_nvngx_dlss)
// on WARP: the module is found and detoured, the originals still run, a
// feature per eye becomes a stream each, every evaluation publishes a
// snapshot of the motion vectors recorded into the game's list, and the
// resolver pairs the two streams into a stereo guide set whose fields are
// the parameters' - rectangles, scale, jitter, the low-resolution vector
// size from the creation flags.
#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/ngx_guide_capture.hpp"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_hresult(HRESULT result, const char* what) {
    if (FAILED(result)) {
        char text[256];
        std::snprintf(text, sizeof(text), "%s failed: 0x%08lX", what, static_cast<unsigned long>(result));
        throw std::runtime_error(text);
    }
}

// The SDK's parameter interface, as a game fills it: every typed Set and Get.
class Parameters final : public NVSDK_NGX_Parameter {
public:
    void Set(const char* name, unsigned long long value) override { u64_[name] = value; }
    void Set(const char* name, float value) override { f32_[name] = value; }
    void Set(const char* name, double value) override { f64_[name] = value; }
    void Set(const char* name, unsigned int value) override { u32_[name] = value; }
    void Set(const char* name, int value) override { i32_[name] = value; }
    void Set(const char* name, ID3D11Resource* value) override { d3d11_[name] = value; }
    void Set(const char* name, ID3D12Resource* value) override { d3d12_[name] = value; }
    void Set(const char* name, void* value) override { pointers_[name] = value; }
    NVSDK_NGX_Result Get(const char* name, unsigned long long* value) const override { return get(u64_, name, value); }
    NVSDK_NGX_Result Get(const char* name, float* value) const override { return get(f32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, double* value) const override { return get(f64_, name, value); }
    NVSDK_NGX_Result Get(const char* name, unsigned int* value) const override { return get(u32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, int* value) const override { return get(i32_, name, value); }
    NVSDK_NGX_Result Get(const char* name, ID3D11Resource** value) const override { return get(d3d11_, name, value); }
    NVSDK_NGX_Result Get(const char* name, ID3D12Resource** value) const override { return get(d3d12_, name, value); }
    NVSDK_NGX_Result Get(const char* name, void** value) const override { return get(pointers_, name, value); }
    void Reset() override {
        u64_.clear(); f32_.clear(); f64_.clear(); u32_.clear(); i32_.clear();
        d3d11_.clear(); d3d12_.clear(); pointers_.clear();
    }

private:
    template <class T>
    static NVSDK_NGX_Result get(const std::map<std::string, T>& values, const char* name, T* out) {
        const auto found = values.find(name);
        if (found == values.end()) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        *out = found->second;
        return NVSDK_NGX_Result_Success;
    }
    std::map<std::string, unsigned long long> u64_;
    std::map<std::string, float> f32_;
    std::map<std::string, double> f64_;
    std::map<std::string, unsigned int> u32_;
    std::map<std::string, int> i32_;
    std::map<std::string, ID3D11Resource*> d3d11_;
    std::map<std::string, ID3D12Resource*> d3d12_;
    std::map<std::string, void*> pointers_;
};

ComPtr<ID3D12Resource> create_texture(
    ID3D12Device* device, UINT width, UINT height, UINT16 slices, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = slices;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> texture;
    require_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        state, nullptr, IID_PPV_ARGS(texture.GetAddressOf())), "CreateCommittedResource");
    return texture;
}

using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using EvaluateFeatureC = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback_C);
using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using Counter = unsigned (*)();

int run(const wchar_t* module_path) {
    // WARP: the capture only records copies, which WARP executes.
    ComPtr<IDXGIFactory4> factory;
    require_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> warp;
    require_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf())), "EnumWarpAdapter");
    ComPtr<ID3D12Device> device;
    require_hresult(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(device.GetAddressOf())), "D3D12CreateDevice(WARP)");
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    require_hresult(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(queue.GetAddressOf())),
        "CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "CreateCommandList");
    ComPtr<ID3D12Fence> fence;
    require_hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())),
        "CreateFence");

    // Before the module is in the process: nothing to hook, no failure.
    xrfg::poll_ngx_guide_capture();
    require(xrfg::ngx_guide_capture_statistics().modules_hooked == 0,
        "hooked something before the module was loaded");

    const HMODULE module = LoadLibraryW(module_path);
    require(module != nullptr, "the stand-in nvngx_dlss.dll did not load");
    require(GetModuleHandleW(L"nvngx_dlss.dll") == module,
        "the stand-in is not the process's nvngx_dlss.dll");
    const auto create = reinterpret_cast<CreateFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    const auto evaluate_c = reinterpret_cast<EvaluateFeatureC>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature_C"));
    const auto release = reinterpret_cast<ReleaseFeature>(GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    const auto creations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_creations"));
    const auto evaluations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_evaluations"));
    const auto c_evaluations = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_c_evaluations"));
    const auto releases = reinterpret_cast<Counter>(GetProcAddress(module, "fake_ngx_releases"));
    require(create && evaluate && evaluate_c && release && creations && evaluations && c_evaluations && releases,
        "the stand-in lacks an export");

    // Enabling starts capture and the monitor; the poll finds the module now.
    xrfg::configure_ngx_guide_capture(queue.Get(), true);
    xrfg::poll_ngx_guide_capture();
    auto statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.active, "capture did not become active");
    require(statistics.modules_hooked == 1 && statistics.modules_failed == 0,
        "the stand-in's exports were not detoured");

    // A game's stereo DLSS: one feature per eye, low-resolution jittered
    // vectors, rendering 48x32 into a 96x64 output (an array of two eyes).
    constexpr UINT kRenderWidth = 48, kRenderHeight = 32;
    constexpr UINT kOutputWidth = 96, kOutputHeight = 64;
    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_MVJittered));
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &left)) && left,
        "left feature creation failed through the hook");
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &right)) && right,
        "right feature creation failed through the hook");
    // A feature of another kind is not a guide.
    NVSDK_NGX_Handle* other = nullptr;
    require(NVSDK_NGX_SUCCEED(create(list.Get(), NVSDK_NGX_Feature_FrameGeneration, &creation, &other)) && other,
        "frame-generation feature creation failed through the hook");
    require(creations() == 3, "the original CreateFeature did not run three times");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.features_created == 2, "two guide features were not recorded");

    const auto output = create_texture(device.Get(), kOutputWidth, kOutputHeight, 2,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    const auto motion = create_texture(device.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto motion_right = create_texture(device.Get(), kRenderWidth, kRenderHeight, 1,
        DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D12Resource* vectors, float jitter, bool c_entry) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, output.Get());
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, vectors);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_MV_Scale_X, -1.0F);
        frame.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0F);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitter);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -jitter);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        const NVSDK_NGX_Result result = c_entry
            ? evaluate_c(list.Get(), handle, &frame, nullptr)
            : evaluate(list.Get(), handle, &frame, nullptr);
        require(NVSDK_NGX_SUCCEED(result), "evaluation failed through the hook");
    };
    const auto execute = [&](UINT64 value) {
        require_hresult(list->Close(), "Close");
        ID3D12CommandList* lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        require_hresult(queue->Signal(fence.Get(), value), "Signal");
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(done != nullptr, "CreateEvent");
        require_hresult(fence->SetEventOnCompletion(value, done), "SetEventOnCompletion");
        require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the queue did not finish");
        CloseHandle(done);
        require_hresult(allocator->Reset(), "allocator Reset");
        require_hresult(list->Reset(allocator.Get(), nullptr), "list Reset");
    };

    // Frame 1: both eyes, the plain entry point.
    evaluate_frame(left, motion.Get(), 0.25F, false);
    evaluate_frame(right, motion_right.Get(), 0.25F, false);
    execute(1);
    require(evaluations() == 2, "the original EvaluateFeature did not run twice");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.evaluations == 2 && statistics.publications == 2,
        "the two evaluations were not both published");
    const auto vectors = xrfg::dlss_motion_vector_statistics();
    require(vectors.published == 2 && vectors.snapshot_copies == 2 && vectors.snapshot_failures == 0,
        "the registry did not snapshot both publications");

    // Frame 2: the C entry point, which the detour also covers.
    evaluate_frame(left, motion.Get(), -0.25F, true);
    evaluate_frame(right, motion_right.Get(), -0.25F, true);
    execute(2);
    require(c_evaluations() == 2, "the original EvaluateFeature_C did not run twice");
    statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.publications == 4, "the C evaluations were not published");

    // The resolver pairs the two streams into the eyes, with the serials of
    // frame 2 following frame 1, and the parameters' values.
    const auto set = xrfg::resolve_dlss_motion_vectors(output.Get(), queue.Get());
    require(set != nullptr && set->eye_count == 2, "no stereo guide set resolved");
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame != nullptr, "an eye has no frame");
        require(frame->serial == 2 && frame->previous_serial == 1, "the serials do not pair frame 2 with frame 1");
        require(frame->motion_width == kRenderWidth && frame->motion_height == kRenderHeight,
            "low-resolution vectors did not take the render size");
        require(frame->output_width == kOutputWidth && frame->output_height == kOutputHeight,
            "the output rectangle is not the creation's display size");
        require(frame->scale_x == -1.0F && frame->scale_y == 1.0F, "the vector scale was not carried");
        require(frame->jittered && std::fabs(frame->jitter_x - -0.25F) < 1e-6F &&
                std::fabs(frame->previous_jitter_x - 0.25F) < 1e-6F,
            "the jitter and its previous value were not carried");
        require(frame->motion_vectors != nullptr && frame->motion_vectors.Get() != motion.Get() &&
                frame->motion_vectors.Get() != motion_right.Get(),
            "the frame holds the game's vectors rather than a snapshot");
        require(frame->producer_queue.Get() == queue.Get(), "the producer queue is not the session's");
    }
    require(set->eyes[0]->stream != set->eyes[1]->stream, "the eyes share a stream");

    // Paused: evaluations run, nothing is published.
    xrfg::stop_ngx_guide_capture(queue.Get());
    evaluate_frame(left, motion.Get(), 0.0F, false);
    execute(3);
    require(evaluations() == 3, "the original did not run while paused");
    require(xrfg::ngx_guide_capture_statistics().publications == 4, "a paused capture published");

    // Release retires the features; the original runs.
    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)) &&
            NVSDK_NGX_SUCCEED(release(other)),
        "release failed through the hook");
    require(releases() == 3, "the original ReleaseFeature did not run three times");
    require(xrfg::ngx_guide_capture_statistics().features_released == 2,
        "the two guide features were not released");
    std::cout << "ngx guide capture: hooked=" << statistics.modules_hooked
              << " publications=" << statistics.publications << "\n";
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "usage: xrfg_ngx_guide_capture_tests <path to stand-in nvngx_dlss.dll>\n";
        return 2;
    }
    try {
        return run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "ngx guide capture test failed: " << error.what() << "\n";
        return 1;
    }
}
