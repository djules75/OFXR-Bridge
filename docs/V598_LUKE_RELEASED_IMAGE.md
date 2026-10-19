# V598: preserve the released image across REAL VR acquire-ahead

Crimson Desert and Ghost of Tsushima abandon VR during startup with V597.
REAL VR records `xrEndFrame: XR_ERROR_CALL_ORDER_INVALID (-37)` and destroys
its OpenXR instance. The available evidence identifies our rejected frame;
it does not establish a native OpenXR runtime crash.

## Confirmed sequence

Both Crimson flight traces show the same application swapchain sequence:
release image 1, acquire image 1 again for the next render target, then End.
The failures occur at 3174.349 ms and 3220.334 ms. Other submitted chains have
not reacquired their latest release. Native director End calls succeed.
Ghost's REAL VR log records the same End error on both attempts.

The private transport previously selected any unacquired image with no active
source readers. When older images still had readers, this could select the
most recently released image. The application's next acquisition then made
that release ineligible for the current End lease, returning -37.

## Correction

For multi-image private chains, Acquire preserves the latest released image
for End and chooses an older free image. If older copies still hold all
eligible images, Acquire waits for their completion through the existing
condition variable. If all eligible images are already caller-owned, it
returns the existing exhaustion error instead of waiting indefinitely.
Multiple outstanding acquisitions before the first release remain supported.
Internal single-image outputs retain their explicit release/End/copy cycle.

No additional image, swapchain, queue or credit is allocated. This changes
only private image selection. V596 source-copy completion and V597's fixed
1 ms native End phase lead remain intact. AFW, vector/depth math, the physical
period, VDXR, settings, meter and benchmark layout are unchanged.

## Evidence and validation

The new actual-GPU test holds older source copies, releases a third image,
and acquires the next target before End. Exact V597 source reproduces -37
at that End. V598 passes on D3D11 and D3D12: Acquire waits for an older copy,
End succeeds, all three pixel values and metadata arrive in FIFO order,
and the swapchain budget stays unchanged. Additional cases cover acquisition
capacity and bounded exhaustion. The full focused suite result is recorded
in [V598_VALIDATION.json](V598_VALIDATION.json).

This demonstrates the reproduced ownership correction; it does not replace
an owner headset retry. V598 remains a local candidate. Launch Crimson first
with the V598 tray, then Ghost, using the existing configuration. Confirm
entry into VR and normal OFXR/AFW operation. No F8 or full benchmark is needed.
