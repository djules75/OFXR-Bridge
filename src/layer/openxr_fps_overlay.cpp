#include "xrfg/openxr_fps_overlay.hpp"
#include "xrfg/steamvr_delivery.hpp"
#include <openxr/openxr_platform.h>
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace xrfg {
using Microsoft::WRL::ComPtr;
namespace {
std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
template<class T> bool load(PFN_xrGetInstanceProcAddr get, XrInstance instance,
                          const char* name, T& output) {
    PFN_xrVoidFunction function{};
    if (!get || XR_FAILED(get(instance, name, &function)) || !function) return false;
    output = reinterpret_cast<T>(function);
    return true;
}
}

struct OpenXrFpsOverlay::Impl {
    XrInstance instance{};
    XrSession session{};
    XrSystemId system{};
    PFN_xrGetInstanceProcAddr get{};
    PFN_xrEndFrame downstream_end{};
    PFN_xrCreateReferenceSpace create_space{};
    PFN_xrDestroySpace destroy_space{};
    PFN_xrCreateSwapchain create_swapchain{};
    PFN_xrDestroySwapchain destroy_swapchain{};
    PFN_xrEnumerateSwapchainFormats enumerate_formats{};
    PFN_xrEnumerateSwapchainImages enumerate_images{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrGetSystemProperties system_properties{};
    std::filesystem::path ini;
    std::mutex mutex;
    FpsCounter counter;
    // status.json's game and synthetic rates; counted whether the overlay is
    // shown or not.
    FpsCounter application_counter;
    FpsCounter synthetic_counter;
    FpsOverlayPosition position{FpsOverlayPosition::upper_right};
    FpsOverlaySize size{FpsOverlaySize::big};
    bool show_latency{true};
    // Added-latency samples from whichever thread submits the real frame,
    // drained into a window at every redraw; the figure is the mean of the
    // last four windows, about a second.
    std::atomic<std::int64_t> latency_sum_ns{0};
    std::atomic<std::int64_t> latency_samples{0};
    std::array<std::int64_t, 4> latency_window_sum{};
    std::array<std::int64_t, 4> latency_window_count{};
    std::size_t latency_window{};
    std::int64_t next_refresh{};
    bool attempted{}, initialized{}, image_valid{}, acquired{}, waited{}, disabled{};
    bool paused{};
    std::atomic<std::int64_t> display_period_ns{0};
    // The number the overlay draws is what the headset received where that
    // can be known, and what was submitted everywhere else. Only SteamVR
    // reports the former. Borrowed: the session owns it, because the presenter
    // reads the vsync anchor from the same connection.
    SteamVrDelivery* delivery{};
    XrSpace space{};
    XrSwapchain swapchain{};
    std::uint32_t index{}, width{}, height{}, max_layers{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    OverlayPlacement placement{};
    bool placement_valid{};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    ComPtr<ID3D12Fence> fence12;
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D11Multithread> multithread11;
    ComPtr<ID3D11Fence> fence11;
    std::uint64_t next_fence{1}, last_fence{};
    HANDLE event{};
    struct Image {
        ComPtr<ID3D12Resource> texture12, upload;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D11Texture2D> texture11;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        std::uint64_t fence_value{};
    };
    std::vector<Image> images;

    ~Impl() {
        // Only teardown waits for this overlay's last upload. No global queue
        // drain, no runtime frame calls, no application-owned handle retention.
        if (last_fence && event) {
            HRESULT result = S_OK;
            if (fence12 && fence12->GetCompletedValue() < last_fence)
                result = fence12->SetEventOnCompletion(last_fence, event);
            else if (fence11 && fence11->GetCompletedValue() < last_fence)
                result = fence11->SetEventOnCompletion(last_fence, event);
            else result = S_FALSE;
            if (result == S_OK) WaitForSingleObject(event, INFINITE);
        }
        if (swapchain && destroy_swapchain) destroy_swapchain(swapchain);
        if (space && destroy_space) destroy_space(space);
        if (event) CloseHandle(event);
    }

    bool initialize(std::uint32_t eye_width) {
        if (attempted) return initialized;
        attempted = true;
        if ((!device11 && (!device12 || !queue12)) || !system) return false;
        if (!load(get, instance, "xrCreateReferenceSpace", create_space) ||
            !load(get, instance, "xrDestroySpace", destroy_space) ||
            !load(get, instance, "xrCreateSwapchain", create_swapchain) ||
            !load(get, instance, "xrDestroySwapchain", destroy_swapchain) ||
            !load(get, instance, "xrEnumerateSwapchainFormats", enumerate_formats) ||
            !load(get, instance, "xrEnumerateSwapchainImages", enumerate_images) ||
            !load(get, instance, "xrAcquireSwapchainImage", acquire) ||
            !load(get, instance, "xrWaitSwapchainImage", wait) ||
            !load(get, instance, "xrReleaseSwapchainImage", release) ||
            !load(get, instance, "xrGetSystemProperties", system_properties)) return false;
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_FAILED(system_properties(instance, system, &properties))) return false;
        max_layers = std::min(properties.graphicsProperties.maxLayerCount, 128u);
        if (max_layers < 2) return false;
        std::uint32_t count{};
        if (XR_FAILED(enumerate_formats(session, 0, &count, nullptr)) || !count || count > 1024) return false;
        std::vector<std::int64_t> formats(count);
        if (XR_FAILED(enumerate_formats(session, count, &count, formats.data()))) return false;
        for (auto candidate : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                               DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
            if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) { format = candidate; break; }
        }
        if (format == DXGI_FORMAT_UNKNOWN) return false;
        width = overlay_texture_width(eye_width);
        // 24x16 of the number's grid: its 24x12 and four rows below for the
        // latency (rasterize_fps_overlay).
        height = width * 2 / 3;
        if (width > properties.graphicsProperties.maxSwapchainImageWidth ||
            height > properties.graphicsProperties.maxSwapchainImageHeight) return false;
        XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        space_info.poseInReferenceSpace.orientation.w = 1;
        if (XR_FAILED(create_space(session, &space_info, &space))) return false;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
            XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = info.faceCount = info.arraySize = info.mipCount = 1;
        info.width = width;
        info.height = height;
        if (XR_FAILED(create_swapchain(session, &info, &swapchain))) return false;
        if (XR_FAILED(enumerate_images(swapchain, 0, &count, nullptr)) || !count || count > 64) return false;
        images.resize(count);
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return false;
        if (device11) {
            std::vector<XrSwapchainImageD3D11KHR> buffers(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if (XR_FAILED(enumerate_images(swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(buffers.data())))) return false;
            for (std::size_t i = 0; i < images.size(); ++i) {
                images[i].texture11 = buffers[i].texture;
                if (!images[i].texture11) return false;
            }
            ComPtr<ID3D11DeviceContext> immediate;
            device11->GetImmediateContext(&immediate);
            if (!immediate || FAILED(immediate.As(&context11))) return false;
            immediate.As(&multithread11);
            ComPtr<ID3D11Device5> device5;
            if (FAILED(device11.As(&device5)) || FAILED(device5->CreateFence(
                0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence11)))) return false;
        } else {
            std::vector<XrSwapchainImageD3D12KHR> buffers(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            if (XR_FAILED(enumerate_images(swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(buffers.data())))) return false;
            if (FAILED(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence12)))) return false;
            for (std::size_t i = 0; i < images.size(); ++i) {
                auto& image = images[i];
                image.texture12 = buffers[i].texture;
                if (!image.texture12) return false;
                const auto desc = image.texture12->GetDesc();
                UINT64 bytes{};
                device12->GetCopyableFootprints(&desc, 0, 1, 0, &image.footprint, nullptr, nullptr, &bytes);
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC upload_desc{};
                upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                upload_desc.Width = bytes;
                upload_desc.Height = upload_desc.DepthOrArraySize = upload_desc.MipLevels = 1;
                upload_desc.SampleDesc.Count = 1;
                upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&image.upload))) ||
                    FAILED(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&image.allocator))) ||
                    FAILED(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, image.allocator.Get(),
                        nullptr, IID_PPV_ARGS(&image.list))) || FAILED(image.list->Close())) return false;
            }
        }
        quad.space = space;
        quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = swapchain;
        quad.subImage.imageRect.extent = {static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
        quad.pose.orientation.w = 1;
        initialized = true;
        return true;
    }

    bool upload(const FpsSnapshot& snapshot) {
        if (!acquired) {
            XrSwapchainImageAcquireInfo info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(acquire(swapchain, &info, &index))) { disabled = true; return false; }
            acquired = true;
            waited = false;
        }
        if (index >= images.size()) { disabled = true; return false; }
        if (!waited) {
            XrSwapchainImageWaitInfo info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            info.timeout = 0;
            const auto result = wait(swapchain, &info);
            // XR_TIMEOUT_EXPIRED is positive, but does NOT grant ownership.
            if (result == XR_TIMEOUT_EXPIRED) return false;
            if (result != XR_SUCCESS) { disabled = true; return false; }
            waited = true;
        }
        auto& image = images[index];
        const auto completed = fence12 ? fence12->GetCompletedValue() : fence11->GetCompletedValue();
        if (completed == UINT64_MAX) { disabled = true; return false; }
        if (completed < image.fence_value) return false;
        const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const auto pixels = rasterize_fps_overlay(width, height, snapshot, bgra, size);
        if (pixels.empty()) return false;
        HRESULT result = S_OK;
        const auto value = next_fence++;
        if (device11) {
            // Runs on the same application end-frame thread as bridge capture,
            // never from the asynchronous presenter. No context state changes.
            if (multithread11) multithread11->Enter();
            context11->UpdateSubresource(image.texture11.Get(), 0, nullptr, pixels.data(), width * 4, 0);
            result = context11->Signal(fence11.Get(), value);
            context11->Flush();
            if (multithread11) multithread11->Leave();
        } else {
            if (FAILED(image.allocator->Reset()) || FAILED(image.list->Reset(image.allocator.Get(), nullptr))) {
                disabled = true; return false;
            }
            void* mapped{};
            const D3D12_RANGE empty{};
            if (FAILED(image.upload->Map(0, &empty, &mapped))) { disabled = true; return false; }
            for (std::uint32_t row = 0; row < height; ++row)
                std::memcpy(static_cast<std::uint8_t*>(mapped) + image.footprint.Offset +
                    static_cast<std::size_t>(row) * image.footprint.Footprint.RowPitch,
                    pixels.data() + static_cast<std::size_t>(row) * width, width * 4);
            image.upload->Unmap(0, nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = image.texture12.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            image.list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
            source.pResource = image.upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = image.footprint;
            destination.pResource = image.texture12.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            image.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            image.list->ResourceBarrier(1, &barrier);
            if (FAILED(image.list->Close())) { disabled = true; return false; }
            ID3D12CommandList* lists[]{image.list.Get()};
            queue12->ExecuteCommandLists(1, lists);
            result = queue12->Signal(fence12.Get(), value);
        }
        if (FAILED(result)) { disabled = true; return false; }
        last_fence = image.fence_value = value;
        XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(release(swapchain, &info))) { disabled = true; return false; }
        acquired = waited = false;
        image_valid = true;
        return true;
    }

    void application_frame(const XrFrameEndInfo* info) {
        const auto now = now_ns();
        if (!info || info->type != XR_TYPE_FRAME_END_INFO) return;
        application_counter.submitted(now, false, true);
        if (now < next_refresh) return;
        next_refresh = now + 250'000'000;
        latency_window = (latency_window + 1) % latency_window_sum.size();
        latency_window_sum[latency_window] =
            latency_sum_ns.exchange(0, std::memory_order_relaxed);
        latency_window_count[latency_window] =
            latency_samples.exchange(0, std::memory_order_relaxed);
        std::array<wchar_t, 32> setting{};
        GetPrivateProfileStringW(L"overlay", L"position", L"upper_right",
            setting.data(), static_cast<DWORD>(setting.size()), ini.c_str());
        std::string value;
        for (wchar_t c : setting) { if (!c) break; value += c < 128 ? static_cast<char>(c) : '?'; }
        position = parse_overlay_position(value);
        GetPrivateProfileStringW(L"overlay", L"size", L"big",
            setting.data(), static_cast<DWORD>(setting.size()), ini.c_str());
        value.clear();
        for (wchar_t c : setting) { if (!c) break; value += c < 128 ? static_cast<char>(c) : '?'; }
        size = parse_overlay_size(value);
        show_latency = GetPrivateProfileIntW(L"overlay", L"latency", 1, ini.c_str()) != 0;
        if (position == FpsOverlayPosition::off || disabled || !info->layerCount || !info->layers) return;
        float left = -1, right = 1, down = -1, up = 1;
        std::uint32_t eye_width = 0;
        for (std::uint32_t i = 0; i < info->layerCount; ++i) {
            const auto* layer = info->layers[i];
            if (!layer || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
            const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if (!projection->views || projection->viewCount > 16) continue;
            for (std::uint32_t v = 0; v < projection->viewCount; ++v) {
                const auto& view = projection->views[v];
                if (view.subImage.imageRect.extent.width <= 0) continue;
                const auto w = static_cast<std::uint32_t>(view.subImage.imageRect.extent.width);
                eye_width = eye_width ? std::min(eye_width, w) : w;
                // The quad uses physical visible bounds, not the source image's
                // vertical storage direction. Never reorder the submitted FOV.
                const float down_angle = std::min(view.fov.angleDown, view.fov.angleUp);
                const float up_angle = std::max(view.fov.angleDown, view.fov.angleUp);
                if (std::isfinite(view.fov.angleLeft) && std::isfinite(view.fov.angleRight) &&
                    std::isfinite(view.fov.angleDown) && std::isfinite(view.fov.angleUp) &&
                    view.fov.angleLeft < 0 && view.fov.angleRight > 0 &&
                    down_angle < 0 && up_angle > 0 &&
                    down_angle > -1.57079632679489661923F && up_angle < 1.57079632679489661923F) {
                    left = std::max(left, std::tan(view.fov.angleLeft));
                    right = std::min(right, std::tan(view.fov.angleRight));
                    down = std::max(down, std::tan(down_angle));
                    up = std::min(up, std::tan(up_angle));
                }
            }
        }
        if (!eye_width) return;
        placement = overlay_placement(position, left, right, down, up);
        placement_valid = true;
        if (!initialize(eye_width)) return;
        // A quad layer in front of the view, which the runtime composites in
        // the order the layer list gives. An application that submits its own
        // layer over the whole view - a HUD, a menu, a loading or fade overlay
        // - is composited on top of this one and hides the counter until the
        // head turns enough to move it off. Reproduced in Callisto Protocol,
        // and reported as the counter vanishing, so rule it out before
        // suspecting the upload path.
        // The texture is a third taller than the number's panel, the extra
        // below it, so the number stays where the placement puts it.
        quad.pose.position = {placement.x, placement.y - placement.height / 6.0f, placement.z};
        quad.size = {placement.width, placement.height * 4.0f / 3.0f};
        auto snapshot = displayed_snapshot(now);
        snapshot.paused = paused;
        // Only while generation runs: nothing beside a red or paused number.
        snapshot.latency_ms = show_latency && snapshot.active && !paused
            ? latency_ms() : -1;
        upload(snapshot);
    }

    int latency_ms() const {
        std::int64_t sum = 0, count = 0;
        for (std::size_t i = 0; i < latency_window_sum.size(); ++i) {
            sum += latency_window_sum[i];
            count += latency_window_count[i];
        }
        return count > 0 && sum >= 0
            ? static_cast<int>((sum / count + 500'000) / 1'000'000)
            : -1;
    }

    void clear_latency() {
        latency_sum_ns.store(0, std::memory_order_relaxed);
        latency_samples.store(0, std::memory_order_relaxed);
        latency_window_sum.fill(0);
        latency_window_count.fill(0);
    }

    // The figure the overlay draws: what the headset received where SteamVR
    // reports it, less the repeats, snapped to the refresh rate within 2%.
    FpsSnapshot displayed_snapshot(std::int64_t now) const {
        auto snapshot = counter.snapshot(now);
        if (delivery) {
            if (const auto received = delivery->delivered_fps(now)) {
                snapshot.submitted_fps = delivered_new_images(*received, snapshot);
            }
        }
        if (const auto period = display_period_ns.load(std::memory_order_relaxed); period > 0) {
            snapshot.submitted_fps = displayed_rate(
                snapshot.submitted_fps, 1.0e9f / static_cast<float>(period));
        }
        return snapshot;
    }
};

OpenXrFpsOverlay::OpenXrFpsOverlay(XrInstance instance, XrSession session, XrSystemId system,
    PFN_xrGetInstanceProcAddr get_proc, PFN_xrEndFrame end_frame,
    ID3D12Device* device12, ID3D12CommandQueue* queue12,
    ID3D11Device* device11, const std::filesystem::path& ini,
    SteamVrDelivery* delivery) : impl_(std::make_unique<Impl>()) {
    impl_->instance = instance; impl_->session = session; impl_->system = system;
    impl_->get = get_proc; impl_->downstream_end = end_frame;
    impl_->device12 = device12; impl_->queue12 = queue12; impl_->device11 = device11;
    impl_->ini = ini;
    impl_->delivery = delivery;
}
OpenXrFpsOverlay::~OpenXrFpsOverlay() = default;

void OpenXrFpsOverlay::application_frame(const XrFrameEndInfo* info) noexcept {
    try { std::scoped_lock lock(impl_->mutex); impl_->application_frame(info); }
    catch (...) { /* Overlay is optional: never fail the game's frame. */ }
}
void OpenXrFpsOverlay::reset_metrics() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->counter.reset();
    impl_->application_counter.reset();
    impl_->synthetic_counter.reset();
    impl_->clear_latency();
    impl_->image_valid = false;
    impl_->next_refresh = 0;
    impl_->placement_valid = false;
}

void OpenXrFpsOverlay::set_display_period(std::int64_t period_ns) noexcept {
    impl_->display_period_ns.store(period_ns > 0 ? period_ns : 0, std::memory_order_relaxed);
}

void OpenXrFpsOverlay::add_latency_sample(std::int64_t latency_ns) noexcept {
    impl_->latency_sum_ns.fetch_add(latency_ns, std::memory_order_relaxed);
    impl_->latency_samples.fetch_add(1, std::memory_order_relaxed);
}

void OpenXrFpsOverlay::set_paused(bool paused) noexcept {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->paused == paused) return;
    impl_->paused = paused;
    impl_->next_refresh = 0; // Redraw now, not at the next quarter second.
}

void OpenXrFpsOverlay::suspend() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->disabled = true;
    impl_->image_valid = false;
    impl_->counter.reset();
    impl_->application_counter.reset();
    impl_->synthetic_counter.reset();
    impl_->clear_latency();
}

FpsSnapshot OpenXrFpsOverlay::metrics() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->counter.snapshot(now_ns());
}

OpenXrFpsOverlay::Status OpenXrFpsOverlay::status() const noexcept {
    try {
        std::scoped_lock lock(impl_->mutex);
        const auto now = now_ns();
        Status status;
        status.app_fps = impl_->application_counter.snapshot(now).submitted_fps;
        status.synthetic_fps = impl_->synthetic_counter.snapshot(now).submitted_fps;
        status.output_fps = impl_->displayed_snapshot(now).submitted_fps;
        const auto period = impl_->display_period_ns.load(std::memory_order_relaxed);
        status.display_hz = period > 0 ? 1.0e9f / static_cast<float>(period) : 0.0f;
        return status;
    } catch (...) {
        return {};
    }
}

std::optional<OverlayPlacement> OpenXrFpsOverlay::marker_placement() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->disabled || impl_->position == FpsOverlayPosition::off || !impl_->placement_valid)
        return std::nullopt;
    return impl_->placement;
}

XrResult OpenXrFpsOverlay::end_frame(
    const XrFrameEndInfo* info, bool synthetic, bool new_content) {
    std::scoped_lock lock(impl_->mutex);
    std::array<const XrCompositionLayerBaseHeader*, 128> layers{};
    XrFrameEndInfo composed{};
    const bool nonempty = info && info->type == XR_TYPE_FRAME_END_INFO && info->layers && info->layerCount;
    const XrFrameEndInfo* submitted = info;
    if (nonempty && impl_->initialized && impl_->image_valid && !impl_->disabled &&
        impl_->position != FpsOverlayPosition::off && info->layerCount < impl_->max_layers) {
        std::copy_n(info->layers, info->layerCount, layers.begin());
        layers[info->layerCount] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&impl_->quad);
        composed = *info; // Preserve every app layer, next chain and display time.
        composed.layers = layers.data();
        ++composed.layerCount;
        submitted = &composed;
    }
    const auto result = impl_->downstream_end(impl_->session, submitted);
    if (result == XR_SUCCESS && nonempty) {
        const auto now = now_ns();
        impl_->counter.submitted(now, synthetic, new_content);
        if (synthetic && new_content) impl_->synthetic_counter.submitted(now, false, true);
    }
    return result;
}

} // namespace xrfg
