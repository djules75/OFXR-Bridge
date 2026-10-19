# V606: source-scoped UEVR publication sinks

High on Life 2 with UEVR Synched Sequential still refuses a right-eye OFXR
output with XR_ERROR_LIMIT_REACHED in V605. Private producer images alone did
not reduce native handle usage: each private output retained its own XR chain.

V606 keeps the existing normal producer rings (two current and two synthetic
buffers per source) and shares their native publication destination. For two
compatible separate-eye sources, eight virtual output handles now need two
physical XR swapchains. Application chains, depth, UI and overlays remain
separately owned. This is not an automatic Lower VRAM setting.

## Admission and scope

Only SteamVR's existing private endpoint, native D3D12, a loaded
UEVRBackend.dll, AFW/Luke disabled, and an admitted two-source projection can
opt in. Each source must map exactly one distinct view of the same projection,
with arraySize=1 and faceCount=1. Packed/array Native Stereo, non-UEVR callers,
VDXR, Luke Ross and translated D3D11/Vulkan do not opt in. The shape identifies
separate-eye UEVR rendering; the layer does not read or override UEVR's mode.

Only internally created generated outputs receive a publication source key.
Application handles never become pooled virtual handles. Compatibility includes
source identity, format, dimensions, usage/create flags, samples, faces, array,
mips and D3D12 release state. Different eyes or incompatible resources cannot
share a destination. Each independent output retains its private GPU image and
acquire/wait/release tracking. Normal/Lower VRAM and 2X/3X remain explicit policy.

## Native ownership

The preparer uses the existing single final GPU copy. Native acquisition,
waited/unreleased target, retained image-index leases and last-release state
belong to the publication owner shared by that source's rings. A cached copy is
reusable only for the same virtual chain, private image and generation. Reusing
an image index still held by an earlier native End waits on that exact lease.
The consumer releases the FIFO head's completed image immediately before its
native End. Owned layer metadata resolves virtual handles to physical sinks at
the native boundary; poses, rectangles, slices and extension contents stay intact.
Simultaneous distinct source domains use distinct sinks; invalid simultaneous
virtual buffers of a single domain are rejected rather than overwritten.

Destroying an output waits for its accepted consumers and source reads. The
shared sink is destroyed after its final virtual owner, so preset changes free
old resources and build fresh compatible pools. Stop drains GPU reads, balances
waited native images once per publication owner, and resets retained targets.
No director period, lead, refill, FIFO capacity, runtime-clock, generator shader
or guide-association policy changes are included.

## Evidence and owner test

The GPU transport test uses an artificial native budget of two handles, eight
private buffers, distinct left/right pixel sequences, a deliberately blocked
native End and FIFO backpressure. It checks return-to-old-slot identity, repeated
native image indices, exact stereo pixels, strict refresh spacing, incompatible
allocation failure, last-owner destruction, rebuild and session restart.
Shipping-generator tests use two source chains plus a native budget of two
publication sinks; they cover all seven frameworks, Djules/DLSS-G 3X and hot
route changes. Release-bound FSR/DLSS-G tests publish real immutable guides and
submission receipts. A non-UEVR separate-eye control keeps eight physical OFXR
outputs. D3D12 debug-layer errors fail the checks.

The initial integration run exposed fixture errors: the second application
chain had been initialized in COMMON despite its RENDER_TARGET contract, and
manual guide injection did not exercise release-bound split-eye selection. The
fixture now uses the proper application state and real pre-release publication
receipts; production guide/state guards were not relaxed. Initial results are
preserved alongside final validation.

With F9 enabled, allocation-only presenter stage 5737 records source handle,
virtual output handle and native publication handle. There is no new per-frame
query or always-on diagnostic. IPC60401 and provider45601 are unchanged.

Pending headset validation: restart tray and High on Life 2 with V606, keep
Synched Sequential and Lower VRAM OFF, try a normal optical route followed by
FSR and DLSS-G. F9 before the trial records admission and refusals; no F8 or
full benchmark is needed. Native handle reduction is measured in the test
runtime, not yet in the owner's game. No headset FPS improvement or independent
queue-mismatch cure is claimed. No live installation or online publication.
