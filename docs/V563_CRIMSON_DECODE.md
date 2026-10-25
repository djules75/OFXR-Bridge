# V563: exhaustive marker decode in bounded CPU parallelism

Parent V5622d766382. Local runtime-pending candidate. Original benchmark CSV,
provider beta6, motion/depth subtraction, shaders, output pacing and swapchain
counts are unchanged. This build does NOT claim the remaining mouse-motion
green debug is fixed.

## Measured costs and change

V562 PID17876 at145-160s: app EndFrame p50~22.2ms, native marker fence wait
p50~11.8ms; AFTER that fence, Map/luma/exhaustive decode/Unmap p50~9.86ms and
p90~12.43ms. Runtime prediction is overwhelmingly22.2222ms. Beta6 GPU cost
falls from~2.76ms full to~.35ms quarter, but this CPU work is unchanged.

Marker itself remains256x8pixels.3472-square DLSS to3472x3181XR colour requires
a260x303search ROI covering all potential vertical crops. All303rows still
participate. Three sleeping process-lifetime CPU helpers plus the caller now
partition rows across the same two immutable mapped readbacks. Join BEFORE
Unmap, deterministic original-order token/ambiguity reduction. No row sampling,
cached frame/identity, extra GPU command, frame buffer, timeout or delayed
presentation. Small ROIs remain serial. Allocation/start failures fail closed;
partial worker construction joins all started helpers before propagating.

Actual captured CRCs5040/5041 are identical to the original exhaustive search
on both ordinary cached bytes and mapped hardware-GPU readback. Fourteen warm
samples: original1.6015ms, parallel.4593ms on this machine. This microbenchmark
is not a claim that the game recovers the same delta under F9/game load.
Concurrent independent callers, unequal image heights, last-row markers,
corrupt/conflicting tokens and bad strides are checked; existing GPU origin
suite totals2425checks.16targeted CTests pass; see validation JSON.

## Corrected interpretation of motion evidence

The V562F8 buffers prove the provider overwrites a peer with generated colour.
That is NOT evidence that Normal motion must be halved or use N-1. An actual
bundled-provider rotated static-plane control gives zero green with exactN-2
and100%green with half gain. The API defines Normal as current-eye current to
current-eye prior. See
https://github.com/PureDark/UEVR/blob/f49cd54e919c368fdf1b253ac5b1266caf56436a/dependencies/pd-afwmod/include/PDAFWPlugin.h

Offline capture replay now reconstructs prior/source/destination cameras and
feeds full captured colour/depth/motion into that real provider. The second
frame of owner PID28408 reproduces the actual green-debug output at RGB_MAE
about.0041. First-frame bootstrap has incomplete internal history and is NOT
an exact reproduction. Do not mislabel the proxy green-pixel threshold as
whole-image coverage; the actual capture has green/red tints across static
geometry. Native N-2 camera-fit residuals rise to~2.38guide px in its first
frame, whereas stationary samples were~.2px. Remaining cause needs the
unmodified public ingress observation, not an arbitrary flow gain change.

## Next owner trial

Restart Crimson/tray withV563, F9on: compare AFW Off/Combined Full/Quarter at
the same spot for FPS. For remaining green, debugAFWon and F8 during mouse
rotation. F8 adds crimson_public_motion_before_dispatch and its exact
Streamline camera receipt at the OUTERMOST generic SR ingress, before native
forwarding. Runtime guide/camera selection stays unchanged. No capture work
runs outside explicit F8. Suppress only the two mutable full SR-output-at-
consume images in this interleaved span2 diagnostic (retained XR source and
completion-stage colour remain) to keep original1.25GiB budget and both final
eyes. This permits comparison of public/native motion without larger captures.
