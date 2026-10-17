# OFXR Autonomous V557 - shared release (2026-10-17)

The owner explicitly authorizes publishing the V557 endpoint and release.
This supersedes the local-only status recorded during development; broader
UEVR, MUTAR and Luke Ross retesting is still planned. V536 remains the previously
accepted fallback. This is not a claim that every game/configuration is fixed.

Compared with V536, this endpoint includes:

- OFXR optical-flow operation without valid AFW guides; AFW and native-vector
  generators retain their own input requirements. Depth-aware camera-motion
  subtraction and per-eye guide pairing remain the accepted AFW contract.
- Luke private output transport (A), two fewer auxiliary XR handles, release of
  inactive OFXR overlays, swapchain inventory/fallback warnings under Details,
  corrected resource lock order and busy recovery.
- Live AFW Full/Half/Quarter quality replacement with retired-provider ownership,
  plus optional GPU stage measurements. Full-size transport/downstream work
  remains, so Quarter does not guarantee a proportional FPS gain.
- Native SteamVR capture/prediction-clock corrections and Luke physical-period
  clock handling across route changes.
- Output FPS reads observed compositor delivery; Real/Gen submitted remain
  submission rates. Without an observer, Output FPS is N/A (including VDXR),
  never silently replaced by submitted FPS.
- Guarded native queued-output readiness and consumer-time publication;
  bounded counted admission for native D3D12 SteamVR fresh 2X New/Djules/FSR/
  DLSS-G with B off. Existing ownership/GPU reuse gates remain.
- FSR native D3D12 SteamVR, B off: the existing Djules absolute presentation
  grid, physical HMD period and bounded measured phase acquisition. Other
  backends/runtimes do not inherit this FSR scheduling change.

PureDark AFW v1.0-beta.6 is bundled, with its Full/2x2/4x4 shading support.
Official latest-release metadata was checked on 2026-10-17; beta6 is still the
latest published provider. DLL SHA256:
`5f3cfc38903ac438241a3c47cb4bc3ac329cb39b056e33a63c20045fe15336f9`.
See [dependency provenance](AFW_BETA6_IMPORT.json).

## Validation and limits

The exact sealed V557 runtime passed 81/81 focused optimized-Release checks,
92.56 seconds, plus packaged-loader/AMD64/symbol and archive verification.
The shared export preserves every non-documentation source byte from that
checkpoint; only publication documentation/provenance is consolidated.

Owner MSFS tests report a substantial improvement on much of V556. V557's
latest sweep still has configuration-dependent delivery loss: FSR B-off is
roughly 73-82 Output FPS in the final 00 interval, and New A-off remains weaker
than A-on. These observations are not universal performance guarantees.
VDXR delivery scheduling was not changed by the recent SteamVR work; it does
receive shared UI/meter changes. A/B retain route-specific applicability.

V554 waits and the V555 distributor are excluded. Their trial commits, tags,
packages and failed implementations are not exported. No HUD separation
experiments are included. No new runtime installation/registry changes occur
when publishing this release.

## Collaborator retest

Use the complete V557 package and its tray together; do not mix runtime DLLs.
Record game, VR injector, runtime, headset refresh, resolution and A/B state.
Check Luke OFXR with AFW both off/on and Full/Half/Quarter transitions, then
native SteamVR routes below refresh, including FSR with A off/on and B off.
Distinguish keyboard strafe with a stationary head from physical head movement.
Keep diagnostic logs for problems; normal benchmark export excludes the first
and last 3 seconds of each configuration and reports Cycle estimated.
Untried entries are n.a.; insufficient measured intervals are n.d.

The source build version is 0.557.0 (implementation V557), IPC54201/provider45601.
The release tag is ofxr-autonomous-v557-shared. The original local V557 tag and
ZIP remain immutable. Shared ancestry starts at accepted main V536.
