# V532: preserve benchmark labels across independent control revisions

The owner asks to repeat the AC Valhalla benchmark after fixing missing labels,
and uses `n.a.` for routes that were not tried. The V530 run retained FPS/cycle
samples through its end, but configuration records stopped after Prefer FPS.
The logger compared the Autonomous UI revision with the embedded renderer
revision. Those counters are independent: PreferFPS,3X,AFW/display/diagnostic
changes can advance the UI without changing the embedded renderer settings.
No route results after that mismatch are invented from the incomplete sidecar.

V532 checks the UI against its own applied runtime revision, and the session
against the embedded revision. On native Djules/DLSS-G, pipeline preferences
must also match the applied multiplier/PreferFPS state before a label is emitted.
Pending renderer/pipeline changes remain excluded. Later configuration records
keep the correct UI revision used by the recorded FPS/cycle samples.
This is a logging fix; no pacing, generation, GPU waits, shader, AFW, queue or
NVIDIA driver-profile policy changes. Selected V531 compatibility fixes remain.

The new production-logger fixture deliberately creates a PreferFPS UI revision
without an embedded revision, verifies its record, then verifies pending/applied
3X and a later pending/applied transport change. The former comparison omits
the first required record. No user game/profile is edited by this fixture.

The current exporter writes **n.a.** for a route/combination absent from the
recorded configuration attempts, and **n.d.** for attempted trials that are too
short, gapped or lack valid measurements. This distinction applies to every
destination layout. It never converts an invalid attempted result into a
successful result or silently substitutes another configuration.

## Repeat the Valhalla run

Close the game and old tray, extract the whole V532 ZIP and start this tray.
Keep the same scene, resolution, refresh rate and VR settings. F9 logging on
before switches; hold each configuration20-25seconds. Test Disabled and the
OFXR routes/transport flags you want through0.3.0. Without compatible guides,
leave Hybrid/FSR/DLSS-G untried: those cells become n.a. No image dump needed.
If B disables3X, it is not a valid3X trial; do not fill that3X cell with2X data.
Close the game and identify the run. The assistant returns the values-only TSV
from **S**, with Cycle estimated, after excluding the first3 AND last3seconds
of every settings interval. At least10useful seconds must remain after trimming.
**R** is measured separately without OFXR loaded; it is outside this export.

The package includes the current exporter, portable named measurements and
updated instructions. Meganex40cells S:BF, Pimax10cells BI:BR, Quest8cells G:N.
Paste at any matching first cell; use named headers to map a different layout.
Model A/C and transport A/B are distinct. A/C still require a game restart.
Actual headset/log-to-row validation awaits the repeated owner run.

Engineering validation:67 existing focused checks pass; the new production
logger case passes after explicitly initializing its fixture's transport flags
off. Its initial inherited A-on profile made the supposed transport change a
no-op, so that fixture assertion failed. The corrected case passes an isolated
rerun; raw initial/retest results are retained.68distinct checks pass overall,
with no further production change for the fixture correction. Python guards
verify n.a./n.d., both3s exclusions, column order and portable measurements.
Named-game tests were not repeated for this logging-only follow-up.
