# V652: isolate Luke OFXR before native AFW bootstrap

Owner reports V651 still78-86FPS and Shared queue after loading Horizon.
This is not accepted steady90FPS. PID12076 F9 trace is saved locally under
diagnostics/horizon-v651-shared-pid12076, including the loading transition.
Do not conflate the earlier V651 native-clock correction with this queue issue.

## Root cause

Both projection sources were already eligible at19.075/19.098s and actual
OFXR synthesis on those same handles occurs before the separate lane exists.
The native AFW context is confirmed at31.163s; queue creation succeeds at
31.149s. Existing generation objects, however, keep their original synthesizer
queues. The shared-generation count therefore correctly remains nonzero.
The warning starts after native capability8 becomes available, when its Luke
condition is evaluated, rather than when the shared engines were first created.

initialize_luke_synthesis_queue unnecessarily required native AFW capability8.
OFXR only requires the D12 owner already validated for D11 interop/history.
Waiting for SR/RR provenance causes loading-screen color engines to stay shared
through gameplay, even after an unused separate session queue is created.

## Correction

Create the separate OFXR queue on the existing validated D12 owner before
constructing the first Luke color engine, without requiring AFW capability8.
No native guide authority, AFW initialization, device substitution or guessed
producer provenance is granted by this. Same priority, one additional queue
already intended since648, no additional XR images/frame-sized allocation.
AFW-only sessions still create no OFXR queue/helper. Failed allocations and
real per-engine shared fallbacks continue to warn; do not hide the warning.

When no history exists and late bootstrap really replaces the backing D12
device, retire its provisional OFXR queue before creating one on the new owner.
Active histories keep their validated backing owner and queue; compatible
native aliases retain the early OFXR queue. No live engine migration/reset.

## Scope

Common Luke D11 interop startup, on SteamVR AND VDXR including Framework.
This fixes a Luke bootstrap order, not the common Director cadence. Non-Luke
startup, V651 clock fix, V650 dependencies, AFW math/native depth subtraction,
marker identity/provider/quality, swapchain counts and pacing remain unchanged.
Current game/tray must restart to use the corrected initial resource ownership.

## Validation

Real initialized layer/private endpoint fixture constructs the projection
engine before native readiness, checks actual isolated generation ownership,
then enables readiness while retaining the exact queue/history. It also proves
game End returns while OFXR GPU work is blocked and validates final pixels,
poses, output order and cadence. Queue-policy tests cover both runtimes,
legacy/comparison, AFW-only and honest fallback retirement. Existing original
native producer/alias tests now explicitly retain a pre-bootstrap OFXR queue.
67 focused CTests PASS79.89s, including the new actual cold projection/worker
fixture, both-runtime queue policy and three exact native/alias bootstraps.
No failing runtime variant; a test-source edit hit a temporary compiler read
lock, was reapplied after compilation and its target rebuilt before final checks.
Final results and packaged-loader proof accompany the candidate. Headset
performance and disappearance of Shared queue remain owner validation pending.
