# V536: AFW beta6, shading cost control and paired Luke marker readback

The KCD2 run confirms a substantial AFW cost at 2987x4272 pixels per eye.
This candidate adds the official beta6 shading control and removes one of two
serial eye-marker GPU rendezvous. Actual headset FPS/latency gains are pending.

## What changes

- Import official PureDark UEVR AFW v1.0-beta.6. Exact asset/DLL/header identities
  are in AFW_BETA6_IMPORT.json. The packaged DLL wins over a game-local provider;
  game files and their camera/phase calibration remain intact.
- Combined exposes 1x1, 2x2, 4x4 shading, default 1x1. This lowers warping work,
  not the game render or final XR output dimensions. Lower settings can lose detail.
  Save the setting and restart the game. Switching an active beta6 renderer was
  rejected by actual GPU resource-state checks; active/pending state is explicit.
- Decode both frozen eye color markers in one queue submission and one completion
  wait. This generalizes the existing Vulkan pair reader to native Luke D11.
  Both source captures are frozen first, on the same owner queue; separate CRC,
  token, invalid-peer, timeout and resource-lifetime handling remains unchanged.
- Disable effective Hybrid guide mixing while AFW owns the reconstructed peer.
  Published original AER vectors do not form guides for that peer. OFXR/Hybrid
  falls back to optical flow. FSR/DLSS-G still needs complete native stereo guides.
- Record active shading in compact configuration bits20-23 and portable CSV/JSON;
  separate shading configurations into different benchmark rows. Existing columns
  and exclusion of the first/last3seconds remain unchanged.

## Required beta6 compatibility, proven against the real provider

The evaluation structure remains184bytes, with ShadingRate at132 and Reserved
at136. PureDark values are9/10/12/17/18/20/33/34/36, not D3D12 VRS enums.
The first call after a shading change returns no output before recording the
warp; one bounded repeated call completes initialization in the existing list.
No second generated frame or extra frame history is manufactured.

The pinned binary allocates full-rate history with a single padding pixel and
issues unboxed CopyResource calls between512 and513 in the controlled fixture.
Unadapted beta6 fails D3D12 validation; beta5 control passes. A synchronous scoped
adapter clones the32-entry returned renderer interface while Evaluate runs,
intercepts only its Copy method for same-format, single-subresource/single-sample
color textures differing by at most one pixel, and performs explicit region
copies with a clamped optional border. It restores the interface before leaving
that call, serializes provider adaptation, forwards every other operation, and
does not modify DLL code or any command-list vtable. GPU tests keep error checks
strict, including the exact original camera/motion gain and independent pixels.

Live switching additionally failed lookup-texture state checks; it is intentionally
not shipped. Each synthesizer pins its first shading setting. AFW mode/debug
remain independent live controls; shading applies to the next game session.

## Scope and source export

Fresh candidate branch from origin/main exports final V535 source without local
trial ancestry or rejected/pending manifests. This also removes withdrawn V531
C1/C2/C3, retains meter/benchmark labels, exact-limit Luke SteamVR auxiliary fallback
and game-first Luke NGX initialization. No claim that disabling overlays proves
either older fix accepted. No new swapchain allocation/count, forced A/B, queue,
pacing, UEVR/VDXR scheduler change, HUD experiment, or stereo-vector math change.

## Reproduce and test

Run `tools/Import-AfwBeta6.ps1 -DependencyRoot <deps>`; it verifies the official
archive and extracts only its pinned DLL outside Git. Then run
`tools/Build-Autonomous.ps1 -DependencyRoot <deps> -BuildRoot <build>`.
This explicitly selects runtime/afw-beta6/PDAFWPlugin.dll even in a reused cache.
Actual check results are in V536_VALIDATION.json; hardware-in-loop headset
acceptance remains pending and is distinct from the GPU fixtures.

For the owner: close the game and old tray, extract the whole V536 ZIP. First
compare the same KCD2 scene with OFXR disabled/AFW Combined at1x1; then select2x2,
restart the game, and repeat. Test4x4 similarly if useful. Keep resolution, NR,
SteamVR/overlay settings fixed. F9 before launch captures active shading and
paired marker counts; no F8 dump is needed for this performance comparison.
