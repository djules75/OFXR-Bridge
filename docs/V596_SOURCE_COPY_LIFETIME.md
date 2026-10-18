# V596: completed-copy producer source lifetime

Scope requested by owner: prepare V596 with only the fix for Valhalla's
persistent normal-route slowdown reported after leaving Djules 3X. Remaining
MSFS presentation losses are a separate investigation; this build does not
change the director's clock, cadence, latency or native-window policy.

## Evidence and diagnosis

The V595 Valhalla trace PID 40892 has Lower VRAM off and normal Reference 2X
selected, while output remains about 44.6 FPS (22.5 real + 22.5 generated).
The trace does not show a hidden Lower VRAM flag. Virtual application Wait
is about 6 microseconds; application End is about 36.8 milliseconds. A private
auxiliary composition chain has one producer image and blocks acquisition
for about 35 milliseconds, even though its preceding copy has completed.
The normal-mode detailed portion spans approximately 258.99-262.17 seconds;
subsequent Lower VRAM measurements must not be counted as normal mode.

Source image readers previously stayed held until native End returned. Upper
composition metadata can be released sooner, so its existing slot picker can
choose that same single-image chain and wait on this unnecessarily long lease.
This is an identified source ownership bottleneck, not proof that a 3X or
Lower VRAM setting remained active. Root cause of the triggering transition
and residual compositor losses is not otherwise asserted.

## Change and invariants

Release only producer-source read leases when publish has successfully awaited
the exact copy fence. The immutable real target already contains this packet's
pixels. Retain real targets, chain consumer counts and FIFO credits until the
native End finishes. Failures and stop still drain GPU work before freeing it.
A chain cannot be destroyed while any native consumer retains it, including
a packet whose source read lease has already ended.

Increment each private image's generation on successful release. A prepared
target may be shared only for the same image index AND generation. Rewriting
the same source index therefore copies new pixels into a distinct available
real image instead of reusing its older prepared result. Session stop resets
consumer counts after worker join and GPU drain.

This is a source-lifetime correction in the common SteamVR D3D11/D3D12 private
endpoint. It adds no swapchains, images, FIFO slots, queues, phase offsets,
producer sleeps or settings. VDXR and interpolation/AFW math are unchanged.
The optional stage26 trace marks copy-complete source release, with no effect
on scheduling when F9 is off. General Lower VRAM and 3X repair remains deferred.

## Validation and owner check

All 33 focused checks pass. The new test holds native End blocked, rewrites
a single producer image after copy completion and prepares the next generation.
It requires exact old/new pixels {40,80}, distinct real acquisitions without
extra chains, per-packet metadata and destruction blocked until native End.
Both D3D11 and D3D12 use actual GPU resources/copies. Exact V595 fails the new
source-progress assertion. The first full run exceeded the previous aggregate
30-second timeout, while the other 32 checks passed. A diagnostic run with
immediate test progress completes every assertion in 31.619 seconds. The
expanded aggregate now has a 60-second process timeout; all per-case timing,
pixel and progress assertions remain unchanged. The initial timeout and
diagnostic output are retained in the package evidence. Existing FIFO-backpressure, native clock, route/flag,
static-image, stop and acquisition-reuse checks remain unchanged.

Owner check: in Valhalla, use the previous sequence Djules 3X -> normal 2X ->
Reference 0.2.1, keeping Lower VRAM off. Check whether the persistent ~44 FPS
state is gone. F9 can remain on. No F8 or complete benchmark is needed.
This is a local candidate, with no live install or publication.
