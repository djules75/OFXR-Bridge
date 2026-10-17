# V565: Luke scene FPS receipts and AFW-only benchmark qualities

Local candidate on accepted Crimson V564 b28045fc. Headset validation pending.
Owner explicitly prioritizes meter correctness; no pacing, synthesis, image,
AFW mathematics, shader, swapchain or production-performance change is included.
The previous25+25 observation is a variable sample, not a fixed route rate.

## Why the old numbers were inflated

PID25892: Fidelity6s contains182real,182synthetic and159held repeat submissions;
Reference5s contains134real,134synthetic and141repeats. All can get separate
compositor records. De-duplicating repeated scanouts of one compositor record
alone does not de-duplicate re-submission of the same scene image. AFW-only
also reuses its completed stereo pair across later XR intervals.

## Measurement contract

For every detected Luke title the meter, desktop/F10 and small overlay now
require a source identity and a SteamVR presentation receipt. AFW identity
travels with its actual produced stereo pair; held intervals keep the original
production serial. OFXR current/generated outputs retain that source identity
and separate output roles. Raw AFW-off input uses the existing strictly pinned
CPU completed-source reader when supported (currently the matching Crimson/HFW
runtime). Unsupported raw-source identity remains N/A; this build does not
pretend to measure all other raw title layouts. No new marker/GPU readback or
DLSS requirement is introduced for AFW-off OFXR operation.

Around successful, nonempty downstream EndFrame, observe the current OpenVR
frame record. Require the compositor scene PID to match this process and a
changed frame index/readiness receipt across that serialized call. Associate
that observed record with the submitted source/output identity. Unchanged or
unavailable records are not attributed using a time estimate. Only subsequent
positive NumFramePresents records enter the rate. Repeated identities, including
new compositor records naming a held scene, count once; unshown records count
zero. Missing/ambiguous identity or attribution makes the window N/A. Native
interpolation not confirmed as synthetic cannot become generated FPS.

The measured Luke output and real/generated breakdown share one presented-record
window and the labels are Real presented/Gen presented. They are not a sum of
submission counters or a refresh cap. VDXR has no such compositor proof and
remains unavailable rather than being filled with submitted FPS. The new live
attribution itself needs the owner SteamVR trial; automated tests do not prove
that the installed runtime publishes its current timing record synchronously.
F9 codes presenter_source5650/5651/5652 record that evidence if unavailable.
Valve API reference: https://github.com/ValveSoftware/openvr/wiki/Compositor_FrameTiming

## Benchmark/export

Luke runs18trials: six routes (AFW alone, Fidelity,0.2.1,Djules2X,Djules3X,0.3.0)
for1x1,2x2,4x4. No AFW-disabled pass. Ten seconds each, first3discarded. Main CSV
retains six metric rows but18qualitycolumns. Filenames/Build now identify the
actual compiled version rather than hardcoded558. CSV consumes the same scene
presentation proof as the meter. The local-only owner adapter reads both old
24-column and new18-column Luke CSVs, preserves Q:BF/AB/Prefer/reference mapping;
it is not in the DLL/source package. No online-sheet write.

## Validation and next trial

14focused CTests pass21.22s; later held-pair/packet assertions pass2tests2.86s
(one overlaps):15unique applicable tests. Unit control90submissions with25real+
25generated+40repeats yields50FPS;47raw sources across90XRrecords yields47.
Unknown/conflicting identities and unshown records fail closed. Actual GPU
held-pair and post-AFW packet fixtures preserve production identity through
later XR intervals. D3D11/D3D12 overlay fixtures verify no submission fallback
and unchanged composition/ownership.18trial CSV and local adapter pass.

Restart tray/game withV565. Before a full benchmark, check the number briefly
with both off, AFW alone, then one OFXR route, F9on. If N/A persists, retain the
run: it means source/compositor proof is missing, not zero or a fabricated90.
No fresh dump is needed. Performance work resumes after trustworthy measurement.
