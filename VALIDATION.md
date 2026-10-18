## V597: actual End phase and native receipt headroom

33 focused checks pass. Exact V596 fails the new final-call phase assertion;
V597 passes D11/D12 actual-GPU tests with350us of native submission work before
vsync.10000-refresh clocks keep the exact period and fixed1ms lead with no
accumulation. Headset improvement pending. [Evidence](docs/V597_VALIDATION.json).

## V596: copied-source reuse with protected native targets

33 focused checks pass. Exact V595 fails the new single-source progress case;
V596 passes with actual D11/D12 GPU copies, exact generations/pixels and native
chain lifetime checks. Owner Valhalla validation pending; remaining MSFS drops
are outside this build's scope. [Evidence](docs/V596_VALIDATION.json).

## V595: diagnostic clock independence

33 focused checks pass, including D11/D12 native renewal and exact FIFO pixels
with optional diagnostic tracing off. Two earlier fallback cadence failures are
preserved; exact-V594 baseline, isolated current cadence and the subsequent full
current suite pass without a timing/threshold change. Private-runtime policy is
unchanged from V594. Owner game validation pending. [Evidence](docs/V595_VALIDATION.json).

## V594: bounded preventive native renewal

33 focused checks pass; exact V593 fails the new preventive-renewal assertion.
Actual SteamVR source submissions are uniquely matched to receipt IDs; controlled
4096px comparison 877/901 versus 898/901, final-source repeat 900/901. Isolated
losses remain; owner game validation pending. [Evidence](docs/V594_VALIDATION.json).

## V593: native cycle freshness

33focused checks pass. ExactV592 fails the new native-cycle assertion. Actual
SteamVR paired15Hz content comparison:282/361versus361/361unique submissions
shown, all361matched in each run. Full Valhalla/Luke acceptance pending;
sporadic losses at higher rates remain. [Evidence](docs/V593_VALIDATION.json).

## V592 — physical clock and packet eligibility

33focused checks pass. ExactV591 fails the new actual-GPU/mocked-XR phase
contract. D11/D12 preserve exact FIFO pixels; late physical-clock activation
is covered. V591 external audit confirms28/200worst-window losses. V592
headset comparison pending. [Evidence](docs/V592_VALIDATION.json).

## V591 — native margin scope

33focused checks pass, including new D3D11/D3D12 late-preparation and late-native
cases. Exact V590 fails the new regression check. [Evidence](docs/V591_VALIDATION.json).
V591 headset improvement remains pending.

## V590: independent native Wait and held presentation margin

Overlap the next nativeWait with current delivery. Missing headroom becomes
exact retained presentation latency; physical cadence and producer ownership
remain unchanged.33focused checks pass; headset improvement remains pending.
See [details](docs/V590_PIPELINED_NATIVE_WAIT.md).

## V589: add1ms to the native-entry margin

Owner-selected V588 comparison: open the native SteamVR cycle1ms earlier,
within the existing one-period cap. Physical handoff frequency and producer
policy are unchanged.33focused checks pass; headset comparison is pending.
See [details](docs/V589_EXTRA_NATIVE_LEAD.md).

## V588: reserve changing native runtime work before delivery

Include native Release and measured service-time variation when opening the
SteamVR cycle. Physical cadence, preparation, images and producer policy stay
unchanged.33 focused checks pass; MSFS headset comparison remains pending.
See [details](docs/V588_RUNTIME_BUDGET.md).

## V587: changing shared images no longer drain preparation

Prepare the next real image while retaining the previous released generation;
release only the completed FIFO head on delivery. Read physical Hz atomically.
No additional swapchains/images or cadence change.33 focused checks pass;
actual MSFS headset improvement remains pending.
See [details](docs/V587_PREPARED_RELEASE.md).

## V586: final SteamVR delivery priority

Request supported HIGH priority for the existing dedicated D3D12 delivery
queue, matching synthesis. Preserve independent preparation, cadence, refill
and swapchain count. F9-only substages separate preparation waits.33focused
checks pass; headset improvement remains pending.
See [details](docs/V586_DELIVERY_PRIORITY.md).

## V585: automatic presentation margin

Retains V584 independent preparation. Genuine late input builds a retained
refill margin after the late frame; native runtime cycles are selected near
handoff with an adaptive call budget. Same three credits, exact physical
refresh minimum, all available input retained. Headset result pending.
See [details](docs/V585_ADAPTIVE_PRESENTATION.md).

## V584: independent SteamVR preparation

Real-image acquisition/copy/GPU waits move to a separate preparer; the physical
clock consumes completed packets. Same bounded3credits and strict refresh
spacing, no extra XR swapchains, immutable overlays shared. VDXR/AFW/presets
unchanged. Headset FPS result pending.
See [details](docs/V584_INDEPENDENT_PREPARATION.md).

## V583: selected-preset benchmark, A initial preference

Complete tests keep the startup preset instead of forcing C or requesting a
restart. A is the initial next-launch preference; explicit C/game-default
choices remain available. CSV records the actual startup request. Adds the100us early-preparation experiment to V582.
See [details](docs/V583_SELECTED_PRESET.md). Owner comparison pending.

## V582: selected-preset benchmark, A initial preference

Complete tests keep the startup preset instead of forcing C or requesting a
restart. A is the initial next-launch preference; explicit C/game-default
choices remain available. CSV records the actual startup request. Pacing remains V581/V579; no V580 experiment.
See [details](docs/V582_SELECTED_PRESET.md). Owner comparison pending.

## V581: desktop preset refresh

Fix a cached empty/stale DLSS-G game target while desktop settings remain open.
Manual Refresh resolves the remembered game again; visible desktop controls
also detect target changes once per UI second. Profile edits remain explicit.
Based on V579; V580 early-handoff experiment excluded for a separate comparison.
See [details](docs/V581_OFFLINE_PRESET_REFRESH.md). Owner desktop test pending.

## V579: fresh single INI and gaming defaults

One user `ofxr-bridge.ini`, fresh defaults without legacy import. Offline native
DLSS-G preset selection remembers the connected game; no EXE chooser. Driver
rejection of C explicitly retries A. F9 off disables diagnostics; F8 requires
F9. Benchmarks and their GUI controls default off behind `[benchmark] enabled=0`
in the same INI; enabling them still works with F9 off. No interpolation/AFW/
private distributor change.17 focused checks pass; headset A/C check pending.
See [details](docs/V579_GAMING_CONFIGURATION.md). Local candidate only.

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

## V575 engineering validation

35 focused checks pass in26.30seconds. The ready-queue precision fixture moves
from85.993FPS on exactV574 to89.981FPSD3D12/D3D11 onV575, preserving strict
physical minimum spacing, real GPU pixels, raw OpenXR time and queue isolation.
DownstreamXR is mocked; headset recovery is pending. Initial call-edge spacing
failures were fixed without relaxing assertions. See
[V575_VALIDATION.json](docs/V575_VALIDATION.json).

## V574 engineering validation

35 focused checks pass in25.33seconds. A precise4ms preparation fixture fails
against sealedV573 at16.768ms mean spacing and passesV574 at11.556msD3D12 /
11.726msD3D11. FIFO ownership, raw prediction, Begin-before-GPU, physical minimum,
dedicated-queue independence, route switches and VDXR/AFW controls pass.
DownstreamXR is mocked; actual game recovery remains pending. See
[V574_VALIDATION.json](docs/V574_VALIDATION.json).

## V573 engineering validation

35 focused checks pass in25.31seconds. A real GPU fixture distinguishes
shared-queue blocking from independent final delivery when future producer
work is held. A production xrCreateSession fixture verifies the actual runtime
binding queue. Existing routes, FIFO, ownership, VDXR and AFW controls pass.
The downstream runtime is mocked; headset recovery remains pending. See
[V573_VALIDATION.json](docs/V573_VALIDATION.json).

## V572 engineering validation

34 focused checks pass in 23.56 seconds. The common SteamVR consumer paces
before real Wait, keeps Begin before real image preparation, and uses the
runtime's exact prediction. Actual GPU fixtures cover all existing routes,
D3D11/D3D12, FIFO ownership, blocked GPU/runtime, route switches and unchanged
VDXR/AFW controls. The downstream runtime is mocked. See
[V572_VALIDATION.json](docs/V572_VALIDATION.json); headset loss recovery pending.

## V571 engineering validation

34 focused checks pass, including actual GPU private endpoint/generators and unchanged VDXR/AFW controls. Exact evidence and fixture reruns: [V571_VALIDATION.json](docs/V571_VALIDATION.json). The downstream runtime is mocked; actual SteamVR visibility, cadence and performance remain owner-pending.

## V567 bootstrap validation

Shipping optimized DLL: compatible fake host and sealed real OptiScaler V33036
reach downstream instance creation through the layer-name adapter, with one
chain advancement and unchanged input entries. Identity, capability, activation,
SEH, callback ownership, concurrency and process exclusion guards are covered.
Native loader/call-chain/provider API and V566 meter/benchmark regressions pass.
Exact results: docs/V567_VALIDATION.json. Assetto/headset SR, NR and embedded
OFXR remain owner-pending. No live install, registration, push or publication.

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

## V536 targeted release validation

49/49 CTest in43.89seconds plus Python benchmark exporter pass, zero skips.
Real beta6 provider pixels/camera/motion, full/2x2/4x4 shading and deferred
changes, paired marker GPU/CRC/timeouts, KCD/Indiana quality, both Vulkan
bootstraps, NGX startup, Outlaws bootstrap, post-AFW stereo/quad, native D11,
SteamVR auxiliary recovery, meter/UI/IPC,24 native transport A/B/runtime cases.
Headset behavior and performance gains remain pending. V536_VALIDATION.json
records the exact scope and checks. The hot shading-switch experiment failed
GPU state checks and is excluded; next-launch selection is tested instead.

## V532 benchmark-label diagnostic follow-up

Owner requests fixing lost labels before repeating AC Valhalla. The logger
now validates UI and embedded renderer revisions independently, including
applied PreferFPS/3X. Pending pipeline/renderer settings stay excluded.
Untried routes are n.a.; attempted short/invalid trials remain n.d.
Current exporter included in the ZIP: Meganex startsS (RwithoutOFXR separate),
first3/last3seconds excluded, Cycle estimated, portable headers retained.
Based on selected V531 compatibility source; no renderer/pacing/AFW changes.
See autonomous/docs/V532_BENCHMARK_LABELS.md. Owner repeat-run validation pending.

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

63/63 focused optimized Release checks passed in55.47s, including24 flag/engine/
runtime2X cases, six3X cases, and four multiplier/deep readiness GPU checks.
The packaged loader, binary hashes and matching PDB are verified on delivery.

ExactV526 source fails both no-flag2X/3X fixtures under the bounded projection
budget. Candidate checks all24 flag/engine/runtime2X combinations, six3X
Steam/VDXR cases, live direct/staged3X->2X->3X retirement, and staged2X deep
readiness/repeats. Actual GPU constant-color readback verifies both3X
interpolation slots while direct submissions are delayed and the producer
attempts reuse. Exact counts/rawlogs are in package evidence/APPLICABLE_CHECKS.json.
MockXR tests do not establish headset success/FPS/latency. PreferFPS unchanged;
ownerSteamVR test pending. The unchanged interactive foreground-input fixture
is outside this focused selection; actual D11/D12 menuGPU checks included.

## V526 independent output transport

53/53 focused Release checks passed in48.97s, including24/24 GPU matrix cases.
Restoring the former coupling in a temporary mutation makes the separation-only
GPU case fail; restoring the candidate passes. Raw initial UI-label fixture
failure is retained alongside the corrected final run. No production change
was made to satisfy an unrelated input/foreground fixture.

The focused optimized Release suite covers all24 nativeD12 GPU combinations
of three NVIDIA engines, SteamVR/VDXR policy and independent flags. Eight-real/
seven-synthetic stereo pixel readbacks, auxiliary ownership, exact XR budget,
cycle/order accounting, debug errors and teardown remain checked. Live output
shape/profile replacement and migration/restart checks are included. Runtime
policies use a mocked XR dispatch; this does not measure vendor compositor
latency. Raw logs and final counts are in package evidence/APPLICABLE_CHECKS.json.
The unchanged interactive foreground-input fixture blocked on V525 is outside
this focused selection; actual menuGPUchecks are included. No input-hook change.
Owner Ghostwire/VDXR performance and visual acceptance remain pending.

## V525 Reference capture overlap

32 applicable checks pass. One unchanged interactive menu-input fixture fails
to acquire Windows foreground, also on retry; retained as environment-blocked.
Actual D11/D12 menu GPU checks pass. See V525 evidence/APPLICABLE_CHECKS.json.

V524 fails actual next-image Release while its preceding pair is GPU-blocked.
V525 passes eight-real/seven-synthetic stereo and poisoned auxiliary ownership,
without blocking the producer or adding output slots. Route policy verifies
VirtualDesktop/New/Djules/AFW/FSR/staged exclusions. Focused Release checks and
packaged loader are bundled.44ms cycle is an owner target, not a measured gain.

## V524 targeted SteamVR validation

V523 production fails independent six-handle New recovery and exact Reference
GPU readiness fixtures. Fixed fixtures retain stereo pixels and auxiliary
ownership, and the producer returns before the GPU gate releases. Presentation
accounting covers76/90 cadence, repeated45FPS scanouts, actual omissions and
pending tails. Focused final Release suite and packaged loader evidence are
bundled. In-game gain and smoothness remain unverified.

## V521 local candidate — SteamVR OFXR recovery

Based on V520. Prepare a fresh game launch with the V521 tray. In Valhalla keep the same REAL VR resolution/stereo settings, OFXR on and AFW/Neural off; enable F9 before switching Reference 0.2.1 to Djules 0.2.13. Check whether generation starts and recovers after returning to Reference or toggling OFXR off/on. Game validation pending. Details: docs/V521_STEAMVR_PROJECTED_BUDGET.md. Earlier entries below describe historical published versions.

## Current shared Autonomous endpoint: V520

V513 pre-HUD baseline + selected KCD V603 in V519 + accepted Indiana V701 in V520. Combined game validation pending; original KCD V603 is owner-accepted and matches OptiScaler (confirmed 2026-10-14). Private release: https://github.com/tig3rmast3r/ofxr-development/releases/tag/ofxr-autonomous-v520 . See V520_SELECTED_INDIANA_INTEGRATION.md and the shared branch provenance records. HUD experiments remain local and paused.

## Historical shared Autonomous endpoint: V519

Owner-selected KCD V603 merged onto pre-HUD V513. Combined runtime validation pending; original KCD V603 is owner-accepted (confirmed 2026-10-14). See autonomous/docs/V519_SELECTED_KCD_INTEGRATION.md and docs/ofxr-autonomous/KCD_SELECTED_DELTA.md. HUD experiments remain local.

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

# V477 validation

See docs/V477_LUKE_CONTEXT.md and docs/V477_VALIDATION.json. Focused19/19 and actual split-eye loader passed; Horizon headset test pending. No full unrelated Djules suite; providers/shaders retained.
