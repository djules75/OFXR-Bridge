## V595: diagnostica senza ricampionare il clock

F9 legge la cache vsync del director senza aggiornarla con una chiamata
aggiuntiva. Clock e rinnovi rimangono attivi con logging spento.33 controlli
passati, inclusi D11/D12 con diagnostica disabilitata. Il costo ordinario del
logging rimane; i drop osservati con F9 spento non sono dichiarati risolti.
Per la prossima prova usa la modalità normale 2X con Lower VRAM spento.
Lower VRAM e3X rimandati come richiesto. Nessun F8 o benchmark completo necessario.

## V594: consegna SteamVR, rinnovo preventivo limitato

La produzione nella run V593 rimane stabile: restano perdite nella consegna.
Prova controllata4096px:877/901 mostrati con V593,898/901 con questa gestione;
la ripetizione sul sorgente finale mostra900/901. Non dichiarare zero perdite.
Stesso livello Valhalla,0.3.0 2X,LowerVRAM e Neural spenti,F9 per30secondi nel
livello,poi chiudere normalmente. Nessun F8 o benchmark. Correzione comune
SteamVR,compreso Luke Ross con AFW acceso/spento; verifica nel gioco pendente.

## V593: prova della consegna SteamVR, anche Luke Ross

Corregge il ciclo nativo scaduto mantenendo lo stesso frame in FIFO.
Prova controllata sul runtime reale:282/361frame mostrati con V592,361/361
con V593. Valhalla e Luke Ross restano da verificare nel gioco; alle frequenze
alte restano perdite sporadiche nelle sonde. Stessa scena Valhalla,0.3.0 2X,
LowerVRAM/Neural spenti,F9 nel livello20secondi,poi chiudere. Nessun F8 o
benchmark. La correzione opera anche con diagnostica spenta.

## V592 — verifica del director con osservatore esterno

Scadenze agganciate al vsync fisico; il ritardo di preparazione si applica
ai pacchetti, entro i tre slot esistenti. Stessa scena Valhalla,0.3.0 2X,
LowerVRAM e Neural spenti. F9 solo nel livello per20secondi, poi spegnerlo
e chiudere il gioco. Nessun benchmark/F8. Il conteggio esterno dei frame
presentati resta la verifica necessaria;33test tecnici passati.

## V591 — confronto Valhalla

Corretto il margine di1,1ms aggiunto dopo le copie tardive anche con OpenXR
già pronto. Ripetere la stessa scena/sequenza del benchmark585/590. Il fix
passa33controlli; recupero FPS nel visore ancora da verificare. Nessun F8.
Per confrontare prodotti/consegnati usare F9 in una breve prova manuale.

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

## V575: precisione della consegna

Riavvia tray e gioco. Stesso punto/risoluzione Ghostwire,Neural spento,2X,F9:
circa15secondi Disabled,15secondi0.2.1 e15Fidelity. Il timer si sveglia prima;
l'ultima finestra e controllata dal consumer conQPC, senza anticipare i frame.
Il test GPU passa da85.993 a89.981FPS; risultato nel gioco da verificare.

## V574: preparazione dentro il periodo di refresh

Riavvia tray e gioco. Stesso punto e risoluzione Ghostwire, Neural spento,2X,F9:
circa15secondi 0.2.1 e15Fidelity. La preparazione ora avviene durante il periodo
fra due consegne: si aspetta solo il residuo. Coda GPU separata mantenuta.
Confronta produzione e meter compositor. Il guadagno nel gioco e da verificare.

## V573: separazione GPU della consegna

Riavvia tray e gioco. Stesso punto Ghostwire, Neural invariato,2X,F9:
circa15secondi 0.2.1 e15Fidelity. La copia verso SteamVR ora ha una coda
D3D12 distinta dalle attese del produttore privato. Il menu tray indica
"SteamVR preparation queue": quella scelta riguarda la preparazione privata;
la consegna finale rimane separata. Risultato nel visore ancora da verificare.

## V572: consegna OpenXR

Riavvia tray e gioco con V572. Stesso punto Ghostwire, Neural invariato, 2X,
F9 acceso: circa 15 secondi Fidelity e 15 Djules. Controlla fluidita e meter
del compositor. Non serve ancora un benchmark completo. Le attese del
distributore precedono Wait; Begin precede la copia; End usa il tempo del
runtime senza correzioni nostre. Il recupero nel visore e da verificare.

## V571: prima prova SteamVR

Riavvia tray e gioco. Prima Ghostwire nello stesso punto, A/B spenti,2X,Neural invariato e F9: Fidelity -> Djules -> Hybrid -> FSR -> DLSS-G,circa15s ciascuno. Verifica movimento visibile e meter del compositor. Non rifare ancora tutto il benchmark. Su SteamVR la privatizzazione e sempre presente; A non aggiunge staging. B mantiene il controllo degli anelli. Poi MSFS strafe. Nessuna installazione automatica. V570 conservata.

## V567 OptiScaler bootstrap trial

V567 selects an already loaded OFXR-aware OptiScaler as embedded XR owner.
Restart tray and game after selecting this build. In Assetto, use OptiScaler's
in-game controls to check real SR resolution changes, then NR and embedded OFXR
separately. Autonomous tray/F10/benchmark settings are not forwarded to that
owner. Without a compatible OptiScaler, the V566 Autonomous route remains.
No new F8 capture is required before the first verdict. This package does not
automatically install or activate the layer. See docs/V567_OPTISCALER_BOOTSTRAP.md.

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

# V473: Elliottate display selection

Close the game and the old tray, extract the complete ZIP, launch V473, then launch the game. Under Display, select Elliottate's stick to bottom: Applying settings should finish and the panel should appear at the bottom in place of the numeric counter. Then try Elliottate's follow controller, a corner and Off during the same session. Rotate your wrist beyond 120 degrees for at least 250 ms; straightening it for at least 500 ms hides the panel. Without valid tracking, it stays hidden and does not automatically restore the corner counter. Selections remain saved, and changing only the display does not restart frame generation. Logging is not required. FSR and all V472 changes are retained. Headset confirmation is still pending; automated tests verify the actual application and D11/D12 rendering paths. The following sections are historical.

# V471: Elliottate meter and automatic arming

Close the game and the old tray, extract the complete ZIP and launch V471: the tray arms automatically (FG follows the saved preference). Under Display, choose Elliottate's stick to bottom for a fixed panel at the bottom, or Elliottate's follow controller for the same graphics on the rotated controller. Rotate your wrist beyond 120 degrees for at least 250 ms, then straighten it for at least 500 ms to hide the panel. The counters and the Vectors/Status rows are the same as in the GUI; logging is optional. You can switch between the six positions and Off during the session. If the controller lacks tracking or compatible bindings, the wrist panel stays hidden; use the fixed panel. Check its size and readability and compare it with F10. Djules now reports medium latency. Disable/Exit and cleanup retain their existing behavior. --settings-only/--settings-preview do not activate the tray. The preceding sections are the history of sealed builds.

# V470: Cycle estimated

Close the game and the old tray, extract the complete ZIP and launch V470. In the same Village scene, compare 0.2.1 and Djules with the panel visible: Cycle length remains measured, while Cycle estimated replaces Presentation estimated and adds an estimate of headset slots without counting elapsed waits twice. It shows N/A without confirmed generated content or a known period. It does not measure photons or actual display presentation; it assumes the runtime displays distinct outputs. Logging is not required for the counter. If you record a run, check the 256 MB log limit; the tray retains the value from the previous configuration. FSR and all routes retain their previous generation behavior. No new game installation is required.

# V469: Stellar Blade DLSS-G

Close the old tray and the game, extract the complete ZIP and launch V469. Test the same scene and UEVR Sequential mode as V468, rotating the mouse and headset. For the initial comparison, select DLSS Frame Generation and Generated resolution 100%, as recommended upstream for Stellar Blade. Keep R19 and the other settings unchanged. You can also compare OFXR Game motion vectors and Hybrid: R22 composition applies to these routes. FSR retains its previous path. Enable logging if you want to record the run; the correction also works without logging. The optional R23 camera-fitting mode remains outside this targeted port.

## Current owner scope: V464 FSR release-bound guides

V464 inherits sealed V463. The owner explicitly authorizes the targeted ordinary native-D3D12 FSR guide/eye association correction described in [V464 scope](docs/V464_FSR_RELEASE_GUIDES.md). This supersedes historical FSR preservation instructions only for that association. FSR provider, shader math, SDK camera/motion parameters and scheduling remain unchanged. OFXR, native DLSS-G, D11 and Luke/AFW contracts are retained. The owner additionally requests a simpler Hybrid menu: fallback is fixed to FidelityFX and the duplicated fallback controls are removed. R19 stays live and default OFF; Vulkan native guide ingress stays suspended. Game/headset confirmation of the intermittent MSFS/Returnal flicker is pending.

## Current owner scope: V463 Hybrid

V463 inherits sealed V462 and adds selectable Hybrid between OFXR and FSR. R19 camera remains default OFF and live on every route. Existing OFXR/FSR/DLSS-G routes retain their behavior. Hybrid has its own profile and optical-flow fallback; native Vulkan guide capture remains suspended. See docs/V463_HYBRID.md. Earlier no-R19/no-Hybrid constraints below are historical and superseded only by these explicit V462/V463 owner requests.

### Hybrid in V464

Hybrid fallback always uses FidelityFX: there is no additional selector or duplicate quality/resolution/bidirectional settings block. A V463 profile that saved NVIDIA is also migrated to FidelityFX; the regular OFXR menu retains its own settings.

### V464 comparison

Close the old tray and the game, extract the complete V464 ZIP and launch the new tray. For the initial comparison, leave R19 off and keep the other settings the same as in the V460 run. Test FSR from startup and during headset rotation, then FSR -> DLSS-G -> FSR. Enable logging before launching the game: presenter_transition events 751/752 show the release watermark, guide publication and verified stream/epoch identity. If both eyes are evaluated before their releases and there is no verified identity or offset distinguishing them, FSR stays in bypass (guide status temporal_mismatch). This build does not claim visual quality is fixed before game testing.

# Current V462 scope

R19 is an explicitly authorized, default-OFF live checkbox for all FG routes. See [V462 scope](docs/V462_R19_CAMERA.md). This supersedes older R19 exclusions below. Base V461; Vulkan guide capture remains suspended. No Hybrid yet. Game/headset validation pending.

# V461: V460 interface and V457 Vulkan behavior

This build starts from V460 and ports only the suspension of Vulkan motion-vector
capture introduced in V457. FSR on D11/D12 remains unchanged.

Close the old tray and the game, extract the complete ZIP and launch OFXRBridgeTray.exe.
Reopen NMS with the same settings as in the V457 comparison. OFXR should use optical
flow; Vulkan routes requiring motion vectors remain in bypass. The new interface
should report Vulkan detection and the suspension of motion-vector capture.

Compare FPS and stutters, first with FG off and then with one OFXR route active.
This integration does not claim to fix the residual stutters from V457.
Logging and the counter remain available; the NR model is not included in the ZIP.
