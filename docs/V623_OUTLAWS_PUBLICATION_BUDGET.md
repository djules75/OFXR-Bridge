# V623: Outlaws eager startup publication budget

Base: V622 (`160987f9a56bea6e706a946b85aad13108896e4e`). Local candidate;
headset validation pending. V622 AFW-only/DLSS5/meters/tray benchmark remain.

## Evidence and reproduction

The owner's V622 Outlaws PID39196 flight log begins118.202s after process start,
so the original allocation/refusal is absent. It records918 AFW selected cycles
with no generation resources, plus continued valid DLSS inputs. REAL VR's log
identifies SteamVR2.18.4 and successful original VR texture/session creation.
Do not claim the exact initial native handle count was observed in that log.

A new shipping-layer GPU fixture reproduces a concrete startup failure: three
eligible D3D11 color sources enumerated before the first frame, actual private
runtime endpoint, downstream native ceiling16. V622 allocates five independent
native destinations per source. Native counts after each enumeration are6,12,
then3: the third source hits the ceiling, tears down the warmed outputs, marks
the session budget exhausted and leaves no generation resources. The permanent
Outlaws pre-frame seal correctly prevents unsafe cold allocation during play,
so those resources cannot be recovered later by ordinary menu selection.

## Change

For the existing Outlaws pre-frame path with a private endpoint, compatible
plain single-slice color outputs use the existing publication pool, keyed by
**original source handle and exact shape/format/state**. Each private work
texture/handle remains independent. Only its final native OpenXR destination
is shared. Three original sources now require three original native handles
and three publication sinks, six native handles total in the same fixture.

The established FIFO owns each immutable output and prepares/copies/releases
its real image before native EndFrame. Two eyes never share a sink. Native
ring reuse, backpressure, cadence, destruction and preset rebuild use the
existing pool implementation; this is not reduced-resolution AFW or fewer
private work buffers. No new queue, copy, wait, GPU query or pacing policy.

No late Outlaws allocation window is opened. The accepted warm provider/owner
rendezvous, depth/camera-subtracted motion and all title guide fixes remain.
Non-Outlaws, VDXR (no private endpoint) and AFW-only (no private endpoint) retain
their previous allocation paths. The general D3D11 bridge deliberately excludes
Luke sessions; do not introduce an artificial Outlaws bridged-session mode.

## Verification and owner check

The new fixture fails on V622 and passes with this change, including native
handle cleanup after all owners retire. Publication GPU coverage now includes
D3D11 and D3D12, normal/reused real image indices, exact left/right pixel order,
blocked native End backpressure, stable cadence, pool retirement and restart.
Existing Outlaws startup guard/provider tests and ordinary Luke/UEVR controls
are retained. See V623_VALIDATION.json for the final checks.

Restart with the matching V623 tray and game; leave AFW only off for the normal
OFXR+AFW route. Confirm the allocation warning is gone and AFW/OFXR resume.
A full benchmark or F8 is not initially needed. This repair addresses a concrete
reproduced budget defect; its match to the owner's unlogged initial refusal and
headset image/performance acceptance still require that runtime check.
