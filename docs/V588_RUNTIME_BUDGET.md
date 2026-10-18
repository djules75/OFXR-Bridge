# V588: native-entry forecast follows changing runtime work

Base sealed V587. The new MSFS run still has optical drops. Its detailed ring
covers only the final 8.3 seconds on 0.3.0, not all tested routes. Compact medians
reach90 but the0.3.0 mean is 87.884 and minimum 62.222 fps.

## What the new run establishes

The V587 shared-image gate no longer appears: preparation typically starts
9.84ms before the prior End returns.716 of 724 copies complete at least0.2ms
before their deadline. NativeWait remains variable, median8.364ms and maximum
13.203ms. A visible staircase adds0.31-0.42ms per output even though image copies
were ready13-19ms early. The old forecast follows the previous Wait/Begin cost
without reserving its positive variation; it also excludes the realRelease
introduced on the consumer inV587. It therefore opens the next cycle too late
through a rising sequence, independently of GPU image readiness.

An inferred709-frame association matches nativeWait entries to OpenVR
WaitGetPoses timestamps with3.4us median absolute residual. The runtime's frame
ready signal occurs about0.218ms after End entry and before End return. This
does not support future GPUcopy capture as the cause in this retained tail.
Ten zero-present receipts remain real losses, not merely a meter disagreement.

This is not the only observed disturbance. Six late-source episodes and two
latecopy episodes also occur. A native call longer than a physical period or
an unavailable source cannot be made into continuous90fps by a timing forecast.
The GPU's overall utilization alone cannot establish those per-frame deadlines.

## Change

Forecast nativeWait/Begin plus realRelease service time and its observed positive
variation. The first sample does not invent a jitter penalty. Existing service
cost decays by1/16 of excess; variation decays by1/64. Both observations and the
resulting forecast are bounded by the physical period. The prepared-FIFO wait
is excluded, so source delays do not masquerade as native service cost.

Only native entry can move earlier. Physical End deadlines, headset period,
100us metadata lead,2usguard, held lateness and source refill do not change.
No producer wait, GPUwait, fixed extra latency, XRswapchain, image or FIFOcredit
is added. V587 target-generation lookahead and atomicHz snapshot remain intact.
The common SteamVR transport is affected; VDXR and AFW/interpolation math are not.

OpenXR explicitly permits [runtime throttling and variable frame timing](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html).
The duration of that call is not a constant GPU-work estimate.

## Validation and next comparison

A new actualGPU/mocked-runtime test supplies40immutable packets while nativeWait
cost rises0.4ms per cycle. ExactV587 fails it: mean11.498830ms and15of21late
intervals. The corrected transport measures11.126810ms with2of21late intervals
and passes with strict physical minimum and all
FIFO pixels retained. Pure tests cover startup, rising cost, bounded forecast
and eventual low-cost recovery.33 focused checks pass including existing native
freshness, D11/D12 ownership, changingoverlay, stop/restart, routes,UI andINI.

The first suite attempt failed the existing precision-average assertion before
the new staircase test (11.702408ms). That assertion was not changed; a separate
precision rerun passed11.118161/11.113195ms. Earlier evidence remains packaged.
Synthetic/mocked checks do not prove headset improvement under the MSFS workload.

With F9,5725 records realRelease entry/duration;5726 records Begin completion
and the next reserved native-entry budget. No optional GPUquery resources are
added. The two required release QPC readings support scheduling even with F9off.

Restart MSFS and tray withV588, same scene/settings/refresh. About20-30seconds
on 0.3.0 withF9 is enough initially; noF8/fullbenchmark needed. Compare the rising
Wait staircase and remaining source/copy/runtime outliers separately.
This local candidate is runtime-pending and does not install or publish itself.
