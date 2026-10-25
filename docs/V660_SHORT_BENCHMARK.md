# V660: shorter benchmark suite and AFW-only automatic selection

Base: V659 ready-slot delivery, with the previously prepared local Best Pick
changes. Scene rendering, Director schedule, GPU synchronization, providers and timing
formulas are unchanged.

Every automatic trial starts only after actual settings, AFW quality, pacing
and buffer capacity report applied. It then settles for 2500ms and measures
for 5000ms, ending at7500ms. Samples at/after the deadline are excluded even
when the controller wakes late. Details CSV records2.500/5.000; Stop reports
only elapsed useful time, capped at5s. End VRAM is requested once at7000ms.
Countdown and README match. Legacy VDXR suites use the same shorter timing.
The existing Elliott meter adds a progress row from the cached shared snapshot:
actual test/count, applying/settling/measuring and current-trial seconds left.
It also shows completion and an AFW Only restart reminder. The existing paint
cadence/surface is reused; no added GPU readback, OpenXR swapchain or polling.
The log sidecar exporter preserves pre660 timing; only660+ automatic segments
use2.5s settling. Private owner CSV adapter needs no layout/statistics change.

AFW-only automatic winners now save the per-game AFW Only startup flag and
explicitly request a restart in the result. An OFXR winner clears an unapplied
AFW-only request. No startup route is changed during a benchmark. Stop/error
and manual cancellation retain their previous restoration rules.

The additional AFW-only quality preference is implemented in the scoring
header and tested at exact boundaries; public UI does not list ranking rules.

Scope: controls/benchmark/UI text and version660/IPC66001 only. No Git metadata,
push, live installation, user profile, runtime registration or driver changes.
Historical archive and V659 artifacts are retained.
