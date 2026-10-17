# V564: Crimson original public guides, completed wrapper color

Local candidate on V56350b05023. Runtime validation is pending.

V563 PID18188 is confirmed by logger result563. The new folder contains one
F8 request with two AFW evaluations and three matching public/native NGX calls
(camera tokens5378-5380). The earlier two directional folders PID21652 were
V562; do not label them V563. Frozen data/hashes remain local.

On all three matching calls motion changes between the public dispatcher and
typed native SR; camera, scales and jitter do not. SIFT correspondences in the
actual linear native input colors show public N-1 motion median error0.707 and
0.672 guide pixels (3167/3220 features). Native motion is not N-1: errors20.42/
26.69. For token5380 versus5378, native N-2 median error3.083 versus public27.78
(2230 features). These are observed correspondence errors, not proof that all
pixels or moving objects have identical error. No vector halving is justified.

Known Crimson SR calls now own guide snapshots at the original public boundary,
before REALVR mutates them, with declared raw span1. The same protected GPU-copy
and original feature/color token machinery is retained. Nested typed native SR
still executes unchanged, including NR before/after SR at its native points.
The original caller output is marked after successful wrapper completion.
Failed/recursive wrapper completion invalidates history. Unknown generic feature
maps do not invent SR metadata; direct native cold calls retain their span2
contract. Public N-1 is corrected by the existing mandatory depth subtraction
and exact same-eye N-2 reconstruction; provider Normal, thresholds, shader,
vector gain, presentation pacing and swapchain counts are unchanged.

The actual NGX GPU regression now overwrites original motion/depth in place
inside the wrapper, calls an observable typed native child, then overwrites
final caller color. It verifies exactly one original guide publication,
current/previous GPU bytes and fenced lifetimes on an independent consumer queue,
and final color/CRC after the wrapper. Existing cold/generation lifetime and
bundled-provider all-mode interleaved span1/span2 controls remain applicable.
The V563 concurrent-test counter fix is test-only and now uses atomic increments.

F8 omits the reused mutable consume-stage SR color for protected interleaved
history at either span, preserving budget for exact XR color and guide inputs.
No runtime capture work is added unless F8 was requested.

In V563 gameplay65-79s, marker fence wait median12.587ms and CPU map/decode/unmap
2.171ms (p902.740), improved from V562 CPU9.855ms in its measured segment.
These separate runs are not a controlled FPS comparison. The GPU wait remains;
V564 is not presented as a verified fix for the45FPS behavior.

Trial: restart tray and Crimson with V564, same scene AFW/debug on and mouse
rotation. Check static/moving scenery, green debug, and FPS. No additional dump
is required before this first verdict. Original V558 CSV and personal adapter
are unchanged; no live installation or online publication.
