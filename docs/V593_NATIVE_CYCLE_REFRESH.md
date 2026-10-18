# V593: renew the native clock, retain the content

V592 owner Valhalla data confirms real losses, including the low-rate15+15
case. During the detailed level window,2550external/in-process frame IDs and
presentation counts agree;30records never become presented. The broader
42.17s level window receives3126frames and42are unshown. During startup,
seconds30-40 receive314and show266. Physical slots are active and all2554
recorded ends request/submit2layers with shouldRender=true; preparation is
early even for lost frames. This is not a missing-layer or vector refusal.

## Reproduce outside the game

A local standalone actual-SteamVR/OpenXR probe exercises the production
private runtime, with paired15Hz inputs and30fps content on the90Hz display.
It disables the OFXR API layer in its own process to avoid nested directors.
The exact V592 implementation and V593 use identical probe code, inputs,
phase, image size and environment. All361content submissions in each run
are matched uniquely to SteamVR receipt frame IDs through actual second-
Submit timestamps inside native End entry/return, with30us alignment tolerance.
V592 shows282of361; V593 shows361of361. One separate unattributed initial
record per run is excluded. The FPS meter formula is unchanged.

A GPU-completion wait and phase-only/explicit-timing/timestamp-only experiments
did not solve the sparse-input case. None of those experiments is shipped.
The probe and full captures stay local, not in the runtime or source archive.
At75/90fps there are still sporadic unpresented records. This controlled
sparse-input success is not proof of universal loss-free output or steady90,
and owner Valhalla/Luke game validation remains pending.

## What was wrong

After a gap, SteamVR can return an already-expired native prediction. Updating
only End.displayTime by whole periods leaves the native cycle stale. The
old director could therefore meet its physical CPU deadline while submitting
against an old native cycle. Buffer size and handoff frequency alone do not
repair that mismatch.

After GPU preparation, compare the raw native prediction, converted through
the enabled Windows clock extension, against the planned physical handoff.
Renew an expired, unsubmitted native clock using the existing Wait worker,
then Begin again on the consumer. OpenXR defines this Begin-without-End path
as XR_FRAME_DISCARDED. Only an unsubmitted native clock is discarded: the
same source packet, layer metadata, images and FIFO position remain owned.
No content frame is replaced, repeated, ended empty or silently retired.
Re-evaluate the absolute handoff after native work; post lookahead afterward.
Repeated pathological stale responses yield after8renewals instead of busy
spinning. shouldRender=false closes its ordinary empty cycle without renewal.
Stop/error paths keep Wait/Begin/End ownership balanced.

This common SteamVR endpoint covers every route, native D3D11 Luke Ross,
D3D12 and the existing D3D12 translations. AFW enabled/disabled does not select
a different fix. No provider/vector/depth/interpolation change is involved.
VDXR pacing/output is unchanged. No extra images, swapchains or pool credits.
Only existing full-pool backpressure can block a producer.

The advertised Windows clock-conversion extension is now negotiated at
instance creation even with F9/benchmark off. Runtime identity is known only
after creation, so this capability can also be enabled on other supporting
Windows runtimes; only SteamVR uses the private director. Unsupported clock
conversion retains prior behavior. No XR epoch is guessed.

F9-only5733records packet/expired prediction QPC/planned handoff;5734records
packet/final prediction QPC/renewal count. Native Wait ordinal0 also denotes
renewal. Additional native cycles mean packet/native frame-ID offsets are not
necessarily constant; join actual Submit timestamps within native End calls.

## Validation and owner comparison

33focused checks pass, including D11/D12 exact pixels through sparse stale
clocks, failed-renewal cleanup, nonrenderable recovery, common-route switches,
image/overlay lifetime and configuration/UI checks. Exact V592 fails the new
current-native-cycle assertion. The first full check found an old overlay
observation race; synchronize on completed preparation rather than its start,
retaining the exact3-acquisition assertion. Automated checks do not substitute
for the controlled real-runtime receipt comparison above or game acceptance.

Try the same Valhalla scene with0.3.0 2X,LowerVRAM/Neural off and F9 enabled
only in the level for about20seconds, then close normally. No F8/benchmark.
The external read-only observer can capture the same receipts. Luke Ross
is in scope too; its four OFXR routes still require owner gameplay comparison.
Keep V585 online reference untouched until acceptance.

[OpenXR xrBeginFrame contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html)
