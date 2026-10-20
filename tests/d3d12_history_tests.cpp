#include "xrfg/d3d12_history.hpp"
#include "xrfg/bridge_flight_logger.hpp"
#include "xrfg/dlss_motion_vectors.hpp"
#include <filesystem>
#include <fstream>
#include "xrfg/d3d12_frame_synthesizer.hpp"
#include "xrfg/live_tuning.hpp"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kWidth = 7;
constexpr UINT kHeight = 3;
constexpr UINT kEyeCount = 2;
constexpr UINT kBytesPerPixel = 4;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

using StereoPattern = std::array<std::vector<std::uint8_t>, kEyeCount>;
using RgbaBytes = std::array<std::uint8_t, kBytesPerPixel>;
using ReprojectionViews =
    std::array<xrfg::D3D12ReprojectionView, kEyeCount>;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void require_hresult(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        fail(std::string(operation) + " failed with HRESULT " +
             std::to_string(static_cast<std::int32_t>(result)));
    }
}

[[nodiscard]] bool operation_succeeded(HRESULT result) noexcept {
    return SUCCEEDED(result);
}

void require_frame_start_gate(
    xrfg::D3D12FrameSynthesizer& synthesizer,
    const std::string& label) {
    const HRESULT result = synthesizer.wait_for_previous_submission(500);
    require(
        operation_succeeded(result),
        label + " failed with HRESULT " +
            std::to_string(static_cast<std::int32_t>(result)));
}

[[nodiscard]] D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_BARRIER transition_barrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

[[nodiscard]] bool same_description(
    const D3D12_RESOURCE_DESC& left,
    const D3D12_RESOURCE_DESC& right) noexcept {
    return left.Dimension == right.Dimension &&
           left.Alignment == right.Alignment &&
           left.Width == right.Width &&
           left.Height == right.Height &&
           left.DepthOrArraySize == right.DepthOrArraySize &&
           left.MipLevels == right.MipLevels &&
           left.Format == right.Format &&
           left.SampleDesc.Count == right.SampleDesc.Count &&
           left.SampleDesc.Quality == right.SampleDesc.Quality &&
           left.Layout == right.Layout &&
           left.Flags == right.Flags;
}

class D3D12WarpFixture final {
public:
    explicit D3D12WarpFixture(bool use_nvidia_hardware = false) {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug_.GetAddressOf())))) {
            debug_->EnableDebugLayer();
            debug_enabled_ = true;
        }

        UINT factory_flags = debug_enabled_ ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT factory_result =
            CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(factory_.GetAddressOf()));
        if (FAILED(factory_result) && factory_flags != 0) {
            factory_.Reset();
            factory_flags = 0;
            factory_result =
                CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(factory_.GetAddressOf()));
        }
        require_hresult(factory_result, "CreateDXGIFactory2");

        if (use_nvidia_hardware) {
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT enumeration = factory_->EnumAdapterByGpuPreference(
                    index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(candidate.GetAddressOf()));
                if (enumeration == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                require_hresult(
                    enumeration,
                    "IDXGIFactory6::EnumAdapterByGpuPreference");
                DXGI_ADAPTER_DESC1 description{};
                require_hresult(candidate->GetDesc1(&description), "IDXGIAdapter1::GetDesc1");
                if (description.VendorId == 0x10deU &&
                    (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                    adapter_ = candidate;
                    break;
                }
            }
            require(adapter_ != nullptr, "no NVIDIA hardware adapter found");
        } else {
            require_hresult(
                factory_->EnumWarpAdapter(IID_PPV_ARGS(adapter_.GetAddressOf())),
                "IDXGIFactory::EnumWarpAdapter");
        }
        require_hresult(
            D3D12CreateDevice(
                adapter_.Get(),
                use_nvidia_hardware
                    ? D3D_FEATURE_LEVEL_12_0
                    : D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(device_.GetAddressOf())),
            use_nvidia_hardware
                ? "D3D12CreateDevice(NVIDIA)"
                : "D3D12CreateDevice(WARP)");

        D3D12_COMMAND_QUEUE_DESC queue_description{};
        queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queue_description.NodeMask = 0;
        require_hresult(
            device_->CreateCommandQueue(
                &queue_description,
                IID_PPV_ARGS(queue_.GetAddressOf())),
            "ID3D12Device::CreateCommandQueue");
        require_hresult(
            device_->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(fence_.GetAddressOf())),
            "ID3D12Device::CreateFence");

        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(fence_event_ != nullptr, "CreateEventW failed");

        if (debug_enabled_) {
            (void)device_.As(&info_queue_);
            if (info_queue_ != nullptr) {
                info_queue_->ClearStoredMessages();
            }
        }
    }

    ~D3D12WarpFixture() {
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
        }
    }

    D3D12WarpFixture(const D3D12WarpFixture&) = delete;
    D3D12WarpFixture& operator=(const D3D12WarpFixture&) = delete;

    [[nodiscard]] ID3D12Device* device() const noexcept {
        return device_.Get();
    }

    [[nodiscard]] ID3D12CommandQueue* queue() const noexcept {
        return queue_.Get();
    }

    template <typename Recorder>
    void execute_and_wait(Recorder&& recorder) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> command_list;
        require_hresult(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.GetAddressOf())),
            "ID3D12Device::CreateCommandAllocator");
        require_hresult(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(command_list.GetAddressOf())),
            "ID3D12Device::CreateCommandList");

        recorder(command_list.Get());
        require_hresult(command_list->Close(), "ID3D12GraphicsCommandList::Close");
        ID3D12CommandList* command_lists[] = {command_list.Get()};
        queue_->ExecuteCommandLists(1, command_lists);

        const std::uint64_t fence_value = next_fence_value_++;
        require_hresult(queue_->Signal(fence_.Get(), fence_value), "ID3D12CommandQueue::Signal");
        wait_for_fence(fence_.Get(), fence_value);
    }

    void wait_for_fence(ID3D12Fence* fence, std::uint64_t value) {
        require(fence != nullptr, "cannot wait on a null fence");
        const std::uint64_t completed = fence->GetCompletedValue();
        require(
            completed != std::numeric_limits<std::uint64_t>::max(),
            "D3D12 fence reports device removal");
        if (completed >= value) {
            return;
        }

        require_hresult(
            fence->SetEventOnCompletion(value, fence_event_),
            "ID3D12Fence::SetEventOnCompletion");
        require(
            WaitForSingleObject(fence_event_, 10'000) == WAIT_OBJECT_0,
            "timed out waiting for WARP command completion");
        require(
            fence->GetCompletedValue() >= value,
            "D3D12 fence event fired before the requested value completed");
    }

    void require_no_debug_errors() const {
        if (!debug_enabled_ || info_queue_ == nullptr) {
            return;
        }

        const std::uint64_t message_count =
            info_queue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (std::uint64_t index = 0; index < message_count; ++index) {
            SIZE_T message_size = 0;
            require_hresult(
                info_queue_->GetMessage(index, nullptr, &message_size),
                "ID3D12InfoQueue::GetMessage(size)");
            std::vector<std::byte> storage(message_size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            require_hresult(
                info_queue_->GetMessage(index, message, &message_size),
                "ID3D12InfoQueue::GetMessage");
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                fail(std::string("D3D12 debug-layer error: ") +
                     (message->pDescription == nullptr ? "<no description>"
                                                       : message->pDescription));
            }
        }
    }

private:
    ComPtr<ID3D12Debug> debug_;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> info_queue_;
    HANDLE fence_event_{};
    std::uint64_t next_fence_value_{1};
    bool debug_enabled_{};
};

[[nodiscard]] D3D12_RESOURCE_DESC stereo_texture_description(
    UINT width = kWidth,
    UINT height = kHeight) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Alignment = 0;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = static_cast<UINT16>(kEyeCount);
    description.MipLevels = 1;
    description.Format = kFormat;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    return description;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_source_texture(
    D3D12WarpFixture& fixture,
    UINT width = kWidth,
    UINT height = kHeight,
    // The layer's private swapchains rest in COMMON when the synthesis runs
    // on a queue of its own, because that is the state D3D12 wants at a queue
    // ownership transfer. A destination created in any other state would not
    // match the first barrier those command lists record.
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_RENDER_TARGET) {
    const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC description = stereo_texture_description(width, height);
    ComPtr<ID3D12Resource> texture;
    require_hresult(
        fixture.device()->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            initial_state,
            nullptr,
            IID_PPV_ARGS(texture.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(source texture)");
    return texture;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_buffer(
    D3D12WarpFixture& fixture,
    D3D12_HEAP_TYPE heap_type,
    std::uint64_t size,
    D3D12_RESOURCE_STATES initial_state);

[[nodiscard]] ComPtr<ID3D12Resource> create_single_slice_texture(
    D3D12WarpFixture& fixture,
    UINT width,
    UINT height) {
    D3D12_RESOURCE_DESC description = stereo_texture_description(width, height);
    description.DepthOrArraySize = 1;
    const D3D12_HEAP_PROPERTIES properties =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    require_hresult(
        fixture.device()->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            nullptr,
            IID_PPV_ARGS(texture.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(single-slice texture)");
    return texture;
}

void clear_double_wide_pattern(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    UINT half_width,
    UINT height) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    require_hresult(
        fixture.device()->CreateDescriptorHeap(
            &heap_description,
            IID_PPV_ARGS(rtv_heap.GetAddressOf())),
        "ID3D12Device::CreateDescriptorHeap(double-wide RTV)");

    D3D12_RENDER_TARGET_VIEW_DESC rtv_description{};
    rtv_description.Format = kFormat;
    rtv_description.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_description.Texture2DArray.MipSlice = 0;
    rtv_description.Texture2DArray.FirstArraySlice = 0;
    rtv_description.Texture2DArray.ArraySize = 1;
    fixture.device()->CreateRenderTargetView(
        texture,
        &rtv_description,
        rtv_heap->GetCPUDescriptorHandleForHeapStart());

    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        constexpr FLOAT kLeftColor[4] = {1.0F, 0.0F, 0.0F, 1.0F};
        constexpr FLOAT kRightColor[4] = {0.0F, 0.0F, 1.0F, 1.0F};
        const D3D12_RECT left_rect{
            0, 0, static_cast<LONG>(half_width), static_cast<LONG>(height)};
        const D3D12_RECT right_rect{
            static_cast<LONG>(half_width),
            0,
            static_cast<LONG>(half_width * 2U),
            static_cast<LONG>(height)};
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        command_list->ClearRenderTargetView(rtv, kLeftColor, 1, &left_rect);
        command_list->ClearRenderTargetView(rtv, kRightColor, 1, &right_rect);
    });
}

[[nodiscard]] std::vector<std::uint8_t> readback_single_slice(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    D3D12_RESOURCE_STATES initial_state) {
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    require(
        description.DepthOrArraySize == 1 &&
            description.Width <= std::numeric_limits<UINT>::max(),
        "single-slice readback texture shape is unsupported");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT row_count = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    fixture.device()->GetCopyableFootprints(
        &description,
        0,
        1,
        0,
        &layout,
        &row_count,
        &row_size,
        &total_size);
    require(total_size != 0, "single-slice readback layout is empty");
    ComPtr<ID3D12Resource> readback = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_READBACK,
        total_size,
        D3D12_RESOURCE_STATE_COPY_DEST);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(
            texture, initial_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = layout;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const D3D12_RESOURCE_BARRIER restore = transition_barrier(
            texture, D3D12_RESOURCE_STATE_COPY_SOURCE, initial_state);
        command_list->ResourceBarrier(1, &restore);
    });

    const UINT width = static_cast<UINT>(description.Width);
    const UINT height = description.Height;
    require(
        row_count == height && row_size == width * kBytesPerPixel,
        "single-slice readback layout is unexpected");
    std::vector<std::uint8_t> output(
        static_cast<std::size_t>(width) * height * kBytesPerPixel);
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
    require_hresult(
        readback->Map(0, &read_range, &mapped),
        "ID3D12Resource::Map(single-slice readback)");
    const auto* bytes = static_cast<const std::byte*>(mapped);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(
            output.data() + static_cast<std::size_t>(y) * width * kBytesPerPixel,
            bytes + layout.Offset +
                static_cast<std::size_t>(y) * layout.Footprint.RowPitch,
            static_cast<std::size_t>(row_size));
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
    return output;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_depth_source_texture(
    D3D12WarpFixture& fixture,
    D3D12_RESOURCE_FLAGS additional_flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC description = stereo_texture_description();
    description.Format = kDepthFormat;
    description.Flags = static_cast<D3D12_RESOURCE_FLAGS>(
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | additional_flags);

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = kDepthFormat;
    clear_value.DepthStencil.Depth = 1.0F;
    clear_value.DepthStencil.Stencil = 0;

    const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    require_hresult(
        fixture.device()->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(texture.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(depth source texture)");
    return texture;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_buffer(
    D3D12WarpFixture& fixture,
    D3D12_HEAP_TYPE heap_type,
    std::uint64_t size,
    D3D12_RESOURCE_STATES initial_state) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Alignment = 0;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;

    const D3D12_HEAP_PROPERTIES properties = heap_properties(heap_type);
    ComPtr<ID3D12Resource> buffer;
    require_hresult(
        fixture.device()->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            initial_state,
            nullptr,
            IID_PPV_ARGS(buffer.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(buffer)");
    return buffer;
}

[[nodiscard]] StereoPattern make_pattern(std::uint8_t seed) {
    StereoPattern pattern;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        auto& bytes = pattern[eye];
        bytes.resize(static_cast<std::size_t>(kWidth) * kHeight * kBytesPerPixel);
        for (UINT y = 0; y < kHeight; ++y) {
            for (UINT x = 0; x < kWidth; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kWidth + x) * kBytesPerPixel;
                bytes[offset + 0] = static_cast<std::uint8_t>(seed + eye * 37U + x * 11U + y * 3U);
                bytes[offset + 1] = static_cast<std::uint8_t>(seed + eye * 19U + x * 5U + y * 17U);
                bytes[offset + 2] = static_cast<std::uint8_t>(seed + eye * 53U + x * 7U + y * 13U);
                bytes[offset + 3] = static_cast<std::uint8_t>(255U - eye * 7U - x - y);
            }
        }
    }
    return pattern;
}

[[nodiscard]] ReprojectionViews make_reprojection_views(float yaw_radians = 0.0F) {
    ReprojectionViews views{};
    const float half_yaw = yaw_radians * 0.5F;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        views[eye].pose.orientation.y = std::sin(half_yaw);
        views[eye].pose.orientation.w = std::cos(half_yaw);
        views[eye].pose.position.x = eye == 0 ? -0.032F : 0.032F;
        views[eye].fov.angle_left = eye == 0 ? -0.7853981633974483F
                                             : -0.7504915783575616F;
        views[eye].fov.angle_right = eye == 0 ? 0.7504915783575616F
                                              : 0.7853981633974483F;
        views[eye].fov.angle_up = eye == 0 ? 0.6108652381980153F
                                           : 0.5934119456780721F;
        views[eye].fov.angle_down = eye == 0 ? -0.5934119456780721F
                                             : -0.6108652381980153F;
    }
    return views;
}

[[nodiscard]] StereoPattern make_solid_pattern(
    const std::array<RgbaBytes, kEyeCount>& colors) {
    StereoPattern pattern;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        auto& bytes = pattern[eye];
        bytes.resize(static_cast<std::size_t>(kWidth) * kHeight * kBytesPerPixel);
        for (std::size_t offset = 0; offset < bytes.size(); offset += kBytesPerPixel) {
            std::copy(colors[eye].begin(), colors[eye].end(), bytes.begin() + offset);
        }
    }
    return pattern;
}

[[nodiscard]] StereoPattern midpoint_pattern(
    const StereoPattern& previous,
    const StereoPattern& current) {
    StereoPattern midpoint;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        require(
            previous[eye].size() == current[eye].size(),
            "cannot compute a midpoint for differently sized stereo patterns");
        midpoint[eye].resize(previous[eye].size());
        for (std::size_t index = 0; index < previous[eye].size(); ++index) {
            midpoint[eye][index] = static_cast<std::uint8_t>(
                (static_cast<std::uint16_t>(previous[eye][index]) +
                 static_cast<std::uint16_t>(current[eye][index])) /
                2U);
        }
    }
    return midpoint;
}

[[nodiscard]] RgbaBytes motion_texel(UINT x, UINT y, UINT eye) noexcept {
    const float xf = static_cast<float>(x);
    const float yf = static_cast<float>(y);
    const float eye_phase = static_cast<float>(eye) * 0.7F;
    const auto channel = [](float value) noexcept {
        return static_cast<std::uint8_t>(std::lround(
            std::clamp(value, 0.0F, 255.0F)));
    };
    return {
        channel(128.0F + 82.0F * std::sin(xf * 0.061F + eye_phase) +
                31.0F * std::cos(yf * 0.093F)),
        channel(126.0F + 76.0F * std::sin((xf + yf) * 0.047F + eye_phase) +
                34.0F * std::cos((xf - yf) * 0.037F)),
        channel(124.0F + 69.0F * std::cos(xf * 0.041F - eye_phase) +
                41.0F * std::sin(yf * 0.071F)),
        255,
    };
}

[[nodiscard]] StereoPattern translated_motion_pattern(
    UINT width,
    UINT height,
    const std::array<int, kEyeCount>& translations) {
    StereoPattern pattern;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        auto& bytes = pattern[eye];
        bytes.resize(
            static_cast<std::size_t>(width) * height * kBytesPerPixel,
            0);
        for (std::size_t offset = 3; offset < bytes.size(); offset += kBytesPerPixel) {
            bytes[offset] = 255;
        }
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                const int target_x = static_cast<int>(x) + translations[eye];
                if (target_x < 0 || target_x >= static_cast<int>(width)) {
                    continue;
                }
                const RgbaBytes texel = motion_texel(x, y, eye);
                const std::size_t destination =
                    (static_cast<std::size_t>(y) * width +
                     static_cast<std::size_t>(target_x)) *
                    kBytesPerPixel;
                std::copy(texel.begin(), texel.end(), bytes.begin() + destination);
            }
        }
    }
    return pattern;
}

[[nodiscard]] xrfg::Vec3 cross_product(
    const xrfg::Vec3& left,
    const xrfg::Vec3& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] xrfg::Vec3 rotate_vector(
    const xrfg::Quaternion& quaternion,
    const xrfg::Vec3& input) noexcept {
    const xrfg::Vec3 axis{quaternion.x, quaternion.y, quaternion.z};
    const xrfg::Vec3 inner = cross_product(axis, input);
    const xrfg::Vec3 adjusted{
        inner.x + quaternion.w * input.x,
        inner.y + quaternion.w * input.y,
        inner.z + quaternion.w * input.z,
    };
    const xrfg::Vec3 outer = cross_product(axis, adjusted);
    return {
        input.x + 2.0F * outer.x,
        input.y + 2.0F * outer.y,
        input.z + 2.0F * outer.z,
    };
}

[[nodiscard]] std::uint8_t direction_channel(float value) noexcept {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 255.0F)));
}

[[nodiscard]] StereoPattern ray_direction_pattern(
    UINT width,
    UINT height,
    const ReprojectionViews& views) {
    StereoPattern pattern;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        auto& bytes = pattern[eye];
        bytes.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
        const auto& view = views[eye];
        const float left = std::tan(view.fov.angle_left);
        const float right = std::tan(view.fov.angle_right);
        const float top = std::tan(view.fov.angle_up);
        const float bottom = std::tan(view.fov.angle_down);
        const xrfg::D3D12ImageRect rect =
            view.image_rect.width == 0 && view.image_rect.height == 0
                ? xrfg::D3D12ImageRect{0, 0, width, height}
                : view.image_rect;
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel;
                if (x < rect.offset_x || y < rect.offset_y ||
                    x >= rect.offset_x + rect.width ||
                    y >= rect.offset_y + rect.height) {
                    bytes[offset + 0] = 0;
                    bytes[offset + 1] = 0;
                    bytes[offset + 2] = 0;
                    bytes[offset + 3] = 255;
                    continue;
                }
                const float u =
                    (static_cast<float>(x - rect.offset_x) + 0.5F) /
                    static_cast<float>(rect.width);
                const float v =
                    (static_cast<float>(y - rect.offset_y) + 0.5F) /
                    static_cast<float>(rect.height);
                const float tangent_x = left + (right - left) * u;
                const float tangent_y = top + (bottom - top) * v;
                const xrfg::Vec3 world_ray = rotate_vector(
                    view.pose.orientation,
                    {tangent_x, tangent_y, -1.0F});
                const float azimuth = std::atan2(world_ray.x, -world_ray.z);
                const float elevation = std::atan2(
                    world_ray.y,
                    std::sqrt(world_ray.x * world_ray.x +
                              world_ray.z * world_ray.z));
                const float red =
                    127.5F + 74.0F * std::sin(azimuth * 7.0F + elevation * 2.0F) +
                    31.0F * std::cos(azimuth * 3.0F - elevation * 5.0F);
                const float green =
                    127.5F + 69.0F * std::cos(azimuth * 5.0F + elevation * 4.0F) +
                    27.0F * std::sin(azimuth * 11.0F - elevation * 2.0F);
                const float blue =
                    127.5F + 71.0F * std::sin(azimuth * 9.0F - elevation * 3.0F) +
                    29.0F * std::cos(azimuth * 4.0F + elevation * 6.0F);
                bytes[offset + 0] = direction_channel(red);
                bytes[offset + 1] = direction_channel(green);
                bytes[offset + 2] = direction_channel(blue);
                bytes[offset + 3] = 255;
            }
        }
    }
    return pattern;
}

[[nodiscard]] bool target_pixel_maps_to_source(
    const xrfg::D3D12ReprojectionView& source,
    const xrfg::D3D12ReprojectionView& target,
    UINT width,
    UINT height,
    UINT x,
    UINT y,
    float* mapped_x = nullptr,
    float* mapped_y = nullptr) noexcept {
    const xrfg::D3D12ImageRect source_rect =
        source.image_rect.width == 0 && source.image_rect.height == 0
            ? xrfg::D3D12ImageRect{0, 0, width, height}
            : source.image_rect;
    const xrfg::D3D12ImageRect target_rect =
        target.image_rect.width == 0 && target.image_rect.height == 0
            ? xrfg::D3D12ImageRect{0, 0, width, height}
            : target.image_rect;
    if (x < target_rect.offset_x || y < target_rect.offset_y ||
        x >= target_rect.offset_x + target_rect.width ||
        y >= target_rect.offset_y + target_rect.height) {
        return false;
    }
    const float u =
        (static_cast<float>(x - target_rect.offset_x) + 0.5F) /
        static_cast<float>(target_rect.width);
    const float v =
        (static_cast<float>(y - target_rect.offset_y) + 0.5F) /
        static_cast<float>(target_rect.height);
    const float target_x = std::tan(target.fov.angle_left) +
                           (std::tan(target.fov.angle_right) -
                            std::tan(target.fov.angle_left)) *
                               u;
    const float target_y = std::tan(target.fov.angle_up) +
                           (std::tan(target.fov.angle_down) -
                            std::tan(target.fov.angle_up)) *
                               v;
    const xrfg::Vec3 world_ray = rotate_vector(
        target.pose.orientation,
        {target_x, target_y, -1.0F});
    const xrfg::Quaternion inverse_source{
        -source.pose.orientation.x,
        -source.pose.orientation.y,
        -source.pose.orientation.z,
        source.pose.orientation.w,
    };
    const xrfg::Vec3 source_ray = rotate_vector(inverse_source, world_ray);
    if (source_ray.z >= -1.0e-5F) {
        return false;
    }
    const float tangent_x = source_ray.x / -source_ray.z;
    const float tangent_y = source_ray.y / -source_ray.z;
    const float left = std::tan(source.fov.angle_left);
    const float right = std::tan(source.fov.angle_right);
    const float top = std::tan(source.fov.angle_up);
    const float bottom = std::tan(source.fov.angle_down);
    const float source_u = (tangent_x - left) / (right - left);
    const float source_v = (tangent_y - top) / (bottom - top);
    const float source_x = static_cast<float>(source_rect.offset_x) +
                           source_u * static_cast<float>(source_rect.width) -
                           0.5F;
    const float source_y = static_cast<float>(source_rect.offset_y) +
                           source_v * static_cast<float>(source_rect.height) -
                           0.5F;
    if (mapped_x != nullptr) {
        *mapped_x = source_x;
    }
    if (mapped_y != nullptr) {
        *mapped_y = source_y;
    }
    return source_x >= static_cast<float>(source_rect.offset_x) - 0.5F &&
           source_y >= static_cast<float>(source_rect.offset_y) - 0.5F &&
           source_x <= static_cast<float>(source_rect.offset_x + source_rect.width) - 0.5F &&
           source_y <= static_cast<float>(source_rect.offset_y + source_rect.height) - 0.5F;
}

[[nodiscard]] double mean_absolute_rgb_error_rect(
    const StereoPattern& actual,
    const StereoPattern& expected,
    UINT texture_width,
    UINT eye,
    const xrfg::D3D12ImageRect& rect,
    UINT margin) {
    require(
        eye < kEyeCount && margin * 2U < rect.width &&
            margin * 2U < rect.height &&
            actual[eye].size() == expected[eye].size(),
        "invalid viewport MAE input");
    double total = 0.0;
    std::size_t sample_count = 0;
    for (UINT y = rect.offset_y + margin;
         y + margin < rect.offset_y + rect.height;
         ++y) {
        for (UINT x = rect.offset_x + margin;
             x + margin < rect.offset_x + rect.width;
             ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * texture_width + x) *
                kBytesPerPixel;
            for (UINT channel = 0; channel < 3; ++channel) {
                total += std::abs(
                    static_cast<int>(actual[eye][offset + channel]) -
                    static_cast<int>(expected[eye][offset + channel]));
                ++sample_count;
            }
        }
    }
    require(sample_count != 0, "viewport MAE has no samples");
    return total / static_cast<double>(sample_count);
}

[[nodiscard]] double mean_absolute_rgb_error(
    const StereoPattern& actual,
    const StereoPattern& expected,
    UINT width,
    UINT height,
    UINT eye,
    UINT margin) {
    require(
        eye < kEyeCount && margin * 2U < width && margin * 2U < height &&
            actual[eye].size() == expected[eye].size(),
        "invalid stereo MAE input");
    double total = 0.0;
    std::size_t sample_count = 0;
    for (UINT y = margin; y + margin < height; ++y) {
        for (UINT x = margin; x + margin < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel;
            for (UINT channel = 0; channel < 3; ++channel) {
                total += std::abs(
                    static_cast<int>(actual[eye][offset + channel]) -
                    static_cast<int>(expected[eye][offset + channel]));
                ++sample_count;
            }
        }
    }
    require(sample_count != 0, "stereo MAE has no samples");
    return total / static_cast<double>(sample_count);
}

void get_copy_layouts(
    ID3D12Device* device,
    const D3D12_RESOURCE_DESC& description,
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount>* layouts,
    std::array<UINT, kEyeCount>* row_counts,
    std::array<UINT64, kEyeCount>* row_sizes,
    UINT64* total_size) {
    device->GetCopyableFootprints(
        &description,
        0,
        kEyeCount,
        0,
        layouts->data(),
        row_counts->data(),
        row_sizes->data(),
        total_size);
    require(*total_size > 0, "GetCopyableFootprints returned an empty layout");
}

void upload_pattern(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    const StereoPattern& pattern) {
    require(texture != nullptr, "upload target is null");
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(
        fixture.device(),
        description,
        &layouts,
        &row_counts,
        &row_sizes,
        &total_size);

    ComPtr<ID3D12Resource> upload = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_UPLOAD,
        total_size,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped_memory = nullptr;
    const D3D12_RANGE no_read{0, 0};
    require_hresult(upload->Map(0, &no_read, &mapped_memory), "ID3D12Resource::Map(upload)");
    auto* mapped_bytes = static_cast<std::byte*>(mapped_memory);
    const UINT width = static_cast<UINT>(description.Width);
    const UINT height = description.Height;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        require(row_counts[eye] == height, "unexpected upload row count");
        require(
            row_sizes[eye] == static_cast<UINT64>(width) * kBytesPerPixel,
            "unexpected upload row size");
        require(
            pattern[eye].size() ==
                static_cast<std::size_t>(width) * height * kBytesPerPixel,
            "upload pattern dimensions do not match the texture");
        for (UINT y = 0; y < height; ++y) {
            std::byte* destination =
                mapped_bytes + layouts[eye].Offset +
                static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch;
            const std::uint8_t* source =
                pattern[eye].data() +
                static_cast<std::size_t>(y) * width * kBytesPerPixel;
            std::memcpy(destination, source, static_cast<std::size_t>(row_sizes[eye]));
        }
    }
    upload->Unmap(0, nullptr);

    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        const D3D12_RESOURCE_BARRIER to_copy_destination = transition_barrier(
            texture,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_DEST);
        command_list->ResourceBarrier(1, &to_copy_destination);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[eye];

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = eye;
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER to_render_target = transition_barrier(
            texture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list->ResourceBarrier(1, &to_render_target);
    });
}

template <typename MotionForPixel>
[[nodiscard]] ComPtr<ID3D12Resource> create_and_upload_game_motion_field(
    D3D12WarpFixture& fixture,
    UINT width,
    UINT height,
    MotionForPixel&& motion_for_pixel) {
    D3D12_RESOURCE_DESC description = stereo_texture_description(width, height);
    description.Format = DXGI_FORMAT_R32G32_FLOAT;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    const D3D12_HEAP_PROPERTIES properties =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    require_hresult(
        fixture.device()->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(texture.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(game motion)");

    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(
        fixture.device(),
        description,
        &layouts,
        &row_counts,
        &row_sizes,
        &total_size);
    ComPtr<ID3D12Resource> upload = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_UPLOAD,
        total_size,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped_memory = nullptr;
    const D3D12_RANGE no_read{0, 0};
    require_hresult(
        upload->Map(0, &no_read, &mapped_memory),
        "ID3D12Resource::Map(game motion upload)");
    auto* mapped_bytes = static_cast<std::byte*>(mapped_memory);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        require(row_counts[eye] == height, "unexpected game-motion row count");
        require(
            row_sizes[eye] == static_cast<UINT64>(width) * sizeof(float) * 2U,
            "unexpected game-motion row size");
        for (UINT y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<float*>(
                mapped_bytes + layouts[eye].Offset +
                static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch);
            for (UINT x = 0; x < width; ++x) {
                const auto motion = motion_for_pixel(eye, x, y);
                row[x * 2U] = motion[0];
                row[x * 2U + 1U] = motion[1];
            }
        }
    }
    upload->Unmap(0, nullptr);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[eye];
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = eye;
            command_list->CopyTextureRegion(
                &destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER ready = transition_barrier(
            texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &ready);
    });
    return texture;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_and_upload_game_motion(
    D3D12WarpFixture& fixture,
    UINT width,
    UINT height,
    const std::array<float, kEyeCount>& horizontal_motion) {
    return create_and_upload_game_motion_field(
        fixture,
        width,
        height,
        [&](UINT eye, UINT, UINT) {
            return std::array<float, 2>{horizontal_motion[eye], 0.0F};
        });
}

[[nodiscard]] StereoPattern readback_pattern(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    D3D12_RESOURCE_STATES initial_state) {
    require(texture != nullptr, "readback source is null");
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    require(
        description.Width <= std::numeric_limits<UINT>::max() &&
            description.DepthOrArraySize == kEyeCount,
        "readback texture shape is unsupported");
    const UINT width = static_cast<UINT>(description.Width);
    const UINT height = description.Height;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(
        fixture.device(),
        description,
        &layouts,
        &row_counts,
        &row_sizes,
        &total_size);

    ComPtr<ID3D12Resource> readback = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_READBACK,
        total_size,
        D3D12_RESOURCE_STATE_COPY_DEST);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        const D3D12_RESOURCE_BARRIER to_copy_source = transition_barrier(
            texture,
            initial_state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list->ResourceBarrier(1, &to_copy_source);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = eye;

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = layouts[eye];
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER restore_state = transition_barrier(
            texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            initial_state);
        command_list->ResourceBarrier(1, &restore_state);
    });

    StereoPattern result;
    void* mapped_memory = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
    require_hresult(
        readback->Map(0, &read_range, &mapped_memory),
        "ID3D12Resource::Map(pattern readback)");
    const auto* mapped_bytes = static_cast<const std::byte*>(mapped_memory);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        require(
            row_counts[eye] == height &&
                row_sizes[eye] == static_cast<UINT64>(width) * kBytesPerPixel,
            "unexpected pattern readback layout");
        result[eye].resize(
            static_cast<std::size_t>(width) * height * kBytesPerPixel);
        for (UINT y = 0; y < height; ++y) {
            const std::byte* source =
                mapped_bytes + layouts[eye].Offset +
                static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch;
            std::uint8_t* destination =
                result[eye].data() +
                static_cast<std::size_t>(y) * width * kBytesPerPixel;
            std::memcpy(destination, source, static_cast<std::size_t>(row_sizes[eye]));
        }
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
    return result;
}

void require_readback_matches(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    const StereoPattern& expected,
    const char* label,
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON) {
    require(texture != nullptr, std::string(label) + " resource is null");
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(
        fixture.device(),
        description,
        &layouts,
        &row_counts,
        &row_sizes,
        &total_size);

    ComPtr<ID3D12Resource> readback = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_READBACK,
        total_size,
        D3D12_RESOURCE_STATE_COPY_DEST);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        const D3D12_RESOURCE_BARRIER to_copy_source = transition_barrier(
            texture,
            initial_state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list->ResourceBarrier(1, &to_copy_source);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = eye;

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = layouts[eye];
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER restore_state = transition_barrier(
            texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            initial_state);
        command_list->ResourceBarrier(1, &restore_state);
    });

    void* mapped_memory = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
    require_hresult(
        readback->Map(0, &read_range, &mapped_memory),
        "ID3D12Resource::Map(readback)");
    const auto* mapped_bytes = static_cast<const std::byte*>(mapped_memory);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        for (UINT y = 0; y < kHeight; ++y) {
            const std::byte* actual =
                mapped_bytes + layouts[eye].Offset +
                static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch;
            const std::uint8_t* wanted =
                expected[eye].data() +
                static_cast<std::size_t>(y) * kWidth * kBytesPerPixel;
            if (std::memcmp(actual, wanted, static_cast<std::size_t>(row_sizes[eye])) != 0) {
                std::size_t first_difference = 0;
                while (first_difference < row_sizes[eye] &&
                       actual[first_difference] ==
                           static_cast<std::byte>(wanted[first_difference])) {
                    ++first_difference;
                }
                const unsigned actual_value = first_difference < row_sizes[eye]
                    ? std::to_integer<unsigned>(actual[first_difference])
                    : 0U;
                const unsigned wanted_value = first_difference < row_sizes[eye]
                    ? wanted[first_difference]
                    : 0U;
                readback->Unmap(0, nullptr);
                fail(std::string(label) + " pixel mismatch at eye " +
                     std::to_string(eye) + ", row " + std::to_string(y) +
                     ", byte " + std::to_string(first_difference) +
                     ": actual=" + std::to_string(actual_value) +
                     " expected=" + std::to_string(wanted_value));
            }
        }
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
}

void require_source_is_render_target(
    D3D12WarpFixture& fixture,
    ID3D12Resource* source) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = 1;
    heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    require_hresult(
        fixture.device()->CreateDescriptorHeap(
            &heap_description,
            IID_PPV_ARGS(rtv_heap.GetAddressOf())),
        "ID3D12Device::CreateDescriptorHeap(RTV)");

    D3D12_RENDER_TARGET_VIEW_DESC rtv_description{};
    rtv_description.Format = kFormat;
    rtv_description.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_description.Texture2DArray.MipSlice = 0;
    rtv_description.Texture2DArray.FirstArraySlice = 0;
    rtv_description.Texture2DArray.ArraySize = kEyeCount;
    rtv_description.Texture2DArray.PlaneSlice = 0;
    fixture.device()->CreateRenderTargetView(
        source,
        &rtv_description,
        rtv_heap->GetCPUDescriptorHandleForHeapStart());

    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        constexpr FLOAT clear_color[4] = {0.125F, 0.25F, 0.5F, 1.0F};
        command_list->ClearRenderTargetView(
            rtv_heap->GetCPUDescriptorHandleForHeapStart(),
            clear_color,
            0,
            nullptr);
    });
}

void clear_depth_slices(
    D3D12WarpFixture& fixture,
    ID3D12Resource* source,
    const std::array<float, kEyeCount>& depths) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_description.NumDescriptors = kEyeCount;
    heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    require_hresult(
        fixture.device()->CreateDescriptorHeap(
            &heap_description,
            IID_PPV_ARGS(dsv_heap.GetAddressOf())),
        "ID3D12Device::CreateDescriptorHeap(DSV)");

    const UINT descriptor_size =
        fixture.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const D3D12_CPU_DESCRIPTOR_HANDLE first_handle =
        dsv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_description{};
        dsv_description.Format = kDepthFormat;
        dsv_description.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_description.Flags = D3D12_DSV_FLAG_NONE;
        dsv_description.Texture2DArray.MipSlice = 0;
        dsv_description.Texture2DArray.FirstArraySlice = eye;
        dsv_description.Texture2DArray.ArraySize = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = first_handle;
        handle.ptr += static_cast<SIZE_T>(eye) * descriptor_size;
        fixture.device()->CreateDepthStencilView(source, &dsv_description, handle);
    }

    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_CPU_DESCRIPTOR_HANDLE handle = first_handle;
            handle.ptr += static_cast<SIZE_T>(eye) * descriptor_size;
            command_list->ClearDepthStencilView(
                handle,
                D3D12_CLEAR_FLAG_DEPTH,
                depths[eye],
                0,
                0,
                nullptr);
        }
    });
}

void require_depth_readback_matches(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    const std::array<float, kEyeCount>& expected,
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON) {
    require(texture != nullptr, "depth history resource is null");
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(
        fixture.device(),
        description,
        &layouts,
        &row_counts,
        &row_sizes,
        &total_size);

    ComPtr<ID3D12Resource> readback = create_buffer(
        fixture,
        D3D12_HEAP_TYPE_READBACK,
        total_size,
        D3D12_RESOURCE_STATE_COPY_DEST);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        const D3D12_RESOURCE_BARRIER to_copy_source = transition_barrier(
            texture,
            initial_state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list->ResourceBarrier(1, &to_copy_source);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = eye;

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = layouts[eye];
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER restore_state = transition_barrier(
            texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            initial_state);
        command_list->ResourceBarrier(1, &restore_state);
    });

    void* mapped_memory = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
    require_hresult(
        readback->Map(0, &read_range, &mapped_memory),
        "ID3D12Resource::Map(depth readback)");
    const auto* mapped_bytes = static_cast<const std::byte*>(mapped_memory);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        require(row_counts[eye] == kHeight, "unexpected depth readback row count");
        require(
            row_sizes[eye] == static_cast<UINT64>(kWidth) * sizeof(float),
            "unexpected depth readback row size");
        for (UINT y = 0; y < kHeight; ++y) {
            const std::byte* row =
                mapped_bytes + layouts[eye].Offset +
                static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch;
            for (UINT x = 0; x < kWidth; ++x) {
                float actual = 0.0F;
                std::memcpy(&actual, row + static_cast<std::size_t>(x) * sizeof(float), sizeof(actual));
                if (std::fabs(actual - expected[eye]) > 1.0e-6F) {
                    readback->Unmap(0, nullptr);
                    fail(
                        "depth pixel mismatch at eye " + std::to_string(eye) +
                        ", x " + std::to_string(x) + ", y " + std::to_string(y));
                }
            }
        }
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
}

struct CaptureResources {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Fence> fence;
    xrfg::D3D12HistoryConsumerLease lease{};
};

[[nodiscard]] CaptureResources acquire_capture_for_inspection(
    xrfg::D3D12SwapchainHistory& history,
    const xrfg::D3D12HistoryCaptureTicket& ticket) {
    ID3D12Resource* raw_resource = nullptr;
    ID3D12Fence* raw_fence = nullptr;
    xrfg::D3D12HistoryConsumerLease lease{};
    require(
        operation_succeeded(history.acquire_consumer(
            ticket,
            &lease,
            &raw_resource,
            &raw_fence)),
        "acquire_consumer rejected a current ticket");
    require(raw_resource != nullptr, "acquire_consumer returned a null texture");
    require(raw_fence != nullptr, "acquire_consumer returned a null fence");

    CaptureResources resources;
    resources.resource.Attach(raw_resource);
    resources.fence.Attach(raw_fence);
    resources.lease = lease;
    return resources;
}

void retire_completed_inspection(
    D3D12WarpFixture& fixture,
    xrfg::D3D12SwapchainHistory& history,
    const CaptureResources& resources) {
    ComPtr<ID3D12Fence> completed_fence;
    require_hresult(
        fixture.device()->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(completed_fence.GetAddressOf())),
        "ID3D12Device::CreateFence(completed consumer)");
    require_hresult(
        completed_fence->Signal(1),
        "ID3D12Fence::Signal(completed consumer)");
    require(
        operation_succeeded(history.retire_consumer(resources.lease, completed_fence.Get(), 1)),
        "completed consumer lease retirement failed");
}

void require_private_resource_shape(
    ID3D12Resource* source,
    ID3D12Resource* history_resource) {
    require(source != nullptr && history_resource != nullptr, "cannot compare null resources");
    require(source != history_resource, "history aliases an application swapchain image");
    require(
        same_description(source->GetDesc(), history_resource->GetDesc()),
        "private history texture description does not match the source");

    D3D12_HEAP_PROPERTIES properties{};
    D3D12_HEAP_FLAGS flags{};
    require_hresult(
        history_resource->GetHeapProperties(&properties, &flags),
        "ID3D12Resource::GetHeapProperties(history)");
    require(properties.Type == D3D12_HEAP_TYPE_DEFAULT, "history texture is not on a DEFAULT heap");
    constexpr D3D12_HEAP_FLAGS kExternallyVisibleHeapFlags =
        D3D12_HEAP_FLAG_SHARED |
        D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER |
        D3D12_HEAP_FLAG_HARDWARE_PROTECTED;
    require(
        (flags & kExternallyVisibleHeapFlags) == D3D12_HEAP_FLAG_NONE,
        "history texture is shared, cross-adapter, or hardware-protected");
}

void test_stereo_capture_ring(D3D12WarpFixture& fixture) {
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
        sources[2].Get(),
    };

    xrfg::D3D12SwapchainHistory history;
    require(
        operation_succeeded(history.initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "D3D12SwapchainHistory::initialize failed");
    require(history.initialized(), "history did not report initialized state");
    require(xrfg::D3D12SwapchainHistory::kSlotCount == 3, "history ring is not three slots");

    const StereoPattern pattern_a = make_pattern(11);
    const StereoPattern pattern_b = make_pattern(73);
    const StereoPattern pattern_c = make_pattern(149);

    upload_pattern(fixture, sources[2].Get(), pattern_a);
    xrfg::D3D12HistoryCaptureTicket ticket_a{};
    require(
        operation_succeeded(history.capture(2, &ticket_a)),
        "capture A failed");
    require(
        ticket_a.serial == 1 && ticket_a.slot == 0 && ticket_a.source_index == 2 &&
            ticket_a.fence_value != 0,
        "capture A ticket is incorrect");
    require(operation_succeeded(history.commit(ticket_a)), "commit A failed");
    CaptureResources capture_a = acquire_capture_for_inspection(history, ticket_a);
    fixture.wait_for_fence(capture_a.fence.Get(), ticket_a.fence_value);
    require_private_resource_shape(sources[2].Get(), capture_a.resource.Get());
    require_readback_matches(fixture, capture_a.resource.Get(), pattern_a, "capture A");
    retire_completed_inspection(fixture, history, capture_a);

    upload_pattern(fixture, sources[0].Get(), pattern_b);
    xrfg::D3D12HistoryCaptureTicket ticket_b{};
    require(
        operation_succeeded(history.capture(0, &ticket_b)),
        "capture B failed");
    require(
        ticket_b.serial == 2 && ticket_b.slot == 1 && ticket_b.source_index == 0 &&
            ticket_b.fence_value > ticket_a.fence_value,
        "capture B ticket is incorrect");
    require(operation_succeeded(history.commit(ticket_b)), "commit B failed");
    CaptureResources capture_b = acquire_capture_for_inspection(history, ticket_b);
    fixture.wait_for_fence(capture_b.fence.Get(), ticket_b.fence_value);
    require(capture_a.resource.Get() != capture_b.resource.Get(), "A and B use the same history slot");
    require_private_resource_shape(sources[0].Get(), capture_b.resource.Get());
    require_readback_matches(fixture, capture_b.resource.Get(), pattern_b, "capture B");
    retire_completed_inspection(fixture, history, capture_b);
    CaptureResources capture_a_after_b = acquire_capture_for_inspection(history, ticket_a);
    fixture.wait_for_fence(capture_a_after_b.fence.Get(), ticket_a.fence_value);
    require_readback_matches(
        fixture,
        capture_a_after_b.resource.Get(),
        pattern_a,
        "capture A after B");
    retire_completed_inspection(fixture, history, capture_a_after_b);

    const StereoPattern discarded_pattern = make_pattern(103);
    upload_pattern(fixture, sources[1].Get(), discarded_pattern);
    xrfg::D3D12HistoryCaptureTicket discarded_ticket{};
    require(
        operation_succeeded(history.capture(1, &discarded_ticket)),
        "discard candidate capture failed");
    require(
        discarded_ticket.serial == 3 && discarded_ticket.slot == 2 &&
            discarded_ticket.source_index == 1 &&
            discarded_ticket.fence_value > ticket_b.fence_value,
        "discard candidate ticket is incorrect");
    history.discard(discarded_ticket);

    xrfg::D3D12HistoryConsumerLease discarded_lease{};
    ID3D12Resource* discarded_resource = nullptr;
    ID3D12Fence* discarded_fence = nullptr;
    require(
        !operation_succeeded(history.acquire_consumer(
            discarded_ticket,
            &discarded_lease,
            &discarded_resource,
            &discarded_fence)),
        "discarded ticket remained valid");
    require(
        discarded_resource == nullptr && discarded_fence == nullptr,
        "discarded ticket returned COM objects");
    require(
        operation_succeeded(history.wait_for_idle()),
        "discarded capture did not drain");

    upload_pattern(fixture, sources[2].Get(), pattern_c);
    xrfg::D3D12HistoryCaptureTicket ticket_c{};
    require(
        operation_succeeded(history.capture(2, &ticket_c)),
        "capture C failed");
    require(
        ticket_c.serial == 3 && ticket_c.slot == 2 && ticket_c.source_index == 2 &&
            ticket_c.fence_value > discarded_ticket.fence_value,
        "capture C ticket is incorrect");
    require(
        ticket_c.serial == discarded_ticket.serial && ticket_c.slot == discarded_ticket.slot,
        "capture after discard did not reuse the same serial and slot");
    require(operation_succeeded(history.commit(ticket_c)), "commit C failed");
    CaptureResources capture_c = acquire_capture_for_inspection(history, ticket_c);
    fixture.wait_for_fence(capture_c.fence.Get(), ticket_c.fence_value);
    require(
        capture_c.resource.Get() != capture_a.resource.Get() &&
            capture_c.resource.Get() != capture_b.resource.Get(),
        "capture C did not use the independent third history slot");
    require_readback_matches(fixture, capture_c.resource.Get(), pattern_c, "capture C");
    retire_completed_inspection(fixture, history, capture_c);
    CaptureResources capture_b_after_c = acquire_capture_for_inspection(history, ticket_b);
    fixture.wait_for_fence(capture_b_after_c.fence.Get(), ticket_b.fence_value);
    require_readback_matches(
        fixture,
        capture_b_after_c.resource.Get(),
        pattern_b,
        "capture B after C");
    retire_completed_inspection(fixture, history, capture_b_after_c);
    CaptureResources capture_a_after_c = acquire_capture_for_inspection(history, ticket_a);
    fixture.wait_for_fence(capture_a_after_c.fence.Get(), ticket_a.fence_value);
    require_readback_matches(
        fixture,
        capture_a_after_c.resource.Get(),
        pattern_a,
        "capture A retained across the third slot");
    retire_completed_inspection(fixture, history, capture_a_after_c);

    const HRESULT reinitialize_result = history.initialize(
        fixture.device(),
        fixture.queue(),
        std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    require(FAILED(reinitialize_result), "second initialize unexpectedly succeeded");
    CaptureResources capture_b_after_reinitialize =
        acquire_capture_for_inspection(history, ticket_b);
    CaptureResources capture_c_after_reinitialize =
        acquire_capture_for_inspection(history, ticket_c);
    require(
        capture_b_after_reinitialize.resource.Get() == capture_b.resource.Get() &&
            capture_c_after_reinitialize.resource.Get() == capture_c.resource.Get(),
        "failed reinitialize invalidated or replaced committed captures");
    history.cancel_consumer(capture_b_after_reinitialize.lease);
    history.cancel_consumer(capture_c_after_reinitialize.lease);

    require(operation_succeeded(history.invalidate()), "history invalidate failed");
    for (const auto& invalidated_ticket : {ticket_b, ticket_c}) {
        xrfg::D3D12HistoryConsumerLease invalidated_lease{};
        ID3D12Resource* invalidated_resource = nullptr;
        ID3D12Fence* invalidated_fence = nullptr;
        require(
            !operation_succeeded(history.acquire_consumer(
                invalidated_ticket,
                &invalidated_lease,
                &invalidated_resource,
                &invalidated_fence)),
            "invalidate left an old committed ticket resolvable");
        require(
            invalidated_resource == nullptr && invalidated_fence == nullptr,
            "invalidated ticket returned COM objects");
    }

    const StereoPattern pattern_d = make_pattern(181);
    upload_pattern(fixture, sources[1].Get(), pattern_d);
    xrfg::D3D12HistoryCaptureTicket ticket_d{};
    require(
        operation_succeeded(history.capture(1, &ticket_d)),
        "capture after invalidate failed");
    require(
        ticket_d.serial > ticket_c.serial && ticket_d.serial == 4 &&
            ticket_d.slot == 0 && ticket_d.source_index == 1 &&
            ticket_d.fence_value > ticket_c.fence_value,
        "capture after invalidate reset serial/fence identity");
    require(
        operation_succeeded(history.commit(ticket_d)),
        "capture after invalidate commit failed");
    CaptureResources capture_d = acquire_capture_for_inspection(history, ticket_d);
    fixture.wait_for_fence(capture_d.fence.Get(), ticket_d.fence_value);
    require_readback_matches(fixture, capture_d.resource.Get(), pattern_d, "capture D after invalidate");
    retire_completed_inspection(fixture, history, capture_d);

    xrfg::D3D12HistoryConsumerLease stale_lease{};
    ID3D12Resource* stale_resource = nullptr;
    ID3D12Fence* stale_fence = nullptr;
    require(
        !operation_succeeded(history.acquire_consumer(
            ticket_a,
            &stale_lease,
            &stale_resource,
            &stale_fence)),
        "overwritten capture A ticket remained valid");
    require(
        stale_resource == nullptr && stale_fence == nullptr,
        "rejected stale ticket returned COM objects");

    require_source_is_render_target(fixture, sources[1].Get());
    require(operation_succeeded(history.wait_for_idle()), "history wait_for_idle failed");
}

void test_capture_is_async_and_consumer_fence_blocks_reuse(D3D12WarpFixture& fixture) {
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
        sources[2].Get(),
    };
    const StereoPattern pattern = make_pattern(211);
    upload_pattern(fixture, sources[0].Get(), pattern);

    xrfg::D3D12SwapchainHistory history;
    require(
        operation_succeeded(history.initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "gate history initialization failed");

    ComPtr<ID3D12Fence> gate;
    require_hresult(
        fixture.device()->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(gate.GetAddressOf())),
        "ID3D12Device::CreateFence(gate)");
    require_hresult(fixture.queue()->Wait(gate.Get(), 1), "ID3D12CommandQueue::Wait(gate)");

    struct CaptureOutcome {
        HRESULT result{E_FAIL};
        xrfg::D3D12HistoryCaptureTicket ticket{};
    };
    std::promise<void> capture_entered;
    std::future<void> entered = capture_entered.get_future();
    std::future<CaptureOutcome> capture = std::async(
        std::launch::async,
        [&history, promise = std::move(capture_entered)]() mutable {
            promise.set_value();
            CaptureOutcome outcome{};
            outcome.result = history.capture(0, &outcome.ticket);
            return outcome;
        });
    require(
        entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "capture worker did not start");

    const bool returned_while_gpu_blocked =
        capture.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
    if (!returned_while_gpu_blocked) {
        require_hresult(gate->Signal(1), "ID3D12Fence::Signal(gate cleanup)");
        fail("capture blocked the CPU on its producer fence");
    }

    const CaptureOutcome outcome = capture.get();
    require(operation_succeeded(outcome.result), "asynchronous gated capture failed");
    require(operation_succeeded(history.commit(outcome.ticket)), "gated capture commit failed");

    xrfg::D3D12HistoryConsumerLease lease{};
    ID3D12Resource* raw_resource = nullptr;
    ID3D12Fence* raw_producer_fence = nullptr;
    require(
        operation_succeeded(history.acquire_consumer(
            outcome.ticket,
            &lease,
            &raw_resource,
            &raw_producer_fence)),
        "failed to acquire the asynchronous history consumer");
    ComPtr<ID3D12Resource> leased_resource;
    ComPtr<ID3D12Fence> producer_fence;
    leased_resource.Attach(raw_resource);
    producer_fence.Attach(raw_producer_fence);
    require(
        producer_fence->GetCompletedValue() < outcome.ticket.fence_value,
        "capture producer fence completed before the gated assertion");

    ComPtr<ID3D12Fence> consumer_completion;
    require_hresult(
        fixture.device()->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(consumer_completion.GetAddressOf())),
        "ID3D12Device::CreateFence(consumer completion)");
    require(
        operation_succeeded(history.retire_consumer(lease, consumer_completion.Get(), 1)),
        "consumer lease retirement failed");

    xrfg::D3D12HistoryCaptureTicket ticket_b{};
    require(
        operation_succeeded(history.capture(1, &ticket_b)) &&
            operation_succeeded(history.commit(ticket_b)),
        "the independent second ring slot did not accept an asynchronous capture");

    xrfg::D3D12HistoryCaptureTicket ticket_c{};
    require(
        operation_succeeded(history.capture(2, &ticket_c)) &&
            operation_succeeded(history.commit(ticket_c)),
        "the independent third ring slot did not accept an asynchronous capture");
    require(
        ticket_c.serial == 3 && ticket_c.slot == 2,
        "third asynchronous capture did not preserve rolling chronology");

    xrfg::D3D12HistoryCaptureTicket blocked_ticket{};
    require(
        history.capture(0, &blocked_ticket) == HRESULT_FROM_WIN32(ERROR_BUSY) &&
            blocked_ticket.serial == 0,
        "history overwrote a slot protected by an incomplete consumer fence");

    require_hresult(gate->Signal(1), "ID3D12Fence::Signal(gate)");
    fixture.wait_for_fence(producer_fence.Get(), ticket_c.fence_value);
    require(
        history.capture(0, &blocked_ticket) == HRESULT_FROM_WIN32(ERROR_BUSY),
        "producer completion bypassed the independent consumer fence");

    require_hresult(
        consumer_completion->Signal(1),
        "ID3D12Fence::Signal(consumer completion)");
    xrfg::D3D12HistoryCaptureTicket ticket_d{};
    require(
        operation_succeeded(history.capture(0, &ticket_d)) &&
            operation_succeeded(history.commit(ticket_d)),
        "history slot did not become reusable after consumer completion");
    require(
        ticket_d.slot == outcome.ticket.slot && ticket_d.serial == 4,
        "history did not preserve chronological ring identity after lease retirement");
    require(operation_succeeded(history.wait_for_idle()), "asynchronous history drain failed");
    require_source_is_render_target(fixture, sources[0].Get());
}

// V441: a capture that finds its slot still read on the GPU waits for the
// reader for at most its limit, instead of failing at once. Three slots, each
// in a different state: a reader that finishes inside the limit, one that
// does not, and a lease no fence will clear, which must not be waited for.
void test_capture_waits_for_a_busy_slot_within_its_limit(D3D12WarpFixture& fixture) {
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
        sources[2].Get(),
    };
    xrfg::D3D12SwapchainHistory history;
    require(
        operation_succeeded(history.initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "busy-wait history initialization failed");

    std::array<xrfg::D3D12HistoryCaptureTicket, 3> tickets{};
    std::array<xrfg::D3D12HistoryConsumerLease, 3> leases{};
    std::array<ComPtr<ID3D12Fence>, 2> readers{};
    for (std::uint32_t index = 0; index < 3; ++index) {
        require(
            operation_succeeded(history.capture(index, &tickets[index])) &&
                operation_succeeded(history.commit(tickets[index])),
            "busy-wait ring fill failed");
        ID3D12Resource* raw_resource = nullptr;
        ID3D12Fence* raw_producer_fence = nullptr;
        require(
            operation_succeeded(history.acquire_consumer(
                tickets[index], &leases[index], &raw_resource, &raw_producer_fence)),
            "busy-wait consumer acquisition failed");
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Fence> producer;
        resource.Attach(raw_resource);
        producer.Attach(raw_producer_fence);
        fixture.wait_for_fence(producer.Get(), tickets[index].fence_value);
        if (index < 2) {
            require_hresult(
                fixture.device()->CreateFence(
                    0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(readers[index].GetAddressOf())),
                "ID3D12Device::CreateFence(busy-wait reader)");
            require(
                operation_succeeded(history.retire_consumer(leases[index], readers[index].Get(), 1)),
                "busy-wait consumer retirement failed");
        }
    }

    // Slot 0: its reader finishes 20 ms in, inside a 200 ms limit.
    xrfg::D3D12HistoryCaptureWait freed{};
    freed.limit_us = 200'000;
    std::future<void> signaller = std::async(std::launch::async, [&readers] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        static_cast<void>(readers[0]->Signal(1));
    });
    xrfg::D3D12HistoryCaptureTicket ticket_freed{};
    const HRESULT freed_result = history.capture(0, &ticket_freed, &freed);
    signaller.get();
    require(
        operation_succeeded(freed_result) && freed.slot_busy &&
            freed.waited_us >= 10'000 && freed.waited_us < 200'000 &&
            ticket_freed.slot == tickets[0].slot && ticket_freed.serial == 4,
        "a capture did not wait for a reader that finished inside its limit");
    require(operation_succeeded(history.commit(ticket_freed)), "busy-wait commit failed");

    // Slot 1: its reader is still running when a 30 ms limit runs out.
    xrfg::D3D12HistoryCaptureWait expired{};
    expired.limit_us = 30'000;
    xrfg::D3D12HistoryCaptureTicket ticket_expired{};
    require(
        history.capture(1, &ticket_expired, &expired) == HRESULT_FROM_WIN32(ERROR_BUSY) &&
            expired.slot_busy && ticket_expired.serial == 0 &&
            expired.waited_us >= 25'000 && expired.waited_us < 500'000,
        "a capture did not give up on a busy slot at its limit");
    require_hresult(readers[1]->Signal(1), "ID3D12Fence::Signal(busy-wait reader)");
    require(
        operation_succeeded(history.capture(1, &ticket_expired, &expired)) &&
            operation_succeeded(history.commit(ticket_expired)),
        "a slot did not become reusable after its reader finished");

    // Slot 2: leased and not yet submitted - nothing to wait for.
    xrfg::D3D12HistoryCaptureWait leased{};
    leased.limit_us = 200'000;
    xrfg::D3D12HistoryCaptureTicket ticket_leased{};
    require(
        history.capture(2, &ticket_leased, &leased) == HRESULT_FROM_WIN32(ERROR_BUSY) &&
            leased.slot_busy && leased.waited_us < 50'000,
        "a capture waited for a slot held by an unsubmitted lease");
    history.cancel_consumer(leases[2]);
    require(
        operation_succeeded(history.capture(2, &ticket_leased, &leased)) &&
            operation_succeeded(history.commit(ticket_leased)),
        "a slot did not become reusable after its lease was cancelled");

    // A free slot neither waits nor reports a busy slot.
    require(operation_succeeded(history.wait_for_idle()), "busy-wait history drain failed");
    xrfg::D3D12HistoryCaptureWait idle{};
    idle.limit_us = 200'000;
    xrfg::D3D12HistoryCaptureTicket ticket_idle{};
    require(
        operation_succeeded(history.capture(0, &ticket_idle, &idle)) &&
            !idle.slot_busy && idle.waited_us == 0 &&
            operation_succeeded(history.commit(ticket_idle)),
        "a capture into a free slot reported a wait");
    require(operation_succeeded(history.wait_for_idle()), "busy-wait history drain failed");
}

void test_depth_capture_path(D3D12WarpFixture& fixture) {
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_depth_source_texture(fixture),
        create_depth_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 2> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
    };
    constexpr std::array<float, kEyeCount> kCapturedDepths{0.25F, 0.75F};
    clear_depth_slices(fixture, sources[1].Get(), kCapturedDepths);

    xrfg::D3D12SwapchainHistory history;
    require(
        operation_succeeded(history.initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_DEPTH_WRITE)),
        "depth history initialization failed");

    xrfg::D3D12HistoryCaptureTicket ticket{};
    require(
        operation_succeeded(history.capture(1, &ticket)),
        "depth capture failed");
    require(
        ticket.serial == 1 && ticket.slot == 0 && ticket.source_index == 1 &&
            ticket.fence_value != 0,
        "depth capture ticket is incorrect");
    require(operation_succeeded(history.commit(ticket)), "depth capture commit failed");

    CaptureResources capture = acquire_capture_for_inspection(history, ticket);
    fixture.wait_for_fence(capture.fence.Get(), ticket.fence_value);
    require_private_resource_shape(sources[1].Get(), capture.resource.Get());
    require_depth_readback_matches(fixture, capture.resource.Get(), kCapturedDepths);
    retire_completed_inspection(fixture, history, capture);

    // No transition is recorded here: the debug layer validates that capture restored
    // the OpenXR-required DEPTH_WRITE state before returning.
    constexpr std::array<float, kEyeCount> kPostCaptureDepths{0.5F, 0.625F};
    clear_depth_slices(fixture, sources[1].Get(), kPostCaptureDepths);
    require(
        operation_succeeded(history.wait_for_idle()),
        "depth history wait_for_idle failed");
}

void test_depth_private_history_allows_shader_resource_views(D3D12WarpFixture& fixture) {
    ComPtr<ID3D12Resource> source = create_depth_source_texture(
        fixture,
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    std::array<ID3D12Resource*, 1> source_pointers{source.Get()};
    require(
        (source->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0,
        "DENY_SHADER_RESOURCE source fixture is missing its flag");

    constexpr std::array<float, kEyeCount> kDepths{0.375F, 0.875F};
    clear_depth_slices(fixture, source.Get(), kDepths);
    xrfg::D3D12SwapchainHistory history;
    require(
        operation_succeeded(history.initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_DEPTH_WRITE)),
        "DENY_SHADER_RESOURCE history initialization failed");

    xrfg::D3D12HistoryCaptureTicket ticket{};
    require(
        operation_succeeded(history.capture(0, &ticket)),
        "DENY_SHADER_RESOURCE capture failed");
    require(
        operation_succeeded(history.commit(ticket)),
        "DENY_SHADER_RESOURCE capture commit failed");
    CaptureResources capture = acquire_capture_for_inspection(history, ticket);
    fixture.wait_for_fence(capture.fence.Get(), ticket.fence_value);
    require(
        (capture.resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0,
        "private history retained DENY_SHADER_RESOURCE");
    require(
        (capture.resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0,
        "private history lost depth-stencil compatibility");
    require_depth_readback_matches(fixture, capture.resource.Get(), kDepths);
    retire_completed_inspection(fixture, history, capture);
    constexpr std::array<float, kEyeCount> kPostCaptureDepths{0.125F, 0.625F};
    clear_depth_slices(fixture, source.Get(), kPostCaptureDepths);
    require(
        operation_succeeded(history.wait_for_idle()),
        "shader-readable history wait_for_idle failed");
}

// Synthesis on a queue of the layer's own, which is what the layer does for a
// native D3D12 application. The debug layer is on here, so a cross-queue
// ownership violation is reported rather than silently producing the wrong
// pixels - the failure this configuration actually had, where every timing
// record said the frame was generated, paced and complete and the runtime
// still displayed something else.
void test_synthesis_on_a_dedicated_queue(D3D12WarpFixture& fixture) {
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    ComPtr<ID3D12CommandQueue> synthesis_queue;
    require_hresult(
        fixture.device()->CreateCommandQueue(
            &queue_description, IID_PPV_ARGS(synthesis_queue.GetAddressOf())),
        "dedicated synthesis queue creation");

    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ComPtr<ID3D12Resource>, 2> synthetic_destinations{
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(), sources[1].Get(), sources[2].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 2> synthetic_pointers{
        synthetic_destinations[0].Get(), synthetic_destinations[1].Get()};

    upload_pattern(fixture, sources[0].Get(), make_solid_pattern({
        RgbaBytes{16, 40, 72, 255}, RgbaBytes{32, 56, 88, 255}}));
    upload_pattern(fixture, sources[1].Get(), make_solid_pattern({
        RgbaBytes{80, 104, 136, 255}, RgbaBytes{96, 120, 152, 255}}));

    // The history stays on the application's queue. Its capture has to be
    // ordered after the application's own rendering, which is exactly what
    // that queue gives it.
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(
                source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "dedicated-queue history initialization failed");

    // COMMON rather than RENDER_TARGET: these stand in for the layer's own
    // private swapchains, and COMMON is the state D3D12 requires at the point
    // queue ownership transfers.
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(),
            synthesis_queue.Get(),
            history,
            std::span<ID3D12Resource* const>(
                current_pointers.data(), current_pointers.size()),
            std::span<ID3D12Resource* const>(
                synthetic_pointers.data(), synthetic_pointers.size()),
            kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "dedicated-queue synthesizer initialization failed");

    const ReprojectionViews views = make_reprojection_views();

    // A join against the queue the synthesis already runs on is redundant
    // rather than wrong, and has to say so instead of queueing a wait.
    require(
        synthesizer.synchronize_producer_queue(synthesis_queue.Get()) == S_FALSE,
        "producer join did not report the same queue as already ordered");
    require(
        operation_succeeded(
            synthesizer.synchronize_producer_queue(fixture.queue())),
        "producer join across queues failed");

    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(
        operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "dedicated-queue capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime_ticket{};
    require(
        operation_succeeded(synthesizer.submit_prime(
            capture_a, views, 0, &prime_ticket)),
        "prime on the dedicated queue failed");

    // The prime is still queued; the pair tests the same submission slot, so
    // let it land first the way the frame-start poll does in the layer.
    require(
        operation_succeeded(synthesizer.wait_for_previous_submission(500)),
        "waiting out the dedicated-queue prime failed");

    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(
        operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "dedicated-queue capture B failed");

    // Deferred, as the native D3D12 path always is. The pair then reserves two
    // fence values: the synthetic's, signalled now, and the pair's, which the
    // held-back copy signals only once flush_current_copy submits it.
    xrfg::D3D12FrameSynthesisTicket pair_ticket{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_b,
            views,
            views,
            0,
            1,
            &pair_ticket,
            std::nullopt,
            {},
            true)),
        "deferred pair on the dedicated queue failed");
    require(
        pair_ticket.synthetic_fence_value != 0,
        "deferred pair reported no value for the synthetic");
    require(
        pair_ticket.synthetic_fence_value < pair_ticket.fence_value,
        "deferred pair put the synthetic at or after the pair's own value; a "
        "consumer joined on that would park until the copy is flushed a "
        "display period later");

    // The consumer join carries the synthesis back to the queue the runtime
    // orders its own reads against.
    require(
        operation_succeeded(
            synthesizer.synchronize_consumer_queue(fixture.queue(), pair_ticket)),
        "consumer join across queues failed");
    require(
        synthesizer.synchronize_consumer_queue(
            synthesis_queue.Get(), pair_ticket) == S_FALSE,
        "consumer join did not report the same queue as already ordered");

    require(
        operation_succeeded(synthesizer.flush_current_copy(
            fixture.queue(), pair_ticket.fence_value)),
        "flushing the held-back copy across queues failed");
    require(
        operation_succeeded(synthesizer.wait_for_idle()),
        "dedicated-queue synthesizer did not go idle");

    // The synthesizer falls out of scope here. Its destructor has to drain the
    // synthesis queue itself, because nothing outside waits on it. Without
    // that the slot allocators are released with work still in flight, which
    // crashed six call-chain tests at process teardown.
}

// True when everything queued on `queue` so far completes within the timeout.
bool queue_drains_within(D3D12WarpFixture& fixture, ID3D12CommandQueue* queue, DWORD milliseconds) {
    ComPtr<ID3D12Fence> fence;
    require_hresult(fixture.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(fence.GetAddressOf())), "drain fence creation");
    require_hresult(queue->Signal(fence.Get(), 1), "drain fence signal");
    if (fence->GetCompletedValue() >= 1) return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require(event != nullptr, "drain event creation");
    require_hresult(fence->SetEventOnCompletion(1, event), "drain event");
    const bool done = WaitForSingleObject(event, milliseconds) == WAIT_OBJECT_0;
    CloseHandle(event);
    return done;
}

// A setting switched while a game runs rebuilds the synthesizer (V499). Pairs
// already handed out still carry their fence values: the presenter joins the
// runtime's queue on them and flushes held-back copies against them after the
// rebuild. A rebuilt synthesizer that started its fence from zero left those
// joins waiting for values it would reach only minutes later, and the game
// froze with its queue parked. The rebuild has to continue the same fences.
void test_reconfigure_keeps_fence_values_in_flight(D3D12WarpFixture& fixture) {
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> synthesis_queue;
    require_hresult(fixture.device()->CreateCommandQueue(
        &queue_description, IID_PPV_ARGS(synthesis_queue.GetAddressOf())),
        "reconfigure test queue creation");
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture), create_source_texture(fixture), create_source_texture(fixture)};
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture), create_source_texture(fixture)};
    std::array<ComPtr<ID3D12Resource>, 2> synthetic_destinations{
        create_source_texture(fixture), create_source_texture(fixture)};
    std::array<ID3D12Resource*, 3> source_pointers{sources[0].Get(), sources[1].Get(), sources[2].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 2> synthetic_pointers{synthetic_destinations[0].Get(), synthetic_destinations[1].Get()};
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(fixture.device(), fixture.queue(),
        std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
        D3D12_RESOURCE_STATE_RENDER_TARGET)), "reconfigure test history initialization");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(operation_succeeded(synthesizer.initialize(fixture.device(), synthesis_queue.Get(), history,
        std::span<ID3D12Resource* const>(current_pointers.data(), current_pointers.size()),
        std::span<ID3D12Resource* const>(synthetic_pointers.data(), synthetic_pointers.size()),
        kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET)), "reconfigure test synthesizer initialization");
    const ReprojectionViews views = make_reprojection_views();

    // Several deferred pairs, so the values in flight are well above where a
    // fresh fence would start.
    xrfg::D3D12FrameSynthesisTicket pair_ticket{};
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        xrfg::D3D12HistoryCaptureTicket capture{};
        require(operation_succeeded(history->capture(frame % 2, &capture)) &&
            operation_succeeded(history->commit(capture)), "reconfigure test capture");
        if (frame == 0) {
            xrfg::D3D12FrameSynthesisTicket prime{};
            require(operation_succeeded(synthesizer.submit_prime(capture, views, 0, &prime)),
                "reconfigure test prime");
        } else {
            require(operation_succeeded(synthesizer.submit_pair(capture, views, views, 0, 1,
                &pair_ticket, std::nullopt, {}, true)), "reconfigure test pair");
            if (frame < 3)
                require(operation_succeeded(synthesizer.flush_current_copy(
                    fixture.queue(), pair_ticket.fence_value)), "reconfigure test copy flush");
        }
        require(operation_succeeded(synthesizer.wait_for_previous_submission(500)),
            "reconfigure test submission");
    }
    require(pair_ticket.fence_value > 2, "reconfigure test reached no fence values worth testing");

    // The switch, with the last pair's copy still held back.
    require(operation_succeeded(synthesizer.reconfigure(
        xrfg::D3D12OpticalFlowBackend::fidelity_fx, xrfg::D3D12NvidiaOpticalFlowOptions{})),
        "reconfigure failed");

    // What the presenter does next frame with the pair it still holds.
    static_cast<void>(synthesizer.synchronize_consumer_queue(fixture.queue(), pair_ticket));
    static_cast<void>(synthesizer.flush_current_copy(fixture.queue(), pair_ticket.fence_value));
    require(queue_drains_within(fixture, fixture.queue(), 2000),
        "the runtime's queue parked on a fence value from before the rebuild");

    // And the rebuilt synthesizer goes on from there.
    xrfg::D3D12HistoryCaptureTicket capture{};
    require(operation_succeeded(history->capture(0, &capture)) &&
        operation_succeeded(history->commit(capture)), "capture after the rebuild");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(operation_succeeded(synthesizer.submit_prime(capture, views, 0, &prime)),
        "prime after the rebuild");
    require(operation_succeeded(synthesizer.wait_for_idle()), "idle after the rebuild");
    require(prime.fence_value == 0 || prime.fence_value > pair_ticket.fence_value,
        "the rebuilt synthesizer restarted its fence values");
}

void test_rolling_frame_synthesizer(D3D12WarpFixture& fixture) {
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ComPtr<ID3D12Resource>, 3> current_destinations{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ComPtr<ID3D12Resource>, 3> synthetic_destinations{
        create_source_texture(fixture),
        create_source_texture(fixture),
        create_source_texture(fixture),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
        sources[2].Get(),
    };
    std::array<ID3D12Resource*, 3> current_destination_pointers{
        current_destinations[0].Get(),
        current_destinations[1].Get(),
        current_destinations[2].Get(),
    };
    std::array<ID3D12Resource*, 3> synthetic_destination_pointers{
        synthetic_destinations[0].Get(),
        synthetic_destinations[1].Get(),
        synthetic_destinations[2].Get(),
    };

    const StereoPattern pattern_a = make_solid_pattern({
        RgbaBytes{16, 40, 72, 255},
        RgbaBytes{32, 56, 88, 255},
    });
    const StereoPattern pattern_b = make_solid_pattern({
        RgbaBytes{80, 104, 136, 255},
        RgbaBytes{96, 120, 152, 255},
    });
    const StereoPattern pattern_c = make_solid_pattern({
        RgbaBytes{160, 184, 208, 255},
        RgbaBytes{176, 200, 224, 255},
    });
    const StereoPattern pattern_d = make_solid_pattern({
        RgbaBytes{24, 112, 200, 255},
        RgbaBytes{48, 136, 224, 255},
    });
    upload_pattern(fixture, sources[0].Get(), pattern_a);
    upload_pattern(fixture, sources[1].Get(), pattern_b);
    upload_pattern(fixture, sources[2].Get(), pattern_c);

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "rolling synthesizer history initialization failed");

    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(),
            fixture.queue(),
            history,
            std::span<ID3D12Resource* const>(
                current_destination_pointers.data(),
                current_destination_pointers.size()),
            std::span<ID3D12Resource* const>(
                synthetic_destination_pointers.data(),
             synthetic_destination_pointers.size()),
            kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "rolling frame synthesizer initialization failed");
    require(synthesizer.initialized(), "rolling frame synthesizer did not report initialized");
    const ReprojectionViews static_views = make_reprojection_views();

    xrfg::D3D12FrameSynthesizer invalid_synthesizer;
    require(
        FAILED(invalid_synthesizer.initialize(
            fixture.device(),
            fixture.queue(),
            history,
            {},
            std::span<ID3D12Resource* const>(
                synthetic_destination_pointers.data(),
                synthetic_destination_pointers.size()),
            kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "synthesizer accepted an empty current-destination set");

    ComPtr<ID3D12Fence> gate;
    require_hresult(
        fixture.device()->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(gate.GetAddressOf())),
        "ID3D12Device::CreateFence(synthesis gate)");
    require_hresult(
        fixture.queue()->Wait(gate.Get(), 1),
        "ID3D12CommandQueue::Wait(synthesis gate)");

    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(
        operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "prime capture A failed");
    require(
        capture_a.serial == 1 && capture_a.slot == 0 && capture_a.source_index == 0,
        "prime capture A chronology is incorrect");

    xrfg::D3D12FrameSynthesisTicket invalid_prime{};
    require(
        FAILED(synthesizer.submit_prime(
            capture_a,
            std::span<const xrfg::D3D12ReprojectionView>(static_views.data(), 1),
            0,
            &invalid_prime)) &&
            invalid_prime.current_serial == 0 && invalid_prime.fence_value == 0,
        "prime accepted incomplete per-eye camera metadata");
    require(
        FAILED(synthesizer.submit_prime(
            capture_a,
            static_views,
            static_cast<std::uint32_t>(current_destinations.size()),
            &invalid_prime)) &&
            invalid_prime.current_serial == 0 && invalid_prime.fence_value == 0,
        "out-of-range prime submission was not rejected atomically");

    struct SynthesisOutcome {
        HRESULT result{E_FAIL};
        xrfg::D3D12FrameSynthesisTicket ticket{};
    };
    std::future<SynthesisOutcome> prime_future = std::async(
        std::launch::async,
        [&synthesizer, &capture_a, &static_views] {
            SynthesisOutcome outcome{};
            outcome.result =
                synthesizer.submit_prime(capture_a, static_views, 2, &outcome.ticket);
            return outcome;
        });
    if (prime_future.wait_for(std::chrono::milliseconds(500)) !=
        std::future_status::ready) {
        require_hresult(gate->Signal(1), "ID3D12Fence::Signal(prime timeout cleanup)");
        (void)prime_future.get();
        fail("prime submission blocked the CPU on GPU completion");
    }
    const SynthesisOutcome prime = prime_future.get();
    require(operation_succeeded(prime.result), "asynchronous prime submission failed");
    require(
        prime.ticket.previous_serial == 0 &&
            prime.ticket.current_serial == capture_a.serial &&
            prime.ticket.fence_value != 0 &&
            prime.ticket.synthetic_destination_index ==
                std::numeric_limits<std::uint32_t>::max() &&
            prime.ticket.current_destination_index == 2,
        "prime synthesis ticket is incorrect");

    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(
        operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "pair capture B failed while prime work was queued");
    require(
        capture_b.serial == 2 && capture_b.slot == 1 && capture_b.source_index == 1 &&
            capture_b.fence_value > capture_a.fence_value,
        "capture B chronology is incorrect");

    xrfg::D3D12FrameSynthesisTicket invalid_pair{};
    const ReprojectionViews mismatched_target_views = make_reprojection_views(0.05F);
    require(
        FAILED(synthesizer.submit_pair(
            capture_b,
            static_views,
            mismatched_target_views,
            0,
            0,
            &invalid_pair)) &&
            invalid_pair.current_serial == 0 && invalid_pair.fence_value == 0,
        "synthesizer accepted a target camera different from render pose B");
    require(
        FAILED(synthesizer.submit_pair(
            capture_b,
            static_views,
            static_views,
            static_cast<std::uint32_t>(synthetic_destinations.size()),
            0,
            &invalid_pair)) &&
            invalid_pair.current_serial == 0 && invalid_pair.fence_value == 0,
        "out-of-range pair submission was not rejected atomically");

    // Release the deliberately blocked queue, then model V090's frame-start
    // gate before advancing the temporal pair. Submission itself remains
    // nonblocking and cannot accumulate another complete transaction.
    require_hresult(gate->Signal(1), "ID3D12Fence::Signal(synthesis gate)");
    require(
        operation_succeeded(synthesizer.wait_for_previous_submission(500)),
        "frame-start synthesis gate did not settle the asynchronous prime");

    std::future<SynthesisOutcome> pair_future = std::async(
        std::launch::async,
        [&synthesizer, &capture_b, &static_views] {
            SynthesisOutcome outcome{};
            outcome.result = synthesizer.submit_pair(
                capture_b,
                static_views,
                static_views,
                2,
                1,
                &outcome.ticket);
            return outcome;
        });
    if (pair_future.wait_for(std::chrono::milliseconds(500)) !=
        std::future_status::ready) {
        require_hresult(gate->Signal(1), "ID3D12Fence::Signal(pair timeout cleanup)");
        (void)pair_future.get();
        fail("pair submission blocked the CPU on GPU completion");
    }
    const SynthesisOutcome pair_ab = pair_future.get();
    require(operation_succeeded(pair_ab.result), "asynchronous A/B pair submission failed");
    require(
        pair_ab.ticket.previous_serial == capture_a.serial &&
            pair_ab.ticket.current_serial == capture_b.serial &&
            pair_ab.ticket.fence_value > prime.ticket.fence_value &&
            pair_ab.ticket.work_slot != prime.ticket.work_slot &&
            pair_ab.ticket.synthetic_destination_index == 2 &&
            pair_ab.ticket.current_destination_index == 1,
        "A/B synthesis ticket is incorrect");

    require(
        operation_succeeded(synthesizer.wait_for_previous_submission(500)),
        "frame-start synthesis gate did not settle the A/B pair");
    xrfg::D3D12FrameSynthesisTicket repeated_pair{};
    const ReprojectionViews repeated_views = make_reprojection_views(0.025F);
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_b,
            repeated_views,
            repeated_views,
            0,
            0,
            &repeated_pair)) &&
            repeated_pair.previous_serial == capture_b.serial &&
            repeated_pair.current_serial == capture_b.serial &&
            repeated_pair.fence_value > pair_ab.ticket.fence_value &&
            repeated_pair.synthetic_destination_index == 0 &&
            repeated_pair.current_destination_index == 0,
        "rolling synthesizer did not reuse an unchanged eye capture");

    xrfg::D3D12HistoryCaptureTicket capture_c{};
    require(
        operation_succeeded(history->capture(2, &capture_c)) &&
            operation_succeeded(history->commit(capture_c)),
        "third rolling slot did not accept C while A/B work was in flight");
    require(
        capture_c.serial == 3 && capture_c.slot == 2 && capture_c.source_index == 2 &&
            capture_c.fence_value > capture_b.fence_value,
        "capture C did not preserve A0/B1/C2 rolling chronology");

    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});

    require_readback_matches(
        fixture,
        current_destinations[2].Get(),
        pattern_a,
        "prime current A",
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    require_readback_matches(
        fixture,
        current_destinations[1].Get(),
        pattern_b,
        "pair current B",
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    const StereoPattern synthetic_ab = readback_pattern(
        fixture,
        synthetic_destinations[2].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    require(
        synthetic_ab != pattern_a && synthetic_ab != pattern_b,
        "A/B synthetic output did not remain temporally distinct");
    require_source_is_render_target(fixture, sources[0].Get());
    require_source_is_render_target(fixture, sources[1].Get());
    require_readback_matches(
        fixture,
        current_destinations[0].Get(),
        pattern_b,
        "repeated current B",
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    xrfg::D3D12FrameSynthesisTicket pair_bc{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_c,
            static_views,
            static_views,
            0,
            0,
            &pair_bc)),
        "B/C rolling pair submission failed");
    require(
        pair_bc.previous_serial == capture_b.serial &&
            pair_bc.current_serial == capture_c.serial &&
            pair_bc.fence_value > pair_ab.ticket.fence_value &&
            pair_bc.synthetic_destination_index == 0 &&
            pair_bc.current_destination_index == 0,
        "B/C synthesis ticket is incorrect");

    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    require_readback_matches(
        fixture,
        current_destinations[0].Get(),
        pattern_c,
        "pair current C",
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    const StereoPattern synthetic_bc = readback_pattern(
        fixture,
        synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    require(
        synthetic_bc != pattern_b && synthetic_bc != pattern_c,
        "B/C synthetic output did not remain temporally distinct");
    require_source_is_render_target(fixture, sources[2].Get());

    upload_pattern(fixture, sources[0].Get(), pattern_d);
    xrfg::D3D12HistoryCaptureTicket capture_d{};
    require(
        operation_succeeded(history->capture(0, &capture_d)) &&
            operation_succeeded(history->commit(capture_d)),
        "capture D did not reuse slot zero after synthesis completion");
    require(
        capture_d.serial == 4 && capture_d.slot == 0 && capture_d.source_index == 0 &&
            capture_d.fence_value > capture_c.fence_value,
        "capture D did not preserve A0/B1/C2/D0 rolling chronology");

    xrfg::D3D12FrameSynthesisTicket repeated_c{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_c,
            static_views,
            static_views,
            1,
            1,
            &repeated_c)) &&
            repeated_c.previous_serial == capture_c.serial &&
            repeated_c.current_serial == capture_c.serial &&
            repeated_c.fence_value > pair_bc.fence_value,
        "rolling synthesizer did not retain a repeated capture after an advancing pair");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    require_readback_matches(
        fixture,
        current_destinations[1].Get(),
        pattern_c,
        "repeated current C",
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    require(
        operation_succeeded(synthesizer.retire_previous()),
        "rolling previous retirement failed");
    require(
        operation_succeeded(synthesizer.retire_previous()),
        "rolling previous retirement was not idempotent");
    require(operation_succeeded(synthesizer.wait_for_idle()), "synthesizer drain failed");

    CaptureResources history_b = acquire_capture_for_inspection(*history, capture_b);
    fixture.wait_for_fence(history_b.fence.Get(), capture_b.fence_value);
    require_readback_matches(
        fixture,
        history_b.resource.Get(),
        pattern_b,
        "history B after rolling synthesis");
    retire_completed_inspection(fixture, *history, history_b);

    CaptureResources history_c = acquire_capture_for_inspection(*history, capture_c);
    fixture.wait_for_fence(history_c.fence.Get(), capture_c.fence_value);
    require_readback_matches(
        fixture,
        history_c.resource.Get(),
        pattern_c,
        "history C after rolling synthesis");
    retire_completed_inspection(fixture, *history, history_c);
    CaptureResources history_d = acquire_capture_for_inspection(*history, capture_d);
    fixture.wait_for_fence(history_d.fence.Get(), capture_d.fence_value);
    require_readback_matches(
        fixture,
        history_d.resource.Get(),
        pattern_d,
        "history D after rolling synthesis");
    retire_completed_inspection(fixture, *history, history_d);
    require(operation_succeeded(history->wait_for_idle()), "synthesis leases did not retire");

    xrfg::D3D12HistoryConsumerLease stale_lease{};
    ID3D12Resource* stale_resource = nullptr;
    ID3D12Fence* stale_fence = nullptr;
    require(
        FAILED(history->acquire_consumer(
            capture_a,
            &stale_lease,
            &stale_resource,
            &stale_fence)) &&
            stale_resource == nullptr && stale_fence == nullptr,
        "overwritten capture A remained consumable after serial 3");

    require(operation_succeeded(history->invalidate()), "synthesis history invalidate failed");
}

void test_stereo_motion_synthesis_beats_same_pixel_blend(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend =
        xrfg::D3D12OpticalFlowBackend::fidelity_fx,
    bool validate_repeated_capture = false,
    xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options = {}) {
    constexpr UINT kMotionWidth = 1024;
    constexpr UINT kMotionHeight = 512;
    constexpr UINT kEvaluationMargin = 32;

    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, kMotionWidth, kMotionHeight),
        create_source_texture(fixture, kMotionWidth, kMotionHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture, kMotionWidth, kMotionHeight),
        create_source_texture(fixture, kMotionWidth, kMotionHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_source_texture(fixture, kMotionWidth, kMotionHeight),
    };
    std::array<ID3D12Resource*, 2> source_pointers{
        sources[0].Get(),
        sources[1].Get(),
    };
    std::array<ID3D12Resource*, 2> current_destination_pointers{
        current_destinations[0].Get(),
        current_destinations[1].Get(),
    };
    std::array<ID3D12Resource*, 1> synthetic_destination_pointers{
        synthetic_destinations[0].Get(),
    };

    const StereoPattern previous = translated_motion_pattern(
        kMotionWidth,
        kMotionHeight,
        {0, 0});
    const StereoPattern current = translated_motion_pattern(
        kMotionWidth,
        kMotionHeight,
        {16, -16});
    const StereoPattern expected_midpoint = translated_motion_pattern(
        kMotionWidth,
        kMotionHeight,
        {8, -8});
    const StereoPattern same_pixel_blend = midpoint_pattern(previous, current);
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "motion synthesis history initialization failed");

    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(),
            fixture.queue(),
            history,
            std::span<ID3D12Resource* const>(
                current_destination_pointers.data(),
                current_destination_pointers.size()),
            std::span<ID3D12Resource* const>(
                synthetic_destination_pointers.data(),
                synthetic_destination_pointers.size()),
            kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            backend,
            nvidia_options,
            backend == xrfg::D3D12OpticalFlowBackend::nvidia)),
        "motion frame synthesizer initialization failed");
    const ReprojectionViews static_views = make_reprojection_views();

    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(
        operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "motion capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(
        operation_succeeded(
            synthesizer.submit_prime(capture_a, static_views, 0, &prime)),
        "motion prime submission failed");
    require_frame_start_gate(synthesizer, "motion frame-start gate");

    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(
        operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "motion capture B failed");
    require(
        capture_a.serial == 1 && capture_a.slot == 0 &&
            capture_b.serial == 2 && capture_b.slot == 1,
        "motion pair history chronology is incorrect");
    xrfg::D3D12FrameSynthesisTicket pair{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_b,
            static_views,
            static_views,
            0,
            1,
            &pair)),
        "motion A/B pair submission failed");
    require(
        pair.previous_serial == capture_a.serial &&
            pair.current_serial == capture_b.serial,
        "motion pair synthesis ticket is incorrect");
    const StereoPattern actual_current = readback_pattern(
        fixture,
        current_destinations[1].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    const StereoPattern actual_synthetic = readback_pattern(
        fixture,
        synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    require(actual_current == current, "motion current output is not a bit-exact copy of B");
    require(
        actual_synthetic != previous && actual_synthetic != current,
        "motion synthetic output duplicates one of its inputs");
    require(
        actual_synthetic != same_pixel_blend,
        "motion synthetic output ignored the optical-flow field");

    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const double synthetic_error = mean_absolute_rgb_error(
            actual_synthetic,
            expected_midpoint,
            kMotionWidth,
            kMotionHeight,
            eye,
            kEvaluationMargin);
        const double blend_error = mean_absolute_rgb_error(
            same_pixel_blend,
            expected_midpoint,
            kMotionWidth,
            kMotionHeight,
            eye,
            kEvaluationMargin);
        const double previous_duplicate_error = mean_absolute_rgb_error(
            previous,
            expected_midpoint,
            kMotionWidth,
            kMotionHeight,
            eye,
            kEvaluationMargin);
        const double current_duplicate_error = mean_absolute_rgb_error(
            current,
            expected_midpoint,
            kMotionWidth,
            kMotionHeight,
            eye,
            kEvaluationMargin);
        std::cout << "motion eye=" << eye
                  << " synthetic_mae=" << synthetic_error
                  << " blend_mae=" << blend_error
                  << " previous_mae=" << previous_duplicate_error
                  << " current_mae=" << current_duplicate_error << '\n';
        require(
            synthetic_error <= blend_error * 1.10 &&
                synthetic_error < previous_duplicate_error &&
                synthetic_error < current_duplicate_error,
            "motion midpoint is less stable than blend/duplicates for eye " +
                std::to_string(eye) +
                ": synthetic=" + std::to_string(synthetic_error) +
                " blend=" + std::to_string(blend_error));
    }

    require_source_is_render_target(fixture, sources[0].Get());
    require_source_is_render_target(fixture, sources[1].Get());
    if (validate_repeated_capture) {
        const ReprojectionViews repeated_views = static_views;
        xrfg::D3D12FrameSynthesisTicket repeated{};
        require(
            operation_succeeded(synthesizer.submit_pair(
                capture_b,
                repeated_views,
                repeated_views,
                0,
                0,
                &repeated)) &&
                repeated.previous_serial == capture_b.serial &&
                repeated.current_serial == capture_b.serial &&
                repeated.fence_value > pair.fence_value,
            "motion synthesizer did not accept an unchanged capture");
        require(
            operation_succeeded(synthesizer.wait_for_idle()),
            "repeated motion synthesis drain failed");
        fixture.require_no_debug_errors();
        const StereoPattern repeated_current = readback_pattern(
            fixture,
            current_destinations[0].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        require(
            repeated_current == current,
            "repeated motion current output is not a bit-exact copy of B");
        const StereoPattern repeated_synthetic = readback_pattern(
            fixture,
            synthetic_destinations[0].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            const double repeated_error = mean_absolute_rgb_error(
                repeated_synthetic,
                current,
                kMotionWidth,
                kMotionHeight,
                eye,
                kEvaluationMargin);
            require(
                repeated_error < 0.5,
                "repeated capture pose synthesis is unstable for eye " +
                    std::to_string(eye) + ": error=" +
                    std::to_string(repeated_error));
        }
    } else {
        require(
            operation_succeeded(synthesizer.wait_for_idle()),
            "motion synthesis drain failed");
    }
    if (backend == xrfg::D3D12OpticalFlowBackend::nvidia) {
        xrfg::D3D12NvidiaGpuTiming timing{};
        require(
            synthesizer.consume_nvidia_gpu_timing(&timing) == S_OK &&
                timing.previous_serial == capture_a.serial &&
                timing.current_serial == capture_b.serial &&
                timing.eye_count == kEyeCount &&
                timing.total_microseconds >=
                    timing.pack_microseconds +
                        timing.composition_microseconds,
            "NVIDIA GPU timing did not resolve the measured stereo pair");
        std::cout << "nvidia gpu pack_us=" << timing.pack_microseconds
                  << " eye0_us=" << timing.eye0_microseconds
                  << " eye1_us=" << timing.eye1_microseconds
                  << " composition_us=" << timing.composition_microseconds
                  << " total_us=" << timing.total_microseconds << '\n';
    }
    require(operation_succeeded(history->invalidate()), "motion history invalidate failed");
}

void test_dlss_motion_vector_stereo_stream_pairing(D3D12WarpFixture& fixture) {
    xrfg::configure_dlss_motion_vector_tracking(true);
    constexpr UINT width = 64;
    constexpr UINT height = 32;
    const auto create_texture = [&](UINT texture_width, UINT texture_height, UINT16 slices) {
        D3D12_RESOURCE_DESC description =
            stereo_texture_description(texture_width, texture_height);
        description.DepthOrArraySize = slices;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> texture;
        require_hresult(fixture.device()->CreateCommittedResource(
            &properties, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(texture.GetAddressOf())),
            "create DLSS provenance texture");
        return texture;
    };
    const auto left_output = create_texture(width, height, 1);
    const auto right_output = create_texture(width, height, 1);
    const auto motion = create_texture(width / 2, height / 2, 1);
    const auto xr_output = create_texture(width, height, 2);

    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        xrfg::publish_dlss_motion_vectors({
            101, left_output.Get(), motion.Get(), fixture.queue(), 0, 0, width, height,
            0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
            D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
        xrfg::publish_dlss_motion_vectors({
            202, right_output.Get(), motion.Get(), fixture.queue(), 0, 0, width, height,
            0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
            D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
    });

    const auto resolved = xrfg::resolve_dlss_motion_vectors(
        xr_output.Get(), fixture.queue());
    require(resolved && resolved->eye_count == 2 && resolved->eyes[0] &&
            resolved->eyes[1] && resolved->eyes[0]->stream == 101 &&
            resolved->eyes[1]->stream == 202 &&
            resolved->eyes[0]->motion_vectors.Get() != motion.Get() &&
            resolved->eyes[1]->motion_vectors.Get() != motion.Get() &&
            resolved->eyes[0]->motion_vectors.Get() !=
                resolved->eyes[1]->motion_vectors.Get() &&
            resolved->eyes[0]->output_slice == 0 &&
            resolved->eyes[1]->output_slice == 0,
        "two DLSS streams were not associated with the XR stereo image");

    // Publish in the opposite order. Eye assignment must remain tied to the
    // stable first-seen stream order, not to this frame's evaluation order.
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        xrfg::publish_dlss_motion_vectors({
            202, right_output.Get(), motion.Get(), fixture.queue(), 0, 0, width, height,
            0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
            D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
        xrfg::publish_dlss_motion_vectors({
            101, left_output.Get(), motion.Get(), fixture.queue(), 0, 0, width, height,
            0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
            D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
    });
    const auto reordered = xrfg::resolve_dlss_motion_vectors(
        xr_output.Get(), fixture.queue());
    require(reordered && reordered->eyes[0]->stream == 101 &&
            reordered->eyes[1]->stream == 202 &&
            reordered->eyes[0]->serial == 2 &&
            reordered->eyes[1]->serial == 2,
        "DLSS stream pairing changed eye assignment with evaluation order");

    const auto tracked = xrfg::dlss_motion_vector_statistics();
    require(tracked.published == 4 && tracked.snapshot_copies == 4 &&
            tracked.snapshot_failures == 0 && tracked.matched == 2 &&
            tracked.resolve_missing_streams == 0 &&
            tracked.resolve_stale_pairs == 0,
        "DLSS stream diagnostics did not account for the stereo pair");
    xrfg::report_dlss_motion_vector_use();
    const auto used = xrfg::dlss_motion_vector_statistics();
    require(used.status == xrfg::DlssMotionVectorStatus::used && used.used == 1 &&
            used.last_used_publication == used.published,
        "DLSS stream diagnostics did not retain the last successful use");
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
        xrfg::publish_dlss_motion_vectors({
            101, left_output.Get(), motion.Get(), fixture.queue(), 0, 0, width, height,
            0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
            D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
    });
    require(xrfg::dlss_motion_vector_statistics().status ==
                xrfg::DlssMotionVectorStatus::used,
        "a new DLSS publication falsely demoted a recently used vector route");
    xrfg::configure_dlss_motion_vector_tracking(false);
    require(!xrfg::resolve_dlss_motion_vectors(xr_output.Get(), fixture.queue()),
        "disabled DLSS vector tracking still resolved a pair");
    xrfg::configure_dlss_motion_vector_tracking(true);
    require(!xrfg::resolve_dlss_motion_vectors(xr_output.Get(), fixture.queue()),
        "re-enabled DLSS vector tracking exposed a stale pair");

    const auto publish_alternating_eye = [&](ID3D12Resource* eye_output) {
        fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
            xrfg::publish_dlss_motion_vectors({
                303, eye_output, motion.Get(), fixture.queue(), 0, 0, width, height,
                0, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
                D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
        });
    };
    publish_alternating_eye(left_output.Get());
    require(!xrfg::resolve_dlss_motion_vectors(xr_output.Get(), fixture.queue()),
        "single DLSS stream resolved before the second eye arrived");
    publish_alternating_eye(right_output.Get());
    const auto alternating_ab = xrfg::resolve_dlss_motion_vectors(
        xr_output.Get(), fixture.queue());
    require(alternating_ab && alternating_ab->eyes[0] && alternating_ab->eyes[1] &&
            alternating_ab->eyes[0]->stream == 303 &&
            alternating_ab->eyes[1]->stream == 303 &&
            alternating_ab->eyes[0]->serial == 1 &&
            alternating_ab->eyes[1]->serial == 2 &&
            alternating_ab->eyes[0]->previous_serial == 0 &&
            alternating_ab->eyes[1]->previous_serial == 0,
        "single alternating DLSS stream did not form its first stereo pair");
    publish_alternating_eye(left_output.Get());
    require(!xrfg::resolve_dlss_motion_vectors(xr_output.Get(), fixture.queue()),
        "single alternating DLSS stream exposed a half-updated stereo pair");
    publish_alternating_eye(right_output.Get());
    const auto alternating_cd = xrfg::resolve_dlss_motion_vectors(
        xr_output.Get(), fixture.queue());
    require(alternating_cd && alternating_cd->eyes[0] && alternating_cd->eyes[1] &&
            alternating_cd->eyes[0]->serial == 3 &&
            alternating_cd->eyes[1]->serial == 4 &&
            alternating_cd->eyes[0]->previous_serial == 1 &&
            alternating_cd->eyes[1]->previous_serial == 2,
        "single alternating DLSS stream lost per-eye temporal continuity");
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::retire_dlss_motion_vector_stream(101);
    xrfg::retire_dlss_motion_vector_stream(202);
    xrfg::retire_dlss_motion_vector_stream(303);
}

// A swapchain per eye, each the size of a DLSS output (V497, from 0.3): with
// its eye known, a one-slice image takes its own eye's evaluation, whether
// the game runs two DLSS features or one for both eyes.
void test_dlss_motion_vector_per_eye_swapchains(D3D12WarpFixture& fixture) {
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::configure_dlss_motion_vector_tracking(true);
    constexpr UINT width = 64;
    constexpr UINT height = 32;
    const auto create_texture = [&](UINT texture_width, UINT texture_height) {
        D3D12_RESOURCE_DESC description =
            stereo_texture_description(texture_width, texture_height);
        description.DepthOrArraySize = 1;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> texture;
        require_hresult(fixture.device()->CreateCommittedResource(
            &properties, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(texture.GetAddressOf())),
            "create per-eye DLSS texture");
        return texture;
    };
    const auto left_output = create_texture(width, height);
    const auto right_output = create_texture(width, height);
    const auto motion = create_texture(width, height / 2);
    const auto left_xr = create_texture(width, height);
    const auto right_xr = create_texture(width, height);
    const auto publish = [&](std::uint64_t stream, ID3D12Resource* eye_output, UINT motion_x = 0) {
        fixture.execute_and_wait([&](ID3D12GraphicsCommandList* command_list) {
            xrfg::publish_dlss_motion_vectors({
                stream, eye_output, motion.Get(), fixture.queue(), 0, 0, width, height,
                motion_x, 0, width / 2, height / 2, 1.0F, 1.0F, 0.0F, 0.0F,
                D3D12_RESOURCE_STATE_COMMON, false, false, command_list});
        });
    };

    // Right-eye first discovery: input offsets override first-seen order.
    for (int frame = 0; frame < 2; ++frame) {
        publish(622, right_output.Get(), width / 2);
        publish(611, left_output.Get());
    }
    const auto left = xrfg::resolve_dlss_motion_vectors(left_xr.Get(), fixture.queue(), 0);
    const auto right = xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue(), 1);
    require(left && right && left->eye_count == 1 && right->eye_count == 1 &&
            left->eyes[0]->stream == 611 && right->eyes[0]->stream == 622 &&
            left->eyes[0]->serial == 2 && right->eyes[0]->serial == 2,
        "per-eye swapchains did not each take their own eye's DLSS evaluation");
    require(right->eyes[0]->motion_x == width / 2,
        "eye ordering changed the right eye's vector rectangle");
    // The right eye evaluated first this frame: the eyes stay put.
    publish(622, right_output.Get(), width / 2);
    publish(611, left_output.Get());
    const auto left_again = xrfg::resolve_dlss_motion_vectors(left_xr.Get(), fixture.queue(), 0);
    const auto right_again = xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue(), 1);
    require(left_again && right_again && left_again->eyes[0]->stream == 611 &&
            right_again->eyes[0]->stream == 622 && left_again->eyes[0]->serial == 3 &&
            right_again->eyes[0]->serial == 3,
        "per-eye swapchains swapped eyes with this frame's evaluation order");
    // Without the eye, the newest as before.
    const auto unknown = xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue());
    require(unknown && unknown->eyes[0]->stream == 611,
        "an image of unknown eye no longer took the newest evaluation");
    // An evaluation written straight into the image is that image's.
    publish(611, left_xr.Get());
    publish(622, right_output.Get(), width / 2);
    const auto direct = xrfg::resolve_dlss_motion_vectors(left_xr.Get(), fixture.queue(), 1);
    require(direct && direct->eyes[0]->stream == 611 && direct->eyes[0]->serial == 4,
        "an evaluation whose output is the image did not go to that image");
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::retire_dlss_motion_vector_stream(611);
    xrfg::retire_dlss_motion_vector_stream(622);
    xrfg::configure_dlss_motion_vector_tracking(true);

    // One DLSS feature for both eyes, evaluated left then right: each eye
    // sees every other serial.
    publish(633, left_output.Get());
    require(!xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue(), 1),
        "first left evaluation was incorrectly borrowed by the right eye");
    const auto first_left = xrfg::resolve_dlss_motion_vectors(left_xr.Get(), fixture.queue(), 0);
    require(first_left && first_left->eyes[0]->serial == 1,
        "the left eye did not take the first evaluation of one shared stream");
    publish(633, right_output.Get(), width / 2);
    publish(633, left_output.Get());
    const auto half = xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue(), 1);
    require(!half, "the right eye took the previous frame's evaluation of a shared stream");
    publish(633, right_output.Get(), width / 2);
    const auto shared_left = xrfg::resolve_dlss_motion_vectors(left_xr.Get(), fixture.queue(), 0);
    const auto shared_right = xrfg::resolve_dlss_motion_vectors(right_xr.Get(), fixture.queue(), 1);
    require(shared_left && shared_right && shared_left->eyes[0]->serial == 3 &&
            shared_right->eyes[0]->serial == 4 &&
            shared_left->eyes[0]->previous_serial == 1 &&
            shared_right->eyes[0]->previous_serial == 2,
        "one shared DLSS stream did not give each eye its own evaluation");
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::retire_dlss_motion_vector_stream(633);
}

// The release-bound join (V497, from 0.3's R15): each eye's guide is the
// newest evaluation at its own colour release, so a game that evaluates the
// right eye first, or both eyes before either release, still gives each eye
// its own.
void test_release_bound_dlss_guides(D3D12WarpFixture& fixture) {
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::retire_disabled_dlss_motion_vector_resources();
    xrfg::configure_dlss_motion_vector_tracking(true);
    const auto texture = [&](UINT width) {
        auto description = stereo_texture_description(width, 32);
        description.DepthOrArraySize = 1;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        const auto properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> resource;
        require_hresult(fixture.device()->CreateCommittedResource(&properties,
            D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(resource.GetAddressOf())), "release-bound texture");
        return resource;
    };
    const auto color = texture(64), xr_color = texture(64), motion = texture(64);
    const auto spectator = texture(128);
    const auto publish = [&](std::uint64_t stream, ID3D12Resource* output, bool reset = false) {
        fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
            xrfg::DlssMotionVectorPublication p{};
            p.stream = stream;
            p.output = output;
            p.motion_vectors = motion.Get();
            p.output_width = static_cast<UINT>(output->GetDesc().Width);
            p.output_height = 32;
            p.motion_width = 64;
            p.motion_height = 32;
            p.producer_queue = fixture.queue();
            p.producer_command_list = list;
            p.resource_state = D3D12_RESOURCE_STATE_COMMON;
            p.reset = reset;
            const auto before = xrfg::dlss_motion_vector_publications();
            xrfg::publish_dlss_motion_vectors(p);
            require(xrfg::dlss_motion_vector_publications() == before + 1,
                "release-bound publication failed");
        });
        return xrfg::dlss_motion_vector_publications();
    };
    const auto resolve = [&](int eye, std::uint64_t release, std::uint64_t other) {
        return xrfg::resolve_dlss_motion_vectors(xr_color.Get(), fixture.queue(), eye, release, other);
    };
    publish(755, color.Get()); // same-size stream retained from start-up
    publish(744, spectator.Get());
    publish(733, color.Get()); // capture begins on the RIGHT eye, serial 1
    std::uint64_t left_release{}, right_release{};
    for (int frame = 0; frame < 3; ++frame) {
        left_release = publish(733, color.Get());
        right_release = publish(733, color.Get());
    }
    const auto left = resolve(0, left_release, right_release);
    const auto right = resolve(1, right_release, left_release);
    require(left && right && left->eyes[0]->stream == 733 && right->eyes[0]->stream == 733 &&
            left->eyes[0]->serial == 6 && right->eyes[0]->serial == 7 &&
            left->eyes[0]->previous_serial == 4 && right->eyes[0]->previous_serial == 5 &&
            left->eyes[0]->publication <= left_release && right->eyes[0]->publication <= right_release,
        "right-first shared stream did not follow each colour release");
    // Without a watermark the eye path refuses this half-updated pair.
    require(!xrfg::resolve_dlss_motion_vectors(xr_color.Get(), fixture.queue(), 0),
        "the eye path without a watermark accepted a stale stream");
    publish(733, color.Get());
    publish(733, color.Get());
    const auto both = xrfg::dlss_motion_vector_publications();
    const auto first = resolve(0, both, both), second = resolve(1, both, both);
    require(first && second && first->eyes[0]->serial == 8 && second->eyes[0]->serial == 9,
        "both evaluated before the releases did not use left-first evaluation order");
    require(!resolve(0, left_release > 10 ? left_release - 10 : 1,
                right_release > 10 ? right_release - 10 : 1),
        "a newer guide was borrowed for an evicted release");
    publish(733, color.Get(), true);
    const auto after_reset = xrfg::dlss_motion_vector_publications();
    require(!resolve(0, after_reset, after_reset),
        "an opposite-eye guide was borrowed across a reset");
    xrfg::retire_dlss_motion_vector_stream(733);
    xrfg::retire_dlss_motion_vector_stream(744);
    xrfg::retire_dlss_motion_vector_stream(755);

    // Separate streams with identical input rectangles: first discovery is
    // right-first. Release order, rather than that discovery order, wins.
    for (int frame = 0; frame < 2; ++frame) {
        right_release = publish(722, color.Get());
        left_release = publish(711, color.Get());
    }
    const auto separate_left = resolve(0, left_release, right_release);
    const auto separate_right = resolve(1, right_release, left_release);
    require(separate_left && separate_right && separate_left->eyes[0]->stream == 711 &&
            separate_right->eyes[0]->stream == 722,
        "separate streams followed discovery order rather than the releases");
    const auto legacy_left = xrfg::resolve_dlss_motion_vectors(xr_color.Get(), fixture.queue(), 0);
    require(legacy_left && legacy_left->eyes[0]->stream == 722,
        "without a watermark two equal streams no longer follow discovery order");
    xrfg::retire_dlss_motion_vector_stream(711);
    xrfg::retire_dlss_motion_vector_stream(722);

    // V524: eye images a texel narrower than the DLSS output (R.E.A.L. VR's
    // Cyberpunk 2077, 4480 evaluated, 4479 submitted) still take their own
    // eye's evaluation of one shared stream; an output far larger does not.
    const auto cropped = texture(63);
    publish(744, spectator.Get());
    for (int frame = 0; frame < 3; ++frame) {
        left_release = publish(766, color.Get());
        right_release = publish(766, color.Get());
    }
    const auto crop_left = xrfg::resolve_dlss_motion_vectors(
        cropped.Get(), fixture.queue(), 0, left_release, right_release);
    const auto crop_right = xrfg::resolve_dlss_motion_vectors(
        cropped.Get(), fixture.queue(), 1, right_release, left_release);
    require(crop_left && crop_right && crop_left->eyes[0]->stream == 766 &&
            crop_right->eyes[0]->stream == 766 && crop_left->eyes[0]->serial == 5 &&
            crop_right->eyes[0]->serial == 6 && crop_left->eyes[0]->previous_serial == 3 &&
            crop_right->eyes[0]->previous_serial == 4,
        "eye images a texel narrower than the DLSS output lost their guides");
    const auto narrow = texture(32);
    require(!xrfg::resolve_dlss_motion_vectors(
                narrow.Get(), fixture.queue(), 0, left_release, right_release),
        "an eye image half the DLSS output's width took its vectors");
    xrfg::retire_dlss_motion_vector_stream(744);
    xrfg::retire_dlss_motion_vector_stream(766);

    // V524: one shared stream evaluated twice before either release, with
    // Streamline's cameras: the earlier is the left eye's whether the cameras
    // stand apart or not. R.E.A.L. VR's cameras are the same for both eyes and
    // stand apart only where the player moved between the two evaluations.
    const auto publish_eye = [&](std::uint64_t stream, float side, std::uint64_t tag = 0,
                                 bool reset = false) {
        fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
            xrfg::DlssMotionVectorPublication p{};
            p.stream = stream;
            p.eye_tag = tag;
            p.reset = reset;
            p.output = color.Get();
            p.motion_vectors = motion.Get();
            p.output_width = 64;
            p.output_height = 32;
            p.motion_width = 64;
            p.motion_height = 32;
            p.producer_queue = fixture.queue();
            p.producer_command_list = list;
            p.resource_state = D3D12_RESOURCE_STATE_COMMON;
            p.view.known = true;
            p.view.right = {0.0F, 0.0F, 1.0F};
            p.view.position = {100.0F, 5.0F, 2000.0F + side};
            xrfg::publish_dlss_motion_vectors(p);
        });
        return xrfg::dlss_motion_vector_publications();
    };
    std::uint64_t pair_release = 0;
    for (int frame = 0; frame < 3; ++frame) {
        publish_eye(777, 0.032F);
        pair_release = publish_eye(777, -0.032F);
    }
    const auto camera_left = xrfg::resolve_dlss_motion_vectors(
        cropped.Get(), fixture.queue(), 0, pair_release, pair_release);
    const auto camera_right = xrfg::resolve_dlss_motion_vectors(
        cropped.Get(), fixture.queue(), 1, pair_release, pair_release);
    require(camera_left && camera_right && camera_left->eyes[0]->serial == 5 &&
            camera_right->eyes[0]->serial == 6 && camera_left->eyes[0]->previous_serial == 3 &&
            camera_right->eyes[0]->previous_serial == 4,
        "cameras that stand apart reordered a shared stream's eyes");
    for (int frame = 0; frame < 1; ++frame) {
        publish_eye(777, 0.0F);
        pair_release = publish_eye(777, 0.0F);
    }
    const auto together_left = xrfg::resolve_dlss_motion_vectors(
        cropped.Get(), fixture.queue(), 0, pair_release, pair_release);
    require(together_left && together_left->eyes[0]->serial == 7,
        "cameras that do not stand apart did not keep the evaluation order");
    xrfg::retire_dlss_motion_vector_stream(777);

    // V524: alternating eyes (R.E.A.L. VR). One stream for both eyes guides
    // no eye before its anchor; anchored, each eye takes the newest
    // evaluation of its serial parity at its release, its previous two
    // serials back, through a frame that carried one evaluation.
    xrfg::configure_dlss_alternating_eyes(true);
    std::uint64_t alternating_release = 0;
    for (int frame = 0; frame < 2; ++frame) {
        publish_eye(799, 0.0F);
        alternating_release = publish_eye(799, 0.0F);
    }
    require(!xrfg::resolve_dlss_motion_vectors(
                cropped.Get(), fixture.queue(), 0, alternating_release, alternating_release),
        "an alternating stream guided an eye before its anchor");
    const auto position = xrfg::dlss_motion_vector_stream_position(799);
    require(position.known && !position.anchored && position.serial == 4,
        "the alternating stream's position was not reported");
    xrfg::anchor_dlss_alternating_eyes(799, position.epoch, 1);
    require(xrfg::dlss_motion_vector_stream_position(799).anchored,
        "the alternating stream did not take its anchor");
    const auto resolve_alternating = [&](int eye) {
        return xrfg::resolve_dlss_motion_vectors(
            cropped.Get(), fixture.queue(), eye, alternating_release, alternating_release);
    };
    // The newest evaluation's eye takes it; the other eye, a frame late as
    // R.E.A.L. VR's AERv2 hands it over, takes the one before its newest.
    auto alternating_left = resolve_alternating(0);
    auto alternating_right = resolve_alternating(1);
    require(alternating_left && alternating_right && alternating_left->eyes[0]->serial == 1 &&
            alternating_right->eyes[0]->serial == 4 &&
            alternating_right->eyes[0]->previous_serial == 2,
        "an anchored alternating stream did not give each eye its evaluation");
    // Asked again in the same frame (the release, then xrEndFrame): the same.
    require(resolve_alternating(1)->eyes[0]->serial == 4 &&
            resolve_alternating(1)->eyes[0]->previous_serial == 2,
        "a second resolve in the frame moved the eye on");
    // A frame with one evaluation: the left eye has the newest now, its
    // previous the 1 it had; the right eye's second-newest (2) is older than
    // the 4 it showed, so the 4 again, without motion.
    alternating_release = publish_eye(799, 0.0F);
    alternating_left = resolve_alternating(0);
    alternating_right = resolve_alternating(1);
    require(alternating_left && alternating_right && alternating_left->eyes[0]->serial == 5 &&
            alternating_left->eyes[0]->previous_serial == 1 &&
            alternating_right->eyes[0]->serial == 4 &&
            alternating_right->eyes[0]->previous_serial == 4 &&
            alternating_right->eyes[0]->scale_x < 1.0e-3F &&
            alternating_left->eyes[0]->scale_x > 0.5F,
        "a frame with one evaluation did not keep each eye's pairs");
    alternating_release = publish_eye(799, 0.0F);
    alternating_left = resolve_alternating(0);
    alternating_right = resolve_alternating(1);
    require(alternating_left && alternating_right && alternating_left->eyes[0]->serial == 5 &&
            alternating_left->eyes[0]->previous_serial == 5 &&
            alternating_right->eyes[0]->serial == 6 &&
            alternating_right->eyes[0]->previous_serial == 4 &&
            alternating_right->eyes[0]->scale_x > 0.5F,
        "the alternating stream lost its eyes after a frame with one evaluation");

    // V540: each eye through a DLSS feature of its own (R.E.A.L. VR's fix),
    // its handle the evaluation's eye_tag. Anchored by the tags, the eyes hold
    // through a reset - a new epoch, which a parity's anchor does not survive
    // - that swaps the order the eyes evaluate in.
    {
        // One alternating stream of the image's size at a time.
        xrfg::retire_dlss_motion_vector_stream(799);
        constexpr std::uint64_t kLeftTag = 11, kRightTag = 22;
        std::uint64_t tagged_release = 0;
        for (int frame = 0; frame < 2; ++frame) {
            publish_eye(801, 0.0F, kRightTag);
            tagged_release = publish_eye(801, 0.0F, kLeftTag);
        }
        const auto resolve_tagged = [&](int eye) {
            return xrfg::resolve_dlss_motion_vectors(
                cropped.Get(), fixture.queue(), eye, tagged_release, tagged_release);
        };
        require(!resolve_tagged(0) && !xrfg::dlss_motion_vector_stream_position(801).anchored,
            "a tagged alternating stream guided an eye before its anchor");
        xrfg::anchor_dlss_alternating_eye_tags(kLeftTag, kRightTag);
        const auto tagged_position = xrfg::dlss_motion_vector_stream_position(801);
        require(tagged_position.anchored && tagged_position.tag_anchored,
            "the tags did not anchor the alternating stream");
        require(resolve_tagged(0)->eyes[0]->eye_tag == kLeftTag &&
                resolve_tagged(0)->eyes[0]->serial == 4 &&
                resolve_tagged(1)->eyes[0]->eye_tag == kRightTag &&
                resolve_tagged(1)->eyes[0]->serial == 1,
            "the tags did not give each eye its evaluation");
        publish_eye(801, 0.0F, kLeftTag, true);
        publish_eye(801, 0.0F, kRightTag);
        publish_eye(801, 0.0F, kLeftTag);
        tagged_release = publish_eye(801, 0.0F, kRightTag);
        require(xrfg::dlss_motion_vector_stream_position(801).epoch == tagged_position.epoch + 1,
            "the reset did not start an epoch");
        require(resolve_tagged(0) && resolve_tagged(0)->eyes[0]->eye_tag == kLeftTag &&
                resolve_tagged(0)->eyes[0]->serial == 1 &&
                resolve_tagged(1)->eyes[0]->eye_tag == kRightTag &&
                resolve_tagged(1)->eyes[0]->serial == 4,
            "the tags' anchor did not hold through a reset that swapped the eyes' order");
        require(xrfg::dlss_alternating_eye(801, tagged_position.epoch + 1, 4, kRightTag) == 1 &&
                xrfg::dlss_alternating_eye(801, tagged_position.epoch + 1, 3, kLeftTag) == 0 &&
                xrfg::dlss_alternating_eye(801, tagged_position.epoch + 1, 3, 99) == -1,
            "an evaluation's eye was not reported by its tag");
        // V542: the newest evaluation's eye one of its own behind as well -
        // each eye its second-newest. A frame on (5 left, 6 right): the right
        // eye 4, which it showed the frame before, again; a frame on, 6, and
        // the left 5.
        publish_eye(801, 0.0F, kLeftTag);
        tagged_release = publish_eye(801, 0.0F, kRightTag);
        xrfg::set_dlss_alternating_skips(1, 1);
        require(resolve_tagged(1)->eyes[0]->serial == 4 && resolve_tagged(0)->eyes[0]->serial == 3,
            "a late newest evaluation's eye still took the newest");
        publish_eye(801, 0.0F, kLeftTag);
        tagged_release = publish_eye(801, 0.0F, kRightTag);
        const auto late_right = resolve_tagged(1);
        const auto late_left = resolve_tagged(0);
        require(late_right && late_left && late_right->eyes[0]->serial == 6 &&
                late_right->eyes[0]->previous_serial == 4 && late_left->eyes[0]->serial == 5 &&
                late_left->eyes[0]->previous_serial == 3,
            "a late newest evaluation's eye did not take its second-newest");
        // Two behind: a frame on (9 left, 10 right), the right eye 6 again,
        // the left 7; a frame on, the right 8 and the left 9.
        publish_eye(801, 0.0F, kLeftTag);
        tagged_release = publish_eye(801, 0.0F, kRightTag);
        xrfg::set_dlss_alternating_skips(2, 1);
        require(resolve_tagged(1)->eyes[0]->serial == 6 && resolve_tagged(0)->eyes[0]->serial == 7,
            "the newest evaluation's eye two behind did not hold its picture");
        publish_eye(801, 0.0F, kLeftTag);
        tagged_release = publish_eye(801, 0.0F, kRightTag);
        const auto behind_right = resolve_tagged(1);
        const auto behind_left = resolve_tagged(0);
        require(behind_right && behind_left && behind_right->eyes[0]->serial == 8 &&
                behind_right->eyes[0]->previous_serial == 6 && behind_left->eyes[0]->serial == 9 &&
                behind_left->eyes[0]->previous_serial == 7,
            "the newest evaluation's eye two behind did not take its third-newest");
        xrfg::set_dlss_alternating_skips(0, 1);
        xrfg::anchor_dlss_alternating_eye_tags(0, 0);
        xrfg::retire_dlss_motion_vector_stream(801);
    }

    // V533: R.E.A.L. VR's Ghost of Tsushima. The DLSS output is wider than the
    // eye image (4192 against 4003): the eye image is its middle window, the
    // camera's field (+-1.2043) holding the eye's (+-1.1501). Scaled down: a
    // 64-wide output, a 61-wide eye image, the eye's field 61/64 of the
    // camera's. Without the eye's view the stream is refused by size; with it,
    // each eye takes the stream placed on its window.
    {
        constexpr float kCameraTan = 1.2043F;
        constexpr float kEyeTan = kCameraTan * 61.0F / 64.0F;
        const auto window = texture(61);
        const auto publish_wide = [&]() {
            fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
                xrfg::DlssMotionVectorPublication p{};
                p.stream = 833;
                p.output = color.Get();
                p.motion_vectors = motion.Get();
                p.output_width = 64;
                p.output_height = 32;
                p.motion_width = 64;
                p.motion_height = 32;
                p.producer_queue = fixture.queue();
                p.producer_command_list = list;
                p.resource_state = D3D12_RESOURCE_STATE_COMMON;
                p.view.known = true;
                p.view.tan_left = -kCameraTan;
                p.view.tan_right = kCameraTan;
                p.view.tan_up = 0.9656F;
                p.view.tan_down = -0.9656F;
                p.view.right = {0.0F, 0.0F, 1.0F};
                p.view.position = {100.0F, 5.0F, 2000.0F};
                xrfg::publish_dlss_motion_vectors(p);
            });
            return xrfg::dlss_motion_vector_publications();
        };
        std::uint64_t wide_release = 0;
        for (int frame = 0; frame < 2; ++frame) {
            publish_wide();
            wide_release = publish_wide();
        }
        const auto wide_position = xrfg::dlss_motion_vector_stream_position(833);
        xrfg::anchor_dlss_alternating_eyes(833, wide_position.epoch, 1);
        const auto resolve_wide = [&](int eye) {
            return xrfg::resolve_dlss_motion_vectors(
                window.Get(), fixture.queue(), eye, wide_release, wide_release);
        };
        require(!resolve_wide(0), "a wider stream guided an eye whose view is unknown");
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            xrfg::set_dlss_eye_view(eye, {-kEyeTan, kEyeTan, 0.9656F, -0.9656F, 0, 0, 61, 32});
        }
        const auto wide_left = resolve_wide(0);
        const auto wide_right = resolve_wide(1);
        const auto on_window = [](const std::shared_ptr<const xrfg::DlssMotionVectorSet>& set) {
            if (!set || !set->eyes[0]) return false;
            const auto& frame = *set->eyes[0];
            return frame.placed && frame.output_x == 0 && frame.output_width == 61 &&
                frame.output_height == 32 && std::fabs(frame.placed_motion_rect[0] - 1.5F) < 0.05F &&
                std::fabs(frame.placed_motion_rect[2] - 61.0F) < 0.05F &&
                std::fabs(frame.placed_motion_rect[1]) < 0.05F &&
                std::fabs(frame.placed_motion_rect[3] - 32.0F) < 0.05F;
        };
        require(on_window(wide_left) && on_window(wide_right) &&
                wide_left->eyes[0]->serial != wide_right->eyes[0]->serial,
            "a wider stream was not placed on each eye's window");
        // The anchor's patches: the output's origin a texel and a half to the
        // left of the eye image's.
        std::int32_t origin_x = 0, origin_y = 0;
        require(xrfg::dlss_inset_origin(833, 0, &origin_x, &origin_y) &&
                (origin_x == -2 || origin_x == -1) && origin_y == 0,
            "the eye image's window was not reported for the anchor");
        // A camera that does not hold the eye's field places nothing.
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            xrfg::set_dlss_eye_view(eye, {-1.5F, 1.5F, 0.9656F, -0.9656F, 0, 0, 61, 32});
        }
        require(!resolve_wide(0), "a stream whose camera misses the eye's field guided it");
        for (std::uint32_t eye = 0; eye < 2; ++eye) xrfg::set_dlss_eye_view(eye, {});
        xrfg::retire_dlss_motion_vector_stream(833);

        // V534: Kingdom Come, no camera. The same 61 of 64 is placed in the
        // middle for a symmetric eye field, and refused for a lopsided one.
        const auto publish_plain = [&]() {
            fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
                xrfg::DlssMotionVectorPublication p{};
                p.stream = 834;
                p.output = color.Get();
                p.motion_vectors = motion.Get();
                p.output_width = 64;
                p.output_height = 32;
                p.motion_width = 32;
                p.motion_height = 16;
                p.producer_queue = fixture.queue();
                p.producer_command_list = list;
                p.resource_state = D3D12_RESOURCE_STATE_COMMON;
                xrfg::publish_dlss_motion_vectors(p);
            });
            return xrfg::dlss_motion_vector_publications();
        };
        for (int frame = 0; frame < 2; ++frame) {
            publish_plain();
            wide_release = publish_plain();
        }
        xrfg::anchor_dlss_alternating_eyes(
            834, xrfg::dlss_motion_vector_stream_position(834).epoch, 1);
        const auto resolve_plain = [&](int eye) {
            return xrfg::resolve_dlss_motion_vectors(
                window.Get(), fixture.queue(), eye, wide_release, wide_release);
        };
        require(!resolve_plain(0), "a stream without a camera guided an eye whose view is unknown");
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            xrfg::set_dlss_eye_view(eye, {-kEyeTan, kEyeTan, 0.9656F, -0.9656F, 0, 0, 61, 32});
        }
        const auto centred = resolve_plain(0);
        require(centred && centred->eyes[0] && centred->eyes[0]->placed &&
                centred->eyes[0]->output_width == 61 &&
                std::fabs(centred->eyes[0]->placed_motion_rect[0] - 0.75F) < 0.01F &&
                std::fabs(centred->eyes[0]->placed_motion_rect[2] - 30.5F) < 0.01F &&
                std::fabs(centred->eyes[0]->placed_motion_rect[1]) < 0.01F &&
                std::fabs(centred->eyes[0]->placed_motion_rect[3] - 16.0F) < 0.01F,
            "a stream without a camera was not placed on the eye image's middle");
        std::int32_t centre_x = 0, centre_y = 0;
        require(xrfg::dlss_inset_origin(834, 0, &centre_x, &centre_y) &&
                (centre_x == -2 || centre_x == -1) && centre_y == 0,
            "the middle window was not reported for the anchor");
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            xrfg::set_dlss_eye_view(eye, {-1.2F, 1.0F, 0.9656F, -0.9656F, 0, 0, 61, 32});
        }
        require(!resolve_plain(0), "a lopsided eye field took a stream's middle");
        for (std::uint32_t eye = 0; eye < 2; ++eye) xrfg::set_dlss_eye_view(eye, {});
        xrfg::retire_dlss_motion_vector_stream(834);

        // V537: Avatar, the output smaller than the eye image and of its
        // shape - the whole image enlarged. A 36 x 16 output (vectors 30 x 15)
        // on a 72 x 32 eye image (wider than every stream above) is placed
        // over all of it; an output of another shape is refused.
        const auto large_eye = texture(72);
        const auto publish_small = [&](std::uint64_t stream, UINT width, UINT height) {
            fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
                xrfg::DlssMotionVectorPublication p{};
                p.stream = stream;
                p.output = color.Get();
                p.motion_vectors = motion.Get();
                p.output_width = width;
                p.output_height = height;
                p.motion_width = 30;
                p.motion_height = 15;
                p.producer_queue = fixture.queue();
                p.producer_command_list = list;
                p.resource_state = D3D12_RESOURCE_STATE_COMMON;
                if (stream == 838U) {
                    p.view.known = true;
                    p.view.tan_left = -kEyeTan;
                    p.view.tan_right = kEyeTan;
                    p.view.tan_up = 0.9656F;
                    p.view.tan_down = -0.9656F;
                    p.view.right = {0.0F, 0.0F, 1.0F};
                }
                xrfg::publish_dlss_motion_vectors(p);
            });
            return xrfg::dlss_motion_vector_publications();
        };
        // With a camera (Avatar evaluates inside Streamline) the camera
        // places it: a field as wide as the eye's covers the whole image.
        for (const auto& [stream, height, enlarged] : {std::tuple{836U, 16U, true},
                 std::tuple{837U, 20U, false}, std::tuple{838U, 16U, true}}) {
            for (int frame = 0; frame < 2; ++frame) {
                publish_small(stream, 36, height);
                wide_release = publish_small(stream, 36, height);
            }
            xrfg::anchor_dlss_alternating_eyes(
                stream, xrfg::dlss_motion_vector_stream_position(stream).epoch, 1);
            for (std::uint32_t eye = 0; eye < 2; ++eye) {
                xrfg::set_dlss_eye_view(eye, {-kEyeTan, kEyeTan, 0.9656F, -0.9656F, 0, 0, 72, 32});
            }
            const auto placed_small = xrfg::resolve_dlss_motion_vectors(
                large_eye.Get(), fixture.queue(), 0, wide_release, wide_release);
            if (enlarged) {
                require(placed_small && placed_small->eyes[0] && placed_small->eyes[0]->placed &&
                        placed_small->eyes[0]->output_width == 72 && placed_small->eyes[0]->output_height == 32 &&
                        placed_small->eyes[0]->placed_motion_rect[0] == 0.0F &&
                        placed_small->eyes[0]->placed_motion_rect[1] == 0.0F &&
                        placed_small->eyes[0]->placed_motion_rect[2] == 30.0F &&
                        placed_small->eyes[0]->placed_motion_rect[3] == 15.0F,
                    "an output smaller than the eye image, of its shape, was not enlarged over it");
                std::int32_t origin_x = 0, origin_y = 0;
                require(!xrfg::dlss_inset_origin(stream, 0, &origin_x, &origin_y),
                    "an enlarged output was reported at the eye image's scale");
            } else {
                require(!placed_small, "an output of another shape was enlarged over the eye image");
            }
            for (std::uint32_t eye = 0; eye < 2; ++eye) xrfg::set_dlss_eye_view(eye, {});
            xrfg::retire_dlss_motion_vector_stream(stream);
        }
    }
    xrfg::configure_dlss_alternating_eyes(false);
    xrfg::configure_dlss_motion_vector_tracking(false);
    xrfg::retire_dlss_motion_vector_stream(799);
}

void test_dlss_motion_vector_gpu_ingress(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend =
        xrfg::D3D12OpticalFlowBackend::fidelity_fx) {
    constexpr UINT width = 256;
    constexpr UINT height = 128;
    constexpr UINT motion_width = width / 2;
    constexpr UINT motion_height = height / 2;
    constexpr UINT margin = 16;
    const std::array<int, kEyeCount> translation{8, -8};

    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_source_texture(fixture, width, height),
    };
    std::array<ID3D12Resource*, 2> source_pointers{
        sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_destination_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_destination_pointers{
        synthetic_destinations[0].Get()};

    const StereoPattern previous = translated_motion_pattern(width, height, {0, 0});
    const StereoPattern current = translated_motion_pattern(width, height, translation);
    const StereoPattern expected = translated_motion_pattern(width, height, {4, -4});
    const StereoPattern unscaled_expected = translated_motion_pattern(width, height, {2, -2});
    const StereoPattern same_pixel_blend = midpoint_pattern(previous, current);
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);
    ComPtr<ID3D12Resource> game_motion = create_and_upload_game_motion(
        fixture,
        motion_width,
        motion_height,
        {-static_cast<float>(translation[0]) * motion_width / width,
         -static_cast<float>(translation[1]) * motion_width / width});

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(), fixture.queue(), source_pointers,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "DLSS-motion history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(), fixture.queue(), history,
            current_destination_pointers, synthetic_destination_pointers,
            kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET, backend)),
        "DLSS-motion synthesizer initialization failed");

    auto frame_a = std::make_shared<xrfg::DlssMotionVectorFrame>();
    frame_a->stream = 17;
    frame_a->epoch = 3;
    frame_a->serial = 1;
    frame_a->motion_vectors = game_motion;
    frame_a->producer_queue = fixture.queue();
    frame_a->output_width = width;
    frame_a->output_height = height;
    frame_a->motion_width = motion_width;
    frame_a->motion_height = motion_height;
    frame_a->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    auto frame_b = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_a);
    frame_b->previous_serial = 1;
    frame_b->serial = 2;
    auto frame_a_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_a);
    frame_a_right->motion_slice = 1;
    auto frame_b_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_b);
    frame_b_right->motion_slice = 1;
    auto set_a = std::make_shared<xrfg::DlssMotionVectorSet>();
    set_a->eye_count = kEyeCount;
    set_a->eyes = {frame_a, frame_a_right};
    auto set_b = std::make_shared<xrfg::DlssMotionVectorSet>();
    set_b->eye_count = kEyeCount;
    set_b->eyes = {frame_b, frame_b_right};

    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(
        operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "DLSS-motion capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(
        operation_succeeded(synthesizer.submit_prime(
            capture_a, views, 0, &prime, set_a)),
        "DLSS-motion prime failed");
    require_frame_start_gate(synthesizer, "DLSS-motion frame-start gate");

    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(
        operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "DLSS-motion capture B failed");
    const auto statistics_before = xrfg::dlss_motion_vector_statistics();
    xrfg::D3D12FrameSynthesisTicket pair{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_b, views, views, 0, 1, &pair, std::nullopt, set_b)),
        "DLSS-motion pair failed");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    const auto statistics_after = xrfg::dlss_motion_vector_statistics();
    require(
        statistics_after.used == statistics_before.used + 1 &&
            statistics_after.status == xrfg::DlssMotionVectorStatus::used,
        "DLSS-motion path was not selected by the GPU synthesizer");

    const StereoPattern actual = readback_pattern(
        fixture, synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    // The normal backend invocation above proves shader sampling. The NVIDIA
    // fixture reuses this case specifically to exercise its extra work-list
    // lifetime while game motion bypasses OFA; rounding can make its confidence
    // result byte-identical to the smooth same-pixel reference.
    if (backend != xrfg::D3D12OpticalFlowBackend::nvidia) {
        require(actual != same_pixel_blend,
            "DLSS-motion texture was not consumed by the synthesis shader");
    }
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const double actual_error = mean_absolute_rgb_error(
            actual, expected, width, height, eye, margin);
        const double blend_error = mean_absolute_rgb_error(
            same_pixel_blend, expected, width, height, eye, margin);
        const double unscaled_error = mean_absolute_rgb_error(
            actual, unscaled_expected, width, height, eye, margin);
        const double previous_error = mean_absolute_rgb_error(
            previous, expected, width, height, eye, margin);
        const double current_error = mean_absolute_rgb_error(
            current, expected, width, height, eye, margin);
        std::cout << "DLSS motion eye=" << eye
                  << " synthetic_mae=" << actual_error
                  << " unscaled_mae=" << unscaled_error
                  << " blend_mae=" << blend_error << '\n';
        require(
            actual_error < 0.05 && actual_error < unscaled_error &&
                actual_error < previous_error && actual_error < current_error,
            "DLSS-motion low-resolution vectors were not exactly converted to output space for eye " +
                std::to_string(eye));
    }

    // Exercise enough advancing pairs to reuse a work slot. NVIDIA owns an
    // additional OFA command list per slot even though this path bypasses OFA.
    std::uint64_t guide_serial = frame_b->serial;
    for (UINT iteration = 0; iteration < 3; ++iteration) {
        const std::uint32_t source_index = iteration % 2U;
        xrfg::D3D12HistoryCaptureTicket capture{};
        require(
            operation_succeeded(history->capture(source_index, &capture)) &&
                operation_succeeded(history->commit(capture)),
            "DLSS-motion slot-reuse capture failed");
        auto guide_left = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_b);
        guide_left->previous_serial = guide_serial;
        guide_left->serial = ++guide_serial;
        auto guide_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*guide_left);
        guide_right->motion_slice = 1;
        auto guide = std::make_shared<xrfg::DlssMotionVectorSet>();
        guide->eye_count = kEyeCount;
        guide->eyes = {guide_left, guide_right};
        xrfg::D3D12FrameSynthesisTicket advancing{};
        require(
            operation_succeeded(synthesizer.submit_pair(
                capture, views, views, 0, source_index, &advancing,
                std::nullopt, guide)),
            "DLSS-motion work-slot reuse failed");
        fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    }
    require(
        operation_succeeded(synthesizer.wait_for_idle()),
        "DLSS-motion final drain failed");
    require(
        operation_succeeded(history->invalidate()),
        "DLSS-motion history invalidate failed");
}

void test_dlss_motion_vector_strafe_rejects_double_edges(
    D3D12WarpFixture& fixture) {
    constexpr UINT width = 256;
    constexpr UINT height = 128;
    constexpr UINT object_width = 64;
    constexpr UINT object_top = 32;
    constexpr UINT object_bottom = 96;
    const std::array<int, kEyeCount> previous_left{48, 144};
    const std::array<int, kEyeCount> displacement{32, -32};
    const std::array<RgbaBytes, kEyeCount> background{
        RgbaBytes{12, 20, 28, 255},
        RgbaBytes{20, 12, 32, 255}};
    const RgbaBytes foreground{238, 222, 56, 255};

    const auto make_scene = [&](bool current) {
        StereoPattern pattern;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto& bytes = pattern[eye];
            bytes.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
            const int left = previous_left[eye] +
                (current ? displacement[eye] : 0);
            const int right = left + static_cast<int>(object_width);
            for (UINT y = 0; y < height; ++y) {
                for (UINT x = 0; x < width; ++x) {
                    const bool object = y >= object_top && y < object_bottom &&
                        static_cast<int>(x) >= left && static_cast<int>(x) < right;
                    const auto& color = object ? foreground : background[eye];
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel;
                    std::copy(color.begin(), color.end(), bytes.begin() + offset);
                }
            }
        }
        return pattern;
    };

    const StereoPattern previous = make_scene(false);
    const StereoPattern current = make_scene(true);
    auto game_motion = create_and_upload_game_motion_field(
        fixture, width, height, [&](UINT eye, UINT x, UINT y) {
            const int left = previous_left[eye] + displacement[eye];
            const int right = left + static_cast<int>(object_width);
            const bool object = y >= object_top && y < object_bottom &&
                static_cast<int>(x) >= left && static_cast<int>(x) < right;
            return std::array<float, 2>{
                object ? static_cast<float>(-displacement[eye]) : 0.0F,
                0.0F};
        });

    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_source_texture(fixture, width, height)};
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);
    std::array<ID3D12Resource*, 2> source_pointers{
        sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{
        synthetic_destinations[0].Get()};

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(
                fixture.device(), fixture.queue(), source_pointers,
                D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "strafe history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(operation_succeeded(synthesizer.initialize(
                fixture.device(), fixture.queue(), history, current_pointers,
                synthetic_pointers, kFormat,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                xrfg::D3D12OpticalFlowBackend::fidelity_fx)),
        "strafe synthesizer initialization failed");

    auto frame_a = std::make_shared<xrfg::DlssMotionVectorFrame>();
    frame_a->stream = 301;
    frame_a->epoch = 7;
    frame_a->serial = 1;
    frame_a->motion_vectors = game_motion;
    frame_a->producer_queue = fixture.queue();
    frame_a->output_width = width;
    frame_a->output_height = height;
    frame_a->motion_width = width;
    frame_a->motion_height = height;
    frame_a->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    auto frame_b = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_a);
    frame_b->previous_serial = 1;
    frame_b->serial = 2;
    auto frame_a_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_a);
    frame_a_right->stream = 302;
    frame_a_right->motion_slice = 1;
    auto frame_b_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*frame_b);
    frame_b_right->stream = 302;
    frame_b_right->motion_slice = 1;
    auto set_a = std::make_shared<xrfg::DlssMotionVectorSet>();
    set_a->eye_count = kEyeCount;
    set_a->eyes = {frame_a, frame_a_right};
    auto set_b = std::make_shared<xrfg::DlssMotionVectorSet>();
    set_b->eye_count = kEyeCount;
    set_b->eyes = {frame_b, frame_b_right};

    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "strafe capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(operation_succeeded(synthesizer.submit_prime(
                capture_a, views, 0, &prime, set_a)),
        "strafe prime failed");
    require_frame_start_gate(synthesizer, "strafe frame-start gate");
    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "strafe capture B failed");
    xrfg::D3D12FrameSynthesisTicket pair{};
    require(operation_succeeded(synthesizer.submit_pair(
                capture_b, views, views, 0, 1, &pair, std::nullopt, set_b)),
        "strafe pair failed");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});

    const StereoPattern actual = readback_pattern(
        fixture, synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    const StereoPattern same_pixel_blend = midpoint_pattern(previous, current);
    std::array<std::size_t, kEyeCount> double_edge_pixels{};
    std::array<std::size_t, kEyeCount> baseline_double_edge_pixels{};
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const int minimum_x = std::min(previous_left[eye],
            previous_left[eye] + displacement[eye]) - 2;
        const int maximum_x = std::max(previous_left[eye],
            previous_left[eye] + displacement[eye]) +
            static_cast<int>(object_width) + 2;
        for (UINT y = object_top + 4; y < object_bottom - 4; ++y) {
            for (int x = minimum_x; x < maximum_x; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width +
                        static_cast<UINT>(x)) * kBytesPerPixel;
                const auto distance = [&](const RgbaBytes& color) {
                    return std::abs(static_cast<int>(actual[eye][offset]) - color[0]) +
                        std::abs(static_cast<int>(actual[eye][offset + 1]) - color[1]) +
                        std::abs(static_cast<int>(actual[eye][offset + 2]) - color[2]);
                };
                if (std::min(distance(background[eye]), distance(foreground)) > 24) {
                    ++double_edge_pixels[eye];
                }
                const auto baseline_distance = [&](const RgbaBytes& color) {
                    return std::abs(static_cast<int>(same_pixel_blend[eye][offset]) - color[0]) +
                        std::abs(static_cast<int>(same_pixel_blend[eye][offset + 1]) - color[1]) +
                        std::abs(static_cast<int>(same_pixel_blend[eye][offset + 2]) - color[2]);
                };
                if (std::min(baseline_distance(background[eye]),
                        baseline_distance(foreground)) > 24) {
                    ++baseline_double_edge_pixels[eye];
                }
            }
        }
        std::cout << "DLSS strafe eye=" << eye
                  << " double_edge_pixels=" << double_edge_pixels[eye]
                  << " same_pixel_baseline=" << baseline_double_edge_pixels[eye] << '\n';
        require(double_edge_pixels[eye] * 2 <=
                baseline_double_edge_pixels[eye] + 64,
            "DLSS strafe did not remove the motion-compensable double edge for eye " +
                std::to_string(eye));
    }
    require(operation_succeeded(synthesizer.wait_for_idle()),
        "strafe final drain failed");
    require(operation_succeeded(history->invalidate()),
        "strafe history invalidate failed");
}

void test_rotation_aware_synthesis_beats_uncompensated_flow(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend =
        xrfg::D3D12OpticalFlowBackend::fidelity_fx,
    xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options = {}) {
    constexpr UINT kRotationWidth = 160;
    constexpr UINT kRotationHeight = 80;
    constexpr UINT kEvaluationMargin = 20;
    constexpr float kYawRadians = 0.2094395102393195F;

    const ReprojectionViews views_a = make_reprojection_views(0.0F);
    const ReprojectionViews views_b = make_reprojection_views(kYawRadians);
    const ReprojectionViews identity_views = make_reprojection_views(0.0F);
    const StereoPattern previous =
        ray_direction_pattern(kRotationWidth, kRotationHeight, views_a);
    const StereoPattern current =
        ray_direction_pattern(kRotationWidth, kRotationHeight, views_b);
    const StereoPattern same_pixel_blend = midpoint_pattern(previous, current);

    const auto synthesize = [&](const char* label,
                                const ReprojectionViews& source_views_a,
                                const ReprojectionViews& source_views_b,
                                const ReprojectionViews& target_views,
                                const StereoPattern& input_previous,
                                const StereoPattern& input_current,
                                std::shared_ptr<const xrfg::DlssMotionVectorSet> motion_a = {},
                                std::shared_ptr<const xrfg::DlssMotionVectorSet> motion_b = {}) {
        std::array<ComPtr<ID3D12Resource>, 2> sources{
            create_source_texture(fixture, kRotationWidth, kRotationHeight),
            create_source_texture(fixture, kRotationWidth, kRotationHeight),
        };
        std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
            create_source_texture(fixture, kRotationWidth, kRotationHeight),
            create_source_texture(fixture, kRotationWidth, kRotationHeight),
        };
        std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
            create_source_texture(fixture, kRotationWidth, kRotationHeight),
        };
        std::array<ID3D12Resource*, 2> source_pointers{
            sources[0].Get(),
            sources[1].Get(),
        };
        std::array<ID3D12Resource*, 2> current_destination_pointers{
            current_destinations[0].Get(),
            current_destinations[1].Get(),
        };
        std::array<ID3D12Resource*, 1> synthetic_destination_pointers{
            synthetic_destinations[0].Get(),
        };
        upload_pattern(fixture, sources[0].Get(), input_previous);
        upload_pattern(fixture, sources[1].Get(), input_current);

        auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
        require(
            operation_succeeded(history->initialize(
                fixture.device(),
                fixture.queue(),
                std::span<ID3D12Resource* const>(
                    source_pointers.data(),
                    source_pointers.size()),
                D3D12_RESOURCE_STATE_RENDER_TARGET)),
            std::string(label) + " history initialization failed");
        xrfg::D3D12FrameSynthesizer synthesizer;
        require(
            operation_succeeded(synthesizer.initialize(
                fixture.device(),
                fixture.queue(),
                history,
                std::span<ID3D12Resource* const>(
                    current_destination_pointers.data(),
                    current_destination_pointers.size()),
                std::span<ID3D12Resource* const>(
                    synthetic_destination_pointers.data(),
                    synthetic_destination_pointers.size()),
                kFormat,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                backend,
                nvidia_options)),
            std::string(label) + " synthesizer initialization failed");
        xrfg::D3D12HistoryCaptureTicket capture_a{};
        require(
            operation_succeeded(history->capture(0, &capture_a)) &&
                operation_succeeded(history->commit(capture_a)),
            std::string(label) + " capture A failed");
        xrfg::D3D12FrameSynthesisTicket prime{};
        require(
            operation_succeeded(synthesizer.submit_prime(
                capture_a,
                source_views_a,
                0,
                &prime,
                std::move(motion_a))),
            std::string(label) + " prime submission failed");
        require_frame_start_gate(
            synthesizer, std::string(label) + " frame-start gate");

        xrfg::D3D12HistoryCaptureTicket capture_b{};
        require(
            operation_succeeded(history->capture(1, &capture_b)) &&
                operation_succeeded(history->commit(capture_b)),
            std::string(label) + " capture B failed");
        xrfg::D3D12FrameSynthesisTicket pair{};
        require(
            operation_succeeded(synthesizer.submit_pair(
                capture_b,
                source_views_b,
                target_views,
                0,
                1,
                &pair,
                std::nullopt,
                std::move(motion_b))),
            std::string(label) + " pair submission failed");
        require(
            pair.previous_serial == capture_a.serial &&
                pair.current_serial == capture_b.serial,
            std::string(label) + " pair chronology is incorrect");
        require(
            operation_succeeded(synthesizer.wait_for_idle()),
            std::string(label) + " synthesis drain failed");

        const StereoPattern actual_current = readback_pattern(
            fixture,
            current_destinations[1].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        const StereoPattern actual_synthetic = readback_pattern(
            fixture,
            synthetic_destinations[0].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        require(
            actual_current == input_current,
            std::string(label) + " current output is not a bit-exact copy of B");
        require_source_is_render_target(fixture, sources[0].Get());
        require_source_is_render_target(fixture, sources[1].Get());
        require(
            operation_succeeded(history->invalidate()),
            std::string(label) + " history invalidate failed");
        return actual_synthetic;
    };

    const auto validate_cropped_viewport = [&] {
        constexpr xrfg::D3D12ImageRect kCroppedRect{16, 20, 128, 40};
        ReprojectionViews cropped_views_a = make_reprojection_views(0.0F);
        ReprojectionViews cropped_views_b = make_reprojection_views(kYawRadians);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            cropped_views_a[eye].image_rect = kCroppedRect;
            cropped_views_b[eye].image_rect = kCroppedRect;
        }
        const StereoPattern cropped_previous = ray_direction_pattern(
            kRotationWidth, kRotationHeight, cropped_views_a);
        const StereoPattern cropped_current = ray_direction_pattern(
            kRotationWidth, kRotationHeight, cropped_views_b);
        const StereoPattern cropped_synthetic = synthesize(
            "cropped rotation-aware",
            cropped_views_a,
            cropped_views_b,
            cropped_views_b,
            cropped_previous,
            cropped_current);
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            const double cropped_error = mean_absolute_rgb_error_rect(
                cropped_synthetic,
                cropped_current,
                kRotationWidth,
                eye,
                kCroppedRect,
                8);
            std::cout << "cropped rotation eye=" << eye
                      << " aware_mae=" << cropped_error << '\n';
            require(
                cropped_error <= 1.0,
                "cropped viewport camera mapping is inaccurate for eye " +
                    std::to_string(eye) + ": aware=" +
                    std::to_string(cropped_error));
        }
    };

    if (backend == xrfg::D3D12OpticalFlowBackend::nvidia) {
        validate_cropped_viewport();
        return;
    }

    const StereoPattern rotation_aware = synthesize(
        "rotation-aware",
        views_a,
        views_b,
        views_b,
        previous,
        current);
    // Feed a dense B-to-A field that already includes the complete projective
    // head rotation. Subtracting the pose as a linear pixel displacement is
    // only an approximation and leaves rotation residue. The shader must map
    // the source endpoint back through the exact inverse OpenXR camera map.
    ComPtr<ID3D12Resource> pose_inclusive_game_motion =
        create_and_upload_game_motion_field(
            fixture,
            kRotationWidth,
            kRotationHeight,
            [&](UINT eye, UINT x, UINT y) {
                float source_x = static_cast<float>(x);
                float source_y = static_cast<float>(y);
                const bool valid = target_pixel_maps_to_source(
                    views_a[eye],
                    views_b[eye],
                    kRotationWidth,
                    kRotationHeight,
                    x,
                    y,
                    &source_x,
                    &source_y);
                return valid
                    ? std::array<float, 2>{
                          source_x - static_cast<float>(x),
                          source_y - static_cast<float>(y)}
                    : std::array<float, 2>{0.0F, 0.0F};
            });
    auto game_frame_a = std::make_shared<xrfg::DlssMotionVectorFrame>();
    game_frame_a->stream = 31;
    game_frame_a->epoch = 1;
    game_frame_a->serial = 1;
    game_frame_a->motion_vectors = pose_inclusive_game_motion;
    game_frame_a->producer_queue = fixture.queue();
    game_frame_a->output_width = kRotationWidth;
    game_frame_a->output_height = kRotationHeight;
    game_frame_a->motion_width = kRotationWidth;
    game_frame_a->motion_height = kRotationHeight;
    game_frame_a->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    auto game_frame_b = std::make_shared<xrfg::DlssMotionVectorFrame>(*game_frame_a);
    game_frame_b->previous_serial = 1;
    game_frame_b->serial = 2;
    auto game_frame_a_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*game_frame_a);
    game_frame_a_right->stream = 32;
    game_frame_a_right->motion_slice = 1;
    auto game_frame_b_right = std::make_shared<xrfg::DlssMotionVectorFrame>(*game_frame_b);
    game_frame_b_right->stream = 32;
    game_frame_b_right->motion_slice = 1;
    auto game_set_a = std::make_shared<xrfg::DlssMotionVectorSet>();
    game_set_a->eye_count = kEyeCount;
    game_set_a->eyes = {game_frame_a, game_frame_a_right};
    auto game_set_b = std::make_shared<xrfg::DlssMotionVectorSet>();
    game_set_b->eye_count = kEyeCount;
    game_set_b->eyes = {game_frame_b, game_frame_b_right};
    const StereoPattern game_pose_separated = synthesize(
        "DLSS iterative endpoint inversion",
        views_a,
        views_b,
        views_b,
        previous,
        current,
        game_set_a,
        game_set_b);
    const StereoPattern uncompensated = synthesize(
        "uncompensated-flow baseline",
        identity_views,
        identity_views,
        identity_views,
        previous,
        current);

    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const double aware_error = mean_absolute_rgb_error(
            rotation_aware,
            current,
            kRotationWidth,
            kRotationHeight,
            eye,
            kEvaluationMargin);
        const double uncompensated_error = mean_absolute_rgb_error(
            uncompensated,
            current,
            kRotationWidth,
            kRotationHeight,
            eye,
            kEvaluationMargin);
        const double blend_error = mean_absolute_rgb_error(
            same_pixel_blend,
            current,
            kRotationWidth,
            kRotationHeight,
            eye,
            kEvaluationMargin);
        const double previous_error = mean_absolute_rgb_error(
            previous,
            current,
            kRotationWidth,
            kRotationHeight,
            eye,
            kEvaluationMargin);
        const double game_pose_error = mean_absolute_rgb_error(
            game_pose_separated,
            current,
            kRotationWidth,
            kRotationHeight,
            eye,
            kEvaluationMargin);
        double uncovered_total = 0.0;
        std::size_t uncovered_samples = 0;
        for (UINT y = 0; y < kRotationHeight; ++y) {
            for (UINT x = 0; x < kRotationWidth; ++x) {
                if (target_pixel_maps_to_source(
                        views_a[eye],
                        views_b[eye],
                        kRotationWidth,
                        kRotationHeight,
                        x,
                        y)) {
                    continue;
                }
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kRotationWidth + x) *
                    kBytesPerPixel;
                for (UINT channel = 0; channel < 3; ++channel) {
                    uncovered_total += std::abs(
                        static_cast<int>(rotation_aware[eye][offset + channel]) -
                        static_cast<int>(current[eye][offset + channel]));
                    ++uncovered_samples;
                }
            }
        }
        require(uncovered_samples != 0, "rotation fixture has no uncovered target edge");
        const double uncovered_error =
            uncovered_total / static_cast<double>(uncovered_samples);
        std::cout << "rotation eye=" << eye
                  << " aware_mae=" << aware_error
                  << " game_pose_mae=" << game_pose_error
                  << " uncompensated_flow_mae=" << uncompensated_error
                  << " blend_mae=" << blend_error
                  << " previous_mae=" << previous_error
                  << " uncovered_mae=" << uncovered_error << '\n';
        require(
            aware_error < uncompensated_error * 0.35 &&
                aware_error < blend_error * 0.35 &&
                aware_error < previous_error * 0.25 &&
                uncovered_error <= 0.5,
            "rotation-aware synthesis does not materially beat uncompensated flow for eye " +
                std::to_string(eye) + ": aware=" + std::to_string(aware_error) +
                " uncompensated=" + std::to_string(uncompensated_error) +
                " uncovered=" + std::to_string(uncovered_error));
        require(
            game_pose_error <= 1.0,
            "DLSS vectors retained projective OpenXR rotation residue for eye " +
                std::to_string(eye) + ": game_pose=" +
                std::to_string(game_pose_error));
    }

    validate_cropped_viewport();
}

void test_double_wide_single_slice_views(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend =
        xrfg::D3D12OpticalFlowBackend::fidelity_fx,
    xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options = {}) {
    constexpr UINT kHalfWidth = 64;
    constexpr UINT kDoubleWidth = kHalfWidth * 2U;
    constexpr UINT kDoubleHeight = 64;

    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_single_slice_texture(fixture, kDoubleWidth, kDoubleHeight),
        create_single_slice_texture(fixture, kDoubleWidth, kDoubleHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_single_slice_texture(fixture, kDoubleWidth, kDoubleHeight),
        create_single_slice_texture(fixture, kDoubleWidth, kDoubleHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_single_slice_texture(fixture, kDoubleWidth, kDoubleHeight),
    };
    clear_double_wide_pattern(
        fixture, sources[0].Get(), kHalfWidth, kDoubleHeight);
    clear_double_wide_pattern(
        fixture, sources[1].Get(), kHalfWidth, kDoubleHeight);

    std::array<ID3D12Resource*, 2> source_pointers{
        sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{
        synthetic_destinations[0].Get()};

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(),
            fixture.queue(),
            std::span<ID3D12Resource* const>(
                source_pointers.data(), source_pointers.size()),
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "double-wide history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(),
            fixture.queue(),
            history,
            std::span<ID3D12Resource* const>(
                current_pointers.data(), current_pointers.size()),
            std::span<ID3D12Resource* const>(
                synthetic_pointers.data(), synthetic_pointers.size()),
            kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            backend,
            nvidia_options)),
        "double-wide synthesizer initialization failed");

    ReprojectionViews views = make_reprojection_views();
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        views[eye].image_rect = {
            eye * kHalfWidth, 0, kHalfWidth, kDoubleHeight};
        views[eye].array_slice = 0;
    }

    xrfg::D3D12HistoryCaptureTicket capture{};
    require(
        operation_succeeded(history->capture(0, &capture)) &&
            operation_succeeded(history->commit(capture)),
        "double-wide capture failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(
        operation_succeeded(synthesizer.submit_prime(
            capture, views, 0, &prime)),
        "double-wide prime failed");
    require_frame_start_gate(synthesizer, "double-wide frame-start gate");
    xrfg::D3D12FrameSynthesisTicket repeated{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture, views, views, 0, 1, &repeated)),
        "double-wide repeated pair failed");
    const auto validate_pattern = [&](ID3D12Resource* resource,
                                      const char* label,
                                      double maximum_mae) {
        const std::vector<std::uint8_t> pixels = readback_single_slice(
            fixture, resource, D3D12_RESOURCE_STATE_RENDER_TARGET);
        double total_error = 0.0;
        std::size_t sample_count = 0;
        for (UINT y = 0; y < kDoubleHeight; ++y) {
            for (UINT x = 0; x < kDoubleWidth; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kDoubleWidth + x) *
                    kBytesPerPixel;
                const RgbaBytes expected = x < kHalfWidth
                    ? RgbaBytes{255, 0, 0, 255}
                    : RgbaBytes{0, 0, 255, 255};
                for (UINT channel = 0; channel < 3; ++channel) {
                    total_error += std::abs(
                        static_cast<int>(pixels[offset + channel]) -
                        static_cast<int>(expected[channel]));
                    ++sample_count;
                }
            }
        }
        const double mae = total_error / static_cast<double>(sample_count);
        if (maximum_mae > 0.0) {
            std::cout << label << " mae=" << mae << '\n';
        }
        require(
            mae <= maximum_mae,
            std::string(label) +
                " does not preserve both single-slice eye viewports: mae=" +
                std::to_string(mae));
    };
    validate_pattern(
        current_destinations[1].Get(), "double-wide current output", 0.0);
    validate_pattern(
        synthetic_destinations[0].Get(), "double-wide synthetic output", 0.0);

    xrfg::D3D12HistoryCaptureTicket advanced_capture{};
    require(
        operation_succeeded(history->capture(1, &advanced_capture)) &&
            operation_succeeded(history->commit(advanced_capture)),
        "double-wide advancing capture failed");
    xrfg::D3D12FrameSynthesisTicket advanced_pair{};
    const HRESULT advanced_pair_result = synthesizer.submit_pair(
        advanced_capture, views, views, 0, 0, &advanced_pair);
    require(
        operation_succeeded(advanced_pair_result),
        "double-wide advancing pair failed with HRESULT " +
            std::to_string(static_cast<std::int32_t>(advanced_pair_result)));
    require(
        operation_succeeded(synthesizer.wait_for_idle()),
        "double-wide advancing synthesis drain failed");
    validate_pattern(
        current_destinations[0].Get(),
        "double-wide advancing current output",
        0.0);
    validate_pattern(
        synthetic_destinations[0].Get(),
        "double-wide advancing synthetic output",
        backend == xrfg::D3D12OpticalFlowBackend::nvidia ? 2.0 : 0.0);
    require(
        operation_succeeded(history->invalidate()),
        "double-wide history invalidate failed");
}

void test_submission_backpressure_and_recovery(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend,
    xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options = {}) {
    constexpr UINT width = 1024;
    constexpr UINT height = 512;
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 3> current_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 2> synthetic_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(), sources[1].Get(), sources[2].Get()};
    std::array<ID3D12Resource*, 3> current_destination_pointers{
        current_destinations[0].Get(), current_destinations[1].Get(),
        current_destinations[2].Get()};
    std::array<ID3D12Resource*, 2> synthetic_destination_pointers{
        synthetic_destinations[0].Get(), synthetic_destinations[1].Get()};

    upload_pattern(
        fixture, sources[0].Get(),
        translated_motion_pattern(width, height, {0, 0}));
    upload_pattern(
        fixture, sources[1].Get(),
        translated_motion_pattern(width, height, {16, -16}));
    upload_pattern(
        fixture, sources[2].Get(),
        translated_motion_pattern(width, height, {32, -32}));

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(), fixture.queue(), source_pointers,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "backpressure history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(), fixture.queue(), history,
            current_destination_pointers, synthetic_destination_pointers,
            kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET,
            backend, nvidia_options)),
        "backpressure synthesizer initialization failed");

    ComPtr<ID3D12Fence> gate;
    require_hresult(
        fixture.device()->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf())),
        "ID3D12Device::CreateFence(backpressure gate)");
    require_hresult(
        fixture.queue()->Wait(gate.Get(), 1),
        "ID3D12CommandQueue::Wait(backpressure gate)");

    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket capture_a{};
    require(
        operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "backpressure capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(
        operation_succeeded(
            synthesizer.submit_prime(capture_a, views, 0, &prime)),
        "backpressure prime failed");

    xrfg::D3D12HistoryCaptureTicket capture_b{};
    require(
        operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "backpressure capture B failed");
    // Backpressure is per-resource, not global. A pair that reuses the
    // destination the prime is still writing - current index 0 here, the
    // prime's - must be refused, and refused without blocking: the caller
    // fails open on ERROR_BUSY and cannot afford a stall to learn that.
    xrfg::D3D12FrameSynthesisTicket collided{};
    const auto immediate_started = std::chrono::steady_clock::now();
    const HRESULT immediate_result = synthesizer.submit_pair(
        capture_b, views, views, 1, 0, &collided);
    const auto immediate_elapsed =
        std::chrono::steady_clock::now() - immediate_started;
    require(
        immediate_result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
            collided.fence_value == 0 &&
            immediate_elapsed < std::chrono::milliseconds(50),
        "a pair reusing the prime's destination was not refused immediately");

    // A pair whose resources do not collide is admitted while the prime is
    // still outstanding, and its work queues behind the prime's on the GPU.
    // The synthesiser holds three work slots and alternating destinations so
    // that this is legal; there is deliberately no global gate that would
    // collapse it to one submission in flight.
    xrfg::D3D12FrameSynthesisTicket pair_ab{};
    require(
        operation_succeeded(synthesizer.submit_pair(
            capture_b, views, views, 0, 1, &pair_ab)) &&
            pair_ab.previous_serial == capture_a.serial &&
            pair_ab.current_serial == capture_b.serial &&
            pair_ab.fence_value > prime.fence_value,
        "a non-colliding pair was refused while earlier work was outstanding");

    const auto blocked_started = std::chrono::steady_clock::now();
    const HRESULT blocked_result =
        synthesizer.wait_for_previous_submission(100);
    const auto blocked_elapsed = std::chrono::steady_clock::now() - blocked_started;
    require(
        blocked_result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
            blocked_elapsed >= std::chrono::milliseconds(80),
        "the drain wait did not block on unfinished bridge work");

    require_hresult(
        gate->Signal(1),
        "ID3D12Fence::Signal(backpressure release)");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    require(
        operation_succeeded(synthesizer.wait_for_previous_submission(100)),
        "the drain wait did not recover after the queue advanced");
    const HRESULT drain_result = synthesizer.wait_for_idle();
    require(
        operation_succeeded(drain_result),
        "backpressure final drain failed with HRESULT " +
            std::to_string(static_cast<std::int32_t>(drain_result)));
    require(
        operation_succeeded(history->invalidate()),
        "backpressure history invalidate failed");
}

void test_nvidia_serialized_eye_context_stress(
    D3D12WarpFixture& fixture,
    xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options) {
    constexpr UINT width = 1024;
    constexpr UINT height = 512;
    constexpr std::uint32_t pair_count = 1000;
    std::array<ComPtr<ID3D12Resource>, 3> sources{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 3> current_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ComPtr<ID3D12Resource>, 3> synthetic_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height),
    };
    std::array<ID3D12Resource*, 3> source_pointers{
        sources[0].Get(), sources[1].Get(), sources[2].Get()};
    std::array<ID3D12Resource*, 3> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get(),
        current_destinations[2].Get()};
    std::array<ID3D12Resource*, 3> synthetic_pointers{
        synthetic_destinations[0].Get(), synthetic_destinations[1].Get(),
        synthetic_destinations[2].Get()};
    for (UINT index = 0; index < sources.size(); ++index) {
        upload_pattern(
            fixture,
            sources[index].Get(),
            translated_motion_pattern(
                width,
                height,
                {static_cast<int>(index * 16),
                 -static_cast<int>(index * 16)}));
    }

    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(
        operation_succeeded(history->initialize(
            fixture.device(), fixture.queue(), source_pointers,
            D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "NVIDIA context stress history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(
        operation_succeeded(synthesizer.initialize(
            fixture.device(), fixture.queue(), history, current_pointers,
            synthetic_pointers, kFormat,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            xrfg::D3D12OpticalFlowBackend::nvidia, nvidia_options)),
        "NVIDIA context stress synthesizer initialization failed");

    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket first{};
    require(
        operation_succeeded(history->capture(0, &first)) &&
            operation_succeeded(history->commit(first)),
        "NVIDIA context stress prime capture failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(
        operation_succeeded(
            synthesizer.submit_prime(first, views, 0, &prime)),
        "NVIDIA context stress prime failed");

    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t pair = 1; pair <= pair_count; ++pair) {
        const std::uint32_t index = static_cast<std::uint32_t>(
            pair % sources.size());
        xrfg::D3D12HistoryCaptureTicket capture{};
        HRESULT capture_result = E_FAIL;
        const auto capture_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            capture_result = history->capture(index, &capture);
            if (capture_result == HRESULT_FROM_WIN32(ERROR_BUSY)) {
                Sleep(0);
            }
        } while (capture_result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
                 std::chrono::steady_clock::now() < capture_deadline);
        require(
            operation_succeeded(capture_result) &&
                operation_succeeded(history->commit(capture)),
            "NVIDIA context stress capture failed at pair " +
                std::to_string(pair) + " HRESULT " +
                std::to_string(static_cast<std::int32_t>(capture_result)));
        xrfg::D3D12FrameSynthesisTicket output{};
        HRESULT submit_result = E_FAIL;
        const auto submit_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            submit_result = synthesizer.submit_pair(
                capture, views, views, index, index, &output);
            if (submit_result == HRESULT_FROM_WIN32(ERROR_BUSY)) {
                Sleep(0);
            }
        } while (submit_result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
                 std::chrono::steady_clock::now() < submit_deadline);
        require(
            operation_succeeded(submit_result),
            "NVIDIA context stress submit failed at pair " +
                std::to_string(pair) + " HRESULT " +
                std::to_string(static_cast<std::int32_t>(submit_result)) +
                " serial " + std::to_string(capture.serial) + " slot " +
                std::to_string(capture.slot));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(
        operation_succeeded(synthesizer.wait_for_idle()) &&
            operation_succeeded(history->invalidate()),
        "NVIDIA context stress retirement failed");
    fixture.require_no_debug_errors();
    std::cout << "NVIDIA serialized eye-context stress passed "
              << pair_count << " pairs in " << elapsed.count() << " ms\n";
}

#include "nvidia_fast_patterns.inc"

}  // namespace

// The hybrid composition on WARP: the game's vectors describe a moving
// object over a moving background (occlusion), a static overlay the vectors
// wrongly say moves with the background (overlay - a HUD drawn after DLSS),
// and a brightness fade the vectors describe exactly (fade). With FidelityFX
// flow beside the vectors, the pixels at the object's edges and across the
// overlay must come out closer to the true in-between frame than a
// same-pixel blend of the two frames does, which is what a composition with
// no motion at all would show.
// occlusion_low_res: the occlusion scene with DLSS-style render-resolution
// vectors, two thirds of the output (DI2 renders 2782 for a 4172 output).
// occlusion_periphery (V515): the occlusion scene with a guide placed by its
// camera, as MSFS 2024's foveated periphery is - its field a sixteenth wider
// than the eye's on every side, its vectors a third of the eye's pixels
// over that field - through place_guide_in_view as the resolver places it.
// occlusion_foveated* (V516): the periphery above with MSFS 2024's inset
// beside it - a 160 x 96 field around the object at two image pixels a
// vector, composed into one guide (guide_compose.hlsl). _moved: the inset
// sat 8 pixels further left at its previous evaluation, and its vectors lead
// into that previous inset (reading A); _moved_current: the same move, its
// vectors in the current inset (reading B). The composition must find which.
enum class HybridScene {
    occlusion, overlay, fade, occlusion_low_res, occlusion_periphery,
    occlusion_foveated, occlusion_foveated_moved, occlusion_foveated_moved_current,
    // V539: the object twice as fast over a flat background, the revealed
    // strip wider than the edge probes' reach.
    occlusion_fast,
    // V539: the same, dark and low in contrast - the object within the
    // second-surface search's margin of the background (Returnal's wall).
    occlusion_fast_dark
};

// backend nvidia: real OFA on the NVIDIA GPU ("hybrid nvidia"), measured
// only - the bounds below are FidelityFX's on WARP.
void test_hybrid_composition(D3D12WarpFixture& fixture, HybridScene kind,
    xrfg::D3D12OpticalFlowBackend backend = xrfg::D3D12OpticalFlowBackend::fidelity_fx) {
    // The fast scene without the coverage mask measures the ghost it removes.
    const bool measure_only = backend == xrfg::D3D12OpticalFlowBackend::nvidia ||
        ((kind == HybridScene::occlusion_fast || kind == HybridScene::occlusion_fast_dark) &&
            GetEnvironmentVariableW(L"XRFG_DISABLE_COVERAGE_MASK", nullptr, 0) != 0);
    constexpr UINT width = 256, height = 128;
    constexpr int object_left = 72, object_width = 80, object_top = 32, object_bottom = 96;
    constexpr int background_motion = 6;
    const bool overlay = kind == HybridScene::overlay, fade = kind == HybridScene::fade;
    const bool low_res = kind == HybridScene::occlusion_low_res;
    const bool foveated = kind == HybridScene::occlusion_foveated ||
        kind == HybridScene::occlusion_foveated_moved ||
        kind == HybridScene::occlusion_foveated_moved_current;
    const bool periphery = kind == HybridScene::occlusion_periphery || foveated;
    const float inset_shift = kind == HybridScene::occlusion_foveated ? 0.0F : 8.0F;
    const bool inset_reading_a = kind == HybridScene::occlusion_foveated_moved;
    constexpr float inset_x = 48.0F, inset_y = 16.0F, inset_width = 160.0F, inset_height = 96.0F;
    constexpr UINT inset_motion_width = 80, inset_motion_height = 48;
    const bool dark = kind == HybridScene::occlusion_fast_dark;
    const bool fast = kind == HybridScene::occlusion_fast || dark;
    const bool occluding = kind == HybridScene::occlusion || low_res || periphery || fast;
    const int object_motion = overlay ? 0 : fade ? background_motion : fast ? 40 : 20;
    const char* const name = fade ? "fade" : overlay ? "overlay"
        : dark ? "occlusion fast, dark low contrast"
        : fast ? "occlusion fast, flat background"
        : low_res ? "occlusion low-res vectors"
        : kind == HybridScene::occlusion_foveated ? "occlusion foveated inset"
        : kind == HybridScene::occlusion_foveated_moved ? "occlusion foveated inset moved (A)"
        : kind == HybridScene::occlusion_foveated_moved_current ? "occlusion foveated inset moved (B)"
        : periphery ? "occlusion periphery vectors" : "occlusion";
    // The vectors' field in image pixels: the image itself, or for the
    // periphery 16 and 8 pixels wider each side.
    const float field_left = periphery ? -16.0F : 0.0F;
    const float field_top = periphery ? -8.0F : 0.0F;
    const float field_width = periphery ? width + 32.0F : static_cast<float>(width);
    const float field_height = periphery ? height + 16.0F : static_cast<float>(height);
    const UINT motion_width = low_res ? width * 2U / 3U : periphery ? (width + 32U) / 3U : width;
    const UINT motion_height = low_res ? height * 2U / 3U : periphery ? (height + 16U) / 3U : height;
    const auto make_scene = [&](int background_shift, int object_shift, float brightness = 1.0F) {
        StereoPattern pattern;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto& bytes = pattern[eye];
            bytes.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
            for (UINT y = 0; y < height; ++y) {
                for (UINT x = 0; x < width; ++x) {
                    const int ox = static_cast<int>(x) - object_shift;
                    const bool object = static_cast<int>(y) >= object_top &&
                        static_cast<int>(y) < object_bottom && ox >= object_left &&
                        ox < object_left + object_width;
                    const int bx = static_cast<int>(x) - background_shift + 64;
                    const RgbaBytes color = object
                        ? (dark ? ((ox / 5) % 2 ? RgbaBytes{40, 40, 42, 255} : RgbaBytes{17, 17, 19, 255})
                                : (ox / 5) % 2 ? RgbaBytes{240, 210, 40, 255} : RgbaBytes{30, 60, 200, 255})
                        : dark ? RgbaBytes{28, 28, 30, 255}
                        : fast ? RgbaBytes{70, 90, 110, 255}
                        : RgbaBytes{static_cast<std::uint8_t>(40 + (bx * 13 + static_cast<int>(y) * 5) % 160),
                                    static_cast<std::uint8_t>(60 + ((bx / 3) % 7) * 20 + eye * 9),
                                    static_cast<std::uint8_t>(90 + (bx * 7) % 120), 255};
                    const RgbaBytes lit{static_cast<std::uint8_t>(std::lround(color[0] * brightness)),
                                        static_cast<std::uint8_t>(std::lround(color[1] * brightness)),
                                        static_cast<std::uint8_t>(std::lround(color[2] * brightness)), 255};
                    std::copy(lit.begin(), lit.end(), bytes.begin() +
                        (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel);
                }
            }
        }
        return pattern;
    };
    const StereoPattern previous = make_scene(0, 0);
    const StereoPattern current = make_scene(background_motion, object_motion, fade ? 0.6F : 1.0F);
    const StereoPattern expected =
        make_scene(background_motion / 2, object_motion / 2, fade ? 0.8F : 1.0F);
    // The game's B-to-A vectors, in render pixels: the background's motion
    // everywhere, the object's where the object is in B. The overlay scene's
    // vectors claim the object moved with the background.
    auto game_motion = create_and_upload_game_motion_field(
        fixture, motion_width, motion_height, [&](UINT, UINT mx, UINT my) {
            // The output pixel under this render pixel's centre.
            const int x = static_cast<int>(std::floor(field_left +
                (static_cast<float>(mx) + 0.5F) * field_width / motion_width));
            const int y = static_cast<int>(std::floor(field_top +
                (static_cast<float>(my) + 0.5F) * field_height / motion_height));
            const int ox = x - object_motion;
            const bool object = y >= object_top && y < object_bottom &&
                ox >= object_left && ox < object_left + object_width;
            const float scale = static_cast<float>(motion_width) / field_width;
            return std::array<float, 2>{
                -scale * static_cast<float>(object && !overlay ? object_motion : background_motion),
                0.0F};
        });
    // The inset's vectors, in its output pixels (one an image pixel) at two
    // output pixels a vector: the true motion, plus the inset's own move
    // where they lead into the previous inset.
    ComPtr<ID3D12Resource> inset_motion;
    if (foveated) {
        inset_motion = create_and_upload_game_motion_field(
            fixture, inset_motion_width, inset_motion_height, [&](UINT, UINT mx, UINT my) {
                const int x = static_cast<int>(std::floor(inset_x + (static_cast<float>(mx) + 0.5F) * 2.0F));
                const int y = static_cast<int>(std::floor(inset_y + (static_cast<float>(my) + 0.5F) * 2.0F));
                const int ox = x - object_motion;
                const bool object = y >= object_top && y < object_bottom &&
                    ox >= object_left && ox < object_left + object_width;
                const float motion = -static_cast<float>(object ? object_motion : background_motion) +
                    (inset_reading_a ? inset_shift : 0.0F);
                return std::array<float, 2>{motion * 0.5F, 0.0F};
            });
    }
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture, width, height),
        create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_source_texture(fixture, width, height)};
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);
    std::array<ID3D12Resource*, 2> source_pointers{sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{synthetic_destinations[0].Get()};
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(fixture.device(), fixture.queue(),
                source_pointers, D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "hybrid history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    xrfg::D3D12NvidiaOpticalFlowOptions options;
    options.hybrid = true;
    require(operation_succeeded(synthesizer.initialize(fixture.device(), fixture.queue(),
                history, current_pointers, synthetic_pointers, kFormat,
                D3D12_RESOURCE_STATE_RENDER_TARGET, backend,
                options)),
        "hybrid synthesizer initialization failed");
    const auto guides = [&](std::uint64_t serial) {
        auto set = std::make_shared<xrfg::DlssMotionVectorSet>();
        set->eye_count = kEyeCount;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto frame = std::make_shared<xrfg::DlssMotionVectorFrame>();
            frame->stream = 311 + eye;
            frame->epoch = 3;
            frame->serial = serial;
            frame->previous_serial = serial - 1;
            frame->motion_vectors = game_motion;
            frame->producer_queue = fixture.queue();
            frame->output_width = width;
            frame->output_height = height;
            frame->motion_width = motion_width;
            frame->motion_height = motion_height;
            frame->motion_slice = frame->output_slice = eye;
            frame->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (periphery) {
                // The eye's field is tangent +-1 each way; the camera's is
                // the field above, in the same tangents.
                frame->output_width = motion_width * 2U;
                frame->output_height = motion_height * 2U;
                frame->view.known = true;
                frame->view.tan_left = -1.0F + 2.0F * field_left / width;
                frame->view.tan_right = -1.0F + 2.0F * (field_left + field_width) / width;
                frame->view.tan_up = 1.0F - 2.0F * field_top / height;
                frame->view.tan_down = 1.0F - 2.0F * (field_top + field_height) / height;
                const xrfg::DlssEyeView eye_view{-1.0F, 1.0F, 1.0F, -1.0F, 0, 0, width, height};
                auto placed = xrfg::place_guide_in_view(*frame, eye_view);
                require(placed != nullptr && placed->placed && placed->output_width == width &&
                        placed->output_height == height &&
                        std::fabs(placed->placed_motion_rect[0] - 16.0F / 3.0F) < 1e-3F &&
                        std::fabs(placed->placed_motion_rect[2] - width / 3.0F) < 1e-3F,
                    "the periphery guide was not placed over the eye");
                if (foveated) {
                    auto inset = std::make_shared<xrfg::DlssMotionVectorFrame>();
                    inset->stream = 331 + eye;
                    inset->epoch = 3;
                    inset->serial = serial;
                    inset->previous_serial = serial - 1;
                    inset->motion_vectors = inset_motion;
                    inset->producer_queue = fixture.queue();
                    inset->motion_slice = inset->output_slice = eye;
                    inset->motion_width = inset_motion_width;
                    inset->motion_height = inset_motion_height;
                    inset->output_width = static_cast<std::uint32_t>(inset_width);
                    inset->output_height = static_cast<std::uint32_t>(inset_height);
                    inset->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    inset->view_rect = {inset_x, inset_y, inset_width, inset_height};
                    inset->previous_view_rect = {inset_x - inset_shift, inset_y, inset_width, inset_height};
                    inset->previous_view_rect_known = true;
                    auto owner = std::make_shared<xrfg::DlssMotionVectorFrame>(*placed);
                    owner->inset = inset;
                    set->eyes[eye] = owner;
                    continue;
                }
                set->eyes[eye] = placed;
                continue;
            }
            set->eyes[eye] = frame;
        }
        return set;
    };
    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket capture_a{}, capture_b{};
    require(operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "hybrid capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{}, pair{};
    require(operation_succeeded(synthesizer.submit_prime(capture_a, views, 0, &prime, guides(1))),
        "hybrid prime failed");
    require_frame_start_gate(synthesizer, "hybrid frame-start gate");
    require(operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "hybrid capture B failed");
    const auto statistics_before = xrfg::dlss_motion_vector_statistics();
    require(operation_succeeded(synthesizer.submit_pair(capture_b, views, views, 0, 1, &pair,
                std::nullopt, guides(2))),
        "hybrid pair failed");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    const auto statistics_after = xrfg::dlss_motion_vector_statistics();
    require(statistics_after.hybrid_used == statistics_before.hybrid_used + 1 &&
            statistics_after.used == statistics_before.used + 1,
        "the pair was not composed by the hybrid");
    const StereoPattern actual = readback_pattern(fixture, synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto blend = midpoint_pattern(previous, current);
    // The bands either side of each edge of the object at the midpoint; the
    // overlay is measured whole, the fade everywhere but the borders.
    const int left = object_left + object_motion / 2, right = left + object_width;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        double total = 0, blend_total = 0;
        std::size_t count = 0;
        for (int y = object_top + 2; y < object_bottom - 2; ++y) {
            for (int x = 0; x < static_cast<int>(width); ++x) {
                if (fade ? x < 16 || x >= static_cast<int>(width) - 16
                    : overlay ? x < left - 2 || x >= right + 2
                              : std::abs(x - left) >= 12 && std::abs(x - right) >= 12) continue;
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + static_cast<UINT>(x)) * kBytesPerPixel;
                for (UINT c = 0; c < 3; ++c, ++count) {
                    blend_total += std::abs(static_cast<int>(blend[eye][offset + c]) -
                                            static_cast<int>(expected[eye][offset + c]));
                    total += std::abs(static_cast<int>(actual[eye][offset + c]) -
                                      static_cast<int>(expected[eye][offset + c]));
                }
            }
        }
        const double edge_error = total / static_cast<double>(count);
        const double blend_error = blend_total / static_cast<double>(count);
        std::cout << "hybrid " << name << " eye=" << eye << " edge_mae=" << edge_error
                  << " blend_mae=" << blend_error << '\n';
        if (occluding) {
            // Each edge alone: the object moves right, so its left edge
            // reveals background and its right edge covers it.
            for (const int edge : {left, right}) {
                double side_total = 0;
                std::size_t side_count = 0;
                for (int y = object_top + 2; y < object_bottom - 2; ++y) {
                    for (int x = edge - 12; x < edge + 12; ++x) {
                        const std::size_t offset =
                            (static_cast<std::size_t>(y) * width + static_cast<UINT>(x)) * kBytesPerPixel;
                        for (UINT c = 0; c < 3; ++c, ++side_count)
                            side_total += std::abs(static_cast<int>(actual[eye][offset + c]) -
                                                   static_cast<int>(expected[eye][offset + c]));
                    }
                }
                const double side_error = side_total / static_cast<double>(side_count);
                std::cout << "  " << (edge == left ? "revealing" : "covering") << " edge_mae="
                          << side_error << '\n';
                // V539: the coverage mask keeps the fast object's revealed
                // strip clean (25.6 without it, 0.08 with; the 20 px scenes
                // 5.2 -> 0.10).
                require(measure_only || !fast || edge != left || side_error <= (dark ? 0.5 : 1.0),
                    "the fast object's revealed background ghosted it");
            }
        }
        // The overlay's blend is exact (nothing moved), so it is held to an
        // absolute bound; the moving scenes must beat the blend.
        require(measure_only || (overlay ? edge_error <= 12.0 : edge_error < blend_error),
            std::string("hybrid ") + name + " did not beat the same-pixel blend for eye " +
                std::to_string(eye) + ": " + std::to_string(edge_error) + " vs " +
                std::to_string(blend_error));
        // V500: the vectors keep the edges they explain as an occlusion
        // (2.4 before, when the flow took them; 0.7 after).
        // V503: render-resolution vectors held to the same bound.
        // V507: the cheaper defaults chosen in DI2 measure 2.8 here (the
        // revealing edge 5.2 against 1.3 with V506's values; 32.5 without
        // the one edge surface solve). Held at 6 so losing that solve fails.
        require(measure_only || !occluding || edge_error <= 6.0,
            std::string("hybrid occlusion edges went back to the flow for eye ") +
                std::to_string(eye) + ": " + std::to_string(edge_error));
        // V516: with the inset composed in, its edges are the full-density
        // vectors' (2.8; the periphery alone 4.0, the wrong reading of a
        // moved inset 85).
        require(measure_only || !foveated || edge_error <= 3.3,
            std::string("hybrid ") + name + " did not take the inset's vectors for eye " +
                std::to_string(eye) + ": " + std::to_string(edge_error));
    }
    require(operation_succeeded(synthesizer.wait_for_idle()), "hybrid final drain failed");
    require(operation_succeeded(history->invalidate()), "hybrid history invalidate failed");
}

// A per-eye depth field (R32_FLOAT, two slices), for the guides' depth.
template <typename DepthForPixel>
[[nodiscard]] ComPtr<ID3D12Resource> create_and_upload_game_depth_field(
    D3D12WarpFixture& fixture, UINT width, UINT height, DepthForPixel&& depth_for_pixel) {
    D3D12_RESOURCE_DESC description = stereo_texture_description(width, height);
    description.Format = DXGI_FORMAT_R32_FLOAT;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    require_hresult(fixture.device()->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                        &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(texture.GetAddressOf())),
        "ID3D12Device::CreateCommittedResource(game depth)");
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kEyeCount> layouts{};
    std::array<UINT, kEyeCount> row_counts{};
    std::array<UINT64, kEyeCount> row_sizes{};
    UINT64 total_size = 0;
    get_copy_layouts(fixture.device(), description, &layouts, &row_counts, &row_sizes, &total_size);
    ComPtr<ID3D12Resource> upload = create_buffer(
        fixture, D3D12_HEAP_TYPE_UPLOAD, total_size, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    require_hresult(upload->Map(0, &no_read, &mapped), "ID3D12Resource::Map(game depth upload)");
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        for (UINT y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<float*>(static_cast<std::byte*>(mapped) +
                layouts[eye].Offset + static_cast<std::size_t>(y) * layouts[eye].Footprint.RowPitch);
            for (UINT x = 0; x < width; ++x) row[x] = depth_for_pixel(eye, x, y);
        }
    }
    upload->Unmap(0, nullptr);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[eye];
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = eye;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        const D3D12_RESOURCE_BARRIER ready = transition_barrier(texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &ready);
    });
    return texture;
}

void upload_single_slice(
    D3D12WarpFixture& fixture,
    ID3D12Resource* texture,
    const std::vector<std::uint8_t>& bytes) {
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT row_count = 0;
    UINT64 row_size = 0, total_size = 0;
    fixture.device()->GetCopyableFootprints(
        &description, 0, 1, 0, &layout, &row_count, &row_size, &total_size);
    const UINT width = static_cast<UINT>(description.Width);
    require(bytes.size() == static_cast<std::size_t>(width) * description.Height * kBytesPerPixel,
        "single-slice pattern dimensions do not match the texture");
    ComPtr<ID3D12Resource> upload = create_buffer(
        fixture, D3D12_HEAP_TYPE_UPLOAD, total_size, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    require_hresult(upload->Map(0, &no_read, &mapped), "ID3D12Resource::Map(single-slice upload)");
    for (UINT y = 0; y < row_count; ++y) {
        std::memcpy(static_cast<std::byte*>(mapped) + layout.Offset +
                static_cast<std::size_t>(y) * layout.Footprint.RowPitch,
            bytes.data() + static_cast<std::size_t>(y) * width * kBytesPerPixel,
            static_cast<std::size_t>(row_size));
    }
    upload->Unmap(0, nullptr);
    fixture.execute_and_wait([&](ID3D12GraphicsCommandList* list) {
        const auto to_copy = transition_barrier(
            texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = layout;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const auto back = transition_barrier(
            texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &back);
    });
}

// V501: UEVR's Native Stereo - both eyes side by side in one array-size-one
// image, a DLSS stream per eye. The hybrid has to draw each half with its own
// eye's vectors; the right eye's object moves at another speed, so a half
// drawn with the other eye's vectors misplaces its edges. UEVR crops each eye
// inside its half (DI2: 0-3663 and 4680-8344 of 8344), so the views here are
// cropped too, and the guides are placed as the resolver places them.
// one_field (V513): No Man's Sky evaluates DLSS once for its whole
// side-by-side image, so one guide spans both eyes, each view reading its
// own half of it.
void test_hybrid_side_by_side(D3D12WarpFixture& fixture, bool one_field = false) {
    constexpr UINT half = 256, width = half * 2U, height = 128;
    constexpr int object_left = 72, object_width = 80, object_top = 32, object_bottom = 96;
    constexpr int background_motion = 6;
    constexpr std::array<int, 2> object_motion{20, 12};
    const auto make_scene = [&](int background_shift, const std::array<int, 2>& object_shift) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * kBytesPerPixel);
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                const UINT eye = x / half;
                const int local = static_cast<int>(x % half);
                const int ox = local - object_shift[eye];
                const bool object = static_cast<int>(y) >= object_top &&
                    static_cast<int>(y) < object_bottom && ox >= object_left &&
                    ox < object_left + object_width;
                const int bx = local - background_shift + 64;
                const RgbaBytes color = object
                    ? ((ox / 5) % 2 ? RgbaBytes{240, 210, 40, 255} : RgbaBytes{30, 60, 200, 255})
                    : RgbaBytes{static_cast<std::uint8_t>(40 + (bx * 13 + static_cast<int>(y) * 5) % 160),
                                static_cast<std::uint8_t>(60 + ((bx / 3) % 7) * 20 + eye * 9),
                                static_cast<std::uint8_t>(90 + (bx * 7) % 120), 255};
                std::copy(color.begin(), color.end(), bytes.begin() +
                    (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel);
            }
        }
        return bytes;
    };
    const auto previous = make_scene(0, {0, 0});
    const auto current = make_scene(background_motion, object_motion);
    const auto expected = make_scene(background_motion / 2, {object_motion[0] / 2, object_motion[1] / 2});
    // One render-resolution field per eye, as two slices of one texture; or
    // one field across the whole image, in the first slice.
    auto game_motion = create_and_upload_game_motion_field(
        fixture, one_field ? width : half, height, [&](UINT field_eye, UINT field_x, UINT y) {
            const UINT eye = one_field ? field_x / half : field_eye;
            const UINT x = one_field ? field_x % half : field_x;
            const int ox = static_cast<int>(x) - object_motion[eye];
            const bool object = static_cast<int>(y) >= object_top &&
                static_cast<int>(y) < object_bottom && ox >= object_left &&
                ox < object_left + object_width;
            return std::array<float, 2>{
                static_cast<float>(-(object ? object_motion[eye] : background_motion)), 0.0F};
        });
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_single_slice_texture(fixture, width, height),
        create_single_slice_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_single_slice_texture(fixture, width, height),
        create_single_slice_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_single_slice_texture(fixture, width, height)};
    upload_single_slice(fixture, sources[0].Get(), previous);
    upload_single_slice(fixture, sources[1].Get(), current);
    std::array<ID3D12Resource*, 2> source_pointers{sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{synthetic_destinations[0].Get()};
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(fixture.device(), fixture.queue(),
                source_pointers, D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "side-by-side history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    xrfg::D3D12NvidiaOpticalFlowOptions options;
    options.hybrid = true;
    require(operation_succeeded(synthesizer.initialize(fixture.device(), fixture.queue(),
                history, current_pointers, synthetic_pointers, kFormat,
                D3D12_RESOURCE_STATE_RENDER_TARGET, xrfg::D3D12OpticalFlowBackend::fidelity_fx,
                options)),
        "side-by-side synthesizer initialization failed");
    const auto guides = [&](std::uint64_t serial) {
        auto set = std::make_shared<xrfg::DlssMotionVectorSet>();
        if (one_field) {
            auto frame = std::make_shared<xrfg::DlssMotionVectorFrame>();
            frame->stream = 421;
            frame->epoch = 3;
            frame->serial = serial;
            frame->previous_serial = serial - 1;
            frame->motion_vectors = game_motion;
            frame->producer_queue = fixture.queue();
            frame->output_width = frame->motion_width = width;
            frame->output_height = frame->motion_height = height;
            frame->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            set->eye_count = 1;
            set->eyes[0] = frame;
            return set;
        }
        set->eye_count = kEyeCount;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto frame = std::make_shared<xrfg::DlssMotionVectorFrame>();
            frame->stream = 411 + eye;
            frame->epoch = 3;
            frame->serial = serial;
            frame->previous_serial = serial - 1;
            frame->motion_vectors = game_motion;
            frame->producer_queue = fixture.queue();
            frame->output_width = frame->motion_width = half;
            frame->output_height = frame->motion_height = height;
            frame->output_x = eye * half;
            frame->motion_slice = eye;
            frame->output_slice = 0;
            frame->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            set->eyes[eye] = frame;
        }
        return set;
    };
    constexpr UINT crop = 32;
    ReprojectionViews views = make_reprojection_views();
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        views[eye].image_rect = {eye * half + (eye == 1 ? crop : 0U), 0, half - crop, height};
        views[eye].array_slice = 0;
    }
    xrfg::D3D12HistoryCaptureTicket capture_a{}, capture_b{};
    require(operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "side-by-side capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{}, pair{};
    require(operation_succeeded(synthesizer.submit_prime(capture_a, views, 0, &prime, guides(1))),
        "side-by-side prime failed");
    require_frame_start_gate(synthesizer, "side-by-side frame-start gate");
    require(operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "side-by-side capture B failed");
    const auto statistics_before = xrfg::dlss_motion_vector_statistics();
    require(operation_succeeded(synthesizer.submit_pair(capture_b, views, views, 0, 1, &pair,
                std::nullopt, guides(2))),
        "side-by-side pair failed");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    const auto statistics_after = xrfg::dlss_motion_vector_statistics();
    require(statistics_after.hybrid_used == statistics_before.hybrid_used + 1,
        "the side-by-side pair was not composed by the hybrid");
    const auto actual = readback_single_slice(
        fixture, synthetic_destinations[0].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        const int left = object_left + object_motion[eye] / 2, right = left + object_width;
        double total = 0, blend_total = 0;
        std::size_t count = 0;
        for (int y = object_top + 2; y < object_bottom - 2; ++y) {
            for (int local = 0; local < static_cast<int>(half); ++local) {
                if (std::abs(local - left) >= 12 && std::abs(local - right) >= 12) continue;
                const std::size_t offset = (static_cast<std::size_t>(y) * width +
                    eye * half + static_cast<UINT>(local)) * kBytesPerPixel;
                for (UINT c = 0; c < 3; ++c, ++count) {
                    const int truth = expected[offset + c];
                    blend_total += std::abs((previous[offset + c] + current[offset + c]) / 2 - truth);
                    total += std::abs(static_cast<int>(actual[offset + c]) - truth);
                }
            }
        }
        const double edge_error = total / static_cast<double>(count);
        const double blend_error = blend_total / static_cast<double>(count);
        std::cout << "hybrid side-by-side" << (one_field ? " one field" : "") << " eye=" << eye
                  << " edge_mae=" << edge_error
                  << " blend_mae=" << blend_error << '\n';
        // A half drawn with the other eye's vectors measures about 48; the
        // V507 defaults measure 2.8 (the occlusion scene's bound).
        require(edge_error <= 6.0,
            "hybrid side-by-side eye " + std::to_string(eye) +
                " did not draw with its own vectors: " + std::to_string(edge_error));
    }
    require(operation_succeeded(synthesizer.wait_for_idle()), "side-by-side final drain failed");
    require(operation_succeeded(history->invalidate()), "side-by-side history invalidate failed");
}

// V505: thin near bars on a lateral move. The bars travel 16 px a frame
// over a background travelling 4, so between the two frames each bar sweeps
// over background that both frames show: the solve from the generated pixel
// finds that background, whose samples agree, and the bar is missing from
// the generated frame - the bars look half transparent at 90 Hz. With the
// game's depth the nearer surface reaching the pixel is found and kept.
// Returns the share of the bars' true pixels that show bar. V530: `length`
// past 256 adds background far from every bar, where the depth tiles skip
// the search; `image` receives the generated frame. V531: `vertical` turns
// the scene a quarter round, bars and motion along y.
double test_hybrid_bars(D3D12WarpFixture& fixture, bool with_depth,
    xrfg::D3D12OpticalFlowBackend backend = xrfg::D3D12OpticalFlowBackend::fidelity_fx,
    UINT length = 256, StereoPattern* image = nullptr, bool vertical = false) {
    const UINT width = vertical ? 128U : length;
    const UINT height = vertical ? length : 128U;
    // Along the motion (u) and across it (v), in image pixels.
    const auto along = [&](UINT x, UINT y) { return static_cast<int>(vertical ? y : x); };
    const auto across = [&](UINT x, UINT y) { return static_cast<int>(vertical ? x : y); };
    constexpr int bar_width = 3, bar_spacing = 28, bar_first = 20, bar_count = 7;
    constexpr int bar_top = 16, bar_bottom = 112;
    constexpr int background_motion = 4, bar_motion = 16;
    const auto bar_at = [&](int x, int y, int shift) {
        if (y < bar_top || y >= bar_bottom) return false;
        const int local = x - shift - bar_first;
        return local >= 0 && local < bar_spacing * bar_count && local % bar_spacing < bar_width;
    };
    const auto make_scene = [&](int background_shift, int bar_shift) {
        StereoPattern pattern;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto& bytes = pattern[eye];
            bytes.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
            for (UINT y = 0; y < height; ++y) {
                for (UINT x = 0; x < width; ++x) {
                    const int bx = along(x, y) - background_shift + 64;
                    const RgbaBytes color = bar_at(along(x, y), across(x, y), bar_shift)
                        ? RgbaBytes{20, 20, 24, 255}
                        : RgbaBytes{static_cast<std::uint8_t>(80 + (bx * 13 + across(x, y) * 5) % 160),
                                    static_cast<std::uint8_t>(90 + ((bx / 3) % 7) * 20 + eye * 9),
                                    static_cast<std::uint8_t>(100 + (bx * 7) % 120), 255};
                    std::copy(color.begin(), color.end(), bytes.begin() +
                        (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel);
                }
            }
        }
        return pattern;
    };
    const StereoPattern previous = make_scene(0, 0);
    const StereoPattern current = make_scene(background_motion, bar_motion);
    auto game_motion = create_and_upload_game_motion_field(
        fixture, width, height, [&](UINT, UINT x, UINT y) {
            const bool bar = bar_at(along(x, y), across(x, y), bar_motion);
            const float motion = static_cast<float>(-(bar ? bar_motion : background_motion));
            return vertical ? std::array<float, 2>{0.0F, motion} : std::array<float, 2>{motion, 0.0F};
        });
    // Inverted depth, as DI2's and MSFS's DLSS: larger is nearer.
    auto game_depth = create_and_upload_game_depth_field(
        fixture, width, height, [&](UINT, UINT x, UINT y) {
            return bar_at(along(x, y), across(x, y), bar_motion) ? 0.5F : 0.01F;
        });
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_source_texture(fixture, width, height), create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_source_texture(fixture, width, height), create_source_texture(fixture, width, height)};
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_source_texture(fixture, width, height)};
    upload_pattern(fixture, sources[0].Get(), previous);
    upload_pattern(fixture, sources[1].Get(), current);
    std::array<ID3D12Resource*, 2> source_pointers{sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{synthetic_destinations[0].Get()};
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(fixture.device(), fixture.queue(),
                source_pointers, D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "bars history initialization failed");
    xrfg::D3D12FrameSynthesizer synthesizer;
    xrfg::D3D12NvidiaOpticalFlowOptions options;
    options.hybrid = true;
    require(operation_succeeded(synthesizer.initialize(fixture.device(), fixture.queue(),
                history, current_pointers, synthetic_pointers, kFormat,
                D3D12_RESOURCE_STATE_RENDER_TARGET, backend, options)),
        "bars synthesizer initialization failed");
    const auto guides = [&](std::uint64_t serial) {
        auto set = std::make_shared<xrfg::DlssMotionVectorSet>();
        set->eye_count = kEyeCount;
        for (UINT eye = 0; eye < kEyeCount; ++eye) {
            auto frame = std::make_shared<xrfg::DlssMotionVectorFrame>();
            frame->stream = 511 + eye;
            frame->epoch = 3;
            frame->serial = serial;
            frame->previous_serial = serial - 1;
            frame->motion_vectors = game_motion;
            frame->producer_queue = fixture.queue();
            frame->output_width = frame->motion_width = width;
            frame->output_height = frame->motion_height = height;
            frame->motion_slice = frame->output_slice = eye;
            frame->resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (with_depth) {
                frame->depth = game_depth;
                frame->depth_width = width;
                frame->depth_height = height;
                frame->depth_inverted = true;
                frame->depth_resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            set->eyes[eye] = frame;
        }
        return set;
    };
    const ReprojectionViews views = make_reprojection_views();
    xrfg::D3D12HistoryCaptureTicket capture_a{}, capture_b{};
    require(operation_succeeded(history->capture(0, &capture_a)) &&
            operation_succeeded(history->commit(capture_a)),
        "bars capture A failed");
    xrfg::D3D12FrameSynthesisTicket prime{}, pair{};
    require(operation_succeeded(synthesizer.submit_prime(capture_a, views, 0, &prime, guides(1))),
        "bars prime failed");
    require_frame_start_gate(synthesizer, "bars frame-start gate");
    require(operation_succeeded(history->capture(1, &capture_b)) &&
            operation_succeeded(history->commit(capture_b)),
        "bars capture B failed");
    require(operation_succeeded(synthesizer.submit_pair(capture_b, views, views, 0, 1, &pair,
                std::nullopt, guides(2))),
        "bars pair failed");
    fixture.execute_and_wait([](ID3D12GraphicsCommandList*) {});
    const StereoPattern actual = readback_pattern(fixture, synthetic_destinations[0].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    double coverage_total = 0;
    for (UINT eye = 0; eye < kEyeCount; ++eye) {
        std::size_t bar_pixels = 0, shown = 0;
        for (int v = bar_top + 4; v < bar_bottom - 4; ++v) {
            for (int u = 0; u < static_cast<int>(length); ++u) {
                if (!bar_at(u, v, bar_motion / 2)) continue;
                const UINT x = static_cast<UINT>(vertical ? v : u);
                const UINT y = static_cast<UINT>(vertical ? u : v);
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel;
                ++bar_pixels;
                // Bar is near black, the background at least 80 a channel.
                const int brightness = (actual[eye][offset] + actual[eye][offset + 1] +
                    actual[eye][offset + 2]) / 3;
                if (brightness < 50) ++shown;
            }
        }
        const double coverage = static_cast<double>(shown) / static_cast<double>(bar_pixels);
        std::cout << "hybrid bars " << (with_depth ? "with" : "without") << " depth eye=" << eye
                  << " length=" << length << (vertical ? " vertical" : "")
                  << " bar_coverage=" << coverage << '\n';
        coverage_total += coverage;
    }
    if (image != nullptr) *image = actual;
    require(operation_succeeded(synthesizer.wait_for_idle()), "bars final drain failed");
    require(operation_succeeded(history->invalidate()), "bars history invalidate failed");
    return coverage_total / kEyeCount;
}

// V509: a flat panel's own generated image. UEVR's HUD is a quad layer with
// a transparent background, and its quest markers are drawn at a new spot
// inside it every frame. The layer synthesizes such a panel as one view over
// the whole image with the same identity camera in both frames, so the
// camera mapping is the identity and the flow alone moves the marker. The
// synthetic has to show one opaque marker halfway - not two half-transparent
// ones at either end - and leave the background transparent.
//
// WARP's optical flow finds no motion (the stereo motion test's synthetic
// equals its blend there too), so the default suite checks only that a
// single-view panel synthesizes and stays transparent; `panel` runs the
// placement check on the GPU, with FidelityFX and NVIDIA.
void test_flat_panel_marker_synthesis(
    D3D12WarpFixture& fixture,
    xrfg::D3D12OpticalFlowBackend backend,
    bool hybrid,
    bool require_motion,
    UINT move = 16,
    // V511: a 24 px quest-marker glyph (a diamond with a dark rim and a clear
    // hole) with the layer's panel flow, FidelityFX at full resolution.
    bool glyph = false) {
    constexpr UINT kPanelWidth = 1024;
    constexpr UINT kPanelHeight = 512;
    const UINT kMarker = glyph ? 24U : 64U;
    const UINT kMarkerY = kPanelHeight / 2 - kMarker / 2;
    constexpr UINT kPreviousX = 256;
    const UINT kCurrentX = kPreviousX + move;
    const auto panel_image = [&](UINT marker_x) {
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(kPanelWidth) * kPanelHeight * kBytesPerPixel, 0);
        for (UINT y = kMarkerY; y < kMarkerY + kMarker; ++y) {
            for (UINT x = marker_x; x < marker_x + kMarker; ++x) {
                RgbaBytes texel = motion_texel((x - marker_x) * 3U, (y - kMarkerY) * 3U, 0);
                texel[3] = 255;
                if (glyph) {
                    const int cx = static_cast<int>(x - marker_x) * 2 - static_cast<int>(kMarker) + 1;
                    const int cy = static_cast<int>(y - kMarkerY) * 2 - static_cast<int>(kMarker) + 1;
                    const int d = (std::abs(cx) + std::abs(cy)) / 2;
                    const int r = static_cast<int>(kMarker) / 2;
                    if (d >= r || d < r / 4) {
                        texel = {0, 0, 0, 0};
                    } else if (d >= r - 2 || d < r / 4 + 2) {
                        texel = {20, 20, 20, 255};
                    } else {
                        texel = {250, 230, 120, 255};
                    }
                }
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kPanelWidth + x) * kBytesPerPixel;
                bytes[offset + 0] = texel[0];
                bytes[offset + 1] = texel[1];
                bytes[offset + 2] = texel[2];
                bytes[offset + 3] = texel[3];
            }
        }
        return bytes;
    };
    std::array<ComPtr<ID3D12Resource>, 2> sources{
        create_single_slice_texture(fixture, kPanelWidth, kPanelHeight),
        create_single_slice_texture(fixture, kPanelWidth, kPanelHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 2> current_destinations{
        create_single_slice_texture(fixture, kPanelWidth, kPanelHeight),
        create_single_slice_texture(fixture, kPanelWidth, kPanelHeight),
    };
    std::array<ComPtr<ID3D12Resource>, 1> synthetic_destinations{
        create_single_slice_texture(fixture, kPanelWidth, kPanelHeight),
    };
    const std::vector<std::uint8_t> previous_image = panel_image(kPreviousX);
    const std::vector<std::uint8_t> current_image = panel_image(kCurrentX);
    const std::vector<std::uint8_t> midpoint_image = panel_image((kPreviousX + kCurrentX) / 2U);
    upload_single_slice(fixture, sources[0].Get(), previous_image);
    upload_single_slice(fixture, sources[1].Get(), current_image);
    std::array<ID3D12Resource*, 2> source_pointers{sources[0].Get(), sources[1].Get()};
    std::array<ID3D12Resource*, 2> current_pointers{
        current_destinations[0].Get(), current_destinations[1].Get()};
    std::array<ID3D12Resource*, 1> synthetic_pointers{synthetic_destinations[0].Get()};
    auto history = std::make_shared<xrfg::D3D12SwapchainHistory>();
    require(operation_succeeded(history->initialize(fixture.device(), fixture.queue(),
                std::span<ID3D12Resource* const>(source_pointers.data(), source_pointers.size()),
                D3D12_RESOURCE_STATE_RENDER_TARGET)),
        "flat panel history initialization failed");
    xrfg::D3D12NvidiaOpticalFlowOptions options{};
    options.hybrid = hybrid;
    if (glyph) {
        options.input_scale = xrfg::D3D12OpticalFlowInputScale::full;
    }
    xrfg::D3D12FrameSynthesizer synthesizer;
    require(operation_succeeded(synthesizer.initialize(fixture.device(), fixture.queue(), history,
                std::span<ID3D12Resource* const>(current_pointers.data(), current_pointers.size()),
                std::span<ID3D12Resource* const>(synthetic_pointers.data(), synthetic_pointers.size()),
                kFormat, D3D12_RESOURCE_STATE_RENDER_TARGET, backend, options)),
        "flat panel synthesizer initialization failed");
    // As the layer marks a panel's synthesizer (V510).
    synthesizer.set_flat_panel(true);
    // The layer's panel view (prepare_panel_generation).
    std::array<xrfg::D3D12ReprojectionView, 1> view{};
    view[0].fov = {-0.785398F, 0.785398F, 0.785398F, -0.785398F};
    view[0].image_rect = {0, 0, kPanelWidth, kPanelHeight};
    view[0].array_slice = 0;
    xrfg::D3D12HistoryCaptureTicket previous{};
    require(operation_succeeded(history->capture(0, &previous)) &&
            operation_succeeded(history->commit(previous)),
        "flat panel previous capture failed");
    xrfg::D3D12FrameSynthesisTicket prime{};
    require(operation_succeeded(synthesizer.submit_prime(previous, view, 0, &prime)),
        "flat panel prime failed");
    xrfg::D3D12HistoryCaptureTicket current{};
    require(operation_succeeded(history->capture(1, &current)) &&
            operation_succeeded(history->commit(current)),
        "flat panel current capture failed");
    xrfg::D3D12FrameSynthesisTicket pair{};
    require(operation_succeeded(synthesizer.submit_pair(current, view, view, 0, 1, &pair)),
        "flat panel pair failed");
    require(operation_succeeded(synthesizer.wait_for_idle()), "flat panel drain failed");
    const std::vector<std::uint8_t> pixels = readback_single_slice(
        fixture, synthetic_destinations[0].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    std::size_t opaque = 0;
    std::size_t stray = 0;
    double alpha_sum = 0.0;
    double alpha_x = 0.0;
    double synthetic_error = 0.0;
    double blend_error = 0.0;
    for (UINT y = 0; y < kPanelHeight; ++y) {
        for (UINT x = 0; x < kPanelWidth; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kPanelWidth + x) * kBytesPerPixel;
            const std::uint8_t alpha = pixels[offset + 3];
            if (alpha > 200) ++opaque;
            alpha_sum += alpha;
            alpha_x += static_cast<double>(alpha) * (x + 0.5);
            // Well away from either marker: the background stays clear.
            const bool distant = y + 8 < kMarkerY || y > kMarkerY + kMarker + 8 ||
                x + 8 < kPreviousX || x > kCurrentX + kMarker + 8;
            if (distant && alpha > 8) ++stray;
            for (UINT channel = 0; channel < kBytesPerPixel; ++channel) {
                synthetic_error += std::abs(static_cast<int>(pixels[offset + channel]) -
                    static_cast<int>(midpoint_image[offset + channel]));
                blend_error += std::abs((static_cast<int>(previous_image[offset + channel]) +
                    static_cast<int>(current_image[offset + channel])) / 2 -
                    static_cast<int>(midpoint_image[offset + channel]));
            }
        }
    }
    const double samples = static_cast<double>(pixels.size());
    synthetic_error /= samples;
    blend_error /= samples;
    const double centroid = alpha_sum > 0.0 ? alpha_x / alpha_sum : 0.0;
    const double expected = (kPreviousX + kCurrentX) * 0.5 + kMarker * 0.5;
    std::size_t opaque_at_midpoint = 0;
    for (std::size_t offset = 3; offset < midpoint_image.size(); offset += kBytesPerPixel) {
        if (midpoint_image[offset] > 200) ++opaque_at_midpoint;
    }
    const double area = static_cast<double>(opaque_at_midpoint);
    std::cout << "flat panel marker backend=" << static_cast<int>(backend)
              << (hybrid ? " hybrid" : "") << " move=" << move << ": opaque=" << opaque << "/" << area
              << " centroid=" << centroid << " expected=" << expected << " stray=" << stray
              << " synthetic_mae=" << synthetic_error << " blend_mae=" << blend_error << '\n';
    require(stray == 0, "flat panel background is no longer transparent");
    require(std::abs(centroid - expected) <= 3.0,
        "flat panel marker is not between the frames");
    if (require_motion) {
        // V510: the icon's edge strips follow the icon too.
        require(static_cast<double>(opaque) >= (glyph ? 0.9 : 0.97) * area,
            "flat panel marker is doubled or faded, not moved");
        require(synthetic_error < blend_error * 0.2,
            "flat panel synthetic is no closer to the midpoint than a blend");
    }
    require(operation_succeeded(history->invalidate()), "flat panel history invalidate failed");
}

int main(int argc, char** argv) {
    // V532: off by default in the product; the harness checks it on.
    xrfg::set_hybrid_depth_search(true);
    const bool trace_test = std::getenv("XRFG_TEST_GPU_TRACE") != nullptr;
    const auto trace_dir = std::filesystem::temp_directory_path() /
        ("xrfg-v073-gpu-" + std::to_string(GetCurrentProcessId()));
    if (trace_test) {
        std::filesystem::create_directories(trace_dir);
        std::ofstream(trace_dir / "ofxr_bridge.ini") <<
            "[diagnostics]\nlogging_enabled=1\nmax_file_mb=32\nflush_each_event=0\n";
        xrfg::bridge_flight_logger().initialize(trace_dir);
    }
    try {
        if (GetEnvironmentVariableA("XRFG_TEST_NVIDIA_QUALITY", nullptr, 0)) {
            run_nvidia_fast_controls();
            return 0;
        }
        if (argc > 2 && std::string_view(argv[1]) == "hybrid" &&
            std::string_view(argv[2]) == "nvidia") {
            SetEnvironmentVariableW(L"XRFG_IGNORE_LIVE_TUNING", L"1");
            D3D12WarpFixture nvidia_fixture(true);
            for (const auto scene : {HybridScene::occlusion, HybridScene::overlay, HybridScene::fade,
                                     HybridScene::occlusion_low_res, HybridScene::occlusion_periphery,
                                     HybridScene::occlusion_foveated,
                                     HybridScene::occlusion_foveated_moved,
                                     HybridScene::occlusion_foveated_moved_current,
                                     HybridScene::occlusion_fast,
                                     HybridScene::occlusion_fast_dark}) {
                test_hybrid_composition(nvidia_fixture, scene, xrfg::D3D12OpticalFlowBackend::nvidia);
            }
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "panel") {
            // V509 on the GPU: a flat panel's marker moved by the flow.
            SetEnvironmentVariableW(L"XRFG_IGNORE_LIVE_TUNING", L"1");
            D3D12WarpFixture gpu_fixture(true);
            for (const auto backend : {xrfg::D3D12OpticalFlowBackend::fidelity_fx,
                                       xrfg::D3D12OpticalFlowBackend::nvidia}) {
                test_flat_panel_marker_synthesis(gpu_fixture, backend, false, true);
                test_flat_panel_marker_synthesis(gpu_fixture, backend, true, true);
                test_flat_panel_marker_synthesis(gpu_fixture, backend, false, true, 32);
            }
            // The layer's panel flow (V511) on a marker-sized icon.
            for (const UINT move : {6U, 10U}) {
                test_flat_panel_marker_synthesis(gpu_fixture,
                    xrfg::D3D12OpticalFlowBackend::fidelity_fx, false, true, move, true);
            }
            gpu_fixture.require_no_debug_errors();
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "hybrid") {
            // The built-in values, not a tray window's (V507).
            SetEnvironmentVariableW(L"XRFG_IGNORE_LIVE_TUNING", L"1");
            D3D12WarpFixture hybrid_fixture;
            for (const auto scene : {HybridScene::occlusion, HybridScene::overlay, HybridScene::fade,
                                     HybridScene::occlusion_low_res, HybridScene::occlusion_periphery,
                                     HybridScene::occlusion_foveated,
                                     HybridScene::occlusion_foveated_moved,
                                     HybridScene::occlusion_foveated_moved_current,
                                     HybridScene::occlusion_fast,
                                     HybridScene::occlusion_fast_dark}) {
                test_hybrid_composition(hybrid_fixture, scene);
            }
            test_hybrid_side_by_side(hybrid_fixture);
            test_hybrid_side_by_side(hybrid_fixture, true);
            // Without depth the bars are missing (V543: with the mask's band
            // on, a third of them is found - the band marks the background
            // they sweep as revealed, and the second-surface search finds
            // them).
            const double bars_without_depth = test_hybrid_bars(hybrid_fixture, false);
            require(bars_without_depth < 0.95, "hybrid bars without depth are whole: depth is not needed");
            // V505: with the game's depth the bars are whole in the
            // generated frame.
            require(test_hybrid_bars(hybrid_fixture, true) >= 0.95,
                "hybrid bars with depth are not whole in the generated frame");
            // V530: the depth tiles skip the search only where it would
            // have found nothing, so the frame is the same without them;
            // V531 reads them by row or by column, so both orientations.
            for (const bool vertical : {false, true}) {
                StereoPattern with_tiles, without_tiles;
                require(test_hybrid_bars(hybrid_fixture, true,
                            xrfg::D3D12OpticalFlowBackend::fidelity_fx, 512, &with_tiles,
                            vertical) >= 0.95,
                    "hybrid bars with depth tiles are not whole in the generated frame");
                SetEnvironmentVariableW(L"XRFG_DISABLE_DEPTH_TILES", L"1");
                test_hybrid_bars(hybrid_fixture, true,
                    xrfg::D3D12OpticalFlowBackend::fidelity_fx, 512, &without_tiles, vertical);
                SetEnvironmentVariableW(L"XRFG_DISABLE_DEPTH_TILES", nullptr);
                std::size_t differing = 0;
                for (UINT eye = 0; eye < kEyeCount; ++eye) {
                    for (std::size_t index = 0; index < with_tiles[eye].size(); ++index) {
                        if (with_tiles[eye][index] != without_tiles[eye][index]) ++differing;
                    }
                }
                std::cout << "hybrid bars depth tiles" << (vertical ? " vertical" : "") << ": "
                          << differing << " bytes differ\n";
                require(differing == 0, "the depth tiles changed the generated frame");
            }
            // V532: the menu's "Hybrid: depth search" off, the bars are as
            // without depth again.
            xrfg::set_hybrid_depth_search(false);
            require(test_hybrid_bars(hybrid_fixture, true) <= bars_without_depth + 0.05,
                "Hybrid: depth search off did not reach the shader");
            xrfg::set_hybrid_depth_search(true);
            // V507: the tray's live tuning block reaches the shader. With a
            // block whose depth reach is nothing and (V543) no mask band, the
            // bars go again, as before V543.
            // Skipped where a tray already owns the block, so a live window
            // is not written over.
            {
                const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                    PAGE_READWRITE, 0, sizeof(xrfg::live_tuning::Block),
                    xrfg::live_tuning::kMappingName);
                const bool owned = mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
                if (owned) {
                    auto* block = static_cast<xrfg::live_tuning::Block*>(MapViewOfFile(
                        mapping, FILE_MAP_WRITE, 0, 0, sizeof(xrfg::live_tuning::Block)));
                    require(block != nullptr, "mapping the live tuning block");
                    xrfg::live_tuning::Block values = xrfg::live_tuning::kDefaults;
                    values.sequence = 1;
                    values.depth_near_reach_px = 0;
                    values.depth_far_reach_px = 0;
                    values.mask_band_percent = 0;
                    *block = values;
                    // The synthesizer retries a failed open once a second.
                    SetEnvironmentVariableW(L"XRFG_IGNORE_LIVE_TUNING", nullptr);
                    Sleep(1100);
                    const double with_block = test_hybrid_bars(hybrid_fixture, true);
                    SetEnvironmentVariableW(L"XRFG_IGNORE_LIVE_TUNING", L"1");
                    require(with_block <= 0.05,
                        "the live tuning block's depth reach and mask band did not reach the shader");
                    values.depth_far_reach_px = xrfg::live_tuning::kDefaults.depth_far_reach_px;
                    values.sequence = 2;
                    *block = values;
                    UnmapViewOfFile(block);
                    CloseHandle(mapping);
                } else if (mapping != nullptr) {
                    CloseHandle(mapping);
                    std::cout << "live tuning block owned by a tray; switch check skipped\n";
                }
            }
            hybrid_fixture.require_no_debug_errors();
            return 0;
        }
        D3D12WarpFixture fixture;
        test_stereo_capture_ring(fixture);
        test_capture_is_async_and_consumer_fence_blocks_reuse(fixture);
        test_capture_waits_for_a_busy_slot_within_its_limit(fixture);
        test_depth_capture_path(fixture);
        test_depth_private_history_allows_shader_resource_views(fixture);
        test_rolling_frame_synthesizer(fixture);
        test_synthesis_on_a_dedicated_queue(fixture);
        test_reconfigure_keeps_fence_values_in_flight(fixture);
        test_double_wide_single_slice_views(fixture);
        test_flat_panel_marker_synthesis(
            fixture, xrfg::D3D12OpticalFlowBackend::fidelity_fx, false, false);
        test_flat_panel_marker_synthesis(
            fixture, xrfg::D3D12OpticalFlowBackend::fidelity_fx, true, false);
        test_stereo_motion_synthesis_beats_same_pixel_blend(fixture);
        test_dlss_motion_vector_stereo_stream_pairing(fixture);
        test_dlss_motion_vector_per_eye_swapchains(fixture);
        test_release_bound_dlss_guides(fixture);
        test_dlss_motion_vector_gpu_ingress(fixture);
        test_dlss_motion_vector_strafe_rejects_double_edges(fixture);
        test_rotation_aware_synthesis_beats_uncompensated_flow(fixture);
        test_submission_backpressure_and_recovery(
            fixture,
            xrfg::D3D12OpticalFlowBackend::fidelity_fx);
        fixture.require_no_debug_errors();
        std::array<char, 8> nvidia_test{};
        const DWORD nvidia_test_length = GetEnvironmentVariableA(
            "XRFG_TEST_NVIDIA",
            nvidia_test.data(),
            static_cast<DWORD>(nvidia_test.size()));
        if (nvidia_test_length == 1 && nvidia_test[0] == '1') {
            xrfg::D3D12NvidiaOpticalFlowOptions nvidia_options;
            std::array<char, 16> preset{};
            const DWORD preset_length = GetEnvironmentVariableA(
                "XRFG_TEST_NVIDIA_PRESET",
                preset.data(),
                static_cast<DWORD>(preset.size()));
            if (preset_length == 4 && _stricmp(preset.data(), "slow") == 0) {
                nvidia_options.preset =
                    xrfg::D3D12NvidiaPerformancePreset::slow;
            } else if (preset_length == 4 && _stricmp(preset.data(), "fast") == 0) {
                nvidia_options.preset =
                    xrfg::D3D12NvidiaPerformancePreset::fast;
            }
            std::array<char, 8> input_scale{};
            const DWORD input_scale_length = GetEnvironmentVariableA(
                "XRFG_TEST_NVIDIA_INPUT_SCALE",
                input_scale.data(),
                static_cast<DWORD>(input_scale.size()));
            if (input_scale_length == 3 &&
                std::string_view(input_scale.data(), input_scale_length) == "100") {
                nvidia_options.input_scale = xrfg::D3D12NvidiaInputScale::full;
            } else if (input_scale_length == 2 &&
                std::string_view(input_scale.data(), input_scale_length) ==
                    "75") {
                nvidia_options.input_scale =
                    xrfg::D3D12NvidiaInputScale::three_quarter;
            } else if (input_scale_length == 2 &&
                       std::string_view(
                           input_scale.data(), input_scale_length) == "50") {
                nvidia_options.input_scale =
                    xrfg::D3D12NvidiaInputScale::half;
            } else if (input_scale_length == 2 &&
                       std::string_view(
                           input_scale.data(), input_scale_length) == "25") {
                nvidia_options.input_scale =
                    xrfg::D3D12NvidiaInputScale::quarter;
            }
            std::array<char, 8> bidirectional{};
            const DWORD bidirectional_length = GetEnvironmentVariableA(
                "XRFG_TEST_NVIDIA_BIDIRECTIONAL",
                bidirectional.data(),
                static_cast<DWORD>(bidirectional.size()));
            nvidia_options.bidirectional = bidirectional_length == 1 &&
                bidirectional[0] == '1';
            D3D12WarpFixture nvidia_fixture(true);
            test_stereo_motion_synthesis_beats_same_pixel_blend(
                nvidia_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia,
                true,
                nvidia_options);
            test_double_wide_single_slice_views(
                nvidia_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia,
                nvidia_options);
            test_rotation_aware_synthesis_beats_uncompensated_flow(
                nvidia_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia,
                nvidia_options);
            test_dlss_motion_vector_gpu_ingress(
                nvidia_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia);
            test_submission_backpressure_and_recovery(
                nvidia_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia,
                nvidia_options);
            if (GetEnvironmentVariableA(
                    "XRFG_TEST_NVIDIA_CONTEXT_STRESS", nullptr, 0)) {
                test_nvidia_serialized_eye_context_stress(
                    nvidia_fixture,
                    nvidia_options);
            }
            nvidia_fixture.require_no_debug_errors();
            std::cout << "D3D12 NVIDIA OFA synthesis test passed\n";
        }
        std::array<char, 8> nvidia_cropped_test{};
        const DWORD nvidia_cropped_test_length = GetEnvironmentVariableA(
            "XRFG_TEST_NVIDIA_CROPPED",
            nvidia_cropped_test.data(),
            static_cast<DWORD>(nvidia_cropped_test.size()));
        if (nvidia_cropped_test_length == 1 &&
            nvidia_cropped_test[0] == '1') {
            D3D12WarpFixture nvidia_cropped_fixture(true);
            test_rotation_aware_synthesis_beats_uncompensated_flow(
                nvidia_cropped_fixture,
                xrfg::D3D12OpticalFlowBackend::nvidia);
            nvidia_cropped_fixture.require_no_debug_errors();
            std::cout << "D3D12 NVIDIA OFA cropped synthesis test passed\n";
        }
        std::array<char, 8> hardware_repeat_test{};
        const DWORD hardware_repeat_test_length = GetEnvironmentVariableA(
            "XRFG_TEST_HARDWARE_REPEAT",
            hardware_repeat_test.data(),
            static_cast<DWORD>(hardware_repeat_test.size()));
        if (hardware_repeat_test_length == 1 &&
            hardware_repeat_test[0] == '1') {
            D3D12WarpFixture hardware_repeat_fixture(true);
            test_stereo_motion_synthesis_beats_same_pixel_blend(
                hardware_repeat_fixture,
                xrfg::D3D12OpticalFlowBackend::fidelity_fx,
                true);
            hardware_repeat_fixture.require_no_debug_errors();
            std::cout << "D3D12 hardware repeated-capture test passed\n";
        }
        std::cout << "D3D12 WARP asynchronous history/synthesis tests passed\n";
        if (trace_test) {
            const auto path = xrfg::bridge_flight_logger().log_path();
            std::ifstream input(path);
            const std::string contents{std::istreambuf_iterator<char>(input), {}};
            require(
                contents.find(
                    "op=synthesis_frame_start_wait result=-2147024726") !=
                    std::string::npos,
                "transient frame-start synthesis wait breadcrumb missing");
            xrfg::bridge_flight_logger().shutdown();
            input.close();
            std::filesystem::remove_all(trace_dir);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "D3D12 WARP asynchronous history/synthesis test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
