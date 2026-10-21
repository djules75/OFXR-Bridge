# V619: reduced AFW colour raster candidate

Base: V617 d3cdc173a73679989300a6fddf8b7a9fc590a614. V618 diagnostics are excluded.
Status: local/runtime-pending. No installation or publication.

The owner reports coloured contour shadows in BOTH eyes at AFW 2x2/4x4 in
Horizon Forbidden West, with 1x1 apparently correct. Two fresh V617 F8 folders
provide one complete first evaluation per generated eye. In each, provider
returned colour, bridge generated colour and corresponding XR terminal colour
are byte-identical (RGBA, 3408x3123). Thus the final copy does not introduce the
artifact. Both recorded settings snapshots are 2x2. The requested third dump
was not present at inspection; no 1x1 capture is claimed.

Offline replay of captured colours, corrected depth, Normal motion and matrices
shows a different contour reconstruction when using beta6 reduced-rate lookup
versus an actual smaller colour raster with the provider resolving at 1x1.
Prior camera reconstruction is approximate; this is not a bit-exact replay of
all hidden SDK history, nor proof of a specific proprietary shader defect.
A naive removal of shader rounding worsened the independent GPU fixture and
was rejected. No replacement DXIL, modified provider DLL or probe is shipped.

For Combined 2x2/4x4, V619 instead initializes AFW at ceil(eye/factor), area
resamples each incoming colour, retains the accepted full guide-domain depth
reconstruction and camera subtraction, scales motion gain by the exact work/
eye ratio, and linearly expands the generated eye. The provider resolves at
1x1 on that working raster. Camera matrices stay normalized and unchanged.
One private full-size expansion texture is allocated only for reduced quality.
There are no new XR swapchains, waits, readbacks or pacing changes. Existing
allocator completion retires resources and descriptor reuse. Quality/mode
changes retain isolated provider replacement; frame discontinuities remain
conservative. Formats without typed UAV stores keep their prior provider path.

This is a quality-path workaround, not a transparent shader correction: the
generated image loses detail, especially at 4x4, because colour is genuinely
reduced. Full-resolution real eyes and the existing 1x1 path stay unchanged.
Visual acceptance and performance in the owner's headset remain pending.

Validation includes real pinned beta6 execution, per-eye independent rotated
plane errors, native spans 1/2, odd crop/jitter, interleaved input, live quality
changes, publication gaps, producer epoch and geometry resets. Error limits
are checked per eye rather than averaged. An independent CPU area/bilinear
oracle checks every RGBA channel and edge of an odd 37x29 GPU image for both
factors (max error <=1.6 byte levels). D3D12 errors fail the test.

Owner check: same Horizon scene, matching V619 tray/game, AFW Combined, compare
1x1 ->2x2 ->4x4 in both eyes. Check contour halo, acceptable softness and FPS.
A new full benchmark is unnecessary for the first visual check. If the halo
persists, retain the unaccepted candidate locally and capture its exact inputs.
