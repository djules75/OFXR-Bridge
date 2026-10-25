# V584: independent preparation and physical output

Base sealed V583. The owner requires the distributor to consume completed
frames independently instead of blocking behind preparation of later ones.

Valhalla's compact V583 rates confirm a production/presentation gap across
routes. The retained detailed tail already shows11.113ms on early-ready
handoffs. The compound real-image preparation stage can take several ms;
the same thread previously performed it and the physical frame clock. This
does not prove every route's loss has one cause or that GPU copy alone accounts
for the whole measured compound interval. Full evidence stays local.

## Pipeline

The input endpoint still accepts owned images/metadata into the same bounded
three-credit FIFO and returns to the game. A separate preparer observes source
GPU readiness, acquires/waits real images, copies and observes exact completion,
then releases images and publishes a completed packet. Runtime Wait/Begin and
fixed-period End delivery run on the other thread, concurrently with preparation.
There is no acquire/copy/fence wait in that physical delivery loop. An empty
ready queue produces no new output; notification resumes the clock with the
exact retained late displacement, without catch-up or a shorter physical period.
The ready event wait is idle queue consumption, not a GPU/producer operation.

A real chain remains leased until every queued End using its current contents
has returned. Preparation of a different source on that chain waits outside
the delivery thread. Unchanged immutable private images, such as an overlay
still leased by both packets, share prepared contents rather than serializing
the scene or allocating another chain. Private acquire cannot rewrite a leased
image. End preserves each packet's immutable pose/FOV/layer metadata and the
actual runtime prediction. The existing100us early metadata window is retained.

No additional XR swapchains or GPU image pool are allocated. Input, preparing,
ready and presenting entries all occupy the original three credits. A full
pool still backpressures the game. Stop/error wakes both workers, joins them,
drains submitted GPU work and only then clears leases. Different source images
cannot replace the current packet's real contents before its EndFrame.

The preparation/acquire path may run outside BeginFrame. OpenXR specifies
acquire/wait/release ordering per swapchain, not a BeginFrame prerequisite:
[Acquire](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrAcquireSwapchainImage.html),
[Wait](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitSwapchainImage.html).
The preparer completes and releases an image before waiting another on that
chain; a busy chain's next source is held until presentation consumes it.

All SteamVR routes share this transport, including native D3D11/12 and existing
Vulkan/D3D11-to-D3D12 interop. VDXR remains unchanged. No AFW, guide, shader,
interpolation, preset or user-configuration change. Optional trace clock sampling
is bypassed with F9 off; no new GPU profiling is enabled. Wait trace emission
can follow copy completion in the file; its saved QPC still marks actual Wait.

## Validation and owner comparison

Tests verify two ready outputs continue while a third preparation is blocked,
different preparation/delivery thread IDs,5ms runtime waiting overlapping8ms
preparation at approximately one refresh, immutable overlay sharing, exact FIFO
pixels/metadata and strict minimum spacing. Existing stop/failure/restart and
all shipping generator routes/flags remain covered. The fixture measures native
entry before its own validation; physical period/pixel thresholds are unchanged.
Exact results and limitations are in V584_VALIDATION.json.

Headset result pending. This establishes the architectural separation, not
complete recovery of every compositor frame. Compare V583 and V584 in the same
Valhalla scene/route/flags/refresh. Close the game and old tray completely before
switching renderer versions; a tray change alone does not prove DLL replacement.
Local candidate only; no live installation, profile edit or publication.
