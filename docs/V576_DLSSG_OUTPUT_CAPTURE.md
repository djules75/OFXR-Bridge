# V576: observe native DLSS-G intermediate and composed pixels

Diagnostic candidate only, based on sealed V575. The owner reports that MSFS
now works correctly on all tested routes except mild double images on DLSS-G,
visible on a stationary propeller while moving. This accepts the observed
MSFS improvement, not every game/headset or the remaining DLSS-G visual issue.

## V575 evidence

PID3176 flight log: 4,558 private acceptances and 4,558 successful real OpenXR
End returns, no transport errors. The logged interval covers DLSS-G C, 2X,
full native resolution, A/B off. It includes F8 readback stalls: do not use its
whole-run FPS as a new benchmark or infer that every frame was physically shown.
The median handoff interval is 11.114 ms, with slow capture/loading outliers.

The F8 has eight original stereo pairs and raw engine motion/depth, but no NGX
generated output. Across seven successive left-eye pairs, tracked feature
motion agrees with the raw backward engine vectors: median residual magnitude
0.054-0.074 output pixels overall and 0.061-0.106 in the propeller region.
This excludes a large raw-input displacement/sign/scale error at those sampled
points. It does not prove the later packed inputs or generated pixels correct.

## Change and bounds

V576 records the aligned previous color when a reseed actually occurs, packed
current color/motion/depth, actual NGX interpolation and final composed output.
Snapshots are on the existing producer command list, with source state restored.
Only an active full F8 session can claim one pair per stream, at most two
streams. Normal execution and the F9 rolling ring do not enable these copies.
The existing 48-image / 1.25 GiB budget stays; later ordinary snapshots can be
dropped when the budget fills, reported in README/manifest. No new always-on
GPU waits, queues, XR swapchains or worker threads are introduced.

The native helper explicitly associates its captured command list after actual
submission with the recorder's completion fences; no assumption about a hooked
ExecuteCommandLists call is required. Resource/state retention lasts through
completion. These fence signals/readbacks occur only for a claimed F8 pair.

Stages in manifest.tsv: dlssg_aligned_previous, dlssg_packed_current,
dlssg_packed_motion, dlssg_packed_depth, dlssg_ngx_generated,
dlssg_composed_output. A feature can contain both eyes. Manifest swapchain for
these stages is private resource identity, not an XR handle; xr-timeline maps
that identity to the output index. capture_serial is the guide publication.
guide-contracts.tsv native_dlssg_pair holds original A/B poses/projections.
native_dlssg_output_camera has B as source pose, while destination_orientation
holds the output-to-B rotation (identity if absent), mode is output index.

Interpolation math, model request, guide scaling, output poses, AFW, VDXR and
the complete V575 private distributor are unchanged. V576 is not a visual fix.

## One next capture

Restart the V576 tray and MSFS. Same scene, DLSS-G 2X, AFW/Neural/debug off,
F9 optional. Reproduce the slight doubling while moving and press plain F8
once. Wait for the capture to finish, then close or switch F9 off if it was on.
Do not compare FPS while the full-resolution diagnostic readbacks are active.
The captured NGX output versus composed output distinguishes interpolation
from the later camera/compose stage without modifying working delivery.
