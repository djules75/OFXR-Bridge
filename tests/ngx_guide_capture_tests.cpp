// The NGX guide capture against a stand-in nvngx_dlss.dll (fake_nvngx_dlss)
// on WARP: the module is found and detoured, the originals still run, a
// feature per eye becomes a stream each, every evaluation publishes a
// snapshot of the motion vectors recorded into the game's list, and the
// resolver pairs the two streams into a stereo guide set whose fields are
// the parameters' - rectangles, scale, jitter, the low-resolution vector
// size from the creation flags.
#include "xrfg/d3d11_d3d12_interop.hpp"
#include "xrfg/dlss_motion_vectors.hpp"
#include "xrfg/ngx_guide_capture.hpp"

#include <windows.h>
#include <d3d11_4.h>
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
#include <utility>
#include <vector>

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

using CreateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeature11 = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D11DeviceContext*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);

// V512: a bridged D3D11 session. The game's DLSS runs on its D3D11 device;
// the capture copies each evaluation's vectors and depth into textures shared
// with the layer's D3D12 device and publishes them on its queue. Checked: the
// D3D11 exports are detoured, every evaluation publishes with its depth, the
// resolver pairs the two eye streams for a side-by-side image twice an
// output's width (UEVR's Native Stereo, Ready or Not), and the snapshot on
// the D3D12 side holds the very vectors the game wrote in D3D11.
int run_d3d11(HMODULE module) {
    const auto create = reinterpret_cast<CreateFeature11>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_CreateFeature"));
    const auto evaluate = reinterpret_cast<EvaluateFeature11>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_EvaluateFeature"));
    const auto release = reinterpret_cast<ReleaseFeature>(
        GetProcAddress(module, "NVSDK_NGX_D3D11_ReleaseFeature"));
    const auto d3d11_evaluations = reinterpret_cast<Counter>(
        GetProcAddress(module, "fake_ngx_d3d11_evaluations"));
    require(create && evaluate && release && d3d11_evaluations, "the stand-in lacks a D3D11 export");

    // The game's device, and the layer's D3D12 device on its adapter, as the
    // bridge makes them.
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1};
    require_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, device11.GetAddressOf(), nullptr, context11.GetAddressOf()),
        "D3D11CreateDevice");
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    require_hresult(xrfg::create_d3d12_device_for_d3d11(device11.Get(),
        device12.GetAddressOf(), queue12.GetAddressOf()), "create_d3d12_device_for_d3d11");

    xrfg::configure_ngx_guide_capture_d3d11(device11.Get(), device12.Get(), queue12.Get(), true);
    xrfg::poll_ngx_guide_capture();
    auto statistics = xrfg::ngx_guide_capture_statistics();
    require(statistics.d3d11_active && statistics.d3d11_modules_hooked == 1,
        "the stand-in's D3D11 exports were not detoured");

    constexpr UINT kRenderWidth = 48, kRenderHeight = 32;
    constexpr UINT kOutputWidth = 96, kOutputHeight = 64;
    Parameters creation;
    creation.Set(NVSDK_NGX_Parameter_Width, kRenderWidth);
    creation.Set(NVSDK_NGX_Parameter_Height, kRenderHeight);
    creation.Set(NVSDK_NGX_Parameter_OutWidth, kOutputWidth);
    creation.Set(NVSDK_NGX_Parameter_OutHeight, kOutputHeight);
    creation.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
            NVSDK_NGX_DLSS_Feature_Flags_MVJittered | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
    const auto created_before = statistics.features_created;
    NVSDK_NGX_Handle* left = nullptr;
    NVSDK_NGX_Handle* right = nullptr;
    require(NVSDK_NGX_SUCCEED(create(context11.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &left)) &&
            left && NVSDK_NGX_SUCCEED(create(context11.Get(), NVSDK_NGX_Feature_SuperSampling, &creation, &right)) &&
            right,
        "D3D11 feature creation failed through the hook");
    require(xrfg::ngx_guide_capture_statistics().features_created == created_before + 2,
        "the two D3D11 features were not recorded");

    const auto texture = [&](UINT width, UINT height, DXGI_FORMAT format, UINT bind, std::uint32_t fill) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = bind;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height, fill);
        D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4U, 0};
        ComPtr<ID3D11Texture2D> result;
        require_hresult(device11->CreateTexture2D(&description, &data, result.GetAddressOf()),
            "CreateTexture2D");
        return result;
    };
    // R16G16_FLOAT: (1.5, -2.0) for the left eye, (3.0, 0.5) for the right.
    constexpr std::uint32_t kLeftVector = 0x3E00U | (0xC000U << 16U);
    constexpr std::uint32_t kRightVector = 0x4200U | (0x3800U << 16U);
    const auto output = texture(kOutputWidth, kOutputHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    const auto motion_left = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, kLeftVector);
    const auto motion_right = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, kRightVector);
    const auto depth = texture(kRenderWidth, kRenderHeight, DXGI_FORMAT_R32_FLOAT,
        D3D11_BIND_SHADER_RESOURCE, 0x3F000000U);

    const auto vectors_before = xrfg::dlss_motion_vector_statistics();
    const auto evaluate_frame = [&](NVSDK_NGX_Handle* handle, ID3D11Texture2D* vectors, float jitter) {
        Parameters frame;
        frame.Set(NVSDK_NGX_Parameter_Output, static_cast<ID3D11Resource*>(output.Get()));
        frame.Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(vectors));
        frame.Set(NVSDK_NGX_Parameter_Depth, static_cast<ID3D11Resource*>(depth.Get()));
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, kRenderWidth);
        frame.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, kRenderHeight);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitter);
        frame.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -jitter);
        frame.Set(NVSDK_NGX_Parameter_Reset, 0);
        require(NVSDK_NGX_SUCCEED(evaluate(context11.Get(), handle, &frame, nullptr)),
            "D3D11 evaluation failed through the hook");
    };
    // Each frame's snapshot copies go to the queue at the bridged session's
    // image release, as the layer flushes them.
    for (int frame = 0; frame < 3; ++frame) {
        evaluate_frame(left, motion_left.Get(), frame % 2 ? 0.25F : -0.25F);
        evaluate_frame(right, motion_right.Get(), frame % 2 ? 0.25F : -0.25F);
        context11->Flush();
        xrfg::flush_ngx_guide_capture_d3d11();
    }
    require(d3d11_evaluations() == 6, "the original D3D11 EvaluateFeature did not run six times");
    statistics = xrfg::ngx_guide_capture_statistics();
    const auto vectors_after = xrfg::dlss_motion_vector_statistics();
    require(statistics.d3d11_publications == 6 && statistics.d3d11_transport_failures == 0,
        "the D3D11 evaluations were not all published");
    require(vectors_after.published - vectors_before.published == 6 &&
            vectors_after.depth_publications - vectors_before.depth_publications == 6,
        "the registry did not take every D3D11 publication with its depth");

    // The side-by-side image UEVR submits: both eyes' outputs in one.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image_description{};
    image_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    image_description.Width = kOutputWidth * 2U;
    image_description.Height = kOutputHeight;
    image_description.DepthOrArraySize = 1;
    image_description.MipLevels = 1;
    image_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image_description.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> image;
    require_hresult(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image_description,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(image.GetAddressOf())), "eye image");
    const auto set = xrfg::resolve_dlss_motion_vectors(image.Get(), queue12.Get());
    require(set != nullptr && set->eye_count == 2, "no side-by-side guide set resolved from D3D11");

    // Read each eye's snapshot back on the D3D12 queue.
    ComPtr<ID3D12CommandAllocator> allocator;
    require_hresult(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator.GetAddressOf())), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require_hresult(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(list.GetAddressOf())), "list");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer_description{};
    buffer_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_description.Width = 2U * 256U * kRenderHeight;
    buffer_description.Height = 1;
    buffer_description.DepthOrArraySize = 1;
    buffer_description.MipLevels = 1;
    buffer_description.SampleDesc.Count = 1;
    buffer_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    require_hresult(device12->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
        &buffer_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(readback.GetAddressOf())), "readback");
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& frame = set->eyes[eye];
        require(frame && frame->motion_vectors && frame->depth, "an eye lacks its vectors or depth");
        require(frame->producer_queue.Get() == queue12.Get(), "the D3D11 frame is not on the layer's queue");
        require(frame->motion_width == kRenderWidth && frame->depth_width == kRenderWidth &&
                frame->depth_inverted,
            "the D3D11 frame's rectangles or flags were not carried");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = frame->motion_vectors.Get();
        barrier.Transition.StateBefore = frame->resource_state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = frame->motion_vectors.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Offset = eye * 256ULL * kRenderHeight;
        destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16_FLOAT, kRenderWidth, kRenderHeight, 1, 256};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
    require_hresult(list->Close(), "Close");
    ID3D12CommandList* lists[]{list.Get()};
    queue12->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    require_hresult(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())), "fence");
    require_hresult(queue12->Signal(fence.Get(), 1), "Signal");
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require_hresult(fence->SetEventOnCompletion(1, done), "SetEventOnCompletion");
    require(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "the D3D12 queue did not finish");
    CloseHandle(done);
    std::uint32_t* mapped = nullptr;
    require_hresult(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map");
    const std::uint32_t eye0 = mapped[0];
    const std::uint32_t eye1 = mapped[256U * kRenderHeight / 4U];
    readback->Unmap(0, nullptr);
    require((eye0 == kLeftVector && eye1 == kRightVector) || (eye0 == kRightVector && eye1 == kLeftVector),
        "the D3D12 snapshots do not hold the vectors the game wrote in D3D11");

    const auto released_before = xrfg::ngx_guide_capture_statistics().features_released;
    require(NVSDK_NGX_SUCCEED(release(left)) && NVSDK_NGX_SUCCEED(release(right)),
        "D3D11 release failed through the hook");
    require(xrfg::ngx_guide_capture_statistics().features_released == released_before + 2,
        "the two D3D11 features were not released");
    xrfg::stop_ngx_guide_capture_d3d11(queue12.Get());
    std::cout << "ngx guide capture D3D11: publications=" << statistics.d3d11_publications
              << " eye0=0x" << std::hex << eye0 << " eye1=0x" << eye1 << std::dec << "\n";
    return 0;
}

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
    // Its D3D12 exports, and since V512 its D3D11 ones.
    require(statistics.modules_hooked == 2 && statistics.d3d11_modules_hooked == 1 &&
            statistics.modules_failed == 0,
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
    return run_d3d11(module);
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
