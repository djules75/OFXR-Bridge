## V578: native DLSS-G call-contract diagnostic

Diagnostic on V577; no visual fix claimed. Full F8 now records the exact NGX
reset/pair options and the seed motion/depth resources. Failed local scale and
camera experiments were removed; generation math and SteamVR delivery stay at
V577. See [scope and one next capture](docs/V578_DLSSG_CALL_CONTRACT.md).
12 focused checks pass. IPC57701/provider45601; normal benchmark/memory controls
and the local-only owner spreadsheet adapter are unchanged. Local only.

## V577: combined Lower VRAM and memory-aware suites

One Lower VRAM control, no Luke effect, Normal/VRAM ordinary tests and unchanged
18-quality Luke suite. End-of-trial process VRAM/RAM snapshots work with F9 off.
Experimental features is under Display. See [current methodology](docs/V577_LOWER_VRAM_BENCHMARK.md).
IPC57701/provider45601. Restart matching tray/runtime. DLSS-G output capture
is retained; no visual fix is claimed.

## V576 diagnostic: native DLSS-G output capture

V575 delivery is retained. Full F8 now includes the actual native DLSS-G
inputs, generated pixels and composed output. No visual repair is claimed.
See [scope and one capture](docs/V576_DLSSG_OUTPUT_CAPTURE.md).

## V575 local: precise final delivery wakeup

The common SteamVR consumer wakes its timer early and checks the final bounded
deadline window with QPC. This removes accumulated timer overshoot while keeping
physical minimum spacing and raw OpenXR predictions. Headset result pending.
See [precision evidence](docs/V575_PRECISE_DELIVERY_WAKEUP.md).

## V574 local: prepare during the refresh interval

The common SteamVR consumer now prepares the next output before the deadline
and waits only the residual interval. This removes the full-period-plus-copy
ceiling while retaining V573's isolated GPU queue and the raw OpenXR prediction.
Headset performance is pending. See [delivery budget](docs/V574_OVERLAP_DELIVERY_PREPARATION.md).

## V573 local: isolate final SteamVR GPU delivery

The D3D12 private producer and real OpenXR consumer now use different queues,
so later producer waits cannot hold an already-ready output's final copy.
OpenXR call order and raw runtime prediction remain. Actual headset recovery
is pending. See [GPU delivery details](docs/V573_PRIVATE_GPU_DELIVERY.md).

## V572 local: OpenXR handoff after distributor pacing

The common SteamVR consumer now waits before real Wait, keeps Begin before its
GPU copy, and uses the exact runtime prediction in End. VDXR retains its path.
Headset loss recovery is pending. See [handoff details](docs/V572_OPENXR_HANDOFF.md).

## V571 local: private SteamVR delivery

Common private-image delivery across existing generators; bounded FIFO and one physical refresh between runtime handoffs. VDXR retains its path. Runtime/headset verdict pending. See [architecture and trial](docs/V571_STEAMVR_PRIVATE_RUNTIME.md).

## V567 local: OptiScaler bootstrap restored

Based on sealed V566. A loaded compatible OFXR-aware OptiScaler owns the XR
chain through the existing embedded renderer. Its SR/NR/OFXR controls stay in
the OptiScaler in-game menu. Without that host, Autonomous keeps its V566 route
and counters. No second global layer registration is needed. Headset validation
pending; see [scope and checks](docs/V567_OPTISCALER_BOOTSTRAP.md).

## V558 local: automatic benchmark suites

Display now has optical-only, complete C-only, and Luke Ross benchmark buttons.
10s per applied configuration; discard first3s only. Optional CSV includes FPS
and Cycle estimated min/median/max, works with F9 off and menus hidden. Luke
exports each route as blank/1x1/2x2/4x4, including AFW-only.
See [suite instructions and validation](docs/V558_AUTOMATIC_BENCHMARK.md). IPC55801/provider45601.
Local candidate; headset/game validation pending. Shared main remains V557.

## Current shared endpoint: V557 (2026-10-17)

Owner-authorized V557 publication supersedes earlier local-only entries.
See [V557 release scope, validation and collaborator retest](docs/V557_RELEASE.md).
PureDark AFW v1.0-beta.6 is bundled. CMake version 0.557.0; implementation V557.
This endpoint includes live AFW quality and the current SteamVR delivery/meter
work. V536 remains the accepted fallback; broader runtime retesting is planned.
Earlier version sections below are historical, not the current release policy.

## V536 accepted for main (2026-10-15)

The owner confirms that V536 is OK and explicitly requests main alignment.
[V536 acceptance](docs/V536_ACCEPTANCE.json) records the exact sealed source/tag and
ZIP hash. Preserve those artifacts unchanged; the earlier runtime-pending
entries below describe the release before this verdict. The accepted renderer
is exactly V536, with no additional runtime change for this integration.

Next work is OFXR performance inside Luke Ross. The paired AFW marker read
optimization does not resolve the separate route-generation/delivery slowdown;
no quantified FPS recovery or all-game/runtime validation is claimed.

## V536 AFW beta6 and paired Luke marker readback (2026-10-15)

Owner requests the next KCD2/Luke performance build and explicitly asks to
update private Git. V536 exports the surviving V535 source on a fresh branch
from current main; local V533-V535 trial commits/tags/manifests are not imported.
Withdraw C1/R19, C2/R26 and C3/R35 from the active renderer; keep V532 labels,
V533 meter corrections, the exact-limit Luke/native-D11/SteamVR inline fallback,
and cold Luke private-NGX capacity deferral. Their headset effects are not
reclassified as independently accepted: the owner changed overlays as well.

New work: official pinned PureDark AFW beta6; full/2x2/4x4 Combined shading
control (NEXT GAME LAUNCH); batch both already-frozen eye barcode reads on one
queue submission/wait, previously Vulkan-only. No new image of latency, guessed
eye token, raw/cross-eye guide substitution, or change to accepted KCD603 /
Indiana701 depth-plus-camera-subtraction mathematics. A/B policy/counts retained.
Post-AFW Hybrid/native-vector OFXR reports optical-flow fallback; reconstructed
eye still has no native stereo guide pair for FSR/DLSS-G.

Beta6 integration needs ABI ShadingRate at132, reserved136, sizeof184; a bounded
null-output configuration handshake; and a scoped renderer Copy adapter for
its invalid full-image copies between W/H and W+1/H+1 color history. The adapter
uses region copies and clamps the single border, with no code or command-list
vtable patch. Full-rate beta5 control passed; unadapted beta6 failed GPU validation.
Live shading changes separately failed resource-state validation. Do not enable
hot switching: each AFW instance pins its initial shading rate, active/pending
are separate in UI/benchmark metadata. Restart the game to compare rates.

KCD run: 2987x4272 eye images. Disabled/AFW-off often84-90FPS; AFW-only often
53-58FPS (not a controlled same-scene benchmark). Retained final marker reads:
7318 waits vs3635 evaluations; median2.723ms/p9012.567ms per wait. These are CPU
GPU-rendezvous durations, not the GPU kernel cost or a promised additive gain.
Earlier generator timings have rolled out; no generic pacing fix is claimed.
See [V536 implementation/validation](docs/V536_AFW_BETA6.md).
IPC53601/provider45601. Runtime and performance validation in headset pending.

## V532 benchmark-label diagnostic follow-up

Owner requests fixing lost labels before repeating AC Valhalla. The logger
now validates UI and embedded renderer revisions independently, including
applied PreferFPS/3X. Pending pipeline/renderer settings stay excluded.
Untried routes are n.a.; attempted short/invalid trials remain n.d.
Current exporter included in the ZIP: Meganex startsS (RwithoutOFXR separate),
first3/last3seconds excluded, Cycle estimated, portable headers retained.
Based on selected V531 compatibility source; no renderer/pacing/AFW changes.
See autonomous/docs/V532_BENCHMARK_LABELS.md. Owner repeat-run validation pending.

## V531 compatibility update on V530

Selective Elliott R19 private D12 factory/serializer, R26 original deferred-D11
guide identity, and R35 ordinary array history for the existing composer.
R26 is justified by an actual V530 wrong-publication negative. Release checks:
new88/88, Djules25/25; shader/provider/queue/pacing contracts retained. This
system cannot exercise recovered-factory GPU work; healthy GPU and safe
unavailability are tested. Named-game recovery is not claimed.
See [V531 scope and evidence](docs/V531_COMPATIBILITY.md). The owner authorizes
private sharing and the updated English audit without dedicated named-game tests.
V530 A/C controls and final benchmark exporter remain included.

## V530 DLSS-G preset test prerelease

Owner requests and authorizes private publication for immediate testing.
Desktop/F10 DLSS-G preset dropdown: A (slower), C (faster), Game default.
Desktop chooses and remembers the actual game executable; F10 targets its host.
Per-game NVIDIA DRS selection is verified and requires a game restart. C needs
the updated NVIDIA model/driver; no universal performance gain is claimed.
Benchmark logs preserve process-start preset requests, not active-model proof.
Current exporter uses Meganex S:BF (40cells), A at AW:AZ / C at BA:BD,
Quest G:N, Pimax BI:BR (10cells, separate A/C rows). Cycle estimated selected.
Details: autonomous/docs/V530_DLSSG_PRESETS.md. V529 test instructions remain
applicable with this mapping/restart correction. HUD remains paused.

## V529 private collaborator test prerelease

Start with the [human and AI test handoff](../docs/ofxr-autonomous/V529_COLLABORATOR_AI.md). The owner explicitly
authorizes this endpoint for private Pimax testing. Headset acceptance remains
pending. Download the unchanged sealed V529 ZIP and the current exporter from
the [private release](https://github.com/tig3rmast3r/ofxr-development/releases/tag/ofxr-autonomous-v529-shared).
F9 records compact benchmark logs; hold each combination20-25s, keep the scene
and runtime settings fixed, then export Output FPS with Cycle estimated.
Earlier version sections below are historical development evidence.

## V527 demand output allocation

Djules2X/3X no longer reserves six XR handles per source: full allocation is
four, reduction is two. Owned extra synthetic textures exist only in3X and
are freed on returning to2X. No hidden transport flags. Separate synthesis
chooses private compute textures; Reduce XR swapchains halves XR handles.
Direct3X protects both independent synthetic descriptors through submission;
direct/reduced retains the2X restriction. PreferFPS remains for ownerSteam tests.
Close oldtray/game and startV527. SameGhostwire UEVR scene/settings/Hz,
AFW/Neuraloff: Djules2X then3X with both flagsOFF; then separationON/reductionOFF
and ON/ON. Compare correctness, CycleLength/Estimated and OutputFPS. No dump.
Headset and latency result pending. See docs/V527_DEMAND_OUTPUT_CAPACITY.md.
Earlier entries below describe historical candidates.

## V526 independent output transport experiment

Separate synthesis and Reduce XR swapchains are independent per-engine flags
for applicable native D3D12 routes on SteamVR and VDXR. Test On/Off, then On/On
first; Off/Off and Off/On are additional controls. Old Lower VRAM profiles
migrate both on; explicit separation off remains off. Start a fresh V526 tray
and game because the shared control ABI changes. Same UEVR scene/settings/Hz,
AFW/Neural off,2X: record Cycle length, Cycle estimated and Output FPS. No dump.
Direct reduced output is bounded 2X. AFW/D11/Vulkan transport is unchanged.
Latency gains remain unmeasured. See docs/V526_INDEPENDENT_OUTPUT_TRANSPORT.md.
Earlier sections below describe historical candidates.

## V525 Reference latency candidate

0.2.1 is latency priority:44ms cycle target, not a claimed result. Native
D3D12 SteamVR Reference captures the next frame without globally waiting for
the preceding pair when runtime/application queues are separate. No added
output buffering. Virtual Desktop and New/Djules keep their policies.
Fresh V525 Ghostwire UEVR, same scene/settings, Neural/AFWoff, F9 before
Reference. Compare Cycle length/Cycle estimated and OutputFPS with V524.
Shortest correct cycle matters before FPS. No dump. Headset validation pending.
See docs/V525_REFERENCE_LATENCY.md. Earlier entries are historical.

## V524 local SteamVR candidate

Output FPS counts presented compositor frames without subtracting late vsyncs.
New recovers a bounded second-eye output refusal; Reference waits for its exact
GPU output before starting the runtime cycle, on the consumer only.
Fresh V524 tray/game, same Valhalla settings, AFW/Neuraloff; compare New,
Reference and Djules in one location, F9 before the comparison. Check fluidity
and Game/Generated/Output together. No image capture. Headset result pending.
See docs/V524_STEAMVR_PRESENTATION.md. Earlier entries are historical.

## V523 local candidate: SteamVR output lifecycle

V522 rejected by owner. Fresh V523 tray/game, same Valhalla settings, F9 before
loading; reopen F10 in the level, then New -> Djules -> Reference -> New.
Staged restart, Reference stalled-output exposure and unused auxiliary-output
retirement. Headset result pending; no dump needed. See docs/V523_STEAMVR_LIFECYCLE.md.
Earlier candidate entries are historical.

## V522 local candidate: Valhalla / Meganex / SteamVR

Fresh V522 tray/game, same REAL VR settings, OFXR on and AFW/Neural off.
Enable F9 before New -> Reference -> Djules -> New, and reopen F10 after each
route. Existing staged Djules output and Reference physical display-clock fixes;
menu refusal recovery and diagnostics. Owner headset result pending.
See docs/V522_STEAMVR_ROUTES.md. Earlier candidate entries are historical.

## V521 local candidate — SteamVR OFXR recovery

Based on V520. Prepare a fresh game launch with the V521 tray. In Valhalla keep the same REAL VR resolution/stereo settings, OFXR on and AFW/Neural off; enable F9 before switching Reference 0.2.1 to Djules 0.2.13. Check whether generation starts and recovers after returning to Reference or toggling OFXR off/on. Game validation pending. Details: docs/V521_STEAMVR_PROJECTED_BUDGET.md. Earlier entries below describe historical published versions.

## Current shared Autonomous endpoint: V520

V513 pre-HUD baseline + selected KCD V603 in V519 + accepted Indiana V701 in V520. Combined game validation pending; original KCD V603 is owner-accepted and matches OptiScaler (confirmed 2026-10-14). Private release: https://github.com/tig3rmast3r/ofxr-development/releases/tag/ofxr-autonomous-v520 . See V520_SELECTED_INDIANA_INTEGRATION.md and the shared branch provenance records. HUD experiments remain local and paused.

## Historical shared Autonomous endpoint: V519

Owner-selected KCD V603 merged onto pre-HUD V513. Combined runtime validation pending; original KCD V603 is owner-accepted (confirmed 2026-10-14). See autonomous/docs/V519_SELECTED_KCD_INTEGRATION.md and docs/ofxr-autonomous/KCD_SELECTED_DELTA.md. HUD experiments remain local.

## Historical shared Autonomous endpoint: V513

Owner-selected export through V513. See [integration record](/docs/ofxr-autonomous/ACCEPTED_INTEGRATION.md) and the accepted-build provenance map. Earlier entries below describe historical checkpoints; V514-V518 HUD experiments remain local.

Current shared routing: read [shared Autonomous handoff](../docs/ofxr-autonomous/README.md). Build with `tools/Build-Autonomous.ps1` from the shared repository root; prepared dependencies are in `.local/ofxr-autonomous-deps` and outputs in `builds/ofxr-autonomous-new`. Earlier laboratory build paths below are historical.

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

Run `OFXRBridgeTray.exe`: it starts **disarmed**. Double-click the tray icon to open settings, choose **Activate** explicitly, then start a fresh OpenXR game. `--settings-only` opens the panel without activating or registering the layer. Preferences and runtime cache use the separate `%LOCALAPPDATA%/OFXR Autonomous` profile. See [Testing instructions](TESTING_IT.md) and the [runtime contract](AUTONOMOUS_CONTRACT.md).

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
