# V577: combined Lower VRAM and shorter benchmark suites

## Controls

Frame generation exposes one Lower VRAM checkbox. It requests private synthesis
outputs and reduced XR rings together; turning it off clears both legacy fields.
Old persisted A-only/B-only profiles migrate to combined on. Internal embedded
transport fields remain for compatibility; Autonomous settings keep them paired.
The separate Prefer FPS UI and its dedicated benchmark trial are removed.
Existing scheduling code/preferences remain; benchmark 2X uses the former normal
comparison (Prefer FPS off) and 3X retains its required throughput behavior.

Luke treats both requested memory controls as zero at session initialization and
live reconfiguration. The former A-only delayed acquisition/publication selection
is disabled. It does not alter AFW quality or the established AFW provider/math.
The checkbox is unavailable while Luke is reported; stored preferences remain
usable for ordinary games. Actual inline GPU probes verify both requested states
use the normal Luke order with identical current stereo pixels.

Display has an Experimental features collapsible section below the benchmarks.
It contains the existing Reduce black bars during headset rotation flag, with
its existing semantics and persistence. Moving the control is not a claim that
the experimental behavior improves this owner's games.

## Suites and CSV

Each configuration still lasts ten seconds after application. Only the first
three seconds are excluded. Ordinary routes run Normal then Lower VRAM; there
are 11 optical and 17 full C-only configurations, including one OFXR-off trial.
No A-only, B-only, standalone Prefer FPS or DLSS-G A benchmark is executed.
Luke stays at 18: AFW-only plus Fidelity, 0.2.1, Djules 2X/3X and 0.3.0 for
each 1x1/2x2/4x4 quality. Original settings restore on completion/Stop.

The horizontal CSV retains the six FPS/Cycle estimated min/median/max rows and
adds four scalar rows: VRAM end MiB, RAM working set end MiB, RAM private commit
end MiB, Shared GPU end MiB. The details CSV also records the sample timestamp.
These are snapshots, not medians, maxima, or peak allocation measurements.

After 9.5 seconds the controller requests one revision-bound sample. The next
game End reporting scope queries its rendering adapter via DXGI CurrentUsage
LOCAL/NON_LOCAL and its own K32 process working set/private commitment. It then
publishes the reply without touching GPU queues, waits, swapchains or synthesis.
The controller never waits for the response and changes configuration at ten
seconds as before. Missing/late/unsupported data stay N/A, not zero or stale.
No continuous memory polling is added, and F9 is not required. An empty request
uses an atomic fast path before the existing shared mapping lock.

VRAM is this process's usage on its rendering adapter, not total GPU usage or
the budget. Runtime allocations attributed to SteamVR or other processes are
outside that number. Working set is RAM resident for the process; private commit
is separately labelled and must not be described as resident RAM.

The owner's sheet adapter remains local under .local/tools and is excluded from
Git/product ZIPs. Current Q:AS mapping has three columns per mode. Ordinary
measurements fill Normal and Lower VRAM, leaving the third empty; Luke fills
all three qualities. S/T/U hold AFW-only qualities for Luke, while ordinary
OFXR-off uses only S. Q/R manual references are preserved. Each measured cell is
FPSmedian(min-max)LATmedian(min-max) VRAM=endMiB. Unknown stays N/A, untried n.a.
Legacy 00/AB files map to Normal/Lower VRAM; A or B alone are not substituted.

## Compatibility and validation

Version 0.577.0, control mapping ABI 57701, provider ABI 45601. Restart matching
tray/runtime so older mappings cannot be mistaken for this benchmark protocol.
PureDark beta6, AFW math, V575 private distributor/cadence and V576 DLSS-G capture
remain. This is not a DLSS-G visual repair.

Focused checks cover actual ImGui interactions on desktop/headset surfaces,
profile migration, full suite progression/restoration, all18Luke tests, one-shot
revision-bound memory sampling with F9 off and an independent same-process DXGI
reading, current sheet/legacy adapter mapping, Luke inline/worker ownership,
SteamVR delivery and VDXR normal/combined modes. See V577_VALIDATION.json.
The initial memory fixture's early return retained a stopped endpoint for safe
teardown; an old A-enabled fixture was updated to assert the explicitly requested
Luke no-op rather than the removed pre-acquire experiment. Pixel/fence assertions
remain. Headset benchmarking and actual memory savings remain owner-pending.

The next owner run can use V577 for both these shorter suites and the pending
DLSS-G F8: the native output capture from V576 is retained.
