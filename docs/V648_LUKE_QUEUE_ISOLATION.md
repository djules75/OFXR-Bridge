# OFXR-BRIDGE 0.3.0 (v648): Luke GPU queue isolation

Local candidate based on the accepted-source V647 checkpoint, with no V642–V646
AFW math/history experiments. Runtime performance acceptance is pending.

Luke retains its original native producer identity and its existing private
D3D12 capture/AFW queue. OFXR raw-color and post-AFW optical-flow synthesis use
a second D3D12 direct queue on that exact confirmed device. This applies to
SteamVR and VDXR, including Framework pacing. It does not change VDXR pacing
or replace its delivery path with the SteamVR Director.

The pre-AFW scratch-image copy uses a deferred-flow, copy-only helper on the
capture/AFW lane. It allocates command/fence state but no extra optical-flow
provider and no additional XR swapchains or frame-sized image ring. AFW-only
sessions do not create the OFXR queue/helper. All output/history leases and
the existing bounded backpressure remain necessary.

Each D3D12 publication lane has its own shared fence timeline and one writer.
Capture/AFW never signal the OFXR timeline. Inline publication waits on the
actual submitting lane; Director publication requires its retained completed
synthesis receipt before copying into D3D11. Source/history dependencies are
preserved, not removed in pursuit of queue overlap. An inline Framework route
still has its required output-readiness dependency and shared GPU hardware is
not made independent by creating a second queue.

`Shared queue` appears first in the common status explanation when capture/AFW
and OFXR actually share a queue. The Details panels explain that this may
reduce performance. Dedicated-queue creation or interop-fence setup failure
keeps the existing shared route usable and visible. A shared per-eye allocation
keeps the warning until it retires. The warning does not change generation
activity, meter colors, source admission or the selected pacing. It includes
native SteamVR D3D12 synthesis-queue allocation failures. No per-frame adapter
queries or GPU diagnostics are needed for the status.

Version 648 / IPC 64801. Official AFW beta6 is unchanged. Marker recognition,
depth-based native camera-motion subtraction, per-game guide contracts, AFW
quality, interpolation, delivery cadence and timing meters are unchanged.
No live installation, profile/registry/driver edit, remote push or publication.

Validation includes a real-GPU gate on the OFXR lane while the next capture and
AFW-ingress copy complete on the other lane, exact pixels in both eyes, rejection
of incomplete publication, inline/completed publication, and retirement of both
fence timelines. A shared-lane control really blocks. Additional production
layer fixtures cover queue bootstrap, runtime policy, source selection and
raw/post-AFW Director output. Hardware fixtures cannot establish an in-game
FPS/latency improvement; compare the same scene/settings with V647.
