#pragma once

#include <cstdint>
#include <memory>
#include <optional>

namespace xrfg {

// What the compositor actually scanned out, which is the one thing no OpenXR
// call reports. Everything this layer records is written before xrEndFrame
// hands the frame over, so a session can submit 90 frames a second, space them
// correctly, aim them at 90 distinct slots and still put 45 in front of the
// user with every record reading healthy. SteamVR exposes the truth through
// OpenVR - IVRCompositor::GetCumulativeStats, the same source fpsVR reads.
//
// Fails open and permanently. Not SteamVR, openvr_api.dll missing, the
// compositor interface refused, the counters describing a different process -
// any of them and delivered_fps returns nothing for the life of the session,
// leaving the caller with whatever it was showing before. Nothing here is on
// the per-frame path and nothing here can fail a frame.
class SteamVrDelivery {
public:
    explicit SteamVrDelivery(bool steamvr_runtime) noexcept;
    ~SteamVrDelivery();
    SteamVrDelivery(const SteamVrDelivery&) = delete;
    SteamVrDelivery& operator=(const SteamVrDelivery&) = delete;

    // Distinct images the headset received per second, over a rolling window of
    // about a second. Empty until the first window closes, and empty forever if
    // the attach failed. Attaches lazily on the first call, once. The caller
    // serializes access.
    [[nodiscard]] std::optional<float> delivered_fps(std::int64_t now_ns) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xrfg
