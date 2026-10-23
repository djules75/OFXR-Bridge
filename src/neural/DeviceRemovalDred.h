#pragma once
// Port of the accepted Witcher/OptiScaler V23216/V23074 removal observer.
// The rendering pipeline and queue ordering are unchanged.

// The NR device context owns this observer; it never submits or waits on a GPU
// queue. A private, never-signaled fence notifies device loss on a worker even
// when the application stops calling Present. No spdlog/UI lock is used here.
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "NrFlightTrace.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace DeviceRemovalDred
{
using Microsoft::WRL::ComPtr;

// Settings affect devices created after this call. An already-created game
// device may expose fewer/no breadcrumbs; the retained NR ring is still saved.
// This is diagnostic-only: no debug-layer/GPU validation or queue work.
inline void EnableDiagnosticsOnce()
{
    if (!NrFlightTrace::RecordingEnabled()) return;
    static std::once_flag once;
    std::call_once(once, [] {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings)))) {
            settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    });
}

class File
{
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool failed_ = false;

  public:
    ~File() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    bool Open(const std::filesystem::path& path)
    {
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        return handle_ != INVALID_HANDLE_VALUE;
    }
    bool Good() const { return !failed_ && handle_ != INVALID_HANDLE_VALUE; }
    void Write(const std::string& text)
    {
        if (!Good()) return;
        size_t offset = 0;
        while (offset < text.size())
        {
            DWORD written = 0;
            const DWORD size = static_cast<DWORD>((std::min)(text.size() - offset, size_t(65536)));
            if (!WriteFile(handle_, text.data() + offset, size, &written, nullptr) || written == 0)
            {
                failed_ = true;
                return;
            }
            offset += written;
        }
    }
    template<typename... Args> void Line(const char* format, Args... args)
    {
        char buffer[2048];
        const int length = snprintf(buffer, sizeof(buffer), format, args...);
        if (length < 0) { Write("format_error\n"); return; }
        Write(std::string(buffer, (std::min)(size_t(length), sizeof(buffer) - 1)) + "\n");
        if (size_t(length) >= sizeof(buffer)) Write("line_truncated\n");
    }
    void Flush() { if (Good() && !FlushFileBuffers(handle_)) failed_ = true; }
};

inline std::string Name(const char* narrow, const wchar_t* wide)
{
    // Names are runtime-owned DRED data, valid until the device is released.
    if (narrow && *narrow) return narrow;
    if (!wide || !*wide) return "<unnamed>";
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<invalid-name>";
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

inline void WriteNode(File& file, const D3D12_AUTO_BREADCRUMB_NODE1& node, unsigned ordinal)
{
    const UINT completed = node.pLastBreadcrumbValue ? *node.pLastBreadcrumbValue : 0;
    file.Line("node=%u list=%p queue=%p completed=%u count=%u counter_available=%u",
              ordinal, static_cast<void*>(node.pCommandList), static_cast<void*>(node.pCommandQueue),
              completed, node.BreadcrumbCount, node.pLastBreadcrumbValue != nullptr);
    file.Write("list_name=" + Name(node.pCommandListDebugNameA, node.pCommandListDebugNameW) + "\n");
    file.Write("queue_name=" + Name(node.pCommandQueueDebugNameA, node.pCommandQueueDebugNameW) + "\n");
    file.Flush();
    if (node.pCommandHistory)
    {
        // DRED preserves a 65536-entry ring, even if its operation count is larger.
        const uint64_t end = node.BreadcrumbCount;
        const uint64_t first = end > 65536 ? end - 65536 : 0;
        file.Line("operations retained_first=%llu end=%llu next=%u overwritten=%llu",
                  first, end, completed, first);
        std::string line;
        for (uint64_t index = first; index < end; ++index)
        {
            if (index == completed) line += "<NEXT>";
            line += std::to_string(index) + ":" +
                    std::to_string(static_cast<unsigned>(node.pCommandHistory[index % 65536])) + " ";
            if (line.size() >= 1400) { file.Write(line + "\n"); line.clear(); }
        }
        if (!line.empty()) file.Write(line + "\n");
        if (completed < first) file.Write("next_operation_overwritten\n");
    }
    else file.Write("operations_unavailable\n");
    const UINT contexts = node.pBreadcrumbContexts ? node.BreadcrumbContextsCount : 0;
    file.Line("contexts=%u", contexts);
    const UINT limit = (std::min)(contexts, UINT(65536));
    for (UINT i = 0; i < limit; ++i)
    {
        const auto& context = node.pBreadcrumbContexts[i];
        file.Line("context=%u breadcrumb=%u relation=%s", i, context.BreadcrumbIndex,
                  context.BreadcrumbIndex < completed ? "completed" : "outstanding");
        file.Write("context_text=" + Name(nullptr, context.pContextString) + "\n");
    }
    if (limit != contexts) file.Write("contexts_truncated\n");
    file.Flush();
}

inline void WriteBreadcrumbs(File& file, const D3D12_AUTO_BREADCRUMB_NODE1* head)
{
    // Partially executing lists come first, before potentially thousands of
    // lists merely queued behind the fault. Never let pending lists hide them.
    unsigned partial = 0, pending = 0, complete = 0;
    for (unsigned pass = 0; pass != 2; ++pass)
    {
        auto* node = head;
        unsigned visited = 0;
        for (; node && visited < 4096; node = node->pNext, ++visited)
        {
            const UINT value = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            if (node->BreadcrumbCount == 0 || (node->pLastBreadcrumbValue && value >= node->BreadcrumbCount))
            { if (pass == 0) ++complete; continue; }
            const bool isPartial = node->pLastBreadcrumbValue && value > 0;
            if (isPartial != (pass == 0)) continue;
            WriteNode(file, *node, visited);
            if (isPartial) ++partial; else ++pending;
        }
        if (node) file.Line("nodes_truncated pass=%u limit=4096", pass);
    }
    file.Line("breadcrumb_summary partial=%u pending=%u complete=%u", partial, pending, complete);
    file.Flush();
}

inline void WriteAllocations(File& file, const char* kind, const D3D12_DRED_ALLOCATION_NODE1* node)
{
    unsigned count = 0;
    for (; node && count < 4096; node = node->pNext, ++count)
    {
        file.Line("allocation kind=%s index=%u type=%u object=%p", kind, count,
                  static_cast<unsigned>(node->AllocationType), static_cast<const void*>(node->pObject));
        file.Write("allocation_name=" + Name(node->ObjectNameA, node->ObjectNameW) + "\n");
    }
    if (node) file.Write("allocations_truncated\n");
}

class Monitor
{
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    PTP_WAIT wait_ = nullptr;
    File file_;
    std::filesystem::path path_;

    static void CALLBACK Removed(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT)
    {
        auto& self = *static_cast<Monitor*>(context);
        try { self.Report(); }
        catch (...) { self.file_.Write("report_exception\n"); self.file_.Flush(); }
    }
    void Report()
    {
        if (!NrFlightTrace::RecordingEnabled()) return;
        const HRESULT reason = device_->GetDeviceRemovedReason();
        SYSTEMTIME now {}; GetSystemTime(&now);
        file_.Line("device_removed utc=%02u:%02u:%02u.%03u tid=%lu reason=0x%08X fence=%llu",
                   now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId(),
                   static_cast<unsigned>(reason), fence_->GetCompletedValue());
        file_.Flush();
        if (SUCCEEDED(reason)) { file_.Write("unexpected_healthy_notification\n"); file_.Flush(); return; }

        // The hot path records only into RAM. Once the device is known to be
        // removed, persist the retained window before querying DRED.
        bool flightHeaderWritten = false;
        NrFlightTrace::Dump([&](unsigned long long first, unsigned long long end, size_t capacity,
                                const NrFlightTrace::Record* record)
        {
            if (!flightHeaderWritten)
            {
                file_.Line("nr_flight_begin version=V637 first=%llu end=%llu capacity=%zu",
                           first, end, capacity);
                flightHeaderWritten = true;
            }
            if (record == nullptr)
                return;
            file_.Line("nr_flight seq=%llu tick_ms=%llu tid=%lu kind=%s cmd=%p label=%s resource=%p before=0x%08X after=0x%08X result=0x%llX",
                       record->sequence, record->tickMs, record->threadId, record->kind,
                       record->commandList, record->label, record->resource,
                       record->before, record->after,
                       static_cast<unsigned long long>(record->result));
        });
        file_.Write("nr_flight_end\n");
        file_.Flush();

        ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        const HRESULT query = device_.As(&dred);
        file_.Line("dred_interface=0x%08X", static_cast<unsigned>(query));
        if (SUCCEEDED(query))
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 crumbs {};
            const HRESULT result = dred->GetAutoBreadcrumbsOutput1(&crumbs);
            file_.Line("breadcrumb_query=0x%08X", static_cast<unsigned>(result));
            file_.Flush();
            if (SUCCEEDED(result)) WriteBreadcrumbs(file_, crumbs.pHeadAutoBreadcrumbNode);
            D3D12_DRED_PAGE_FAULT_OUTPUT1 fault {};
            const HRESULT faultResult = dred->GetPageFaultAllocationOutput1(&fault);
            file_.Line("page_fault_query=0x%08X VA=0x%llX", static_cast<unsigned>(faultResult), fault.PageFaultVA);
            if (SUCCEEDED(faultResult))
            {
                WriteAllocations(file_, "existing", fault.pHeadExistingAllocationNode);
                WriteAllocations(file_, "recently_freed", fault.pHeadRecentFreedAllocationNode);
            }
        }
        file_.Write("report_complete\n");
        file_.Flush();
    }

  public:
    ~Monitor()
    {
        if (wait_)
        {
            SetThreadpoolWait(wait_, nullptr, nullptr);
            // CPU callback retirement only, on swapchain destruction; no GPU wait.
            WaitForThreadpoolWaitCallbacks(wait_, FALSE);
            CloseThreadpoolWait(wait_);
        }
        // Destroy the fence/its event registration before closing the event.
        fence_.Reset();
        if (event_) CloseHandle(event_);
        device_.Reset();
    }
    const std::filesystem::path& Path() const { return path_; }

    static std::shared_ptr<Monitor> Observe(IUnknown* owner, const std::filesystem::path& directory)
    {
        if (!NrFlightTrace::RecordingEnabled()) return {};
        if (!owner) return {};
        ComPtr<ID3D12Device> device;
        if (FAILED(owner->QueryInterface(IID_PPV_ARGS(&device))))
        {
            ComPtr<ID3D12CommandQueue> queue;
            if (FAILED(owner->QueryInterface(IID_PPV_ARGS(&queue))) ||
                FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))) return {};
        }
        ComPtr<IUnknown> identity;
        if (FAILED(device.As(&identity))) return {};
        // Registry holds weak references only: no device or swapchain cycle.
        // Its trivial process-lifetime allocation avoids DLL static-destruction ordering.
        struct Registry { std::mutex mutex; std::unordered_map<IUnknown*, std::weak_ptr<Monitor>> entries; unsigned id = 0; };
        static auto* registry = new Registry;
        std::lock_guard<std::mutex> lock(registry->mutex);
        for (auto it = registry->entries.begin(); it != registry->entries.end();)
            if (it->second.expired()) it = registry->entries.erase(it); else ++it;
        const auto existing = registry->entries.find(identity.Get());
        if (existing != registry->entries.end())
            if (auto monitor = existing->second.lock()) return monitor;

        auto monitor = std::make_shared<Monitor>();
        monitor->device_ = device;
        SYSTEMTIME now {}; GetSystemTime(&now);
        wchar_t name[180];
        swprintf_s(name, L"ofxr-dred-V637-%04u%02u%02u-%02u%02u%02u-%lu-device%u.log",
                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                   GetCurrentProcessId(), ++registry->id);
        monitor->path_ = directory / name;
        if (!monitor->file_.Open(monitor->path_)) return {};
        HRESULT result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&monitor->fence_));
        if (FAILED(result))
        { monitor->file_.Line("arm_failed CreateFence=0x%08X", static_cast<unsigned>(result)); monitor->file_.Flush(); return {}; }
        monitor->fence_->SetName(L"OFXR NR device-removal notification only");
        monitor->event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!monitor->event_)
        { monitor->file_.Line("arm_failed CreateEvent=%lu", GetLastError()); monitor->file_.Flush(); return {}; }
        monitor->wait_ = CreateThreadpoolWait(Removed, monitor.get(), nullptr);
        if (!monitor->wait_)
        { monitor->file_.Line("arm_failed CreateThreadpoolWait=%lu", GetLastError()); monitor->file_.Flush(); return {}; }
        result = monitor->fence_->SetEventOnCompletion(UINT64_MAX, monitor->event_);
        if (FAILED(result))
        { monitor->file_.Line("arm_failed SetEventOnCompletion=0x%08X", static_cast<unsigned>(result)); monitor->file_.Flush(); return {}; }
        const auto luid = device->GetAdapterLuid();
        monitor->file_.Line("armed version=V637 pid=%lu device=%p adapter=%08X:%08X trigger=fence_device_removal",
                           GetCurrentProcessId(), static_cast<void*>(device.Get()),
                           static_cast<unsigned>(luid.HighPart), static_cast<unsigned>(luid.LowPart));
        monitor->file_.Line("recorder_mode=%s verbose_logging=%u startup_dred=see_CrashDiagnostics_readiness",
            NrFlightTrace::Enabled()?"verbose":"crash-only",NrFlightTrace::Enabled()?1u:0u);
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr,executable,static_cast<DWORD>(std::size(executable)));
        monitor->file_.Write("game="+Name(nullptr,executable)+"\n");
        wchar_t local[32768]{};
        const DWORD local_size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,static_cast<DWORD>(std::size(local)));
        if(local_size && local_size<std::size(local)) {
            const auto receipt=std::filesystem::path(local)/L"ofxr-bridge/CrashDiagnostics/readiness.txt";
            std::ifstream input(receipt,std::ios::binary);
            if(input) {
                monitor->file_.Write("startup_preparation_receipt_begin\n");
                std::string line;unsigned count{};
                while(count++<8 && std::getline(input,line))
                    monitor->file_.Write(line.substr(0,2048)+"\n");
                monitor->file_.Write("startup_preparation_receipt_end\n");
            } else monitor->file_.Write("startup_preparation_receipt=unavailable; full DRED may require game restart\n");
        }
        monitor->file_.Flush();
        if (!monitor->file_.Good()) return {};
        registry->entries[identity.Get()] = monitor;
        SetThreadpoolWait(monitor->wait_, monitor->event_, nullptr);
        return monitor;
    }
};
}
