# OFXR-BRIDGE 0.3.0 (v649): Horizon Director worker

Local candidate over the exact V648 source. In-game performance acceptance is
pending; no live install, profile/registry/driver change, Git integration or push.

The retained Horizon V648 trace shows the admitted real/generated rates falling
together, with no substantial generated-but-undelivered gap. OFXR inline D11
publication waits are roughly7–15ms per eye. Marker CPU decoding is a separate
common AFW cost (about6.5ms in the retained plateau), also paid with OFXR Off;
it must not be presented as the sole explanation of the Off/On gap.

Horizon pins `luke_fresh_only` to preserve its unique-production admission and
duplicate filtering. That flag also unconditionally forbade the upper synthesis
worker, leaving even requested Director pacing on inline OFXR publication.
V649 allows the existing worker for NVIDIA OFXR0.2.1/0.2.13/0.3.0 when SteamVR
private Director pacing and V648's separate OFXR queue are available. AFW remains
on the producer path; its completed immutable packet passes to the worker.
Horizon source admission and held/duplicate rules remain intact. Single-threaded
D11, split application loops, AFW-only and unavailable-queue guards remain.
Framework, Framework+Director and VDXR retain the existing Horizon policy.

While the upper worker owns frame calls, Horizon's inline output helper does
not insert an additional Wait/Begin or update the producer's inline clock.
The same bounded capacity, history leases, completion receipts and independent
physical OpenXR Director remain. No new frame-sized texture/XR swapchain.

With isolated per-eye queues, marker readback/CPU decoding no longer holds the
session GPU submission mutex against OFXR. The reader's own allocator/readback
lock and capture-queue ordering remain. Scratch and ordinary OFXR destinations
are disjoint; shared-queue fallback retains its original locking.

The CPU decoder now rejects candidates whose full64-bit payload provably cannot
fit inside the row, before running the expensive prefix fit. All rows, valid
candidate fits, CRC tests and ambiguity checks remain. A test derives conservative
least-squares bounds for every legal15/16 prefix subset and64 payload edges,
and a frozen V648 oracle verifies identical results on401 differential rows.
Two earlier CPU optimization trials were slower and were discarded.
On a cropped old Horizon F8 background ROI, the readback-backed batch replay
changes3.516ms to1.702ms; these crops contain no decoded valid token, so this is
only a processing-cost comparison. Correct token behavior is covered separately
by the codec/differential/GPU tests. No in-game FPS gain is inferred from replay.

The shipping layer/real D11 textures/private endpoint fixture verifies the game
returns from EndFrame with the OFXR GPU queue deliberately still blocked, then
delivers every accepted real source with its pixels/poses, ordered generated
outputs and native cadence. Tests cover the three NVIDIA routes, pacing switches
and OFXR Off/On. The original post-AFW packet and source-filter fixtures remain.
An initial test-only uninitialized endpoint placeholder crashed; it was removed
and replaced with the actual initialized private-runtime fixture. No such
placeholder or alternate implementation is present in the distributed layer.

Version649 / IPC64901; official AFW beta6 provider unchanged. Native depth/camera
subtraction, vector ownership, interpolation math and marker placement unchanged.
Owner comparison: same Horizon scene/settings, AFW1x1,0.3.0 with Director selected,
matching tray/game restart. Compare with V648 Director; no new F8/full benchmark
is needed. Framework intentionally keeps its existing inline policy.
