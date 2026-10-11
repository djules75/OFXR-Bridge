# V456: R15 release-bound eye guides

Only the per-eye guide association from upstream R15 commit
208a864ad9b54ca64ec136177f8bad624ba1edeb is adapted. This is not a release-wide
R15 merge: promise/scheduler changes are excluded.

Enabled only for OFXR game motion vectors and native DLSS-G. FSR retains the
V455 resolver behavior and unchanged SDK preparation, shader and midpoint pose
path. Pure optical flow and Luke AFW are excluded. R19 camera C is not included.

Each committed XR color ticket retains the process-monotonic publication
watermark observed at release. Once both projection eyes are known, the guide
join is refreshed against those exact releases, without recapturing color or
changing its capture ticket. This corrects right-first capture startup and
ignores stale streams and other-size spectator views. A shared feature keeps
N-2 same-eye continuity. Both-evaluated-before-release uses the existing stereo
ordering; more than two active same-size streams remain an ambiguous refusal.

The actual selected snapshot retains its original device/queue/fence metadata.
Cross-queue use requires the existing submitted producer fence and GPU queue
wait; a CPU publication alone is insufficient. Reset epochs, capture ownership
and native GPU interpolation receipts remain authoritative. No added CPU GPU
wait, renderer shader changes, cadence changes, R19 poses or V453 timestamps.

Live flight record presenter_transition result751: a logical eye (0/1), b color
release watermark, c selected guide publication (0 for refusal). Existing NGX
capture logs and native Evaluate/GPU result records remain available.

Built from a fresh sealed V452 checkout with selected V454 queue correctness
and V455 provider recovery carried forward. V455 source and package are sealed.
Version/IPC ABI45601 prevents mixing V455 tray/layer/private forwarder binaries.

Validation: an actual WARP producer regression covers right-first shared and
separate streams, interleaved and both-evaluated-before-release, stale/spectator,
reset, evicted release, ambiguous streams and exact cross-queue submission.
Its right-first case fails against the V455 resolver and passes with R15.
The original V455 resolver tests remain unchanged, plus explicit legacy FSR
behavior checks. Full suite and package verification results are in VALIDATION.md.
Visual Returnal comparison with V455 remains owner validation, not an assumed fix.
