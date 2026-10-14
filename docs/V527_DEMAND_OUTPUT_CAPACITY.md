# V527: no unused 3X output reservation

Owner requires Djules to work without either transport flag in both 2X and
3X, and removes unused 3X reservations in 2X globally. Both independent flags
remain explicit. Prefer FPS over latency remains available by subsequent owner
instruction; Quest3/VDXR showed no benefit, SteamVR comparison is pending.

## Trigger and allocation

The V526 Ghostwire SteamVR log refuses full Djules synthetic output creation
with `XR_ERROR_LIMIT_REACHED (-10)` and enters budget bypass. Djules reserved
two real plus four synthetic XR handles per source even in 2X, whereas the
working New/Reference allocation uses two plus two. Earlier automatic SteamVR
staging/reduction masked that reservation even with Lower VRAM off. V527 does
not restore that hidden override.

All applicable native D3D12 routes now use two real and two synthetic XR
handles per source with reduction off, or one of each with reduction on.
Djules 3X full/direct produces its two independent synthetics in those two
synthetic handles. CPU descriptor leases retain each until physical submission
has consumed it, preventing a later pair from changing the most recently
released XR image while an old submission still names that handle. These leases
allocate no private color textures. Direct 2X has no extra 3X lease allocation.
XR acquisition guarantees and GPU completion fences remain mandatory.

| Separate synthesis | Reduce XR swapchains | 2X | 3X |
| --- | --- | --- | --- |
| Off | Off | Direct, four XR handles/source | Direct, four handles, protected synthetic reuse |
| On | Off | Four XR handles + four owned textures/source | Four handles + six owned textures/source |
| On | On | Two XR handles + four owned textures/source | Two handles + six owned textures/source |
| Off | On | Direct, two XR handles/source | Existing UI restriction: bounded direct reduction is 2X |

The runtime chooses images per XR swapchain; the table counts handles, not
those images. The flag halves XR handles in every applicable engine. Separation
chooses owned compute destinations and a just-in-time consumer copy, without
secretly changing the allocation count. Reference/New do not reserve 3X.

Owned synthetic capacity is two textures in 2X, four only while 3X is selected.
Changing the multiplier drains old descriptors and GPU work, retires changed
output shapes before allocating replacements, and keeps input histories and
exact released captures. Returning to 2X frees additional owned textures.
Native DLSS-G multiplier transitions also rebuild the evaluator to release its
lazy second-output cache. Optical-flow shaders, interpolation fractions and
mandatory AFW depth/camera motion correction are unchanged. AFW/D11/Vulkan
interop retains its validated transport. Tray/layer IPC stays52601.

## Prefer FPS over latency

The flag does have an implemented 2X asynchronous behavior: staged worker
outputs can remain GPU-pending while the consumer repeats the last ready owned
real frame until the new synthetic is safe. The direct route still waits for
exact GPU completion, so that staged readiness benefit does not apply there.
It is not a SteamVR-only flag; a VDXR synchronous owner has no independent
consumer to perform this repeat. A VDXR asynchronous owner can use the same
policy. Do not infer that all Quest3/VDXR configurations are inert. Keep the
saved preference and checkbox; no new forced 2X enable, removal or latency
gain claim. 3X retains the existing effective preference requirement.

## Validation

The actual D3D12 layer/worker/presenter GPU fixtures impose a bounded eight-
projection-output budget for SteamVR policy; the separate poisoned-quad oracle
is outside that projection budget. Exact V526 source cannot initialize either
2X or3X with flags off under this bound. The candidate validates eight real
and seven or fourteen generated stereo outputs, auxiliary ownership, cycle
ordering, debug messages and teardown. Constant-color 3X readback checks each
synthetic's pair and interpolation slot, including delayed direct submission
while the producer attempts to reuse the same handles. Live3X->2X->3X exercises
actual capacity retirement and rebuild. Staged deep-readiness/switch tests
verify that Prefer FPS's 2X behavior remains implemented.

Mocked runtime policy/budget checks do not establish real SteamVR/VDXR headset
success or performance. Start a fresh V527 tray/game, Ghostwire UEVR, same
scene/resolution/Hz, AFW/Neural off. Test Djules2X then3X with both flags OFF,
followed by separation-only and separation+reduction. Record correct images,
Cycle length/estimated and Output FPS. Keep Prefer FPS constant for this
allocation comparison; test its SteamVR effect separately afterward.
