# Bridge flight recorder

The recorder belongs to the OFXR OpenXR layer and is independent from game or
VR-mod logging. It is disabled by default.

V066 also provides an independent transparent numeric FPS overlay. It is green
when a recent synthetic submission succeeded and red when generation is
inactive. It counts accepted nonempty OpenXR submissions, not physical headset
scanout, and does not replace the recorder for crash or fallback diagnosis. See
[FPS_OVERLAY.md](FPS_OVERLAY.md) for its controls and limitations.

Enable **Bridge flight recorder** from the tray before starting the game. Each
OpenXR process then creates a bounded file named
`ofxr-bridge-flight-YYYYMMDD-HHMMSS-pidNNNN.log`. Use **Open bridge logs** to
open its directory.

The log records layer negotiation, graphics binding, swapchain eligibility,
projection mapping, frame-generation stages, recovery events and potentially
blocked OpenXR call boundaries. An unmatched begin record can identify the
operation which did not return during a freeze.

For a compatibility report, reproduce the problem once and attach the newest
flight log. Also provide the game and version, VR mod, headset, OpenXR runtime,
GPU and driver, selected backend/options and exact reproduction steps. If no
log was created, state that explicitly: the layer may not have loaded or the
process may have stopped before recorder initialization.

The recorder does not capture video and is not a replacement for a native
crash dump. Game logs and crash dumps remain useful when available.

## Records added by V497 (hybrid composition and NGX guide capture)

`presenter_transition` result 702, once per session after 700 and again
when Hybrid is switched while the session runs (V499): `a` whether
Hybrid was on, `b` whether the game's DLSS vectors were asked for (Hybrid, or
`motion_vectors=dlss`), `c` whether the NGX guide capture started, which it
does only for a native D3D12 session with Hybrid on (V499: before, it also ran
for `motion_vectors=dlss`, which the tray always writes, and Hybrid off then
composed from the game's vectors alone).

`embedded_configuration` results:

| result | meaning |
| --- | --- |
| 812 | the ini's composition switch: bit 0 edge ghosting reduction, recorded but off on this build (bit 1, the repeating-pattern lock, left with V507) |
| 813 | the live tuning window's values for the hybrid (V507), once per change, 8 bits a field from bit 0 in `xrfg::live_tuning::Block`'s order: `a` agree threshold (255ths), choice slope, vectors slope, flow rule slope, edge reach (percent), edge surface solves, surface agreement (1000ths), revealed check (1000ths); `b` vector snap (tenths of a pixel), depth search on, near reach (px), far reach (px), depth solves, nearer margin (percent), depth agreement (1000ths); `c` the window's sequence, 0 without the window (the built-in values) |
| 814 | the first pair composed by the hybrid; `a` 1 on the NVIDIA backend, 0 on FidelityFX |
| 815 | a pair with the game's vectors the hybrid could not use, first eight: `a` 1 the two frames' guides do not pair (`b` previous serial `<< 32` or the current guide's previous serial, `c` previous epoch `<< 32` or current epoch), 2 the guides do not cover the views (`b` view width `<< 32` or height, `c` guide output width `<< 32` or height), 3 the guide's resource is on another device |
| 820 | an NGX module's exports are detoured; `a` the module (0 nvngx_dlss.dll, 1 nvngx_dlssd.dll, 2 _nvngx.dll), `b` whether it had a separate `EvaluateFeature_C` |
| 821 | the game created a DLSS super-resolution (1) or ray-reconstruction (13) feature: `a` the type, `b` its creation flags (bit 32: known), `c` render width `<< 32` or height |
| 822 | a feature's first published motion-vector snapshot: `a` the stream's low bits (from V505 also bit 32 the game's depth came with it, bits 40-47 its DXGI format, bit 48 inverted, bit 49 its rectangle is the vectors' - the hybrid uses depth only then), `b` vector width `<< 32` or height, `c` output width `<< 32` or height |
| 823 | detouring a module failed; `a` the module |

With Hybrid on, a healthy DLSS game shows 702 with `c=1`, one 820 per NGX
module, an 821 per eye, an 822 per eye and then 814. Without 821 the game
never created a DLSS feature in this process (DLSS off, or not a D3D12 game);
without 814 the pairs were not valid guide pairs and the flow ran alone.

### Hybrid status on the FPS overlay (V506)

With **FPS overlay > Show Hybrid status** on (the default) and Hybrid selected,
the overlay draws a line above the number while generation runs, judged every
half second: **HYBRID OK** in green while Hybrid composes pairs, otherwise
**HYBRID KO (n)** in red, where n is the record that explains the first step
that stopped:

| n | meaning |
| --- | --- |
| 702 | not a native D3D12 session (a D3D11 or Vulkan game): the capture cannot run |
| 820 | no DLSS module found or hooked in the game |
| 821 | DLSS modules hooked, but no DLSS evaluation: the game is not running DLSS (DI2: the F8 FSR2 toggle), or, before V524, a D3D12 game behind a D3D11 session (R.E.A.L. VR) |
| 824 | DLSS runs, but its evaluations were skipped |
| 827 | vectors captured, but the guide registry refused them (snapshot failed) |
| 828 | vectors captured, but not matched to the eye images |
| 815 | matched, but the hybrid rejected the pairs |

### Flat panels in generated frames (V508)

`presenter_transition` result 760, once per flat panel (quad or cylinder
layer) the application submits beside its projection, at most eight a
session: `a` the layer's XrStructureType, plus bit 32 where the panel moved
between two frames; `b` its swapchain; `c` how far it moved between the two
frames, micrometres in the low 32 bits and microradians in the high. A panel
that moves (UEVR's HUD follows the head) is placed between its two positions
in each generated frame, at the moment the generated image shows; one that
does not is left as it is.

### DLSS vectors from D3D11 games (V512)

A bridged D3D11 session (the game renders D3D11, the runtime gets the
layer's D3D12 session) now captures the game's DLSS vectors for Hybrid: the
D3D11 NGX exports are detoured beside the D3D12 ones, and each evaluation's
vectors, and its depth where the driver can share the format, are copied
into textures shared with the layer's D3D12 device and published on its
queue.

- `presenter_transition` 702 `c`: 1 the native D3D12 capture started, 2 the
  D3D11 one.
- `embedded_configuration` 820 `a` = slot + 10: a module's D3D11 exports are
  detoured. 821 `a` = feature type + 200: a feature created through D3D11.
- 830, a D3D11 stream's first publication: `a` the stream's low bits | bit 32
  the depth came with it | bit 33 the depth's format cannot be shared (a
  packed depth-stencil); `b` vectors format << 32 | depth format; `c` output
  width << 32 | height.
- 824 reasons 5 (the evaluation's context is not the bridged session's
  immediate context) and 6 (the copy into the shared textures failed, `c`
  its HRESULT).
- The Hybrid status line no longer reports 702 for a bridged D3D11 session.

### DLSS vectors from D3D12 games behind a D3D11 session (V524)

A game that renders in D3D12 while its VR mod hands the runtime a D3D11
session - Cyberpunk 2077 under R.E.A.L. VR - runs DLSS on its D3D12 device.
Until V524 only a native D3D12 session captured D3D12 evaluations, so the
game's feature was created (821 `a` = 1) and evaluated every frame while
Hybrid read **HYBRID KO (821)**. In a bridged D3D11 session the D3D12
evaluations are now captured too: each one copies its vectors, and its depth
where the format can be shared, in the game's own command list into textures
shared with the layer's D3D12 device, and the snapshot copies go to the
layer's queue at the session's next image release, as the D3D11 ones do.

- `embedded_configuration` 843, a stream's first publication on this path:
  `a` the stream's low bits | bit 32 the depth came with it | bit 33 the
  depth's format cannot be shared (packed depth-stencil) | bit 34 the game's
  device is the layer's own (D3D12 returns one device per adapter); `b`
  vectors format << 32 | depth format; `c` output width << 32 | height.
- 824 reason 10: this path's transport failed, `c` its HRESULT; reason 4
  with `c` 1 or 0: the vectors and the command list are not one device.
- 825 and 839-842 as for a native session.
- 844, a publication on this path (first 512, then every 2000th): `a` the
  registry's publication number; `b` the stream's low bits | bit 32 inside
  Streamline's DLSS evaluation | bit 33 its camera known | the viewport << 40;
  `c` the camera's position along its right vector (float bits).
- 847, the mean of an evaluation's vectors over 16 patches (first 512): `a`
  its publication; `b` mean x << 32 | mean y; `c` mean |x| << 32 | mean |y|
  (float bits, the vectors' own units). With the head still, means that flip
  sign from one evaluation to the next are the other eye's parallax.

### R.E.A.L. VR (V524)

Luke Ross's R.E.A.L. VR is recognised by its version resource (product
"R.E.A.L. VR", original file RealVR64.dll), whatever it is installed as
(Cyberpunk 2077: `bin\x64\dxgi.dll`). `presenter_transition` 764 `a` 1: found.
Then:

- Its flat panels go out as it placed them: no pose between frames (V508), no
  generated panel images (V509).
- Its one DLSS stream for both eyes, one eye per game frame, goes to the eyes
  by serial parity once anchored, and guides no eye before. The anchor
  compares 16 patches of the two newest finished DLSS outputs with each eye
  image at a release, by rank correlation of luminance; five agreeing votes
  anchor the left eye's parity, and a DLSS reset needs a new anchor.
- 846, each comparison: `a` the older evaluation's publication << 32 | the
  newer's; `b` older-left's summed correlation << 32 | older-right's (float
  bits); `c` outcome (0 a vote, 1 too few patches with detail, 2 too close) |
  the vote's left parity << 8 | agreeing votes << 16 | bit 24 anchored | the
  fewest patches used << 32.
- The anchor takes the stream published most recently: the game may keep an
  earlier session's feature beside the one it plays with.
- Its vectors span the game's last frame, the other eye's (R.E.A.L. VR's
  frames are unevenly spaced, and one eye's vectors run about three times the
  other's while turning), and are stretched to the eye's own frame by what
  they measure: each serial parity's running mean motion (847, read for every
  evaluation here), an eye's vectors times (its own + the other's) / its own,
  2 until both are measured. Neither the recording times nor a frame-time
  delta tell the spans: Cyberpunk gives DLSS none, and the evaluation recorded
  4 ms after the previous one carries the larger motion. 848, the first 512
  and every 2000th: `a` the publication; `b` the eye's mean motion << 32 | the
  other eye's; `c` the factor << 32 | its source (1 measured, 0 the nominal 2)
  (float bits).
- V526: R.E.A.L. VR fixes the DLSS call inside its own _nvngx.dll hook (its
  vectors rebased to the same eye two evaluations back, "DLSS combined MV",
  its colour copy, a feature handle per eye); the layer's hook, outermost,
  saw only the game's raw call. Under R.E.A.L. VR the capture takes the first
  call into a typed module (nvngx_dlss.dll), the nested one, and its per-eye
  handles publish as one stream through one transport. 854, a captured
  evaluation (first 64, every 2000th): `a` publication | handle index << 48 |
  depth << 56; `b` the vectors' resource; `c` its debug name, 8 ASCII
  characters ("DLSS com" for R.E.A.L. VR's). 855, a call passed over (the
  outer, raw one), first 64: `a` handle index << 48 | module slot << 40 |
  depth << 56; `b`, `c` as 854.
- 845, an eye's choice when a stream is shared without the anchor: `a` eye << 32
  | publication; `b` the two cameras' sides; `c` method (1 cameras apart, 2
  equal, 3 unknown) - the evaluation order decides, the cameras never.

### DLSS vectors from Vulkan games (V513)

A bridged Vulkan session (No Man's Sky, Star Citizen) captures the game's
DLSS vectors and depth for Hybrid. The NGX Vulkan exports are detoured; at
each evaluation the vectors are copied, inside the game's command buffer, into
an image imported from a D3D12 shared texture, and the depth through a buffer
(which gives the packed depth-stencil formats' depth too). The Vulkan queue
layer signals a shared timeline right after the game submits that command
buffer. The publication is made only then, at the next flush (an image
release, the end of a frame, the feature's next evaluation), with its
snapshot copy behind a wait on the timeline: No Man's Sky records its DLSS
ahead, and a publication made at the recording was sometimes the next frame.
A copy whose command buffer was never submitted is dropped when its slot
comes round again.

- `presenter_transition` 702 `c` = 3: the Vulkan capture started.
- `embedded_configuration` 820 `a` = slot + 20: a module's Vulkan exports.
- 831, a Vulkan stream's first publication: `a` its low bits | bit 32 the
  depth came | bit 33 the depth's format has no route; `b` vectors VkFormat
  << 32 | depth VkFormat; `c` output width << 32 | height.
- 832, every 100th copy dropped unsubmitted: `a` the drops so far.
- 833 (V514), once: the Vulkan capture started without the queue layer (Star
  Citizen admits no implicit Vulkan layer but the driver's). The copies are
  then published at the bridged release, behind the bridge's own wait for the
  game's queue. Reason 7 stage 3 no longer stops the capture.
- `presenter_transition` 605: the tray's pause stopped the pipelined
  presenter (MSFS 2024, Star Citizen), and the runtime paces the game until
  the resume promotes it again. `a` 1 when the game's next wait had already
  been answered, so its begin takes a runtime wait first.
- `presenter_transition` 606: that game frame goes down at the time of the
  runtime frame its begin waited for, not the older one the presenter had
  handed it, which SteamVR refuses (XR_ERROR_TIME_INVALID; Star Citizen
  exits on it). `a` the game's time, `b` the runtime frame's.
- `presenter_transition` 752, once: one DLSS evaluation feeds both eyes,
  each in a swapchain of its own (No Man's Sky), so both take that
  evaluation: `c` 1 whole (each swapchain holds the whole side-by-side image,
  its view picks its half), 2 halved (each half at its swapchain's origin).
  `a` the evaluation's output width << 32 | height, `b` the view's.
- 824 reasons 7 (the capture could not start, `c`: 1 the Vulkan loader, 2 the
  device functions, 3 the queue layer not loaded), 8 (a command buffer not
  the session device's, or a resource not an image view), 9 (the transport
  failed, `c` its HRESULT).

### DLSS views placed by their cameras (V515)

MSFS 2024's foveated rendering evaluates DLSS once per view, four views a
frame: per eye a periphery over the whole field at `FoveatedRenderingScale`
of the eye's pixels, and an inset at full density. It composites them into
the eye images itself, so no DLSS output is the eye image's size and the
resolver matched nothing (Hybrid KO 828). The capture now detours
`slSetConstants` and `slEvaluateFeature` in Streamline's `sl.interposer.dll`:
each viewport's projection gives its camera's field, and an NGX evaluation
made inside Streamline's DLSS evaluation carries its viewport's camera. When
no evaluation matches the eye images, each eye takes the recent stream whose
camera covers its field and fits it best (the cameras' positions decide
between two that fit alike), placed scaled over the eye: the periphery. The
inset is not used yet.

- `embedded_configuration` 820 `a` = 30: Streamline's two exports detoured
  (823 `a` = 30: they could not be).
- 834, a viewport's camera, the first sixteen constants and every 4000th: `a`
  the viewport `<< 32` or how the projection was read (1 row vectors, 2
  column vectors, 3 no off-centre terms, 0 unreadable); `b` its edges'
  tangents, left, right, up, down from the low bits, each a signed 16-bit
  count of 4096ths; `c` the camera's position along its right vector (float
  bits).
- 835, a stream's first evaluation inside Streamline's: `a` the stream's low
  bits or the viewport `<< 32`, bit 63 its camera is known; `b` the camera's
  tangents as in 834.
- 836, the first eight times cameras were known but none covered both eyes:
  `a` the recent streams with a camera, or those covering the left eye `<< 8`,
  the right `<< 16`; `b` and `c` the left and right eye's field, packed as in
  834.
- `presenter_transition` 753, per eye when the eyes were joined by cameras
  (as often as 751): `a` the eye `<< 32` or the stream's low bits; `b` the
  vector rectangle placed on the eye (x, y, width, height, eighths of a texel,
  16 bits each); `c` the publication.

### The foveated inset composed in (V516)

The resolver also gives each eye its inset: a recent stream whose camera
sits with that eye's periphery camera and whose field lies inside the eye's,
narrower than it. Before a hybrid pair's draws, a compute pass writes, per
eye, one field of vectors in image pixels at half the eye's resolution, with
its depth: the inset's vectors (two image pixels a vector in MSFS) inside the
inset, two vectors in from its border, the periphery's elsewhere. The inset
follows the eyes, so its field moves between frames; the pass measures each
frame, on a 32 x 32 grid over the inset, whether its vectors lead into the
previous frame's inset (reading A) or stay in the current one (reading B),
against the periphery's, and composes with the one that agrees. Without an
inset, or if the pass cannot run, the pair keeps the periphery alone (V515).

- `presenter_transition` 754, beside 753 when the eye's inset was found: `a`
  the eye `<< 32` or the inset stream's low bits; `b` its field in the eye now,
  `c` at its previous evaluation (x, y, width, height in pixels, 16 bits each;
  `c` 0 when unknown).
- `embedded_configuration` 837, the first sixteen compositions and every
  500th: `a` the left eye's reading A error or reading B's `<< 32`, `b` the
  right eye's (summed over the samples in sixteenths of a pixel, each capped
  at 32); `c` the samples, left or right `<< 16`, and the insets' moves since
  their previous evaluation in eighths of a pixel, left `<< 32`, right `<< 48`.
  A below B: the inset's vectors include its move, and were read so.
- 838, once: the first composition. `a` the field's width `<< 32` or height;
  `b` the left inset's rectangle (x, y, width, height, 16 bits each); `c` the
  left eye's flags `<< 32` or the right's (bit 0 inset, 1 its previous field
  known, 2 periphery depth, 3 inset depth).

### Depth search tiles (V530)

Before a hybrid pair's draws, each eye whose guide brings the game's depth
gets a map of the nearest depth per 8 x 8 depth texels (16 x 16 in V530),
dilated along each tile's row and along its column by the tiles the depth
search's reach can span (V531; V530 dilated in a square). A pixel reads the
row-dilated value of each tile row its search points can fall in (columns
for a steep motion, at most eight reads) and skips the search when none
holds anything nearer than its answer: no point could have started a solve,
so the image is the one the search gives (the harness checks it byte for
byte, motion along x and along y). The passes run inside the composition's
GPU timing. Off with the depth search, without depth, or for a dilation
past 7 tiles.

- `embedded_configuration` 856, at an eye's first tiles and whenever its grid
  or dilation changes, per synthesizer (in MSFS each eye has its own, both
  recording as eye 0; V530 recorded only the first): `a` the eye `<< 48` or the dilation in tiles `<< 40`
  or the columns `<< 20` or the rows; `b` the first tile column `<< 32` or
  row; `c` the search's reach in image pixels `<< 32` or the depth texels per
  image pixel in 1000ths.

### Generated images for flat panels (V509)

A flat panel on a swapchain of its own (not one a projection view uses),
in a native D3D12 session, at most two per frame, gets generated images of
its own, so what the application draws at a new place inside the panel
every frame (UEVR's quest markers) moves between the frames as well.

- `presenter_transition` 761, once per panel swapchain when it is armed
  (after the frame that first names it passes through): `a` 1 armed, 0
  declined; `b` the swapchain; `c` its width | height << 32.
- `presenter_transition` 762, once per panel swapchain at its first
  generated pair: `b` the swapchain; `c` its width | height << 32.
- `presenter_transition` 763 (V511), once per panel swapchain when it is
  given the panel's own flow, FidelityFX at full resolution whatever the
  eye images run: `a` the HRESULT; `b` the swapchain. Live settings changes
  leave it alone.

A panel's own `synthesis_prime`/`synthesis_pair` records carry its
swapchain in `a`, next to the eye images'. Its GPU timing records
(`nvidia_gpu_stages`) say one eye.

### Per-eye guides and capture diagnostics

`presenter_transition` result 751, per eye after xrEndFrame's mapping, for
the first 128 frames and every 512th after: `a` the eye (0 left, 1 right),
`b` the publication watermark at that eye's colour release, `c` the
publication of the guide chosen for it, 0 when none qualified. A game with a
swapchain per eye and one DLSS feature for both shows alternating
publications here; both eyes showing the same `c` means the join failed.

`embedded_configuration` results:

| result | meaning |
| --- | --- |
| 824 | the first evaluation the capture skipped for a reason: `a` 1 unknown feature, 2 missing resources, 3 nested in another hooked call, 4 the vectors, the command list and the queue not proven one device; `b` the count so far; for 2, `c` bit 0 output present, bit 1 motion vectors present; for 4, `c` bit 0 vectors and list match by pointer, bit 1 vectors and queue |
| 825 | a feature's first evaluation: `a` output format `<< 48` or output array size `<< 40` or motion format `<< 32` or the stream's low bits; `b` output texture width `<< 32` or height; `c` motion array size `<< 56` or motion texture width `<< 24` or height |
| 829 | an evaluation of a feature created before the layer hooked, once: the same fields as 825 without a stream; the feature is then registered from its evaluation and an 821 with `a` = type + 100 follows |
| 826 | every 1000th evaluation: `a` evaluations, `b` publications, `c` skipped unknown `<< 40` or missing `<< 20` or nested |
| 827 | a publication the registry refused, first eight: `a` invalid publications so far, `b` snapshot failures so far, `c` vector rect width `<< 32` or height |
| 828 | the resolver's totals every 300 resolves: `a` matched `<< 32` or stale pairs, `b` queue mismatches `<< 32` or missing streams, `c` pairs used `<< 32` or hybrid pairs |
