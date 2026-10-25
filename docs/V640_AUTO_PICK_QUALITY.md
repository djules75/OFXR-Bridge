# OFXR-BRIDGE 0.3.0 (v640)

Local laboratory candidate on V639. No Git commit, tag, merge, push or active
installation. No change to frame generation, capture math or pacing algorithms.

## Luke Ross automatic choice

The existing Auto pick best one checkbox gains these ordered rules. They affect
selection only; trial execution and the owner-sheet metric CSV stay unchanged.

1. Exclude 2x2 unless each scene eye has at least one side of 3000 pixels.
   Exclude 4x4 unless each scene eye has at least one side of 4000 pixels.
   Unknown resolution permits only 1x1. FidelityFX remains excluded independently.
2. If an eligible AFW-only trial reaches at least 80% of measured display Hz,
   choose within those qualifying AFW-only trials rather than adding OFXR.
3. Within the remaining group, if 1x1 reaches at least 90% of display Hz,
   prefer 1x1, even when a coarser quality has higher FPS or lower latency.
4. Apply the existing 1 FPS / 2 ms / min-max stability comparison within that group.

Resolution is the actual primary submitted stereo scene rectangle per eye,
not the enclosing double-width texture, a HUD quad, DLSS input size or reduced
AFW working texture. Use the smaller of the eyes' longest sides; within a trial
retain the smallest known size observed during useful samples. No GPU readback
or new OpenXR call is needed. Display Hz is the median of existing telemetry
samples in the useful window. Missing Hz disables relative 80%/90% preferences;
it never invents a refresh rate.

## Native-vector preference

For every suite, first retain candidates within 1 FPS of the best eligible
median as before. If both optical and confirmed native-vector candidates remain,
prefer vectors provided their median latency is at most 12 ms above the lowest
optical median in that FPS group. Among qualifying vector candidates, use the
existing 2 ms and stability tie breakers. A vector route that never confirms
active native guides does not receive this preference; Hybrid optical fallback
still participates as an ordinary measured candidate. Extra latency does not
buy a loss greater than the established 1 FPS tolerance.

Off and AFW-only remain references, including possible valid winners. Complete,
positive measured statistics and generation/AFW confirmation are still required.
Stop/error/manual-edit behavior and default-off per-game preference are unchanged.

The details CSV appends EyeLongSide, DisplayHz and NativeVectors for auditing.
The main metric CSV and spreadsheet columns are unchanged.

## AFW-only status

The Elliott meter and F10 identify fresh, confirmed AFW-only operation as AFW
(or AFW + Neural) and color it green. This works with OFXR unchecked and with
the startup AFW Only transport. It does not claim OFXR generation, ignores an
irrelevant disabled-OFXR provider error, and retains stale/pending/idle and
required-Neural failure handling.

IPC/discovery64001, provider ABI45601 and PureDark beta6 unchanged. Tests cover
pixel and percentage boundaries, packed stereo/HUD exclusion, actual shipping
extent publication, vector latency limits, automatic controller selection and
meter raster colors. See V640_VALIDATION.json. Headset acceptance pending.
