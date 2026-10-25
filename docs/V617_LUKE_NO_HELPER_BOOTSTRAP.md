# V617: recover Luke AFW when no helper D3D12 context exists

Base is the owner's selected V615, not V616. No pacing reset or DOOM HDR/terminal
experiment is imported. This is native producer discovery shared by Luke D3D12
hosts, not a game-name pacing exception.

## Observed Ghost of Tsushima failure

V615 PID23768 has AFW Combined requested throughout. Its actual D3D11 XR binding
has capability3 (D11 device/context interfaces), without a helper D12 device/queue
or a confirmed native producer. All5092 detailed projection frames remain in
lazy arming: valid stereo mappings, empty generation preparation, no AFW stage.
The matching REAL VR log records D3D12CreateDevice failing with0x887A0036,
"already exists", on our fallback call stack. This precedes motion/marker
admission. No conclusion about marker quality or AER motion intervals is needed.
Raw configuration RenderMode2=6 is frozen with the evidence; this change does
not interpret or alter that option.

The discovery loop observed the original SR/RR command list and hooked future
CreateCommandQueue calls, but installed ExecuteCommandLists only after some
queue was explicitly configured. With helper creation failed and the game's
queues already created, no submission hook could identify the queued list.
Consequently the original context could never become available for late AFW
initialization. Existing tests manually configured a helper queue and missed it.

## Correction

If no Execute hook exists, the creation-observed native SR device supplies one
temporary direct queue solely to resolve that implementation's entry point.
The code uses the existing device; it does not call D3D12CreateDevice again.
It bypasses its own CreateCommandQueue hook for this probe, performs no GPU
submission, and neither retains nor publishes the temporary queue. Creation is
outside the discovery mutex; a single in-flight probe and one-second failed
retry bound avoid recursion/concurrent or per-evaluation allocation storms.
Normal already-hooked operation creates no probe and takes no timer reading.

Only Execute of an exact observed SR/RR list on a proven native owner can still
publish the producer. Recording, queue creation and unrelated executions remain
insufficient. Once that original context is known, the unchanged late bootstrap
creates AFW on its confirmed device and a separate execution queue. A one-shot
fallback HRESULT (stage962) and probe result (stage961) join the existing F9
logger; no steady-state timing/sampling diagnostic is added.

## Validation

The new actual-GPU fixture models the observed post-failure state: capability3,
no registered helper device/queue, and a game queue created before interception.
It does not claim to reproduce the driver's0x887A0036 itself. With the V615
native-ingress implementation, the fixture fails at original-context discovery.
With V617 it detects the pre-existing queue, initializes the real AFW provider,
keeps producer provenance separate from execution, defers an in-flight binding,
and retires the context. It also verifies the temporary probe never becomes the
producer. The initial unhandled test exception was corrected in the fixture;
the controlled negative explicitly returns1 at the missing context assertion.
The final JSON report lists the other qualified startup/guide/ownership checks.

No AFW depth/camera subtraction, guide cadence/sign/scale, crop/color marker,
provider, image/swapchain budget, pacing, benchmark plan, UI/INI/IPC or output
meter algorithm changes. The same conditional recovery can help another Luke
D3D12 runtime with the same cold failure; no VDXR policy change is introduced.

Headset validation remains pending: start matching V617 tray/game, use the intended
Legacy mode and check AFW in one level. No F8 or whole benchmark is required.
V617 is local/runtime-pending. No active settings, installation, registry or
remote repository is changed by preparation.
