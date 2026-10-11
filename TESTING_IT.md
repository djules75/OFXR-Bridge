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
