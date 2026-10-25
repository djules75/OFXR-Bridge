#pragma once
#include "xrfg/bridge_flight_logger.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <limits>

namespace xrfg {
// Diagnostic only. Queries go in EXISTING lists; never executes/signals/waits.
// Caller owns slot reuse and must prove its existing completion fence is ready
// before harvest. CPU event QPC is harvest time; field c is calibrated GPU QPC.
template<UINT Points> class D3D12RamTiming {
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    UINT64 gpu_hz_{}, gpu_origin_{}, cpu_origin_{}, cpu_hz_{};
    UINT64 identity_{}, serial_{};
    bool pending_{};
    BridgeFlightOperation operation_{};
    void error(HRESULT result) noexcept {
        bridge_flight_logger().event(BridgeFlightOperation::gpu_timing_error,
            result, identity_, serial_, static_cast<UINT64>(operation_));
    }
public:
    void initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                    BridgeFlightOperation operation, bool disk_diagnostics = false) noexcept {
        operation_=operation;
        if (!bridge_flight_logger().ram_enabled() && !disk_diagnostics) return;
        LARGE_INTEGER hz{};
        if (!QueryPerformanceFrequency(&hz) || hz.QuadPart<=0) { error(E_FAIL); return; }
        cpu_hz_=static_cast<UINT64>(hz.QuadPart);
        HRESULT result=queue->GetTimestampFrequency(&gpu_hz_);
        if (SUCCEEDED(result)) result=queue->GetClockCalibration(&gpu_origin_, &cpu_origin_);
        if (FAILED(result) || !gpu_hz_) { error(FAILED(result)?result:E_FAIL); return; }
        D3D12_QUERY_HEAP_DESC desc{};
        desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; desc.Count=Points;
        desc.NodeMask=queue->GetDesc().NodeMask;
        result=device->CreateQueryHeap(&desc,IID_PPV_ARGS(&heap_));
        if (FAILED(result)) { error(result); return; }
        D3D12_HEAP_PROPERTIES memory{}; memory.Type=D3D12_HEAP_TYPE_READBACK;
        memory.CreationNodeMask=memory.VisibleNodeMask=desc.NodeMask?desc.NodeMask:1;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=Points*sizeof(UINT64);
        buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1;
        buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        result=device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_));
        if (FAILED(result)) { error(result); heap_.Reset(); }
    }
    void mark(ID3D12GraphicsCommandList* list, UINT point) noexcept {
        if (readback_) list->EndQuery(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,point);
    }
    void resolve(ID3D12GraphicsCommandList* list) noexcept {
        if (readback_) list->ResolveQueryData(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,
            0,Points,readback_.Get(),0);
    }
    // Called immediately before existing submission, not while recording lists.
    void submitted(UINT64 identity, UINT64 serial) noexcept {
        if (!readback_) return;
        identity_=identity; serial_=serial; pending_=true;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        bridge_flight_logger().event(operation_,Points,identity_,serial_,now.QuadPart);
    }
    void harvest() noexcept {
        if (!pending_ || !readback_) return;
        pending_=false;
        const D3D12_RANGE range{0,Points*sizeof(UINT64)};
        void* mapped=nullptr;
        const HRESULT result=readback_->Map(0,&range,&mapped);
        if (FAILED(result) || !mapped) { error(FAILED(result)?result:E_FAIL); return; }
        std::array<UINT64,Points> values{};
        const auto* source=static_cast<const UINT64*>(mapped);
        for(UINT i=0;i<Points;++i) values[i]=source[i];
        const D3D12_RANGE empty{0,0}; readback_->Unmap(0,&empty);
        for(UINT i=0;i<Points;++i) {
            if (!values[i] || (i && values[i]<values[i-1])) { error(E_FAIL); return; }
        }
        for(UINT i=0;i<Points;++i) {
            const long double delta=static_cast<long double>(values[i])-gpu_origin_;
            const long double qpc=cpu_origin_+delta*cpu_hz_/gpu_hz_;
            if(qpc<=0 || qpc>=static_cast<long double>(std::numeric_limits<UINT64>::max())) {
                error(E_FAIL); return;
            }
            bridge_flight_logger().event(operation_,i,identity_,serial_,static_cast<UINT64>(qpc));
        }
    }
};
} // namespace xrfg
