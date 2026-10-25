# V663: INI-only Luke publication policy and adaptive benchmark suites

Base: V662, including its exact late native queue-owner correction. No Git or
live installation/profile changes. Provider beta6 ABI45601 remains unchanged.

## Luke publication policy

`[ofxr] luke_shared_swapchains=1` is now the default, including new per-game
profiles and Reset to defaults. The policy is INI-only; desktop and headset
Luke tabs no longer expose its checkbox or restart/forced-title guidance.
An explicit INI0 is honored on the next game start. Outlaws no longer forces
sharing. Its existing preframe warm-up/seal and native ingress behavior remain.
Runtime reports never claim the policy was forced by the title. A real-GPU
fixture covers both shared and unshared Outlaws native allocation.

## Execution policy

All ordinary full and optical-only suites omit standalone optical FidelityFX.
Hybrid/Fidelity+vectors remains in the full suite; manual Fidelity selection is
unchanged. After a completed valid 2X trial on0.2.1 or Djules0.2.13 reaches at
least66% of the measured headset refresh, all remaining3X trials are removed
from the current execution plan. Invalid, partial, unconfirmed or unknown-Hz
results cannot establish this gate. It applies to Legacy and non-Legacy suites.
Maximum basic non-Legacy counts:33optical/57full; sufficiently fast2X results
reduce these to25/49. Legacy maxima9/15 reduce to7/13. Framework/Director, Fast
and buffer variations of the remaining routes are preserved.

Luke suites omit Fidelity and prune coarse AFW qualities using the actual
per-eye projection extent:2x2 requires a side>=3000;4x4 requires>=4000. Unknown
extent admits only1x1. Whole1x1 pass first, then AFW-only2x2 if admitted. At that
checkpoint the existing Best Pick chooses among completed valid results; the
suite completes early if that winner reaches refresh within the established
1FPS equivalence, or if an AFW-only winner reaches80% refresh. The existing
AFW1x1/2x2 ten-percent improvement preference, full-quality priority and restart
notice are preserved. Otherwise all remaining eligible tests run. No Luke3X.
Basic Luke passes are13trials per admitted quality; Legacy passes4. A typical
3376-pixel eye thus runs26maximum, or14when the first checkpoint is sufficient.

## Recording and completion

2.5s settling and5s measurement stay unchanged. Public meter/UI count follows
the pruned execution plan. Early completion uses normal completion semantics:
Auto pick ON saves the actual winner; OFF restores original settings. Stop and
errors do not apply a partial winner. AFW-only selection still requests game
restart. Samples and scores are retained even when CSV output is disabled.

The potential export layout is independent of dynamic execution. Omitted3X,
AFW quality or early-stop configurations retain n.a. values; later columns do
not move. The private owner-sheet adapter already maps by names and preserves
its83AQ:DUcells, including now-unmeasured Fidelity slots. It is not packaged.
No owner Google Sheet edits are part of this work.

Generation algorithms, timing measurements, Director pacing/fences, neural,
AFW quality mathematics, markers and capture are unchanged. The only layer
render-path delta is honoring the Luke INI sharing policy in Outlaws.
