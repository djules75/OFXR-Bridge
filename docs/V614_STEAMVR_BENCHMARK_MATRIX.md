# V614: SteamVR pacing benchmark matrix

Based on V613. This changes benchmark orchestration, CSV identity and UI guidance;
it does not change any renderer, director scheduler, interpolation, AFW contract,
queue, swapchain allocation, timing formula or preset selection.

## SteamVR only

The controller selects the matrix from the connected runtime, not from the
settings saved before launch. Each optical/vector route is tested with Framework,
Director and Framework+director, in that order. For each pacing mode ordinary
suites run Normal then Fast mode (both existing memory operations together).
The OFXR Off reference is measured once with Framework pacing.

- Without vectors: 31 trials (one Off plus five routes x three pacing x two modes).
- Complete: 49 trials (one Off plus eight routes x three pacing x two modes).
- Luke Ross: 39 trials. Three AFW quality passes, 1x1 / 2x2 / 4x4. Each pass
  measures AFW alone once, followed by Fidelity, 0.2.1, Djules 2X and 0.3.0,
  each with all three pacing modes. No AFW-disabled, Fast, vector-only or 3X
  trials are introduced into the Luke suite. Fast remains ineffective on Luke.

The existing VDXR suites are deliberately unchanged: 11 optical, 17 complete,
18 Luke trials, preserving their previous labels, order and saved pacing choice.
This version does not implement a VDXR director experiment. The new owner-sheet
matrix is for SteamVR results only; older VDXR CSVs must not be relabelled.

## Timing, metrics and recovery

Enable `[benchmark] enabled=1` in the one existing `ofxr-bridge.ini`, then use
Display > Automatic tests. Enable Output CSV for exports. Diagnostic logging
may remain off. Keep the same scene, resolution, Neural configuration and
refresh rate; close the menu after starting. Ordinary suites retain AFW choice.

A trial starts only after its settings revision and pending transition have
completed, its effective SteamVR pacing matches, and (for Luke) its AFW quality
matches. Run ten seconds, discard the first three seconds only, use the rest
until the next switch. Read end VRAM once near 9.5 seconds, against that trial's
revision; unavailable measurements remain N/A. FPS min/median/max and Cycle
estimated min/median/max keep the V613 measurement endpoints and validity gates.
The detailed CSV retains the requested pacing and actual startup DLSS-G preset
receipt. The suite never forces C, modifies an NVIDIA profile or changes models.
Stop/completion restores the original settings including pacing. Manual edits
cancel automation and win. The existing disconnected/timeout/INI-disable handling
is retained.

## CSV and owner spreadsheet

Main metric-row CSV headers include route, pacing and Normal/Fast, or AFW quality.
Luke executes whole-quality passes to avoid excessive quality changes; CSV exports
AFW-only three qualities first, then each route/pacing's three qualities together.
Every output column identifies a unique configuration. Detailed CSV still records
PacingMode, memory, useful time and status. No public IPC layout changes (61101).

The owner's separate local adapter is not part of the DLL, source archive or
product scripts. It validates the public sheet's actual 63-column AI:CS block.
Manual AG/AH references and CT Log / CU Tested Build are excluded from output.
Normal results use Normal/Fast/blank in each nine-column optical group. Luke uses
1x1/2x2/4x4, with 3X and vector-only groups n.a. Untried means n.a.; unavailable
recorded measurements mean N/A; intentional unused slots stay empty.
Each cell is `FPSmedian(min-max)LATmedian(min-max)VRAMnumber`, rounded to the
nearest integer, halves up; VRAM is MiB without a suffix. Paste one TSV row at AI
in the chosen game row. No Google Sheet writes are performed by the adapter.

## Validation boundary

See V614_VALIDATION.json. Automated controller/CSV and local owner-adapter checks
verify identity, order, restoration, applied-pacing gating, VRAM attribution and
all 63 destination cells. They do not replace the owner's in-headset suite run.
Load matching V614 tray and runtime before testing. No live installation,
registry mutation or publication is performed while preparing this candidate.
