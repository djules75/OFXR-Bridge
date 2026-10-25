# V650: remove unrelated producer dependencies

Local candidate based exactly on V649; V649 source/package and main31a28d0c
remain intact. No live installation, profile changes or publication.

## Evidence

Horizon V649 PID32436 retained a useful 16.45-second active F9 window.
144 runtime-entry waits >=0.5ms total996.518ms; all144 overlap a different
thread's private EndFrame (996.089ms overlap). A producer virtual image release
was blocked behind a presenter waiting for bounded endpoint FIFO credit.
The native-entry gate protects native D11 calls; the private endpoint already
owns its internal handle/image locks and independent native frame consumer.
Do not hold this old gate around private endpoint operations. Native/AFW-only
sessions without that endpoint retain the gate unchanged. FIFO capacity and
backpressure remain unchanged.

The same trace frequently shows first-eye completion ~1.5ms and second-eye
completion ~11ms, while producer marker readback is pending. Code inspection
finds that post-AFW synthesis consumes a frozen, producer-fenced history ticket
but waits on the interop's mutable latest D11 publication cursor as well.
A disjoint AFW scratch publication/readback can therefore delay an older job.
The trace does not record this cursor's exact fence value: not every long
GPU interval is attributed to that dependency solely from CPU timestamps.

## Changes

Track D11 accesses to each shared current/synthetic output slot at publication
and terminal recapture. The isolated post-AFW worker waits only for accesses to
the destinations it is about to overwrite. Its immutable input history keeps
the existing producer fence/lease. Raw input, inline Framework, shared-lane and
AFW scratch submissions keep the conservative previous synchronization.
No skipped readiness proof, unfenced slot reuse or global latest-input guess.

The gate correction applies to private D11 endpoints generally. The precise
output dependencies apply only to isolated post-AFW workers. VDXR legacy,
AFW math/provider, marker identity, native depth subtraction, swapchain counts,
Director cadence, buffering capacity and latency policy are unchanged.

## Validation

Real GPU fixture parks disjoint AFW scratch readback: frozen OFXR output must complete;
the old conservative dependency control must remain blocked. A pending D11
read of the same output slot MUST still prevent its reuse. Both eye pixels
are checked after completion. An initialized private D11 endpoint is filled
to capacity while real EndFrame is blocked: a producer release of another
owned image must finish, while the fourth packet retains bounded backpressure.
Native D11 gate serialization, final FIFO order and exact GPU pixels are checked.
Final 66 focused CTests PASS in71.40s, including real private D11 transport,
HFW three NVIDIA routes/worker/pacing changes, isolated post-AFW packet,
VDXR legacy controls, marker/eye contracts and beta6 quality/lifecycle.
Initial cursor-control fixture incorrectly used raw ingress, which does not
advance the publication cursor: that control failed. It was corrected to
actual disjoint scratch publication/readback without changing renderer code
to satisfy the erroneous fixture. Initial evidence remains local.
Focused regression results accompany the local package. Headset performance
and remaining 90-to-72 FPS variation require owner validation; not claimed fixed.
