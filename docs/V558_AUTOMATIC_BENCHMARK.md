V566 note: Luke total is New submitted FPS (distinct images accepted by the
runtime), not physical scanout. Existing OFXR real/generated counters restored.
The suite still has18AFW-quality trials and noAFW-off pass. V565 synchronous
compositor receipt mapping is withdrawn. See V566_LUKE_TOTAL_COUNTER.md.

> Current behavior from V565: Luke runs18tests, only1x1/2x2/4x4. The six
> AFW-disabled measurements are removed. Six metric rows remain; the Luke CSV
> has18qualitycolumns. Output FPS and real/generated Luke values require
> source-identity and compositor-presentation receipts. Unknown isN/A, never
> refresh/submitted FPS. See V565_LUKE_SCENE_METER.md. Earlier V558 details
> below describe its original24-step format, still accepted by the local adapter.

# V558 automatic benchmark suites

Local candidate on shared V557. All controls are in the Display tab in both
desktop Settings and F10. Use the matching V558 tray and runtime: IPC55801
isolates the extended control mapping from older trays; provider ABI45601 stays
unchanged. No live installation or changes to generation/guide/pacing math.

## Buttons and execution

- Tests without vectors: Disabled, FidelityFX, 0.2.1, Djules 2X, its 00 Prefer FPS
  comparison, Djules 3X, and 0.3.0. Transport variants run 00, A, AB, B.
- Complete tests (DLSS-G C): the same sequence, then Hybrid, FSR and four DLSS-G
  C transport variants. No A preset trial. The process-start per-game profile
  request must be C. Otherwise this button saves C for the actual running game
  and asks for a game restart before starting measurements. Saving the profile
  is not a live NGX model change, and the startup request is not independent
  proof of the active NGX model. No global NVIDIA profile is changed.
- Test Luke Ross: OFXR only through 0.3.0, including Djules 2X/3X. No vector-only
  generator, A/B permutation or Prefer FPS comparison. Four complete passes:
  AFW off, Combined 1x1, Combined 2x2, Combined 4x4. Each pass includes OFXR off,
  giving three AFW-only quality tests as well as the all-off reference.

Ordinary suites have 21 and 33 configurations; Luke has 24. Each configuration
gets 10 seconds after its settings report applied. Luke quality changes also
wait for the active shading rate. Application failures stop after 30 seconds;
they are not labelled as successful trials. The unsupported Djules 3X B-only
combination is omitted rather than silently changed to 2X.

The tray timer advances while menus are closed. Close F10 after starting and
keep the same scene, resolution, headset refresh and injector sync mode.
Neural settings are preserved. Ordinary suites preserve AFW too; Luke selects
the requested AFW passes. Diagnostic logging is preserved, not enabled for
the suite. Original settings and all route preferences are restored on Stop
or completion. A manual settings edit cancels automation and keeps that edit.
Disconnecting/changing the reported game process stops and restores settings.

## CSV and measurement windows

Output CSV is on by default and can be disabled before starting. It works with
F9 logging off and both menus hidden. The existing interface telemetry gets an
expiring measurement lease; no extra XR swapchain, GPU queue or detailed flight
recorder is added. CSV disk I/O happens in the tray/desktop timer process.

Files are under the local Autonomous profile's Benchmarks directory:

- OFXR-V558-RUN.csv: named horizontal layout, six statistic rows (Output FPS
  minimum/median/maximum, Cycle estimated minimum/median/maximum in ms).
- OFXR-V558-RUN-details.csv: one record per executed configuration, with game,
  flags, multiplier, AFW/Neural settings, sample counts and status.

For Luke the layout groups each route's columns in this exact order:
blank (AFW off), 1x1, 2x2, 4x4. Execution still runs whole quality passes.
The OFXR-off columns contain the all-off and AFW-only measurements.

Discard only the first three seconds after application, then use all available
samples until the next command. There is no three-second tail exclusion.
Samples are polled at up to 4 Hz and deduplicated by telemetry timestamp.
Medians are medians of these samples, not per-frame weighted statistics.
Cycle estimated is the existing cycle estimate, not measured headset latency.
Output FPS requires the existing compositor observer; unavailable delivery
(including VDXR) remains N/A instead of being replaced by submitted FPS.
The detail file distinguishes no confirmed AFW/generation and missing metrics.

The log exporter applies leading-only trimming to V558+; legacy logs retain
their historical first/last-three-second method. Automatic configurations are
marked so manual/restored settings after a suite do not overwrite its results.
Historical sheet A columns remain available to the exporter, but these suites
do not execute A. No labels are fabricated for an unverified live model switch.

## Validation

Optimized Release build; targeted control/UI/IPC/CSV/timing/median and existing
D3D11/D3D12 menu/meter/loader checks. The suite fixture advances virtual time
through all 33 complete and 24 Luke configurations, checks restoration/manual
cancellation and quality-layout order, and verifies hidden metrics with F9 off.
The exporter fixture retains a known late extreme at the end of a ten-second
interval and checks both medians and command boundaries. Hardware/game testing
of this new automation remains pending; this is not a new rendering fix.
