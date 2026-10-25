#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <utility>

namespace NrFlightTrace
{
inline std::atomic<bool> enabled {false};
inline std::atomic<bool> crashEnabled {false};
inline bool Enabled() noexcept { return enabled.load(std::memory_order_relaxed); }
inline bool RecordingEnabled() noexcept { return Enabled() || crashEnabled.load(std::memory_order_relaxed); }
inline void SetEnabled(bool value) noexcept { enabled.store(value, std::memory_order_relaxed); }
inline void SetCrashEnabled(bool value) noexcept { crashEnabled.store(value,std::memory_order_relaxed); }
struct Record
{
    unsigned long long sequence = 0;
    unsigned long long tickMs = 0;
    unsigned long threadId = 0;
    char kind[24] {};
    void* commandList = nullptr;
    char label[256] {};
    void* resource = nullptr;
    unsigned int before = 0;
    unsigned int after = 0;
    long long result = 0;
};

inline constexpr size_t kCapacity = 16384;

struct State
{
    std::mutex mutex;
    std::array<Record, kCapacity> records {};
    std::atomic<unsigned long long> sequence {0};
};

inline State& GetState()
{
    // Process-lifetime storage avoids teardown races after a removed device.
    static State* state = new State();
    return *state;
}

inline void CopyUtf8(char* destination, size_t destinationSize, const wchar_t* source)
{
    if (destinationSize == 0)
        return;
    destination[0] = '\0';
    if (source == nullptr)
        return;

    const int capacity = static_cast<int>(destinationSize);
    const int written = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, capacity,
                                             nullptr, nullptr);
    if (written == 0)
        destination[destinationSize - 1] = '\0';
}

inline void Write(const char* kind, ID3D12GraphicsCommandList* commandList, const wchar_t* label,
                  ID3D12Resource* resource = nullptr,
                  D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON,
                  D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON, long long result = 0)
{
    if (!RecordingEnabled()) return;
    State& state = GetState();
    Record record {};
    record.sequence = state.sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    record.tickMs = GetTickCount64();
    record.threadId = GetCurrentThreadId();
    strncpy_s(record.kind, sizeof(record.kind), kind == nullptr ? "UNKNOWN" : kind, _TRUNCATE);
    record.commandList = commandList;
    CopyUtf8(record.label, sizeof(record.label), label);
    record.resource = resource;
    record.before = static_cast<unsigned int>(before);
    record.after = static_cast<unsigned int>(after);
    record.result = result;

    std::lock_guard<std::mutex> lock(state.mutex);
    state.records[static_cast<size_t>((record.sequence - 1) % kCapacity)] = record;
}

template <typename Sink>
inline void Dump(Sink&& sink)
{
    if (!RecordingEnabled()) return;
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const unsigned long long end = state.sequence.load(std::memory_order_acquire);
    const unsigned long long first = end > kCapacity ? end - kCapacity + 1 : 1;
    sink(first, end, kCapacity, static_cast<const Record*>(nullptr));
    for (unsigned long long sequence = first; sequence <= end; ++sequence)
    {
        const Record& record = state.records[static_cast<size_t>((sequence - 1) % kCapacity)];
        if (record.sequence == sequence)
            sink(first, end, kCapacity, &record);
    }
}

inline void Marker(ID3D12GraphicsCommandList* commandList, const wchar_t* label)
{
    Write("MARKER", commandList, label);
}

inline void StageBegin(ID3D12GraphicsCommandList* commandList, const wchar_t* label)
{
    Write("STAGE_BEGIN", commandList, label);
}

inline void StageEnd(ID3D12GraphicsCommandList* commandList, const wchar_t* label)
{
    Write("STAGE_END", commandList, label);
}

inline void BarrierBefore(ID3D12GraphicsCommandList* commandList, const wchar_t* role,
                          ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after)
{
    if(Enabled()) Write("BARRIER_BEFORE", commandList, role, resource, before, after);
}

inline void BarrierAfter(ID3D12GraphicsCommandList* commandList, const wchar_t* role,
                         ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                         D3D12_RESOURCE_STATES after)
{
    if(Enabled()) Write("BARRIER_RECORDED", commandList, role, resource, before, after);
}

inline void Result(ID3D12GraphicsCommandList* commandList, const wchar_t* label, long long result)
{
    Write("RESULT", commandList, label, nullptr, D3D12_RESOURCE_STATE_COMMON,
          D3D12_RESOURCE_STATE_COMMON, result);
}
} // namespace NrFlightTrace
