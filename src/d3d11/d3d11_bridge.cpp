#include "xrfg/d3d11_bridge.hpp"

#include <array>
#include <limits>
#include <mutex>
#include <vector>

#include <wrl/client.h>

namespace xrfg {

using Microsoft::WRL::ComPtr;

namespace {

struct UniqueHandle {
    HANDLE value{};
    ~UniqueHandle() {
        if (value != nullptr) {
            CloseHandle(value);
        }
    }
};

[[nodiscard]] bool is_depth_format(DXGI_FORMAT format) noexcept {
    switch (format) {
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return true;
        default:
            return false;
    }
}

constexpr std::size_t kCopyListCount = 4;

} // namespace

struct D3D11BridgeSwapchain::Impl {
    struct CopyList {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::uint64_t fence_value{};
    };

    std::mutex mutex;
    ComPtr<ID3D11Device> d3d11_device;
    ComPtr<ID3D11DeviceContext4> d3d11_context4;
    ComPtr<ID3D12Device> d3d12_device;
    ComPtr<ID3D12CommandQueue> d3d12_queue;
    ComPtr<ID3D12Fence> d3d12_fence;
    ComPtr<ID3D11Fence> d3d11_fence;
    HANDLE fence_event{};
    std::uint64_t next_fence_value{1};
    std::uint64_t last_signalled{};
    std::vector<ComPtr<ID3D12Resource>> runtime_images;
    std::vector<ComPtr<ID3D12Resource>> shared_images;
    std::vector<ComPtr<ID3D11Texture2D>> d3d11_images;
    std::vector<ID3D12Resource*> shared_views;
    std::vector<ID3D11Texture2D*> d3d11_views;
    // The fence value the layer's queue signalled after its last read of
    // each shared texture; the application's context waits on it before
    // rendering into that texture again.
    std::vector<std::uint64_t> last_read;
    std::array<CopyList, kCopyListCount> copy_lists{};
    std::size_t next_copy_list{};
    bool enabled{};

    ~Impl() {
        if (fence_event != nullptr) {
            CloseHandle(fence_event);
        }
    }

    [[nodiscard]] std::uint64_t allocate() noexcept {
        if (next_fence_value == std::numeric_limits<std::uint64_t>::max()) {
            enabled = false;
        }
        return next_fence_value++;
    }

    [[nodiscard]] HRESULT create_shared_texture(
        ID3D12Resource* runtime_image,
        D3D12_RESOURCE_FLAGS extra_flags,
        ComPtr<ID3D12Resource>* shared,
        ComPtr<ID3D11Texture2D>* opened,
        std::uint32_t* failure_stage) noexcept {
        const D3D12_RESOURCE_DESC runtime_description =
            runtime_image->GetDesc();
        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;
        // The runtime image's own format, exactly: the copy into it is then
        // between identical formats on every runtime, and the synthesizer
        // sees the same format on the application's images as on the
        // private swapchains the runtime creates for the layer. Runtimes
        // hand a D3D11 session TYPELESS images for a typed request, and are
        // expected to do the same here; a runtime that hands out the typed
        // format gives the application a typed D3D11 texture, which a game
        // creating views of another type in the family would refuse.
        D3D12_RESOURCE_DESC description = runtime_description;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = extra_flags;
        if ((extra_flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0) {
            description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        // Both devices write and read the texture with no state
        // transitions between them; simultaneous access is what makes a
        // D3D12 resource legal to use that way.
        description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        HRESULT result = d3d12_device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_SHARED,
            &description,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(shared->GetAddressOf()));
        if (FAILED(result)) {
            *failure_stage = 10;
            return result;
        }
        UniqueHandle handle;
        result = d3d12_device->CreateSharedHandle(
            shared->Get(), nullptr, GENERIC_ALL, nullptr, &handle.value);
        if (FAILED(result)) {
            *failure_stage = 11;
            return result;
        }
        ComPtr<ID3D11Device1> device1;
        result = d3d11_device.As(&device1);
        if (FAILED(result)) {
            *failure_stage = 12;
            return result;
        }
        result = device1->OpenSharedResource1(
            handle.value, IID_PPV_ARGS(opened->GetAddressOf()));
        if (FAILED(result)) {
            *failure_stage = 13;
        }
        return result;
    }

    [[nodiscard]] HRESULT initialize(
        ID3D11Device* input_d3d11_device,
        ID3D11DeviceContext* input_d3d11_context,
        ID3D12Device* input_d3d12_device,
        ID3D12CommandQueue* input_d3d12_queue,
        std::span<ID3D12Resource* const> input_runtime_images,
        D3D12_RESOURCE_FLAGS extra_flags,
        std::uint32_t* failure_stage) noexcept {
        *failure_stage = 0;
        if (input_d3d11_device == nullptr || input_d3d11_context == nullptr ||
            input_d3d12_device == nullptr || input_d3d12_queue == nullptr ||
            input_runtime_images.empty()) {
            *failure_stage = 1;
            return E_INVALIDARG;
        }
        d3d11_device = input_d3d11_device;
        d3d12_device = input_d3d12_device;
        d3d12_queue = input_d3d12_queue;
        HRESULT result = input_d3d11_context->QueryInterface(
            IID_PPV_ARGS(d3d11_context4.GetAddressOf()));
        if (FAILED(result)) {
            *failure_stage = 2;
            return result;
        }
        ComPtr<ID3D11Device5> device5;
        result = d3d11_device.As(&device5);
        if (FAILED(result)) {
            *failure_stage = 3;
            return result;
        }
        for (ID3D12Resource* image : input_runtime_images) {
            if (image == nullptr) {
                *failure_stage = 4;
                return E_INVALIDARG;
            }
            ComPtr<ID3D12Resource> shared;
            ComPtr<ID3D11Texture2D> opened;
            result = create_shared_texture(
                image, extra_flags, &shared, &opened, failure_stage);
            if (FAILED(result)) {
                return result;
            }
            runtime_images.emplace_back(image);
            shared_images.push_back(std::move(shared));
            d3d11_images.push_back(std::move(opened));
        }
        for (const auto& image : shared_images) shared_views.push_back(image.Get());
        for (const auto& image : d3d11_images) d3d11_views.push_back(image.Get());
        last_read.assign(runtime_images.size(), 0);

        result = d3d12_device->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(d3d12_fence.GetAddressOf()));
        if (FAILED(result)) {
            *failure_stage = 20;
            return result;
        }
        UniqueHandle fence_handle;
        result = d3d12_device->CreateSharedHandle(
            d3d12_fence.Get(), nullptr, GENERIC_ALL, nullptr,
            &fence_handle.value);
        if (FAILED(result)) {
            *failure_stage = 21;
            return result;
        }
        result = device5->OpenSharedFence(
            fence_handle.value, IID_PPV_ARGS(d3d11_fence.GetAddressOf()));
        if (FAILED(result)) {
            *failure_stage = 22;
            return result;
        }
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event == nullptr) {
            *failure_stage = 23;
            return HRESULT_FROM_WIN32(GetLastError());
        }
        for (CopyList& copy : copy_lists) {
            result = d3d12_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(copy.allocator.GetAddressOf()));
            if (FAILED(result)) {
                *failure_stage = 30;
                return result;
            }
            result = d3d12_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, copy.allocator.Get(),
                nullptr, IID_PPV_ARGS(copy.list.GetAddressOf()));
            if (FAILED(result)) {
                *failure_stage = 31;
                return result;
            }
            result = copy.list->Close();
            if (FAILED(result)) {
                *failure_stage = 32;
                return result;
            }
        }
        enabled = true;
        return S_OK;
    }

    // A copy list whose previous use the queue has finished with. Bounded
    // by the ring: if every list is still in flight this waits on the
    // oldest, which cannot happen while the application's own frame loop
    // paces the releases four per swapchain apart.
    [[nodiscard]] HRESULT take_copy_list(CopyList** output) noexcept {
        CopyList& copy = copy_lists[next_copy_list];
        next_copy_list = (next_copy_list + 1) % kCopyListCount;
        if (copy.fence_value != 0 &&
            d3d12_fence->GetCompletedValue() < copy.fence_value) {
            const HRESULT result = d3d12_fence->SetEventOnCompletion(
                copy.fence_value, fence_event);
            if (FAILED(result)) {
                return result;
            }
            WaitForSingleObject(fence_event, 2000);
        }
        HRESULT result = copy.allocator->Reset();
        if (FAILED(result)) {
            return result;
        }
        result = copy.list->Reset(copy.allocator.Get(), nullptr);
        if (FAILED(result)) {
            return result;
        }
        *output = &copy;
        return S_OK;
    }

    [[nodiscard]] HRESULT release(std::uint32_t index) noexcept {
        if (!enabled || index >= runtime_images.size()) {
            return E_INVALIDARG;
        }
        // The application's rendering into the shared texture, complete on
        // its context, before the layer's queue reads it.
        const std::uint64_t rendered = allocate();
        HRESULT result = d3d11_context4->Signal(d3d11_fence.Get(), rendered);
        if (FAILED(result)) {
            enabled = false;
            return result;
        }
        d3d11_context4->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
        result = d3d12_queue->Wait(d3d12_fence.Get(), rendered);
        if (FAILED(result)) {
            enabled = false;
            return result;
        }
        CopyList* copy = nullptr;
        result = take_copy_list(&copy);
        if (FAILED(result)) {
            return result;
        }
        ID3D12Resource* const runtime_image = runtime_images[index].Get();
        // XR_KHR_D3D12_enable: the runtime hands images out in and expects
        // them back in RENDER_TARGET (DEPTH_WRITE for depth). The shared
        // texture is a simultaneous-access resource and needs no barrier.
        const bool depth = is_depth_format(runtime_image->GetDesc().Format);
        const D3D12_RESOURCE_STATES resting = depth
            ? D3D12_RESOURCE_STATE_DEPTH_WRITE
            : D3D12_RESOURCE_STATE_RENDER_TARGET;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = runtime_image;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = resting;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        copy->list->ResourceBarrier(1, &barrier);
        copy->list->CopyResource(runtime_image, shared_images[index].Get());
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = resting;
        copy->list->ResourceBarrier(1, &barrier);
        result = copy->list->Close();
        if (FAILED(result)) {
            return result;
        }
        ID3D12CommandList* const lists[] = {copy->list.Get()};
        d3d12_queue->ExecuteCommandLists(1, lists);
        const std::uint64_t copied = allocate();
        result = d3d12_queue->Signal(d3d12_fence.Get(), copied);
        if (FAILED(result)) {
            enabled = false;
            return result;
        }
        copy->fence_value = copied;
        last_signalled = copied;
        last_read[index] = copied;
        return S_OK;
    }

    [[nodiscard]] HRESULT mark_read(std::uint32_t index) noexcept {
        if (!enabled || index >= runtime_images.size()) {
            return E_INVALIDARG;
        }
        const std::uint64_t value = allocate();
        const HRESULT result = d3d12_queue->Signal(d3d12_fence.Get(), value);
        if (FAILED(result)) {
            enabled = false;
            return result;
        }
        last_signalled = value;
        last_read[index] = value;
        return S_OK;
    }

    [[nodiscard]] HRESULT before_write(std::uint32_t index) noexcept {
        if (!enabled || index >= runtime_images.size()) {
            return E_INVALIDARG;
        }
        if (last_read[index] == 0) {
            return S_OK;
        }
        const HRESULT result =
            d3d11_context4->Wait(d3d11_fence.Get(), last_read[index]);
        if (FAILED(result)) {
            enabled = false;
        }
        return result;
    }

    [[nodiscard]] HRESULT wait_for_idle() noexcept {
        if (!d3d12_fence || last_signalled == 0 ||
            d3d12_fence->GetCompletedValue() >= last_signalled) {
            return S_OK;
        }
        const HRESULT result =
            d3d12_fence->SetEventOnCompletion(last_signalled, fence_event);
        if (FAILED(result)) {
            return result;
        }
        return WaitForSingleObject(fence_event, 2000) == WAIT_OBJECT_0
            ? S_OK
            : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
};

D3D11BridgeSwapchain::D3D11BridgeSwapchain() : impl_(std::make_unique<Impl>()) {}
D3D11BridgeSwapchain::~D3D11BridgeSwapchain() = default;

HRESULT D3D11BridgeSwapchain::initialize(
    ID3D11Device* d3d11_device,
    ID3D11DeviceContext* d3d11_context,
    ID3D12Device* d3d12_device,
    ID3D12CommandQueue* d3d12_queue,
    std::span<ID3D12Resource* const> runtime_images,
    D3D12_RESOURCE_FLAGS extra_flags,
    std::uint32_t* failure_stage) noexcept {
    std::uint32_t stage = 0;
    HRESULT result = E_FAIL;
    try {
        std::scoped_lock lock(impl_->mutex);
        result = impl_->initialize(
            d3d11_device, d3d11_context, d3d12_device, d3d12_queue,
            runtime_images, extra_flags, &stage);
    } catch (...) {
        stage = 99;
        result = E_FAIL;
    }
    if (failure_stage != nullptr) {
        *failure_stage = stage;
    }
    return result;
}

std::span<ID3D11Texture2D* const> D3D11BridgeSwapchain::d3d11_images() const noexcept {
    return impl_->d3d11_views;
}

std::span<ID3D12Resource* const> D3D11BridgeSwapchain::shared_images() const noexcept {
    return impl_->shared_views;
}

HRESULT D3D11BridgeSwapchain::before_write(std::uint32_t index) noexcept {
    try {
        std::scoped_lock lock(impl_->mutex);
        return impl_->before_write(index);
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT D3D11BridgeSwapchain::release(std::uint32_t index) noexcept {
    try {
        std::scoped_lock lock(impl_->mutex);
        return impl_->release(index);
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT D3D11BridgeSwapchain::mark_read(std::uint32_t index) noexcept {
    try {
        std::scoped_lock lock(impl_->mutex);
        return impl_->mark_read(index);
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT D3D11BridgeSwapchain::wait_for_idle() noexcept {
    try {
        std::scoped_lock lock(impl_->mutex);
        return impl_->wait_for_idle();
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace xrfg
