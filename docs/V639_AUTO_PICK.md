# OFXR-BRIDGE 0.3.0 (v639)

Local laboratory candidate on V638, without Git mutations or active installation.

## Automatic benchmark choice

Display / Automatic tests now contains **Auto pick best one**, disabled by
default. It is saved as `[ofxr] benchmark_auto_pick` in the current game profile
(or desktop rendering template without an active game), not as a global tray
choice. A suite freezes this preference when it starts; it cannot be toggled
mid-suite. Existing CSV columns, sample windows and trial counts are unchanged.

Only a fully completed suite can apply a result. Eligible trials must have a
complete verdict, at least two positive finite presented-FPS samples and at
least two positive finite Cycle estimated samples. Missing timing, failed
generation/AFW confirmation, partial trials and zero delivered rates cannot win.
The standalone FidelityFX route is excluded for image quality at the owner's
request, including all pacing/Fast/AFW variants. It still runs and exports to
CSV. Hybrid, FSR and DLSS-G remain eligible. Off and AFW-only references are
eligible when their delivered performance and latency actually win.

Selection uses unrounded measurements:

1. Find the highest eligible median FPS; retain trials at most 1 FPS below it.
2. Within that group, find the lowest median latency; retain trials at most
   2 ms above it.
3. Prefer the narrowest absolute FPS min-max range, then the narrowest latency
   min-max range. Exact remaining ties prefer lower median latency, then higher
   median FPS, then the original trial order.

Thresholds are anchored to the global extrema of each stage, avoiding drift
through chains of pairwise near-ties. Selection is independent of trial order
unless all compared statistics are exactly equal.

The selected tested settings are applied live and persisted through the same
atomic per-profile writer as ordinary controls. This includes the engine,
pacing, Normal/Fast, 2X/3X and, for Luke, AFW quality. Untested preferences such
as Neural configuration remain as they were. The result message identifies the
route, Luke quality where relevant, FPS and latency with min-max values.

With the checkbox off, completion restores the original settings as before.
No eligible result also restores them and reports why. Stop/disconnect/error
never chooses from partial results. Manual edits cancel the suite and are kept.
An automatic-save failure attempts to restore the original profile and reports
an error. Benchmark enablement remains a separate global tray option.

## Scope and validation

No change to pacing algorithms, AFW motion/depth math, capture or providers.
Selection runs once after a benchmark, with no new per-frame measurements.
IPC/discovery is version-isolated as 63901. PureDark beta6 remains unchanged.

Tests cover tolerances, anchored comparisons, medians, stability, invalid and
failed data, actual checkbox behavior, persistent settings, cancellation and
automatic completion for legacy VDXR, basic private optical/full and Luke.
Existing advanced suites remain covered. Full checks are in V639_VALIDATION.json;
physical headset acceptance remains pending.
