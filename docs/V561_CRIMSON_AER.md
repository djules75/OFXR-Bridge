# V561: raw Luke OFXR and interleaved native AFW history

Based on V560 (`555939c89e81e7abf338a515e799069df5e55f61`), retaining the
V558 benchmark/CSV contract. Candidate remains local and runtime-pending.

## Observed failures

The owner's second V558 Crimson run (PID 31252) confirms that Fidelity and
Reference generate with AFW off, while New and Djules fail during gameplay and
generate when the game menu opens. During the failing portion the advanced
presenter repeatedly records `luke_filter result=2`: the released left/right
AER render identities differ, and its same-counter native-pair filter discards
the complete XR colour submission and resets continuity. The inline routes do
not pass through that filter.

Raw optical Luke OFXR now admits its exact captured XR colours without that
native SR-pair filter. The separately pinned HFW fresh-source contract and
guide-dependent backend admission are retained. The GPU regression inserts
different left/right render identities; it fails against V560 with
`GPU order/count`, then passes after the correction, checking stereo pixels and
ordered real/generated publication.

The F8 capture contains eight valid XR eye images and native scalar contracts,
but no native motion/depth images: generic ingress did not record those until
AFW admission. The in-level native projection focal value is 1.01947224 and is
consistent with the final XR views. The narrower 2.74748 projection/refusal in
the earlier run is not established as the persistent gameplay fault.

The native contracts instead show alternating SR feature handles, each with
its own serial counter. A same-feature predecessor is two global publications
back. Treating that as the immediate opposite camera rejects the exact colour
token before AFW evaluation.

## Native history correction

Join a retained A/B/A history only when the three original immutable frames
prove consecutive global publications, protected colour keys and camera
tokens; the current/older original stream and epoch agree and their serials
are consecutive; the middle belongs to a different live feature; geometry,
canonical snapshot owners and producer queue agree. Reset/jittered/incomplete
or ambiguous histories remain rejected. The native-family production receipt
paths retain their single-stream contract. No feature IDs or frame counters
are rewritten.

Calibrate world units from the same immutable A/B/A production order. AFW's
common origin and unit calibration use both original feature/epoch pairs in
canonical order, so switching the current eye does not reinitialize its
history. A reset of either feature invalidates the paired origin. Per-eye
native resource ownership and GPU submission/fence checks remain in place.

Depth and camera subtraction still produce the motion sent to AFW. The
motion/depth shaders, crop/gain rules and provider are unchanged. GPU tests
compare the constants and corrected pixels for identical physical cameras
represented by one feature or two alternating features.

F8 now records generic native motion/depth snapshots, input colour and
completed SR colour even if AFW refuses the input. Scalar contracts also carry
the native camera token, colour-origin key, production receipt, full camera
basis/FOV and resolved interleaved flag. This diagnostic work only runs during
an explicit full-resolution F8 capture.

## Validation and owner trial

Optimized Release: 14/14 focused checks in 14.02 seconds. These include the
negative/positive raw optical stereo regression; exact interleaved registry
GPU pixels and rejection of token gaps/retired opposite epochs; 272 native
motion GPU scenarios (136 also exercising two-feature identities); common
origin lifecycle; the actual bundled AFW provider in all three modes against
an independent rotated-plane oracle; retained KCD/Indiana quality checks;
protected snapshots; raw fallback; output scratch/terminal packets; HFW clock
and pacing; F8 terminal ownership and actual NGX capture integration.

No Crimson headset result is claimed by those fixtures. First trial: same
gameplay scene, F9 on, AFW off and New 0.3.0 then Djules 0.2.13; then enable
AFW. The original source dump is preserved locally. No new capture is required
before trying this candidate. Presentation pacing, swapchain counts, A/B
controls, AFW shading controls and the original six-row CSV layout are retained.
