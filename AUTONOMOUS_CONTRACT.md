## Current shared Autonomous endpoint: V520

V513 pre-HUD baseline + selected KCD V603 in V519 + accepted Indiana V701 in V520. Combined game validation pending; KCD distant-tree wobble remains reported. Private release: https://github.com/tig3rmast3r/ofxr-development/releases/tag/ofxr-autonomous-v520 . See V520_SELECTED_INDIANA_INTEGRATION.md and the shared branch provenance records. HUD experiments remain local and paused.

## Historical shared Autonomous endpoint: V519

Owner-selected KCD V603 merged onto pre-HUD V513. Combined runtime validation pending; distant-tree wobble remains reported. See autonomous/docs/V519_SELECTED_KCD_INTEGRATION.md and docs/ofxr-autonomous/KCD_SELECTED_DELTA.md. HUD experiments remain local.

## Historical shared Autonomous endpoint: V513

Owner-selected export through V513. See [integration record](/docs/ofxr-autonomous/ACCEPTED_INTEGRATION.md) and the accepted-build provenance map. Earlier entries below describe historical checkpoints; V514-V518 HUD experiments remain local.

## V483 candidate: Luke stereo continuity

Horizon stationary flicker: repeated-eye source gaps collapse AFW stereo; real provider regression reproduced and corrected with current-source stereo warp for that transition. Ghost/Crimson native motion supports signed perspective W; actual game recovery pending. Focused30/30 pass. See docs/V483_LUKE_STEREO.md (V483_LUKE_STEREO.md within docs). FSR/OFA/providers/shaders/pacing retained; V474 fallback.

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

## Current owner scope: V474 selective Djules fixes

V473 plus V439/V441/V442 limited to ordinary Djules and V440/V443 on their
shared D11 bridge/lifecycle mechanisms. Existing shared history captures remain
nonblocking unless explicitly opted in by ordinary Djules input capture. All
other quarantine and resource lifetime protections remain. IPC47101/provider45601.
See docs/V474_DJULES_FIXES.md (or V474_DJULES_FIXES.md from this docs directory).

## Current owner scope: V473 Elliottate live display repair

Runtime display codec now supports values5/6, completes application and acknowledges the real revision. No renderer/provider/FSR/timing change. V472 black-bar changes retained. IPC47101/providerABI45601 retained. See [scope](docs/V473_METER_LIVE.md).

## Current owner scope: V472 post-R19 black-bar timing

R20 inline real-output prediction and R21 actual xrLocateViews pose-time promise measurement selectively adapted to existing Autonomous consumers. ALL behind the same live/default-OFF Reduce black bars during headset rotation flag. No new settings/IPC layout. IPC47101/providerABI45601 retained. FSR/AFW SDK/shaders/providers and output cadence retained; ON changes shared OpenXR prediction/camera timing as requested. Existing meter/counters/auto-arm and R22/R23 retained. See [SCOPE](docs/V472_BLACKBAR_TIMING.md). Older no-timing-import instructions are superseded only for this owner-requested opt-in scope.

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

V454 derives directly from sealed V452. Its Vulkan implicit queue layer uses
one stable mutex per actual VkQueue, released after the driver returns. Calls
on the same queue remain serialized; independent queues may progress while
another queue is in WaitIdle/Present. GPU resource ownership, transport fences,
native camera/reset validation and rendering/cadence remain as in V452.
The new V453 GPU timing instrumentation is not carried forward. See
docs/V454_VULKAN_QUEUES.md for the failing baseline regression and limits.

Isolated laboratory trial, based on Elliot Tate's native-dlss-fg commit
`135185a033453ae7f844b867c7ef34ae26d40ab3`, with the accepted V33034 OFXR,
AFW and Neural Rendering source from `6c36e598b9027a852ce2b759a1851378468a0938`.
This trial does not change the original OFXR/OptiScaler sources or installations.

V452 distinguishes copied real native outputs from GPU-confirmed interpolation.
A private pair receipt retains the NGX GPU disable-interpolation decision across
work-slot reuse. Refused pairs remain real-copy bypass and are not extra content
for the counter. Ordinary live logs include native record reasons, NGX evaluate
results and completed GPU decisions. Native ready diagnostics come from presented
content instead of unrelated auxiliary-chain guide matches. See
[content and evidence](docs/V452_NATIVE_CONTENT.md).

## Runtime

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

V449 persists distinct preferences for four OFXR engines, FSR and native DLSS-G.
Switching routes restores their own options; stale edits merge into their original
route. Logging remains a global live session control with actual runtime status.
See `docs/V449_ROUTE_SETTINGS.md`. This does not change the renderer/guide rules.

V448 aligns the private FSR provider with the SDK runtime used by embedded
OptiScaler's FSR tests and adds bounded input diagnostics for ordinary VR.
Returnal rotation artifacts are owner-reported on V447; their visual correction
is pending game validation. See [the evidence and limits](docs/V448_FSR_PROVIDER.md).

V447 observes Streamline 1.x and 2.x through their distinct SDK declarations.
The loaded-image version and frame-token export must agree before hooks install;
unknown ABIs remain unmodified. Camera ownership is exact per frame/viewport,
with nested thread-local restoration. See `docs/V447_RETURNAL_STREAMLINE.md`.

V446 retains pending admission when a paused output-shape change retires an
already-admitted private buffer set. A later enable uses the existing creation
path and its refusal/budget gates, followed by fresh temporal priming. See
`docs/V446_CALLISTO_SWITCH.md`. All V445 renderer and guide rules remain.

V445 selectively ports renderer fixes through upstream 16b7096; it does not
replace the selected base or import the new upstream scheduler. See
`docs/V445_UPSTREAM_INTEGRATION.md` and its full commit audit. Guide snapshots,
recording/fence ownership and the accepted Luke correction remain authoritative.


Lower VRAM is an explicit 0/1 checkbox, default 0 on every route. Legacy -1
profiles normalize to off; explicit on remains on. Both UI surfaces and the
generated runtime INI own this same setting.

Native DLSS-G, like FSR, requires exact stereo motion/depth guides before
private acquisition, priming or worker admission. Missing guides submit the
whole real frame through the existing physical XR owner. Native work slots
reset the OFA-only list only when that list exists. Passive typed SR/RR input
observations run independently of GPU guide capture; their presence is not
proof of an owned, temporally valid stereo guide pair.

Ordinary three-slot generation admits only frames with committed release
captures for every mapped eye resource. Missing capture tickets use the complete
real stereo submission, retire temporal continuity and retain the existing
physical frame-call owner. A new complete stereo frame primes before pairing.
Release-time history capture remains nonblocking; GPU ownership is never bypassed.

The tray configures an independent OpenXR layer. A separate ImGui/D3D11 settings
window publishes one versioned configuration through a named shared mapping.
An in-headset ImGui menu uses the existing FPS compositor path and the same
widgets/IPC. Each UI merges only its edited fields under the mapping mutex,
preserving concurrent edits from the other location. F10 toggles the menu;
Escape, desktop focus loss, XR focus loss, disarm and session teardown close it.
Input uses a virtual cursor in the same 1024x768 framebuffer as hit testing and
drawing; no desktop/mirror rectangle is projected onto the headset panel.
The independent menu quad uses FPS's VIEW space, preserves app layers and
does not enter frame generation or temporal histories. On a one-spare-layer
runtime the menu temporarily has priority over the FPS counter.
The game process owns NGX interception, immutable guide snapshots, synthesis,
AFW and NR GPU work. No OptiScaler DLL, proxy runtime, broad upscaler replacement
or host menu is required. Assetto CSP's special producer is excluded.

Included routes: FidelityFX optical flow, NVIDIA reference, NVIDIA new 0.3.0,
NVIDIA DJ 0.75, FSR3 frame generation, native NVIDIA DLSS-G, Luke AFW all 4 modes,
and NR before/after native super resolution with the accepted codec controls.
The accepted depth-aware Luke correction remains the only functional AFW motion
path: subtract native camera/stereo motion, reconstruct the same-eye interval,
feed corrected motion as Normal. Missing exact camera/guide identity rejects
the AFW preparation rather than inventing or borrowing another frame's camera.

D3D12 ingress observes native SR/RR creation and exact submission recordings.
Generic D3D11 ingress freezes guides and transports NR through the authoritative
D3D12 owner. Vulkan ingress uses native prefix/continuation command segments,
external-buffer transport and exact timeline fences; it restores supported
native state before subsequent application commands. Unsupported Vulkan state,
missing image transfer capability or an unobserved recording is refused before
mutation. Raw DLSS-G and NR feature handles never enter SR/RR guide processing.

Native NVIDIA FG supports 2X and hardware-gated 3X. On a 2X-only adapter a saved 3X
request runs 2X with a status message. Native interpolation metadata uses the
same fixed fractions as NGX. OFXR DJ retains its independent 3X scheduler.

## Isolation and dependencies

Profile: `%LOCALAPPDATA%/OFXR Autonomous`. Unique OpenXR/Vulkan layer identities
and profile-scoped registrations prevent cleanup from touching the original
tray. Startup is disarmed. `--settings-only` previews controls; automated preview
does not activate, register a layer, launch a game or replace a game DLL.

The package supplies the native DLSS-G runtime, private FSR FG alias, official
AFW beta5 provider and the small FG/NR forwarders. NVIDIA's `nvngx_dlssnr.dll` remains
an external requirement, as in the accepted source. The forwarder looks beside
the layer and game executable; an externally supplied model in package `ofxr/`
is staged only when the user activates the tray. NR requires a native SR/RR call.
Luke AFW reads the active game's `RealVR.ini`, including its actual WorldScale.

Vendor SDKs and selected static libraries live in `work/shared/ofxr-autonomous/deps`,
separately from the source/checkpoint. NVIDIA DLSS SDK commit:
`374959484e79a640feaba44c93ac8cfb0a03f5b5` (310.9.1). Khronos headers are pinned
to `d64e9e156ac818c19b722ca142230b68e3daafe3` (Vulkan 1.4.310). No dependency
needs the OptiScaler checkout to remain present to build this trial.

## Validation

Build and automated validation results are recorded in `VALIDATION.md`.
Hardware/runtime/game validation are distinct:
fake OpenXR/Vulkan dispatch checks prove ownership and API behavior, WARP tests
prove shader/transport behavior, and hardware tests exercise actual providers.
No in-game validation or live installation is performed in this session.

Injection after the first NGX initialization can prevent native FG discovery.
The two-module RTX 4090 probe demonstrated first-init provider path success and
late second-init FeatureNotFound/CreateFeatureUnableToInitialize failure on the
same device. Missing pre-injection guide ownership can independently prevent
native FSR/NR/AFW preparation. V445 did not implement late startup recovery; V451 adds the narrowly validated
private FG provider route described above.
