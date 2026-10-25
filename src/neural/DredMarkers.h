#pragma once

#include <d3d12.h>
#include "NrFlightTrace.h"

#include <cwchar>

namespace DredMarkers
{
// PIX/DRED interpret metadata 0 as a UTF-16 event string. The previous NR
// markers used metadata 1 with narrow strings, so the breadcrumb operations
// survived a device hang but their context text did not.
constexpr UINT kUnicodeMetadata = 0;

inline UINT ByteSize(const wchar_t* text)
{
    return text == nullptr ? 0u : static_cast<UINT>((std::wcslen(text) + 1) * sizeof(wchar_t));
}

inline void Set(ID3D12GraphicsCommandList* commandList, const wchar_t* text)
{
    if (NrFlightTrace::Enabled() && commandList != nullptr && text != nullptr)
    {
        NrFlightTrace::Marker(commandList, text);
        commandList->SetMarker(kUnicodeMetadata, text, ByteSize(text));
    }
}

inline void Begin(ID3D12GraphicsCommandList* commandList, const wchar_t* text)
{
    if (NrFlightTrace::RecordingEnabled() && commandList != nullptr && text != nullptr)
    {
        NrFlightTrace::StageBegin(commandList, text);
        commandList->BeginEvent(kUnicodeMetadata, text, ByteSize(text));
    }
}

inline void End(ID3D12GraphicsCommandList* commandList, const wchar_t* text)
{
    if (NrFlightTrace::RecordingEnabled() && commandList != nullptr)
    {
        commandList->EndEvent();
        NrFlightTrace::StageEnd(commandList, text);
    }
}

inline void BeforeBarrier(ID3D12GraphicsCommandList* commandList, const wchar_t* role,
                          ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after)
{
    if (!NrFlightTrace::Enabled() || commandList == nullptr)
        return;

    wchar_t marker[256] {};
    NrFlightTrace::BarrierBefore(commandList, role, resource, before, after);
    swprintf_s(marker, L"OFXR-BRIDGE V448 NR barrier role=%ls resource=%p before=0x%08X after=0x%08X",
               role == nullptr ? L"unspecified" : role, static_cast<void*>(resource),
               static_cast<unsigned int>(before), static_cast<unsigned int>(after));
    Set(commandList, marker);
}
} // namespace DredMarkers
