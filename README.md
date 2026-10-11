Current shared routing: read [shared Autonomous handoff](../docs/ofxr-autonomous/README.md). Build with `tools/Build-Autonomous.ps1` from the shared repository root; prepared dependencies are in `.local/ofxr-autonomous-deps` and outputs in `builds/ofxr-autonomous-new`. Earlier laboratory build paths below are historical.

## V482 candidate: Horizon rolling flicker capture

V481 startup pass-through retained. Ctrl+F8 arms/stops the default-OFF buffer; each F8 saves the previous second and keeps recording, repeatable. Per-eye images/source IDs, AFW contracts and actual OpenXR poses accompany each snapshot. Focused29/29 pass. See docs/V482_LUKE_RING.md (V482_LUKE_RING.md within docs). Capture is diagnostic overhead; visible flicker remains unresolved. V474 fallback; sealed predecessors retained.

## V481 candidate: Horizon startup pass-through

Menus/loading pass until original AER production advances; gameplay duplicate filtering remains. Focused28/28 pass. See docs/V481_LUKE_STARTUP.md (V481_LUKE_STARTUP.md within docs). Owner confirms V480 still flickers: clock defect is absent, but GPU/readback gaps remain. V481 only fixes startup black output; F8 paired image/guide capture is needed for pixel/pose diagnosis. V474 fallback; all sealed predecessors retained.

## V480 candidate: Horizon clock continuity

Fix HFW producer prediction rebasing backward after physical output; fresh-only admission and V479 warm bypass retained. Exact V479 negative reproduces1ns, fix preserves21ms elapsed; focused27/27 pass. See docs/V480_LUKE_CLOCK.md (V480_LUKE_CLOCK.md in docs). Headset flicker validation pending; GPU/readback stalls remain. V474 fallback, V479 preserved. Earlier pointers below are historical.

## V479 candidate: Horizon fresh-content pacing

Pinned HFW AER source admission in all four AFW/OFXR states, no retained output repeats, unique-output FPS and both-Off warm GPU capture bypass. See docs/V479_LUKE_PACING.md (V479_LUKE_PACING.md within docs). Focused26/26 pass; Horizon fluidity/FPS recovery awaits owner run. V474 fallback, V478 preserved; earlier pointers below are historical.

## V478 candidate: Horizon parity with working OptiScaler V33018

HFW physical Forward contract restored selectively; game eye phase/provider preserved. F8 image capture, F9 live logging, F10 menu, Ctrl+Alt+F6 synchronized AFW debug. See docs/V478_LUKE_PARITY.md (V478_LUKE_PARITY.md from docs). Focused24/24 pass; actual headset parity pending. V474 fallback; V475/V476 excluded. Earlier candidate pointers below are historical.

## V477 candidate: Luke standalone native context

V474 plus original-game D12 queue/device handoff and safe delayed Luke initialization. Accepted Luke/AFW fixes retained; see V477_LUKE_CONTEXT.md in docs. Native Vulkan guides remain suspended. No V475/V476 import. Horizon validation pending.

# OFXR 0.3.0 (v473)

Fixed Elliottate display selection stuck on Applying settings. Both controller and bottom modes now reach the runtime through the normal settings adapter. See docs/V473_METER_LIVE.md.

# OFXR 0.3.0 (v472)

Post-R19 black-bar timing fixes are live under the existing Reduce black bars during headset rotation checkbox, default OFF. See docs/V472_BLACKBAR_TIMING.md. All existing routes/meter/counters retained.

## Current owner scope: V471 visual meter and auto arm

R19 Elliottate meter only, displaying existing shared counters/diagnostics in two rows followed by Vectors/Status. Display adds live controller/bottom modes after four corners. Original mode IDs retained. Djules label now medium latency. Normal tray launch auto-arms by explicit owner request; settings-only/preview/watchdog do not. IPC47101/providerABI45601. No synthesis/provider/scheduler changes. See [docs/V471_ELLIOTT_METER.md](docs/V471_ELLIOTT_METER.md). Earlier startup-disarmed wording below is historical and superseded for normal V471 tray launch.

## Current owner scope: V470 Cycle estimated

Replace Presentation estimated with separate pair-slot Cycle estimated; measured Cycle length retained. Same-frame QPC receipts only, no renderer/scheduling change. IPC47001/provider ABI45601. See [docs/V470_CYCLE_ESTIMATE.md](docs/V470_CYCLE_ESTIMATE.md).

## Current owner scope: V469 Stellar Blade native/vector fixes

Targeted R22 guide composition and R23 native object separation. FSR/AFW, Vulkan suspension, existing UI/metrics/cadence preserved. GPU/functional checks required for this renderer change; earlier text-only exceptions no longer apply. See [scope](docs/V469_STELLAR_DLSSG.md).

## Current owner scope: V468 About and branding

Visible name OFXR 0.3.0 (v468); shared settings and tray/About captions only. Credits broaden Djules collaboration and acknowledge Elliotttate DLSS/Hybrid integration. Runtime behavior/profile paths/IPC retained. No new tests for this text-only change. See [scope](docs/V468_ABOUT_BRANDING.md).

## Current owner scope: V467 menu descriptions

Shared desktop/F10 captions and expanded Luke Ross controls only. Existing route/backend profile IDs and runtime behavior are preserved. No GPU suites are rerun for this UI-only change. See [V467 scope](docs/V467_MENU_DESCRIPTIONS.md). IPC46501/provider ABI45601.

## Current owner scope: V466 tray-only menu

V466 changes only the tray arm toggle to two rows, Enable and Disable, with opposite check states and the existing shared toggle command. The package reuses all V465 runtime DLLs unchanged (IPC46501/provider ABI45601). Owner explicitly requests no unnecessary tests; no new GPU/renderer suites are run. See [V466 scope](docs/V466_TRAY_TOGGLE.md).

## Current owner scope: V465 frame-cycle timings

V465 inherits sealed V464 and replaces the displayed GPU render / OFXR delay pair with OFXR time and Cycle length. The latter measures frame production plus all API blocking and held-real residence until downstream acceptance and producer release, using the same Wait ticket. Presentation estimated remains separate. No renderer, shader, FSR guide/provider, route, camera or scheduler change. See [V465 scope](docs/V465_CYCLE_METRICS.md). IPC46501; provider ABI45601. Game/headset timing comparison pending.

## Current owner scope: V464 FSR release-bound guides

V464 inherits sealed V463. The owner explicitly authorizes the targeted ordinary native-D3D12 FSR guide/eye association correction described in [V464 scope](docs/V464_FSR_RELEASE_GUIDES.md). This supersedes historical FSR preservation instructions only for that association. FSR provider, shader math, SDK camera/motion parameters and scheduling remain unchanged. OFXR, native DLSS-G, D11 and Luke/AFW contracts are retained. The owner additionally requests a simpler Hybrid menu: fallback is fixed to FidelityFX and the duplicated fallback controls are removed. R19 stays live and default OFF; Vulkan native guide ingress stays suspended. Game/headset confirmation of the intermittent MSFS/Returnal flicker is pending.

## Current owner scope: V463 Hybrid

V463 inherits sealed V462 and adds selectable Hybrid between OFXR and FSR. R19 camera remains default OFF and live on every route. Existing OFXR/FSR/DLSS-G routes retain their behavior. Hybrid has its own profile and optical-flow fallback; native Vulkan guide capture remains suspended. See docs/V463_HYBRID.md. Earlier no-R19/no-Hybrid constraints below are historical and superseded only by these explicit V462/V463 owner requests.

# Current V462 scope

R19 is an explicitly authorized, default-OFF live checkbox for all FG routes. See [V462 scope](docs/V462_R19_CAMERA.md). This supersedes older R19 exclusions below. Base V461; Vulkan guide capture remains suspended. No Hybrid yet. Game/headset validation pending.

# OFXR Autonomous V461

Owner-selected V460 base (`f1e057a2050cd635e195489729040d9a325744a9`) plus only
the V457 suspension of native Vulkan guide ingress (`18f584bd865c36f486cdc7019c14735510ab61fc`).
V460 shared interface/telemetry is retained. The XR Vulkan color bridge and V454
queue synchronization remain active; D11/D12 FSR, synthesis/shaders and native
provider handshake 45601 are unchanged. IPC 46101 isolates this candidate.
No new stutter fix, scheduler, GPU profiling or V458/V459 changes are imported.
See [scope](docs/V461_VULKAN_UI.md).

## Preserved V460 interface

Shared desktop and in-game interface: four FPS/refresh counters, three separately
sourced timing values, actual vector source and session problems above the
existing four tabs. IPC ABI **46101** in this combined candidate. The independent corner counter remains
available with the menu closed. See [V460 implementation and evidence](docs/V460_INTERFACE.md).

The preserved V460 interface branch starts from sealed V456 `a1fcf74e123f825eefd998f558907cb702212718`.
Rendering, guide admission, providers and pacing retain V456 behavior. The V461
Vulkan suspension above supersedes the earlier separate-branch integration note.
No hybrid or extrapolation/ASW route is imported.

## Preserved V456 behavior

R15 release-bound eye association is enabled only for OFXR game vectors and
native DLSS-G. FSR/OFA/AFW keep their existing behavior. R19 camera, scheduler
and V453 GPU timing changes are excluded. See [scope and regression](docs/V456_R15_EYE_GUIDES.md).

The prior V455 provider recovery and V454 queue correctness are retained.
Returnal visual quality must be compared with V455; no improvement is presumed.

# Preserved V455 behavior

V455 recovers an incompatible game-owned DLSS-G provider after an actual core
Evaluate InvalidParameter refusal. It validates the adjacent private provider,
keeps refused pairs in real-copy bypass, and recreates only OFXR-owned features
after their GPU fence completes. The actual Returnal FG1.0.3.0 fixture fails
before and passes after, including enabled GPU decisions and stereo midpoint
pixel readbacks. Returnal in-game/headset confirmation remains pending.
See [provider recovery](docs/V455_RETURNAL_PROVIDER.md).

V454 starts from sealed V452 and narrows Vulkan host synchronization to each
actual VkQueue. A driver wait/present on another queue no longer blocks unrelated
graphics, compute or transfer submissions. Same-queue serialization stays active.
The regression fails on V452 and passes after this change. No new GPU profiling
is included. No Man's Sky's in-game performance recovery remains to be tested.
See [scope and evidence](docs/V454_VULKAN_QUEUES.md).

A standalone Windows OpenXR frame-generation candidate based on Elliot Tate's native-DLSS-G fork and the accepted V33034 renderer, Luke Ross AFW and Neural Rendering components. Assetto Corsa/CSP is excluded.

V452 distinguishes copied real native outputs from GPU-confirmed interpolation.
A private pair receipt retains the NGX GPU disable-interpolation decision across
work-slot reuse. Refused pairs remain real-copy bypass and are not extra content
for the counter. Ordinary live logs include native record reasons, NGX evaluate
results and completed GPU decisions. Native ready diagnostics come from presented
content instead of unrelated auxiliary-chain guide matches. See
[content and evidence](docs/V452_NATIVE_CONTENT.md).

V451 fixes OFXR game-vector consumption for two eyes packed into one texture,
using the exact r19 packed coordinate mapping and separate per-eye descriptors.
It also recovers native DLSS-G after the game's NGX core cached FeatureNotFound,
through the bundled private FG forwarder, with real provider/hardware validation
and separate handle ownership. Both failures reproduce before the changes and
pass GPU regressions after them. See [Detnoir evidence and scope](docs/V451_DETNOIR_GUIDES.md).

V450 recovers private generation buffers when a provider initialization refusal
is followed by an explicit control reconfiguration. Structural/runtime budget
refusals remain closed. Both menus show the failed provider and HRESULT;
late-started logs retain eligibility and native NGX capability results.
DLSS-G files remain beside the layer; copying a second DLL into the game does
not repair NGX discovery after the game's first initialization. See
[provider recovery and evidence](docs/V450_PROVIDER_RECOVERY.md).

V449 saves separate preferences for all four OFXR engines, FSR and DLSS-G.
Switching routes restores their own motion source, 2X/3X and other options.
Diagnostic logging can be switched in the current session; both settings
surfaces report its actual runtime state. See [changes and tests](docs/V449_ROUTE_SETTINGS.md).

V448 aligns the private FSR provider with the SDK runtime used by embedded
OptiScaler's FSR tests and adds bounded input diagnostics for ordinary VR.
Returnal rotation artifacts are owner-reported on V447; their visual correction
is pending game validation. See [the evidence and limits](docs/V448_FSR_PROVIDER.md).

V447 fixes Returnal startup crashes by binding the actual Streamline 1.x or 2.x
API, preserving exact frame/viewport camera ownership. See
[the crash evidence and compatibility checks](docs/V447_RETURNAL_STREAMLINE.md).

V446 fixes generation staying in bypass after changing the output route while
paused. Re-enabling rebuilds the already-admitted buffers and primes fresh
history. See [the Callisto evidence and regression](docs/V446_CALLISTO_SWITCH.md).

V445 ports selected upstream corrections for interpolation quality, native depth
heuristics and per-eye guide matching, preserving V444 crash/bypass/diagnostic
fixes. Hybrid/extrapolation, new pacing and benchmark UI are deferred. See
[the integration and full commit audit](docs/V445_UPSTREAM_INTEGRATION.md).

V444 makes **Lower VRAM** a checkbox, off by default. It corrects the native
DLSS-G NVIDIA work-slot null access found in the owner's V443 Planet Crafter
dump, bypasses native FG before priming when stereo motion/depth guides are
missing, and adds always-visible input/fallback/bypass diagnostics beneath the
generation options. See [V444 changes and evidence](docs/V444_SETTINGS_AND_GUIDES.md).

It retains V443's correction for V442's Planet Crafter startup failure when a busy D3D12 history
capture left an eye without a ticket. That stereo frame now stays real, and the
next complete capture primes both eyes before synthesis resumes. See
[the failure and regression evidence](docs/CRASH_V442_PLANET_CRAFTER_START.md).

The tray and in-headset Dear ImGui menu share the same controls and publish configuration to the layer running inside the game. Press **F10** in the focused game to open/close the menu, **Esc** to close; use the mouse to move its drawn pointer, click and scroll. Drag an empty area to move the headset window; the fixed footer controls interface size and resets position/size. V444 retains V442's ImGui navigation correction for V441's reopen crash. This keeps the default Insert key available for [UEVR's own menu](https://github.com/praydog/UEVR#in-game-menu). The menu is a dedicated OpenXR quad in the existing FPS overlay composition path, including original and synthetic frames. It works with the FPS counter switched off. No OptiScaler DLL or upscaler proxy is required.

## Capabilities and use

- OFXR FidelityFX optical flow and NVIDIA reference, new and djules routes, with optical-flow or compatible game-motion inputs.
- FSR 3 frame generation and native NVIDIA DLSS-G. These require valid native stereo motion/depth guides. Native DLSS-G supports 2X and hardware-gated 3X; djules has its own 3X scheduler.
- Luke Ross AFW Off/Alternate eye/Previous frame/Combined controls. Functional AFW uses the accepted depth-aware camera/stereo correction and requires the real game's camera guides and `RealVR.ini`.
- Neural Rendering before/after native SR/RR, including model, colour and comparison controls. The external NVIDIA model/runtime `nvngx_dlssnr.dll` is required and is not included. The included `nvngx.dll_dlssnr.dll` is only the forwarder.
- D3D11/D3D12/Vulkan integration, lower-VRAM and pacing options, headset FPS overlay and diagnostic logging.

Guide-driven paths need compatible native SR/RR inputs; missing or invalid guides keep the real-frame path until valid inputs arrive. Colour-only OFXR optical flow does not require those guides. Automated checks and headset/game validation are distinct; current evidence is in [VALIDATION.md](VALIDATION.md).

The OFXR **Game motion vectors** option selects a separate direct interpolation
path which bypasses optical-flow calculation when compatible guides exist;
otherwise OFXR falls back to optical flow. It predates Autonomous, but has no
owner Planet Crafter validation. Optical flow remains the new-profile default.
Input diagnostics observe typed native DLSS SR/RR evaluations even in optical-flow
mode, without enabling GPU guide copies. Input presence and owned XR guide
matches are separate; FSR/XeSS source-upscaler ingress is not implemented.

Late injection can miss the game's first NGX initialization and already-created guide resources. Actual RTX 4090 testing showed that a second NGX initialization does **not** repair native DLSS-G provider discovery after the first initialization omitted its path. A fresh OpenXR session alone does not establish correct startup order for UEVR. The menu does not change this limitation.

Run `OFXRBridgeTray.exe`: it starts **disarmed**. Double-click the tray icon to open settings, choose **Activate** explicitly, then start a fresh OpenXR game. `--settings-only` opens the panel without activating or registering the layer. Preferences and runtime cache use the separate `%LOCALAPPDATA%/OFXR Autonomous` profile. See [Italian testing instructions](TESTING_IT.md) and the [runtime contract](AUTONOMOUS_CONTRACT.md).

Keep this package layout intact:

```text
OFXRBridgeTray.exe
ofxr/
  XR_APILAYER_XRFrameBridge_diagnostic.dll
  OFXR_vulkan_queue_layer.dll
  nvngx_dlssg.dll
  ofxr_amd_fidelityfx_framegeneration_dx12.dll
  PDAFWPlugin.dll
  nvngx.dll_dlssnr.dll
  ofxr_bridge.ini
licenses/
```

An externally supplied `nvngx_dlssnr.dll` may be placed in `ofxr/` before activation or be available beside the game executable. Activation prepares the candidate's own runtime cache; no game DLL replacement is part of this package's setup.

## Build

Use Visual Studio 2022 BuildTools x64, its bundled CMake, the Windows SDK and the prepared shared dependency tree. The build is independent of an OptiScaler checkout. The NVIDIA DLSS SDK is pinned to official [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) commit `374959484e79a640feaba44c93ac8cfb0a03f5b5` (310.9.1); other SDK/library/provider inputs are described in [building instructions](docs/BUILDING.md).

From the source root, build optimized Release and run the reference/new-route suite:

```powershell
.\Build-Autonomous.ps1 -TestRoute new -RunTests
```

Use `-TestRoute djules` for its additional scheduler suite. `-DependencyRoot` and `-BuildRoot` select prepared dependencies and compiler outputs outside the source tree. The helper builds/tests; activation remains the user's tray action.

## Source and licences

The combined candidate is GPL-3.0; see [LICENSE](LICENSE) and [component attribution](licenses/ATTRIBUTION.md). Original OFXR LGPL notices, Dear ImGui MIT notices and vendor licences remain preserved. This is an isolated local test candidate; provenance and the fixed motion/capture rules are recorded in [AUTONOMOUS_CONTRACT.md](AUTONOMOUS_CONTRACT.md).
