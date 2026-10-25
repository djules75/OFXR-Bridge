# V574: prepare inside the distributor's refresh interval

Local candidate on V573 `98e350794f61c473cc5fa53b997a67d94a25a75b`.
Owner headset performance validation is pending.

## Observed performance ceiling

The owner reports improved Reference delivery but approximately70 output FPS
without Neural, versus the earlier OFXR-Off reference near71. A read-only sheet
check identifies71 atS38 and the later73 baseline atS39; these are the Meganex
OFXR-Off column, not an unloaded-DLL baseline. No sheet values were changed.

The V573 PID3468 run contains22,021 accepted packets and22,021 successful real
End returns with no terminal transport error. In the late Neural-Off windows,
Fidelity output median70, Reference68.35, Djules/New69.75. All use the common
consumer, so the ceiling is not a particular generator's workload alone.

Reference's actual mean handoff interval is14.149ms: the next private head is
ready approximately0.726ms after the previous End starts, then the distributor
waits10.883ms, then Wait/Begin/copy preparation adds2.540ms. Every sampled head
in that settled Reference window is ready before the next11.111ms deadline.
Approximately99.2% of observed preparation durations would fit in its remaining
interval if started when the head was ready. This is a timing-budget comparison,
not a simulated physical headset FPS result.

V572 placed the full cadence wait before preparation. V573 retained that order.
It therefore added preparation to every physical interval, even under a full
FIFO. With the measured costs this explains the approximately71FPS output cap.

## Correction

After observing the exact head's GPU completion, the consumer now calls real
Wait/Begin and prepares/copies/releases the real images DURING the interval
after the prior handoff. Once the copy is complete it waits only the residual
time until the common deadline, then calls real End. A late completed copy
shifts the phase by its exact overrun; the next handoff remains at least one
physical period later. No catch-up burst is allowed.

Begin still precedes GPU work and End still receives the exact raw prediction
from its own Wait. No manual XR timestamp adjustment is added. The dedicated
real-runtime queue fromV573 remains isolated from future producer waits. FIFO3,
image leases, backpressure, synthesis, AFW, A/B, and VDXR behavior are retained.
This change applies to the common SteamVR consumer, nativeD3D11/D3D12 and the
existing translated paths. It does not remove real GPU preparation cost.

New transition5718 marks residual cadence wait completion, after5715 copy
completion. Both carry packet sequence and QPCns inb. Stages5717/5716 continue
to mark real Wait entry/return, now before the residual pacing wait. The old
analysis formula5717minus5714 is no longer the cadence wait; use5718minus5715.

## Focused evidence

An actual GPU fixture gives runtime image availability a precise4ms delay.
Against exact sealed V573 source, mean output spacing is16.768ms and the
overlap assertion fails. Final V574 gives11.556ms D3D12 and11.726ms D3D11 while
preserving every image, raw runtime prediction and physical minimum spacing.
These are fixture timings, not Ghostwire performance measurements.

The first development fixture used a coarse4ms sleep and exceeded its own
preparation budget. It now uses a high-resolution waitable timer. The negative
control was then repeated against exact V573 using this corrected fixture,
and the final V574 source was restored and fully rebuilt before final checks.
The prior fake0.75period intake cutoff was removed: it is not an OpenXR rule.
Final fixtures assert call order, exact runtime time, ownership and actual
overlap throughput instead. SeeV574_VALIDATION.json for the complete check list.

First owner trial: restart tray/game withV574, same Ghostwire location and
resolution, Neural-Off,2X,F9. Compare Reference then Fidelity about15s each.
Compare both real+generated production and compositor output, not acceptance
alone. No full benchmark or new dump is needed for the first verdict. No live
installation, activation or online publication is performed automatically.
