# V571: private SteamVR delivery endpoint

Local candidate on exact V570 source ca4f0db24389a207c1d55ee0525910ca5609a74c.
Headset acceptance is pending. This is an architectural delivery change, not an
accepted fix for the Ghostwire/MSFS compositor losses.

All SteamVR generators now terminate their runtime-facing frame calls at a
private endpoint. Native D3D12 and D3D11 use the same queue/schedule policy;
existing Vulkan/D3D11-to-D3D12 translation enters through the D3D12 endpoint.
VDXR retains the original dispatch, image allocation and pacing.

The endpoint supplies private swapchain images, rather than exposing real XR
images to the application/generators. A submitted packet owns copied layer,
view, pose, rectangle and optional depth metadata, plus read leases on its
released private images and an exact GPU completion fence. No extra ingress
colour copy is needed. Image acquisition cannot return a leased image for
writing. Application chains have three private images (one for static content).
Internal generation/composition rings need only one per ring: their existing
role/ring rotation already supplies capacity. This avoids multiplying every
internal private pool by the runtime's real swapchain image count.

Virtual EndFrame returns XR_SUCCESS after accepting the packet, including when
its GPU fence is still pending. Reservations, queued packets and the in-flight
consumer all count against the three-packet FIFO. A full FIFO waits for actual
retirement, not dequeue. No newest-frame replacement or catch-up burst occurs.
Failures are propagated to later producer calls; stop wakes blocked producers.

Only the consumer calls real Wait/Begin/End and acquires real runtime images.
It waits for the head's exact GPU fence, copies into the acquired runtime
images, proves that copy complete, releases them, then hands off the packet.
Between successive content handoffs it enforces the physical headset period,
read from the established SteamVR delivery connection (reported XR period is
the startup fallback). Lateness shifts the phase by the exact nanoseconds;
the next deadline remains a full period after actual handoff. Phase displacement
is retained, not rounded to a whole frame. It is a diagnostic phase total, not
a new latency counter. The bounded FIFO limits retained content.

QPC nanoseconds and XR displayTime remain separate clocks. The runtime's raw
prediction is never moved backwards; a target is only advanced when necessary
for expiration/monotonicity. Provisional/ended sessions close a seeded real
cycle before restarting. Real GPU ownership waits remain intact.

The old generic pacing sleep, director phase/midpoint scheduling, held-frame
replay and 3X application-release delay are bypassed for this endpoint. These
controllers remain available on the unchanged VDXR path. The endpoint is shared
through route switches, rather than rebuilt per generator.
The old GPU join from reacquisition to the real binding queue is also bypassed:
the private image's read lease already proves its final copy complete. The
independent app-to-binding publication fence is retained.

SteamVR now always has the private-image boundary requested by the owner.
The old A staging copy is redundant there and is not additionally allocated;
A retains its prior meaning on VDXR. B still controls the generation output
ring allocation. Existing synthesis algorithms, their execution threads and
GPU safety synchronization remain: this does not promise zero CPU/GPU hook
cost or move every legacy generator operation to a new synthesis worker.
The detached boundary is runtime delivery.

Projection/depth, quad, cube, cylinder, equirect/equirect2 and layer colour-scale
metadata are owned. Unknown extension chains are refused rather than retaining
application pointers after EndFrame. Auxiliary content is copied with its
packet. No AFW motion/depth/camera/provider changes are included.

Diagnostics use presenter_transition 5711 (private acceptance), 5712 (successful
real End return), 5713 (submitted/raw XR target), and 5719 (terminal error).
Match sequence IDs; acceptance is not physical scanout proof. The existing
SteamVR presentation meter remains the source of compositor FPS.

## Validation and first owner trial

See V571_VALIDATION.json for exact final checks and rerun history. The private
endpoint tests use actual D3D11/D3D12 resources and independently blocked GPU
and runtime completion. Shipping-layer tests include real generator work,
private outputs, physical cadence and D3D12 debug validation. Their downstream
XR runtime is mocked; actual SteamVR timing/visibility still requires a headset.

Restart tray/game with V571. First use Ghostwire in the same saved location,
Neural unchanged, A/B off, 2X: Fidelity, Djules, Hybrid, FSR, DLSS-G, about 15s
each with F9. Compare visible movement and the compositor meter. Do not repeat
the full spreadsheet suite before confirming delivery is better. Then verify
MSFS strafe. V570 is preserved for comparison. No live installation or GitHub
publication is performed by preparing this package.
