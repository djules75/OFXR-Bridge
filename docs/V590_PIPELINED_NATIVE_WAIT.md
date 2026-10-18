# V590: overlap native frame waiting and retain presentation headroom

The owner authorizes the missing structural change after V589: prepare the
next OpenXR clock independently of current delivery, and convert insufficient
headroom into held presentation latency. This candidate is based on sealedV589.

## Frame ownership and overlap

One nativeWait worker owns every raw xrWaitFrame call. The consumer owns all
xrBeginFrame/xrEndFrame calls and real image release. After currentBegin succeeds
and its packet is selected, post one nextWait job. It requires actual accepted
owned input, so no speculative frames appear during an idle application, while
its GPU preparation can overlap the native wait. There is at most one pending,
active or completed futureWait; no concurrentWait calls or secondBegin on an
unended frame. Stop joins the worker before native session teardown.

This uses the [OpenXR Wait/Begin pipeline rules](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html)
and preserves [Begin/End external synchronization](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html).

## Actual presentation margin

V589 clipped the native-entry forecast at one period. V590 allows its bounded
one-ahead clock job to start during current delivery. Forecast is capped at
2P+1.1ms, using the existing three-credit default image pipeline. After native
Begin/Release and source copy complete, require1.1ms headroom before End.
If the existing deadline lacks0.7ms, move it exactly0.7ms and retain that phase;
do not round to a refresh or add the same0.7ms again when already covered.

End spacing remains at least one physical refresh, with the existing2usguard.
No period shortening, new producer sleep, image, XRswapchain or credit is added.
The independent GPUpreparer and source-underrun refill remain unchanged.
Waiting for the current prediction is still necessary if even the lookahead
clock is late: this is not a promise to bypass a stalled native runtime.

Each packet uses its own native prediction, not the newest future result. With
the existing QPC conversion, a prediction expired through a long priorEnd is
advanced by whole physical display periods in the same runtime phase. Otherwise
the raw prediction is retained. The no-conversion long-stall case is not
headset-validated. Poses and owned FIFO pixels are not rewritten.

## Stop, failure and validation

Stop closes the current frame, joins an in-flight uncancellableWait and balances
one completed unused prediction with Begin/emptyEnd before session restart/end.
Unentered jobs cancel without calling the runtime. A futureWait failure does
not erase the current valid frame. A separate race exposed by the error test
is fixed: publish Endfailure before waking a producer with a returned credit.

33 focused checks pass. New actualGPU/mockedXR tests block the nextWait while
the current frame delivers on another thread; verify stop/join/restart, future
failure propagation, exact FIFO pixels and strict physical period. A long-held
End explicitly tests expiration of an overlapped prediction. Existing5msWait
plus8msGPUpreparation still overlaps. Schedule tests verify exact retained
latency, no repeated margin charge and unchanged refresh frequency.

ExactV589 source and headers fail the new overlap test as expected; V590 passes.
Earlier core attempts exposed the fixed credit race and one existing strict
native-entry timing assertion. That assertion was not relaxed; two focused
freshness repeats and the rebuilt full suite pass. Evidence is retained.
These checks do not reproduce MSFS contention or prove headset steady90.

One rebuilt-suite attempt also observed11.0988ms between mock native entries
onDjules3,12.311us below the strict minimum. Three predetermined isolated
Djules3 repetitions then pass with the assertion unchanged, followed by the
final full suite. This callback-boundary jitter is not conclusively attributed;
the earlier failure remains in package evidence.

F9 adds5727 for the exact retained margin and5728 for the native worker clock.
No optional GPU profiler is enabled withF9off. VDXR, AFW/vector/interpolation,
presets, INI, UI and benchmark policy are unchanged. All common SteamVR routes
use this transport, including existing D3D11/Vulkan translations to D3D12.

Restart game and tray withV590; same MSFS scene/settings/refresh,0.3.0 andF9
for20-30seconds initially. NoF8 or full benchmark is needed. Headset comparison
remains pending. This candidate does not publish or install itself.
