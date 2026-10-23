#include "xrfg/d3d12_history.hpp"
#include "xrfg/bridge_flight_logger.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace xrfg {
namespace {

using Microsoft::WRL::ComPtr;

void dred_marker(ID3D12GraphicsCommandList* command_list, const char* label) noexcept {
    if (command_list != nullptr && label != nullptr &&
        bridge_flight_logger().enabled()) {
        command_list->SetMarker(1U, label, static_cast<UINT>(std::strlen(label)));
    }
}

[[nodiscard]] bool matching_description(
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

[[nodiscard]] bool same_device(
    ID3D12Device* expected,
    ID3D12DeviceChild* object) noexcept {
    if (expected == nullptr || object == nullptr) {
        return false;
    }

    ComPtr<ID3D12Device> actual;
    if (FAILED(object->GetDevice(IID_PPV_ARGS(actual.GetAddressOf())))) {
        return false;
    }

    ComPtr<IUnknown> expected_identity;
    ComPtr<IUnknown> actual_identity;
    return SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(expected_identity.GetAddressOf()))) &&
           SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(actual_identity.GetAddressOf()))) &&
           expected_identity.Get() == actual_identity.Get();
}

[[nodiscard]] bool same_adapter(
    ID3D12Device* expected,
    ID3D12DeviceChild* object) noexcept {
    if (expected == nullptr || object == nullptr) {
        return false;
    }
    ComPtr<ID3D12Device> actual;
    if (FAILED(object->GetDevice(IID_PPV_ARGS(actual.GetAddressOf())))) {
        return false;
    }
    const LUID expected_luid = expected->GetAdapterLuid();
    const LUID actual_luid = actual->GetAdapterLuid();
    return expected_luid.HighPart == actual_luid.HighPart &&
           expected_luid.LowPart == actual_luid.LowPart;
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

// Bytes per texel of the colour formats a projection swapchain uses, 0 for
// any other: the probe only reads formats it can size.
[[nodiscard]] std::uint32_t probe_texel_bytes(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}

}  // namespace

struct D3D12SwapchainHistory::Impl {
    struct HistorySlot {
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12CommandAllocator> capture_allocator;
        ComPtr<ID3D12GraphicsCommandList> capture_list;
        D3D12HistoryCaptureTicket ticket{};
        std::uint64_t capture_fence_value{};
        std::uint64_t active_lease_serial{};
        ComPtr<ID3D12Fence> consumer_fence;
        std::uint64_t consumer_fence_value{};
        bool valid{};
        // The stale-capture probe (V557fix2): a patch of this slot's last
        // capture, read back once that capture has finished.
        ComPtr<ID3D12Resource> probe_buffer;
        std::uint64_t probe_serial{};
        std::uint32_t probe_source_index{};
        bool probe_pending{};
    };

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    std::vector<ComPtr<ID3D12Resource>> source_images;
    ComPtr<ID3D12Fence> producer_fence;
    HANDLE fence_event{};
    // A busy slot's bounded wait has its own event: a wait that times out
    // leaves its completion registered, and the stale signal must not wake
    // the unbounded waits on fence_event early. The timer bounds it at the
    // microsecond; a plain timeout would round up to the 15.6 ms tick.
    HANDLE busy_event{};
    HANDLE busy_timer{};
    std::array<HistorySlot, D3D12SwapchainHistory::kSlotCount> slots;
    D3D12_RESOURCE_STATES release_state{};
    std::uint64_t next_serial{1};
    std::uint64_t next_fence_value{1};
    std::uint64_t next_lease_serial{1};
    std::uint64_t last_submitted_fence_value{};
    std::uint32_t next_slot{};
    D3D12HistoryCaptureTicket pending_ticket{};
    bool has_pending_capture{};
    bool capture_enabled{};
    bool completion_unknown{};
    // Where the probe reads, and how its copy lands in the readback buffer.
    // Off for a format it cannot size or a multisampled image.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT probe_footprint{};
    D3D12_BOX probe_box{};
    std::uint32_t probe_row_bytes{};
    bool probe_supported{};

    ~Impl() {
        if (fence_event != nullptr) {
            CloseHandle(fence_event);
        }
        if (busy_event != nullptr) {
            CloseHandle(busy_event);
        }
        if (busy_timer != nullptr) {
            CloseHandle(busy_timer);
        }
    }

    [[nodiscard]] static bool matching_ticket(
        const D3D12HistoryCaptureTicket& left,
        const D3D12HistoryCaptureTicket& right) noexcept {
        return left.serial == right.serial &&
               left.fence_value == right.fence_value &&
               left.slot == right.slot &&
               left.source_index == right.source_index;
    }

    [[nodiscard]] static bool matching_lease(
        const D3D12HistoryConsumerLease& lease,
        const HistorySlot& slot,
        std::uint32_t slot_index) noexcept {
        return lease.capture_serial != 0 &&
               lease.lease_serial != 0 &&
               lease.slot == slot_index &&
               slot.valid &&
               slot.ticket.serial == lease.capture_serial &&
               slot.active_lease_serial == lease.lease_serial;
    }

    [[nodiscard]] HRESULT create_closed_command_list(
        UINT node_mask,
        HistorySlot& slot,
        D3D12HistoryInitializationStage* failure_stage) noexcept {
        HRESULT result = device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(slot.capture_allocator.GetAddressOf()));
        if (FAILED(result)) {
            if (failure_stage != nullptr) {
                *failure_stage =
                    D3D12HistoryInitializationStage::create_slot_allocator;
            }
            return result;
        }
        result = device->CreateCommandList(
            node_mask,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            slot.capture_allocator.Get(),
            nullptr,
            IID_PPV_ARGS(slot.capture_list.GetAddressOf()));
        if (FAILED(result)) {
            if (failure_stage != nullptr) {
                *failure_stage =
                    D3D12HistoryInitializationStage::create_slot_command_list;
            }
            return result;
        }
        // DRED reports this stable name if a device removal stops inside the bridge's
        // history copy, distinguishing it from game, NGX and synthesis work.
        slot.capture_list->SetName(L"OFXR History Capture");
        result = slot.capture_list->Close();
        if (FAILED(result) && failure_stage != nullptr) {
            *failure_stage =
                D3D12HistoryInitializationStage::close_slot_command_list;
        }
        return result;
    }

    [[nodiscard]] HRESULT initialize(
        ID3D12Device* input_device,
        ID3D12CommandQueue* input_queue,
        std::span<ID3D12Resource* const> inputs,
        D3D12_RESOURCE_STATES input_release_state,
        D3D12HistoryInitializationStage* failure_stage) {
        if (failure_stage != nullptr) {
            *failure_stage = D3D12HistoryInitializationStage::complete;
        }
        if (input_device == nullptr || input_queue == nullptr || inputs.empty() ||
            (input_release_state != D3D12_RESOURCE_STATE_RENDER_TARGET &&
             input_release_state != D3D12_RESOURCE_STATE_DEPTH_WRITE &&
             input_release_state != D3D12_RESOURCE_STATE_COMMON) ||
            input_queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            !same_device(input_device, input_queue)) {
            if (failure_stage != nullptr) {
                *failure_stage = D3D12HistoryInitializationStage::arguments;
            }
            return E_INVALIDARG;
        }

        ID3D12Resource* const first_source = inputs.front();
        if (first_source == nullptr || !same_adapter(input_device, first_source)) {
            if (failure_stage != nullptr) {
                *failure_stage = D3D12HistoryInitializationStage::first_source;
            }
            return E_INVALIDARG;
        }

        const D3D12_RESOURCE_DESC source_description = first_source->GetDesc();
        if (source_description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            source_description.Width == 0 || source_description.Height == 0 ||
            source_description.DepthOrArraySize == 0 || source_description.MipLevels == 0 ||
            source_description.SampleDesc.Count == 0) {
            if (failure_stage != nullptr) {
                *failure_stage =
                    D3D12HistoryInitializationStage::source_description;
            }
            return E_INVALIDARG;
        }

        source_images.reserve(inputs.size());
        for (ID3D12Resource* source : inputs) {
            if (source == nullptr || !same_adapter(input_device, source) ||
                !matching_description(source_description, source->GetDesc())) {
                if (failure_stage != nullptr) {
                    *failure_stage =
                        D3D12HistoryInitializationStage::source_validation;
                }
                return E_INVALIDARG;
            }
            ComPtr<ID3D12Resource> retained_source = source;
            source_images.push_back(std::move(retained_source));
        }

        device = input_device;
        queue = input_queue;
        release_state = input_release_state;

        const D3D12_COMMAND_QUEUE_DESC queue_description = queue->GetDesc();
        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask =
            queue_description.NodeMask == 0 ? 1U : queue_description.NodeMask;
        heap_properties.VisibleNodeMask = heap_properties.CreationNodeMask;

        D3D12_RESOURCE_DESC history_description = source_description;
        history_description.Flags = static_cast<D3D12_RESOURCE_FLAGS>(
            history_description.Flags & ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        for (HistorySlot& slot : slots) {
            HRESULT result = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &history_description,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(slot.resource.GetAddressOf()));
            if (FAILED(result)) {
                if (failure_stage != nullptr) {
                    *failure_stage =
                        D3D12HistoryInitializationStage::create_slot_resource;
                }
                return result;
            }
            result = create_closed_command_list(
                queue_description.NodeMask, slot, failure_stage);
            if (FAILED(result)) {
                return result;
            }
        }

        HRESULT result = device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(producer_fence.GetAddressOf()));
        if (FAILED(result)) {
            if (failure_stage != nullptr) {
                *failure_stage = D3D12HistoryInitializationStage::create_fence;
            }
            return result;
        }

        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event == nullptr) {
            if (failure_stage != nullptr) {
                *failure_stage = D3D12HistoryInitializationStage::create_event;
            }
            const DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_NOT_ENOUGH_MEMORY : error);
        }

        initialize_probe(source_description, heap_properties.CreationNodeMask);
        capture_enabled = true;
        return S_OK;
    }

    // The stale-capture probe: a 64x64 patch a third of the way across the
    // first slice, copied out with every capture while the flight recorder
    // runs. Optional: a failure here leaves the history working without it.
    void initialize_probe(
        const D3D12_RESOURCE_DESC& source_description,
        UINT node_mask) noexcept {
        const std::uint32_t texel_bytes =
            probe_texel_bytes(source_description.Format);
        if (texel_bytes == 0 || source_description.SampleDesc.Count != 1) {
            return;
        }
        constexpr std::uint32_t kProbeSize = 64;
        const auto image_width = static_cast<std::uint32_t>(source_description.Width);
        const std::uint32_t width = std::min(kProbeSize, image_width);
        const std::uint32_t height = std::min(kProbeSize, source_description.Height);
        probe_box.left = std::min(image_width / 3U, image_width - width);
        probe_box.top = (source_description.Height - height) / 2U;
        probe_box.right = probe_box.left + width;
        probe_box.bottom = probe_box.top + height;
        probe_box.front = 0;
        probe_box.back = 1;
        probe_row_bytes = width * texel_bytes;
        const std::uint32_t pitch =
            (probe_row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1U) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1U);
        probe_footprint.Offset = 0;
        probe_footprint.Footprint.Format = source_description.Format;
        probe_footprint.Footprint.Width = width;
        probe_footprint.Footprint.Height = height;
        probe_footprint.Footprint.Depth = 1;
        probe_footprint.Footprint.RowPitch = pitch;

        D3D12_HEAP_PROPERTIES readback{};
        readback.Type = D3D12_HEAP_TYPE_READBACK;
        readback.CreationNodeMask = node_mask;
        readback.VisibleNodeMask = node_mask;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = static_cast<UINT64>(pitch) * height;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.Format = DXGI_FORMAT_UNKNOWN;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (HistorySlot& slot : slots) {
            if (FAILED(device->CreateCommittedResource(
                    &readback, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(slot.probe_buffer.GetAddressOf())))) {
                for (HistorySlot& created : slots) {
                    created.probe_buffer.Reset();
                }
                return;
            }
        }
        probe_supported = true;
    }

    // FNV-1a over the patch the slot's finished capture copied out.
    void read_probe(HistorySlot& slot, D3D12HistoryCaptureWait* wait) noexcept {
        if (!slot.probe_pending) {
            return;
        }
        slot.probe_pending = false;
        if (wait == nullptr || slot.probe_buffer == nullptr) {
            return;
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range{
            0, static_cast<SIZE_T>(probe_footprint.Footprint.RowPitch) *
                   probe_footprint.Footprint.Height};
        if (FAILED(slot.probe_buffer->Map(0, &read_range, &mapped)) || mapped == nullptr) {
            return;
        }
        std::uint64_t hash = 14695981039346656037ULL;
        const auto* bytes = static_cast<const std::uint8_t*>(mapped);
        for (std::uint32_t row = 0; row < probe_footprint.Footprint.Height; ++row) {
            const std::uint8_t* line =
                bytes + static_cast<std::size_t>(row) * probe_footprint.Footprint.RowPitch;
            for (std::uint32_t index = 0; index < probe_row_bytes; ++index) {
                hash = (hash ^ line[index]) * 1099511628211ULL;
            }
        }
        const D3D12_RANGE nothing_written{0, 0};
        slot.probe_buffer->Unmap(0, &nothing_written);
        wait->probe_valid = true;
        wait->probe_serial = slot.probe_serial;
        wait->probe_hash = hash;
        wait->probe_source_index = slot.probe_source_index;
    }

    [[nodiscard]] static HRESULT fence_status(
        ID3D12Fence* fence,
        std::uint64_t value) noexcept {
        if (value == 0) {
            return S_OK;
        }
        if (fence == nullptr) {
            return E_UNEXPECTED;
        }
        const std::uint64_t completed = fence->GetCompletedValue();
        if (completed == std::numeric_limits<std::uint64_t>::max()) {
            return E_FAIL;
        }
        return completed >= value ? S_OK : S_FALSE;
    }

    [[nodiscard]] HRESULT wait_for_fence(
        ID3D12Fence* fence,
        std::uint64_t value) noexcept {
        const HRESULT status = fence_status(fence, value);
        if (status != S_FALSE) {
            return status;
        }
        if (fence_event == nullptr) {
            return E_UNEXPECTED;
        }
        const HRESULT event_result = fence->SetEventOnCompletion(value, fence_event);
        if (FAILED(event_result)) {
            return event_result;
        }
        if (WaitForSingleObject(fence_event, INFINITE) != WAIT_OBJECT_0) {
            const DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
        }
        return fence_status(fence, value) == S_OK ? S_OK : E_FAIL;
    }

    // Waits until `fence` reaches `value` or the timer armed by
    // wait_for_busy_slot fires. ERROR_BUSY when the time ran out first.
    [[nodiscard]] HRESULT wait_for_fence_bounded(
        ID3D12Fence* fence,
        std::uint64_t value) noexcept {
        for (;;) {
            const HRESULT status = fence_status(fence, value);
            if (status != S_FALSE) {
                return status;
            }
            const HRESULT event_result = fence->SetEventOnCompletion(value, busy_event);
            if (FAILED(event_result)) {
                return event_result;
            }
            const HANDLE handles[] = {busy_event, busy_timer};
            const DWORD wait_result =
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (wait_result == WAIT_OBJECT_0 + 1) {
                return fence_status(fence, value) == S_FALSE
                    ? HRESULT_FROM_WIN32(ERROR_BUSY)
                    : fence_status(fence, value);
            }
            if (wait_result != WAIT_OBJECT_0) {
                const DWORD error = GetLastError();
                return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
            }
            // The event may be a stale registration from an earlier wait that
            // timed out: the loop checks the fence again before trusting it.
        }
    }

    // The bounded wait for a slot whose last reader or writer is still on the
    // GPU: the consumer's completion first, then the slot's own capture.
    // Never for a lease, which is CPU state no fence will clear.
    [[nodiscard]] HRESULT wait_for_busy_slot(
        HistorySlot& slot,
        std::uint32_t limit_us) noexcept {
        if (slot.active_lease_serial != 0 || limit_us == 0) {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        if (busy_event == nullptr) {
            busy_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (busy_event == nullptr) {
                return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
            }
        }
        if (busy_timer == nullptr) {
            busy_timer = CreateWaitableTimerExW(
                nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (busy_timer == nullptr) {
                busy_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            }
            if (busy_timer == nullptr) {
                return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
            }
        }
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(limit_us) * 10;
        if (!SetWaitableTimer(busy_timer, &due, 0, nullptr, nullptr, FALSE)) {
            const DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
        }
        HRESULT result = S_OK;
        if (slot.consumer_fence_value != 0) {
            result = wait_for_fence_bounded(
                slot.consumer_fence.Get(), slot.consumer_fence_value);
        }
        if (SUCCEEDED(result)) {
            result = wait_for_fence_bounded(producer_fence.Get(), slot.capture_fence_value);
        }
        static_cast<void>(CancelWaitableTimer(busy_timer));
        return result;
    }

    [[nodiscard]] HRESULT prepare_slot_for_capture(HistorySlot& slot) noexcept {
        if (slot.active_lease_serial != 0) {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        if (slot.consumer_fence_value != 0) {
            const HRESULT consumer_status =
                fence_status(slot.consumer_fence.Get(), slot.consumer_fence_value);
            if (consumer_status == S_FALSE) {
                return HRESULT_FROM_WIN32(ERROR_BUSY);
            }
            if (FAILED(consumer_status)) {
                return consumer_status;
            }
            slot.consumer_fence.Reset();
            slot.consumer_fence_value = 0;
        }
        const HRESULT producer_status = fence_status(producer_fence.Get(), slot.capture_fence_value);
        if (producer_status == S_FALSE) {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        return producer_status;
    }

    [[nodiscard]] HRESULT execute_and_signal(
        ID3D12GraphicsCommandList* command_list,
        std::uint64_t* output_value) noexcept {
        if (command_list == nullptr || output_value == nullptr ||
            next_fence_value == 0 ||
            next_fence_value == std::numeric_limits<std::uint64_t>::max()) {
            capture_enabled = false;
            return E_FAIL;
        }

        ID3D12CommandList* command_lists[] = {command_list};
        queue->ExecuteCommandLists(1, command_lists);
        completion_unknown = true;

        const std::uint64_t value = next_fence_value;
        const HRESULT result = queue->Signal(producer_fence.Get(), value);
        if (FAILED(result)) {
            capture_enabled = false;
            return result;
        }

        completion_unknown = false;
        last_submitted_fence_value = value;
        ++next_fence_value;
        *output_value = value;
        return S_OK;
    }

    [[nodiscard]] HRESULT capture(
        std::uint32_t source_index,
        D3D12HistoryCaptureTicket* output_ticket,
        D3D12HistoryCaptureWait* wait) noexcept {
        if (output_ticket == nullptr) {
            return E_POINTER;
        }
        *output_ticket = {};

        if (!capture_enabled || has_pending_capture) {
            return E_UNEXPECTED;
        }
        if (source_index >= source_images.size()) {
            return E_INVALIDARG;
        }
        if (next_serial == 0 || next_serial == std::numeric_limits<std::uint64_t>::max()) {
            capture_enabled = false;
            return E_FAIL;
        }

        HistorySlot& slot = slots[next_slot];
        HRESULT result = prepare_slot_for_capture(slot);
        if (result == HRESULT_FROM_WIN32(ERROR_BUSY) && wait != nullptr) {
            wait->slot_busy = true;
            const auto entered = std::chrono::steady_clock::now();
            result = wait_for_busy_slot(slot, wait->limit_us);
            wait->waited_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - entered).count());
            if (SUCCEEDED(result)) {
                result = prepare_slot_for_capture(slot);
            }
        }
        if (FAILED(result)) {
            return result;
        }
        // The slot's previous capture has finished (prepare_slot_for_capture),
        // so its probe can be read before this one overwrites it.
        read_probe(slot, wait);

        slot.valid = false;
        slot.ticket = {};
        result = slot.capture_allocator->Reset();
        if (FAILED(result)) {
            capture_enabled = false;
            return result;
        }
        result = slot.capture_list->Reset(slot.capture_allocator.Get(), nullptr);
        if (FAILED(result)) {
            capture_enabled = false;
            return result;
        }

        dred_marker(slot.capture_list.Get(), "OFXR history begin");

        ID3D12Resource* const source = source_images[source_index].Get();
        const std::array<D3D12_RESOURCE_BARRIER, 2> before_copy{
            transition_barrier(source, release_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition_barrier(
                slot.resource.Get(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COPY_DEST),
        };
        slot.capture_list->ResourceBarrier(
            static_cast<UINT>(before_copy.size()),
            before_copy.data());
        dred_marker(slot.capture_list.Get(), "OFXR history copy");
        slot.capture_list->CopyResource(slot.resource.Get(), source);

        const bool probe = probe_supported && slot.probe_buffer != nullptr &&
            bridge_flight_logger().enabled();
        const D3D12_RESOURCE_STATES copied_state = probe
            ? D3D12_RESOURCE_STATE_COPY_SOURCE
            : D3D12_RESOURCE_STATE_COMMON;
        const std::array<D3D12_RESOURCE_BARRIER, 2> after_copy{
            transition_barrier(source, D3D12_RESOURCE_STATE_COPY_SOURCE, release_state),
            transition_barrier(
                slot.resource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                copied_state),
        };
        slot.capture_list->ResourceBarrier(
            static_cast<UINT>(after_copy.size()),
            after_copy.data());
        if (probe) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = slot.probe_buffer.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = probe_footprint;
            D3D12_TEXTURE_COPY_LOCATION patch{};
            patch.pResource = slot.resource.Get();
            patch.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            patch.SubresourceIndex = 0;
            slot.capture_list->CopyTextureRegion(&destination, 0, 0, 0, &patch, &probe_box);
            const D3D12_RESOURCE_BARRIER probed = transition_barrier(
                slot.resource.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_COMMON);
            slot.capture_list->ResourceBarrier(1, &probed);
            slot.probe_pending = true;
            slot.probe_serial = next_serial;
            slot.probe_source_index = source_index;
        }
        dred_marker(slot.capture_list.Get(), "OFXR history complete");

        result = slot.capture_list->Close();
        if (FAILED(result)) {
            capture_enabled = false;
            return result;
        }

        std::uint64_t fence_value = 0;
        result = execute_and_signal(slot.capture_list.Get(), &fence_value);
        if (FAILED(result)) {
            return result;
        }
        slot.capture_fence_value = fence_value;

        D3D12HistoryCaptureTicket submitted_ticket{};
        submitted_ticket.serial = next_serial;
        submitted_ticket.fence_value = fence_value;
        submitted_ticket.slot = next_slot;
        submitted_ticket.source_index = source_index;
        pending_ticket = submitted_ticket;
        has_pending_capture = true;
        *output_ticket = submitted_ticket;
        return S_OK;
    }

    [[nodiscard]] HRESULT commit(
        const D3D12HistoryCaptureTicket& requested_ticket) noexcept {
        if (!has_pending_capture || !matching_ticket(pending_ticket, requested_ticket) ||
            requested_ticket.slot >= slots.size()) {
            return E_INVALIDARG;
        }

        HistorySlot& slot = slots[requested_ticket.slot];
        slot.ticket = requested_ticket;
        slot.valid = true;
        pending_ticket = {};
        has_pending_capture = false;
        ++next_serial;
        next_slot = (next_slot + 1U) % D3D12SwapchainHistory::kSlotCount;
        return S_OK;
    }

    void discard(const D3D12HistoryCaptureTicket& requested_ticket) noexcept {
        if (!has_pending_capture || !matching_ticket(pending_ticket, requested_ticket) ||
            requested_ticket.slot >= slots.size()) {
            return;
        }
        HistorySlot& slot = slots[requested_ticket.slot];
        slot.ticket = {};
        slot.valid = false;
        // Its serial goes to the next capture: no probe reports it.
        slot.probe_pending = false;
        pending_ticket = {};
        has_pending_capture = false;
    }

    [[nodiscard]] HRESULT acquire_consumer(
        const D3D12HistoryCaptureTicket& requested_ticket,
        D3D12HistoryConsumerLease* output_lease,
        ID3D12Resource** output_resource,
        ID3D12Fence** output_producer_fence) noexcept {
        if (output_lease != nullptr) {
            *output_lease = {};
        }
        if (output_resource != nullptr) {
            *output_resource = nullptr;
        }
        if (output_producer_fence != nullptr) {
            *output_producer_fence = nullptr;
        }
        if (output_lease == nullptr || output_resource == nullptr ||
            output_producer_fence == nullptr) {
            return E_POINTER;
        }
        if (requested_ticket.slot >= slots.size() || requested_ticket.serial == 0 ||
            requested_ticket.fence_value == 0 ||
            next_lease_serial == 0 ||
            next_lease_serial == std::numeric_limits<std::uint64_t>::max()) {
            return E_INVALIDARG;
        }

        HistorySlot& slot = slots[requested_ticket.slot];
        if (!slot.valid || !matching_ticket(slot.ticket, requested_ticket) ||
            slot.active_lease_serial != 0 || slot.resource == nullptr || producer_fence == nullptr) {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        if (slot.consumer_fence_value != 0) {
            const HRESULT consumer_status =
                fence_status(slot.consumer_fence.Get(), slot.consumer_fence_value);
            if (consumer_status == S_FALSE) {
                return HRESULT_FROM_WIN32(ERROR_BUSY);
            }
            if (FAILED(consumer_status)) {
                return consumer_status;
            }
            slot.consumer_fence.Reset();
            slot.consumer_fence_value = 0;
        }

        const std::uint64_t lease_serial = next_lease_serial++;
        slot.active_lease_serial = lease_serial;
        slot.resource->AddRef();
        producer_fence->AddRef();
        *output_resource = slot.resource.Get();
        *output_producer_fence = producer_fence.Get();
        output_lease->capture_serial = requested_ticket.serial;
        output_lease->lease_serial = lease_serial;
        output_lease->slot = requested_ticket.slot;
        return S_OK;
    }

    [[nodiscard]] HRESULT retire_consumer(
        const D3D12HistoryConsumerLease& lease,
        ID3D12Fence* completion_fence,
        std::uint64_t completion_value) noexcept {
        if (lease.slot >= slots.size() || completion_fence == nullptr || completion_value == 0 ||
            !same_adapter(device.Get(), completion_fence)) {
            return E_INVALIDARG;
        }
        HistorySlot& slot = slots[lease.slot];
        if (!matching_lease(lease, slot, lease.slot)) {
            return E_INVALIDARG;
        }
        slot.consumer_fence = completion_fence;
        slot.consumer_fence_value = completion_value;
        slot.active_lease_serial = 0;
        return S_OK;
    }

    void cancel_consumer(const D3D12HistoryConsumerLease& lease) noexcept {
        if (lease.slot >= slots.size()) {
            return;
        }
        HistorySlot& slot = slots[lease.slot];
        if (matching_lease(lease, slot, lease.slot)) {
            slot.active_lease_serial = 0;
        }
    }

    [[nodiscard]] HRESULT wait_for_idle() noexcept {
        if (completion_unknown) {
            capture_enabled = false;
            return HRESULT_FROM_WIN32(ERROR_IO_INCOMPLETE);
        }
        for (const HistorySlot& slot : slots) {
            if (slot.active_lease_serial != 0) {
                return HRESULT_FROM_WIN32(ERROR_BUSY);
            }
        }

        HRESULT result = wait_for_fence(producer_fence.Get(), last_submitted_fence_value);
        if (FAILED(result)) {
            capture_enabled = false;
            return result;
        }
        for (const HistorySlot& slot : slots) {
            result = wait_for_fence(slot.consumer_fence.Get(), slot.consumer_fence_value);
            if (FAILED(result)) {
                capture_enabled = false;
                return result;
            }
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT invalidate() noexcept {
        const HRESULT result = wait_for_idle();
        if (FAILED(result)) {
            return result;
        }
        for (HistorySlot& slot : slots) {
            slot.ticket = {};
            slot.valid = false;
            slot.capture_fence_value = 0;
            slot.active_lease_serial = 0;
            slot.consumer_fence.Reset();
            slot.consumer_fence_value = 0;
            slot.probe_pending = false;
        }
        pending_ticket = {};
        has_pending_capture = false;
        next_slot = 0;
        return S_OK;
    }
};

D3D12SwapchainHistory::D3D12SwapchainHistory() noexcept = default;

D3D12SwapchainHistory::~D3D12SwapchainHistory() {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ != nullptr && FAILED(impl_->wait_for_idle())) {
            // A producer command or consumer lease whose completion cannot be
            // proven must retain the entire COM graph. Releasing the private
            // slot or its allocator here could race the GPU.
            static_cast<void>(impl_.release());
        }
    } catch (...) {
        // Destruction must not let synchronization failures escape.
    }
}

HRESULT D3D12SwapchainHistory::initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    std::span<ID3D12Resource* const> source_images,
    D3D12_RESOURCE_STATES release_state,
    D3D12HistoryInitializationStage* failure_stage) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ != nullptr) {
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        }
        auto candidate = std::make_unique<Impl>();
        const HRESULT result = candidate->initialize(
            device, queue, source_images, release_state, failure_stage);
        if (FAILED(result)) {
            return result;
        }
        impl_ = std::move(candidate);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT D3D12SwapchainHistory::capture(
    std::uint32_t source_index,
    D3D12HistoryCaptureTicket* ticket,
    D3D12HistoryCaptureWait* wait) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ == nullptr) {
            if (ticket != nullptr) {
                *ticket = {};
            }
            return E_UNEXPECTED;
        }
        return impl_->capture(source_index, ticket, wait);
    } catch (const std::bad_alloc&) {
        if (ticket != nullptr) {
            *ticket = {};
        }
        return E_OUTOFMEMORY;
    } catch (...) {
        if (ticket != nullptr) {
            *ticket = {};
        }
        return E_FAIL;
    }
}

HRESULT D3D12SwapchainHistory::commit(
    const D3D12HistoryCaptureTicket& ticket) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        return impl_ == nullptr ? E_UNEXPECTED : impl_->commit(ticket);
    } catch (...) {
        return E_FAIL;
    }
}

void D3D12SwapchainHistory::discard(
    const D3D12HistoryCaptureTicket& ticket) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ != nullptr) {
            impl_->discard(ticket);
        }
    } catch (...) {
        // Discard is a fail-safe cleanup path.
    }
}

HRESULT D3D12SwapchainHistory::acquire_consumer(
    const D3D12HistoryCaptureTicket& ticket,
    D3D12HistoryConsumerLease* lease,
    ID3D12Resource** resource,
    ID3D12Fence** producer_fence) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ == nullptr) {
            if (lease != nullptr) {
                *lease = {};
            }
            if (resource != nullptr) {
                *resource = nullptr;
            }
            if (producer_fence != nullptr) {
                *producer_fence = nullptr;
            }
            return E_UNEXPECTED;
        }
        return impl_->acquire_consumer(ticket, lease, resource, producer_fence);
    } catch (...) {
        if (lease != nullptr) {
            *lease = {};
        }
        if (resource != nullptr) {
            *resource = nullptr;
        }
        if (producer_fence != nullptr) {
            *producer_fence = nullptr;
        }
        return E_FAIL;
    }
}

HRESULT D3D12SwapchainHistory::retire_consumer(
    const D3D12HistoryConsumerLease& lease,
    ID3D12Fence* completion_fence,
    std::uint64_t completion_value) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        return impl_ == nullptr
                   ? E_UNEXPECTED
                   : impl_->retire_consumer(lease, completion_fence, completion_value);
    } catch (...) {
        return E_FAIL;
    }
}

void D3D12SwapchainHistory::cancel_consumer(
    const D3D12HistoryConsumerLease& lease) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (impl_ != nullptr) {
            impl_->cancel_consumer(lease);
        }
    } catch (...) {
        // Cancellation is valid only before GPU submission and must not throw.
    }
}

HRESULT D3D12SwapchainHistory::wait_for_idle() noexcept {
    try {
        std::scoped_lock lock(mutex_);
        return impl_ == nullptr ? S_OK : impl_->wait_for_idle();
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT D3D12SwapchainHistory::invalidate() noexcept {
    try {
        std::scoped_lock lock(mutex_);
        return impl_ == nullptr ? S_OK : impl_->invalidate();
    } catch (...) {
        return E_FAIL;
    }
}

bool D3D12SwapchainHistory::initialized() const noexcept {
    try {
        std::scoped_lock lock(mutex_);
        return impl_ != nullptr && impl_->capture_enabled;
    } catch (...) {
        return false;
    }
}

}  // namespace xrfg
