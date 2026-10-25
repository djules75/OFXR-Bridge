# V653: Luke D11/SteamVR release readiness and one-refresh work margin

Local trial copied from accepted V652; V652 source/ZIP remain intact.
Owner accepts trying to recover one or two refreshes while preserving output.
Target this first trial at ONE refresh (~11.11ms at90Hz), not a claimed gain.

## Evidence and intended tradeoff

V652 PID37616: output median90; worker10.627ms end-to-end, not GPU kernel time.
Prepared outputs wait22.115ms median to native handoff. Private work margin
is retained22.222ms. Generic D11 ready marker at End can follow newer commands
even after a worker publication was CPU-confirmed complete. Final D11 copies
still use the original context: removing the second source marker does NOT
make those copies independent of unrelated commands already queued there.
The trace's longest source/copy observation24.095ms means all retained margin
cannot simply be declared unnecessary. Lower margin may expose rare underruns;
steady headset FPS/latency must be compared against652 before acceptance.

## Changes and proof requirements

Only Luke D11 private endpoints on real SteamVR opt in. Freeze ordinary image
readiness at Release with an ordered marker, before future commands at End.
Worker publications already completed on their exact immutable D11 fence can
release their exact image/index/texture with a completion proof. Pending,
wrong-index and foreign-texture proofs are refused without consuming ownership.
Every new generic release writes a fresh marker; no earlier-generation proof
survives a rewrite. Packet leases combine readiness of ALL referenced images,
including auxiliary/depth/OSD layers. Unproven images keep real GPU waits.
Only completely copied packets can reach native End, as before.

For this opt-in path, retain at most ONE physical period as learned work margin
instead of two. There are still THREE FIFO credits: no change to storage,
backpressure, swapchains, sequence/order, physical frequency or actual readiness.
A late image waits until complete and uses a later absolute slot; no missing
image is invented, overwritten, dropped or delivered at a shortened interval.

This changes Luke D11/SteamVR (Framework and Director readiness; only Director
uses the work margin), not non-Luke, D12, Vulkan-native or VDXR paths. AFW-only
tray sessions still bypass the endpoint. Dynamic FGOff keeps native bypass.
AFW math/provider/depth-subtraction/marker identity, V652 early isolation and
V651 native-window fix remain. No user/profile/registry or pacing UI changes.

## Validation

Actual D11 image pixels and GPU gate: completed image proof reaches source-ready
while later original-context work is parked. Old End-marker control cannot.
The final copy correctly still waits for that GPU queue. Pending auxiliary and
rewritten source generations MUST block and retain their own exact pixels.
Wrong texture/index/pending proof rejection, native FIFO order, physical cadence,
balanced clocks and one-P vs old two-P work-margin controls are checked.
Final focused regression and packaged-loader evidence accompany the ZIP.
Headset FPS gain/latency acceptance pending; no zero-risk performance guarantee.
