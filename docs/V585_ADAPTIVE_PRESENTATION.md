# V585: retained automatic refill and fresh runtime cycles

Base sealed V584. Its independent preparer, completed-packet queue, immutable
image leases and bounded three-credit pool remain in place. Copies and source
GPU waits never move back to the physical output thread. No additional XR
swapchains or images are allocated. VDXR, AFW and generator math are unchanged.

## Input gaps and automatic presentation margin

If owned input arrives after its physical deadline, the clock records the
exact source deficit and schedules an additional refill margin after delivering
that late output. The following deadline includes that margin once; later
ready outputs again use one physical refresh each, retaining the shifted phase.
There is no fixed per-frame delay, no shorter catch-up interval and no attempt
to turn75fps input into90 new images. Empty refreshes are allowed; all accepted
packets retain FIFO ownership. The producer only waits when the existing pool
is full. A finite last packet is never held waiting for another input packet.

Each refill is limited to the useful capacity of the existing pool: two
physical periods with three credits,22.222222ms at90Hz. A loading/pause gap
cannot become a many-second extra replay pause. This limits an individual
refill, not the cumulative held phase or the total game/GPU/display latency.
The implementation does not silently allocate an unbounded latency buffer.

Source timing is sampled before the real-chain lease gate. If a fence was
already complete when dequeued, its acceptance timestamp is used as a lower
bound; a real-chain backlog must not be charged again as irregular production.
A genuinely pending fence uses its completion observation. Copy-only lateness
still shifts the physical phase but does not add a source refill on every copy.

## Native cycle placement

The runtime Wait/Begin is requested near the physical handoff. Its overlap
budget follows the actual native call cost plus the100us metadata lead, grows
immediately for a longer call and releases excess gradually. The independent
preparer continues concurrently. End uses exactly that Wait's raw displayTime;
the minimum physical period and existing2us call guard remain unchanged.

V584 Valhalla has near-matching Fidelity production/output medians around73fps,
but Reference/Djules/New still produce about74-76 versus65-67 shown. Native
Wait-to-End prediction age varies by several milliseconds, and SteamVR's
additional predicted-frame count differs between these routes. This supports
testing the common timing boundary, not a game-specific vector correction.
The retained flight covers only the tail of the manual run. There is no exact
packet-to-compositor-record identity proof or claim all losses have one cause.

## Evidence and limits

33 focused checks pass: actual D3D11/12 GPU ownership, all shipping routes,
flags/switches, fixed minimum cadence, FIFO/backpressure, stop/error/restart,
independent preparation, immutable overlay and finite irregular input. A new
fresh-cycle fixture fails against V584 and passes here. See V585_VALIDATION.json
for exact scope, initial failed fixture assertion and timing limits.

These tests use a mocked downstream runtime. They do not prove all frames are
visible in a headset. Compare Valhalla after fully restarting game and tray,
same location/route/refresh. F9 allows source/refill/cycle/compositor correlation;
no new F8 capture or full benchmark is required for the first comparison.
This local candidate is not installed, published or merged automatically.
