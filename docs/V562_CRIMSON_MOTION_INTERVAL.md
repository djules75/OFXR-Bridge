# V562: Crimson native same-eye motion interval

Based on V561 (`47e8cde402e227ce45d1f99c2804332204a68f8a`). Local candidate;
V561 AFW was rejected by the owner for green motion debug even while stationary.
No acceptance of its raw OFXR correction is inferred from that report.

## Captured cause

The owner's V561 PID31836 F8 at 14:14:00 contains both exact AFW evaluations,
including native motion, converted depth, corrected motion, retained source
colours and native camera transforms. Its debug flag is zero: this is an input
analysis, not a claim that the captured returned image itself is green.

Across about 149,500 scene samples per eye, native motion fits current-to-same-
eye N-2 with median errors 0.260 and 0.194 guide pixels. Current-to-opposite N-1
errors are 12.740 and 12.927 pixels. Reconstructing the V561 shader from the
captured native fields, matrices, depth and jitter reproduces its actual bad
corrected field to below 0.028 pixels at p99 (half precision quantization).
Thus consecutive Streamline camera receipts do not imply consecutive-eye
motion: Crimson's retained field already uses its prior same-eye history.
The N-1 correction adds stereo disparity and an extra camera interval.

V561 also learned an inappropriate depth-remap bias 0.067444 from the wrong
N-1 assumption. The existing same-eye-span path already excludes that
alternating-eye calibration, and resets its state when the interval changes.
Native depth/stencil format19 raw capture is unsupported; the analysis uses
actual converted AFW depth and records that limitation. Raw source images and
logs are hashed and retained locally; sanitized metrics are included here.

## Change

For detected REAL VR in `CrimsonDesert.exe`, generic typed NGX capture declares
native motion span2. KCD scoped capture keeps its existing measured interval;
other titles retain span1. Original camera, stream, serial, resources and tokens
remain intact. The protected two-feature A/B/A join permits consistent span1
or span2 across all three verified frames; mixed/unknown intervals still fail.

Existing depth/camera subtraction handles that declared interval: subtract
current-to-same and reconstruct current-to-same, without inventing opposite-eye
motion or an additional jitter interval. AFW still receives measured depth and
its scene reprojection camera. No shader, provider, pose, swapchain-count,
pacing, A/B, raw OFXR or CSV change is introduced in this build.

## Validation and trial

Release build and 15 focused checks pass in16.27s. Actual NGX capture verifies
Crimson span2 and generic span1; protected registry GPU tests cover both spans,
exact original two-feature identity, token gaps and retired epochs. All272
native shader scenarios now also run with two-feature histories. Bundled AFW
three-mode independent plane oracle covers both interleaved spans, with KCD
and Indiana controls retained. Raw OFXR stereo, output ownership and capture
checks pass. No headset verdict is inferred from fixtures.

Trial: restart Crimson with V562, same scene and AFW settings, check stationary
scene and head motion. F9 logging is useful; no additional F8 required before
trying the fix. Private main and online release remain V557.
