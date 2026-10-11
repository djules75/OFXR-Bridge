# V480: Horizon producer clock continuity

Parent V479 e14f9050392f0def179e4028d39f872c0e4ed6dc. V474 fallback, all sealed predecessors retained. No V475/V476 import; IPC47101/provider45601 unchanged.

## Evidence

Owner reports occasional missed frames and rotation flicker near the end of Horizon V479 PID32608. Preserved complete run and final INI at work/shared/ofxr-autonomous/diagnostics/v479-horizon-pid32608. The final100–139.25s interval is AFW combined, OFXR Off. All downstream outputs succeed and AFW remains ready, with no runtime error or raw bypass. There are occasional27–34ms output gaps and skipped original sources. One source7935 has20.686ms between source prime and original-color-token readback completion, then physical End takes75us. This interval includes the synchronous GPU readback/fence and remains in V480.

Of2970 application predictions in that window,283 advance only1ns;277 of these admit a new original AER source (only6 are duplicate drops). The physical output path overwrites the continuous producer clock with a runtime slot that can be older than the already-requested virtual time. The next virtual wait is pinned by the +1ns floor while new source color arrives. This is a demonstrated timestamp defect consistent with unstable camera motion; the log is not a pixel recording and does not by itself prove all visible flicker comes from it.

## Selective fix

Only the pinned HFW fresh-content clock changes. Physical output admission now carries forward the old producer clock plus elapsed wall time. A newer runtime slot may move it forward; an older one cannot reset it backward. The same helper applies to lazy physical output and adopted internal second cycles. No added frames, repeated retained outputs, missed-source reconstruction, sleep/queue/fence removal, presenter thread, shader/provider/camera math, or other-title scheduler change. The physical output retains its actual slot and the image's original pose/FOV/space/rectangle. V479 admission and warm both-Off GPU bypass remain.

## Verification

Optimized x64 Release;27/27 focused checks pass9.05s, no skips. Production output wrapper followed by actual virtual Wait reproduces delta1ns using the exact sealed V479 function body, then yields21ms elapsed prediction using the fixed function. Physical Wait/Begin/End count remains1 for either case; no polling/repeat introduced. Newer and successive stale anchors are checked. The legacy function is test-only and cannot enter the shipping layer. Existing all-four-state source filtering, strict output metadata/ownership, Luke GPU/guide/depth/context, ABI and loader checks remain passing.

Owner test: restart Horizon with V480 and reproduce the last segment in AFW combined with OFXR Off, debug Off, logging On. Check rotation and occasional flicker. Actual headset improvement remains pending; GPU/readback stalls and missed source productions are not claimed eliminated by this clock correction. F8 can capture visible bad output if it persists.
