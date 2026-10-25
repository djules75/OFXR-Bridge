# V587: prepare changing shared images before delivery

Base sealed V586. The owner reports unchanged MSFS2024 drops after the queue
priority experiment. This candidate addresses a preparation dependency proved
by the new timestamps, rather than increasing priority or changing cadence.

## Evidence

The retained detailed ring covers only the final Fidelity segment. In 25
ordinary copy-late gaps, preparation begins 5-13us after the previous native
End returns. Input was ready 13-21ms earlier; the scene chains differ from the
previous packet. A shared non-scene image lease therefore drains lookahead.
Its recurrence matches the250ms overlay update, although the exact quad handle
was not logged. The previous unchanged-overlay test did not cover this case.

The8.913ms median copy-fence observation is a CPU interval, not isolated GPU
copy execution. A separate approximately0.5s stall overlaps native calls and
observer polling; neither its cause nor its removal is established here.

## Ownership change

The preparer acquires, waits and completes the next real image while an older
released image is retained for delivery. It leaves that next target unreleased.
The physical consumer releases only the completed targets of its own FIFO head
before EndFrame. It does not acquire images, copy pixels or wait GPU fences.
This avoids draining all old packet users merely to prepare a different image.

There remains at most one successfully waited unreleased image per real chain.
Generation leases preserve unchanged overlays and protect repeated old images.
If the runtime returns the same real image index as an older retained generation,
only that alias waits until old users finish. Static images retain their sole
acquisition/release across drained packets and session restart. Shutdown drains
submitted GPU work and resolves held ownership before resetting the endpoint.

This follows OpenXR [last-released image selection](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html),
[waited-image ownership](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitSwapchainImage.html)
and [static release rules](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrReleaseSwapchainImage.html).
No extra XR swapchain, GPU image or FIFO credit is allocated. Physical cadence,
100us preparation lead, V585 source refill, interpolation and AFW math are unchanged.
Copies still use the existing binding queue: this change removes the proved
CPU lease gate; it does not claim isolation from all runtime/GPU queue stalls.

## Physical period observation

The director now reads physical Hz from an atomic snapshot. It never takes
the statistics mutex or calls OpenVR to read that value. The application
observer refreshes the cache even with F9/UI off, with nonblocking mutex access.
Unknown Hz retains the native prediction fallback. Optional slow statistics
can no longer hold the director merely for its frequency read.

## Validation and comparison

33 focused checks pass using actual GPU resources and mocked downstream OpenXR.
As a negative control, a separate Release build of the exact V586 transport
with the new changing-overlay test fails at the expected lookahead assertion;
the corrected transport passes that same test.
New tests hold a previous End open while preparing the next same-chain image
(D3D11/D3D12), change an overlay and read back both old/new scene and quad pixels,
force physical-image-index reuse, cancel a held target and repeat static images
across stop/start. Existing FIFO, strict period, all-route, UI and INI checks pass.
These checks do not reproduce the MSFS workload or measure headset scanout.
An earlier final-run attempt failed the existing strict native-callback period
assertion. Failure-only interval output was added without changing the assertion
or production cadence. Three predetermined full core repeats and the final full
suite pass. The original failure was not reproduced or conclusively attributed;
see the validation receipt rather than treating this as proof against host jitter.

With F9, stage5715 now means copy complete, target still unreleased. Release
occurs on the consumer before its own End. Other substage timestamps remain
CPU observations only; no optional GPU profiler is enabled with F9 off.

Restart game and tray with V587, same MSFS location, refresh and settings.
About20-30seconds on the same optical route with F9 is enough initially; no
F8 or full benchmark is required. Verify that updated shared images prepare
before the previous End returns, then compare delivered FPS and remaining tails.
This local candidate does not publish or install itself.
