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
| 813 | the live tuning window's values for the hybrid (V507), once per change, 8 bits a field from bit 0 in `xrfg::live_tuning::Block`'s order: `a` agree threshold (255ths), choice slope, vectors slope, flow rule slope, edge reach (percent), edge surface solves, surface agreement (1000ths), revealed check (1000ths); `b` vector snap (tenths of a pixel), depth search on, near reach (px), far reach (px), depth solves, nearer margin (percent), depth agreement (1000ths), tint (V539: 0 off, 1 by branch, 2 by the coverage mask); `c` the window's sequence, 0 without the window (the built-in values) |
| 814 | the first pair composed by the hybrid; `a` 1 on the NVIDIA backend, 0 on FidelityFX |
| 815 | a pair with the game's vectors the hybrid could not use, first eight: `a` 1 the two frames' guides do not pair (`b` previous serial `<< 32` or the current guide's previous serial, `c` previous epoch `<< 32` or current epoch), 2 the guides do not cover the views (`b` view width `<< 32` or height, `c` guide output width `<< 32` or height), 3 the guide's resource is on another device |
| 820 | an NGX module's exports are detoured; `a` the module (0 nvngx_dlss.dll, 1 nvngx_dlssd.dll, 2 _nvngx.dll), `b` whether it had a separate `EvaluateFeature_C` |
| 821 | the game created a DLSS super-resolution (1) or ray-reconstruction (13) feature: `a` the type, `b` its creation flags (bit 32: known), `c` render width `<< 32` or height |
| 822 | a feature's first published motion-vector snapshot: `a` the stream's low bits (from V505 also bit 32 the game's depth came with it, bits 40-47 its DXGI format, bit 48 inverted, bit 49 its rectangle is the vectors' - the hybrid uses depth only then), `b` vector width `<< 32` or height, `c` output width `<< 32` or height |
| 823 | detouring a module failed; `a` the module (V535: 31, Streamline 1.x, whose exports are left alone - The Last of Us Part I ships 1.4.7) |

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
  once anchored, and guides no eye before. V540: where R.E.A.L. VR's DLSS fix
  runs, it evaluates each eye through a feature handle of its own
  (RealVR64.dll keeps the handle on its frame parity, the eye it renders), and
  the eyes go by handle: one anchor for the session, through DLSS resets and
  Hybrid switched off and on. Without the fix (one handle) they go by serial
  parity, anchored again after each reset.
- The anchor is the eye measurement's (851): each eye image against the last
  four evaluations, sliding up to 16 pixels sideways. Each pairing of the
  eyes with the evaluations scores its eyes' best matches among their own
  two; five agreeing votes, ahead by 0.1 with some match of at least 0.5,
  anchor. Measured every other release until then. (V524-V539 compared the two
  newest evaluations only: in Cyberpunk 2077 AERv2 shows one eye an older
  evaluation and the other its interpolation, the vote was a coin toss, and
  a Hybrid switched off and on could swap the eyes - each eye's vectors the
  other's, the near scene out by the eyes' parallax: the mask tint's
  outlines left of a weapon in the left eye, right of it in the right.)
- 861, each vote while anchoring (the first 512): `a` the newest
  evaluation's publication << 16 | agreeing votes << 8 | bit 5 the left eye
  on the newest's side | bit 4 by handles | outcome (0 a vote, 1 no match of
  0.5, 2 within 0.1); `b` the two pairings' scores, the left eye on the
  newest's side << 32 | on the other's (float bits); `c` the four
  evaluations' handles (8 bits each, newest low), or the newest's serial.
- 862, the anchor: `a` as 861; `b` the left eye's handle << 32 | the right's,
  or the left eye's serial parity; `c` the stream. Kept at the head of every
  flight log file (846, V524's comparison, is gone).
- V542: at a release AERv2 shows each eye an evaluation behind its newest,
  by an amount that changes between sessions. Cyberpunk 2077: the eye that
  owns the newest evaluation showed it on 2026-10-19, its one before, then
  about two before, on 2026-10-20; the other eye its one before each time.
  An eye given a newer evaluation's vectors than its picture has its
  contour off by the strafe's speed. The measurement compares each eye with
  the last six evaluations (865 holds the fifth and sixth as 851 the first
  four); once the eyes are anchored, per role (the newest evaluation's eye,
  and the other - they trade places when a frame carries one evaluation),
  the eye's own evaluation at the smallest shift while moving is a vote,
  and three agreeing votes decide that role for the session. Until then the
  newest's eye takes it and the other its one before. Measured every 4th
  release until both are decided. 863, each vote (the first 512): `a` the
  newest evaluation's publication << 16 | agreeing votes << 8 | the role
  << 6 | evaluations skipped << 4 | outcome (0 a vote, 1 no match of 0.5, 2
  not moving, 3 two within 2 pixels); `b` the role's three own evaluations'
  shifts (int8, newest low) | their correlations x100 << 32; `c` the eye.
  864, a decision: `a` as 863; `b` the evaluations skipped; `c` the stream.
  Kept like 862. Under alternating eyes the vectors' snapshot ring is 8
  deep, for an eye two of its evaluations behind.
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
- V533: an eye image can be a window of a wider DLSS output. R.E.A.L. VR's
  Ghost of Tsushima renders 4192 x 3360 with a camera of +-1.2043 and submits
  each eye as the middle 4003 x 3360 at +-1.1501. The layer hands the
  registry each eye's view at every mapping (set_dlss_eye_view); a stream
  larger than the eye image whose camera holds the eye's field passes the
  size check and is placed on the eye's window (place_guide_in_view, as for
  MSFS 2024's periphery), the right eye's rotation shift (V524) on top. The
  anchor's and the eye measurement's patches are read at the output's origin
  on the eye image (dlss_inset_origin), so they compare the same picture.
  858, the first eight placements: `a` the eye `<< 32` or the stream's low
  bits, bit 63 placed without a camera (V534), bit 62 an output smaller
  than the image, enlarged over it (V537); `b` the
  output's width `<< 32` or height, as placed (the window's); `c` the placed
  vector rectangle (x, y, width, height in eighths of a texel, 16 bits each).
- V534: without a camera (Kingdom Come calls NGX directly: output 3776 x
  3088, eyes 3679 x 3088), an alternating-eye stream at most 6% larger per
  axis than the eye image, the eye's field symmetric, is placed on the eye
  image's middle at the output's scale, as Ghost of Tsushima's camera
  measured its window. 859, once per reason, a stream refused that window:
  `a` 1 not alternating eyes, 2 the eye's field not symmetric, 3 the output
  too large, 4 smaller than the eye image and not of its shape, 5 (V537)
  an alternating stream's camera does not hold the eye's field, `b` the
  eye's tangents and `c` the camera's (as 836's).
- V537: an output smaller than the eye image, of its aspect within 1% and
  at most 3 times smaller, is the whole eye image enlarged (Avatar: Frontiers
  of Pandora evaluates DLSS 1902 x 1596 into a 3280 x 2752 texture, eyes
  3278 x 2752) and is placed over all of it. The output's true size is 840's
  output rectangle, not the output texture's size in 825. With a camera
  (Avatar evaluates inside Streamline), an alternating stream's smaller
  output is placed by its camera instead (place_guide_in_view). The eye measurement (851) reads
  around the same middle: its correlations say whether the guess holds.
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
- `embedded_configuration` 870 (V549), how the eyes were joined, the first
  16, every 512th, and each change of streams, method or the cameras'
  verdict (the first 1024 changes): `a` the method (1 the releases' order, 2
  the cameras' fit, 3 the cameras' positions), bit 4 the releases told the
  eyes apart, bit 5 the eye released first, bit 6 the cameras' positions
  tell, bit 7 they would swap the eyes, the left eye's stream's low 24 bits
  `<< 8`, the right's `<< 32`; `b` the left camera's position along the two
  cameras' summed right vector (float bits) `<< 32` or the right's; `c` the
  streams considered, the first eye's group `<< 8`, the second's `<< 16`,
  bit 24 both insets found, the joins so far `<< 32`. Since V549 the
  releases' order picks the streams first: a game releases a view's image
  after that view's DLSS, so the streams evaluated before the first eye's
  release are its and those between the two releases the other eye's; the
  cameras' positions decide only when both eyes were evaluated before either
  release. MSFS 2024 foveated on a Pimax Dream Air (#33) carries the same
  symmetric field in both peripheries, and the positions swapped the eyes
  about half the time, the left eye then taking the right periphery's
  previous evaluation. A run of 870 records with bit 7 changing shows that
  swap still happening in the cameras, now without effect.

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

### Depth search tiles (V530-V554, removed in V555)

V530-V554 built, before a hybrid pair's draws, a map of the nearest depth
per 8 x 8 depth texels (16 x 16 in V530), dilated along each tile's row and
column, so a pixel could skip the depth search's probes where nothing
within their reach was nearer than its answer. V555 replaced the probes
with the midpoint splat (below), which has no reach to skip, and the tiles
with them; the tuning window's Near reach, Far reach and Depth solves
sliders left at the same time. Record 856 is no longer written.

The depth search itself runs only with the tray's "Hybrid: depth search" on
(V532; off by default, `[ofxr] hybrid_depth_search`, followed live); the live
tuning window no longer switches it, and 813's depth search field reports
the menu's value.

- `embedded_configuration` 857, at each change of "Hybrid: depth search": `a`
  1 on, 0 off.
- `embedded_configuration` 856 (V530-V554), at an eye's first tiles and whenever its grid
  or dilation changes, per synthesizer (in MSFS each eye has its own, both
  recording as eye 0; V530 recorded only the first): `a` the eye `<< 48` or the dilation in tiles `<< 40`
  or the columns `<< 20` or the rows; `b` the first tile column `<< 32` or
  row; `c` the search's reach in image pixels `<< 32` or the depth texels per
  image pixel in 1000ths.

### Coverage mask (V539)

Background a fast object uncovers ghosted the object behind it with Hybrid
on: at such a pixel the game's vector is the background's, so the previous
frame is sampled where the object still was, and the probes that would
have caught it reach 16 pixels, scaled by the pixel's own displacement -
the background's. Before a hybrid pair's draws, a compute pass at the
vectors' resolution writes, per texel of the previous frame's image, the
current frame's surface that came from there (its vector; the nearest by
the game's depth, else the fastest, keeps a contested texel). A pixel whose
previous-frame sample lies under another surface is revealed background
whatever that surface's speed, and takes the current frame alone there.
From V544 the depth is ranked on a log scale, 8 steps a doubling of
distance; V539-V543 ranked it linearly in 8 bits, so everything past 255
near planes tied (MSFS 2024's buildings and the terrain behind them) and
the surface sliding left won, leaving a ghost behind one sliding right.

- `embedded_configuration` 860, at a synthesizer's first mask for an eye
  and whenever its grid changes: `a` the eye `<< 48` or columns `<< 20` or
  rows; `b` 1 when the game's depth decides the nearer surface, 0 the
  faster.

### Depth search by midpoint splat (V555)

The depth search of V505-V554 read the game's depth at up to sixteen points
along each pixel's motion and solved from the nearer ones; the tiles (V530)
skipped it where nothing nearer lay within reach, but every output pixel
still paid the reads, at the eye's resolution. V555 replaces the probes and
the tiles with a splat. Before the pair's draws a
compute pass at the vectors' resolution writes, per texel of the current
frame, the surface's word (the coverage mask's: its displacement to the
previous frame, and its log-depth rank) at the texel it occupies at the
generated instant, one slice per synthetic output since that instant
differs at 3X; the nearest surface keeps a contested texel. Each pixel then
reads one word and, where the word's surface is nearer than its own by the
Nearer margin, solves once from where that surface is in the current frame,
verified by the Depth agreement as the probes' solves were. Runs with
"Hybrid: depth search" on and only where the game's depth is published.
`XRFG_DISABLE_DEPTH_SPLAT=1` in the game's environment leaves the search
out (the harness's A/B).

Measured 2026-10-21 against the probes with tiles, MSFS 2024 at 7032x5504
per eye, 3X: 0.27-0.28 ms less composition per eye per pair on FidelityFX
and on NVIDIA alike; 1.1 ms in an exterior view at 5884x4608. No visible
difference.

- `embedded_configuration` 867, at a synthesizer's first splat for an eye
  and whenever its grid or its outputs change: `a` the eye `<< 48` or
  columns `<< 20` or rows; `b` the synthetic outputs splatted (1, or 2 at
  3X).

### Two DLSS streams ordered by their depth's offset (V553)

Two streams over one side-by-side eye image are sorted by where their inputs
sit: since V553 the depth rectangle's x first, where the publication carries
a depth, then the vectors' and the output's, then first seen. UEVR's Bodycam
evaluates each eye's vectors in a texture of its own and both outputs at 0,
so V497-V552 fell to first seen, and its depth is one double-wide target with
the right eye's rectangle on the right. After a death or a level change the
engine recreated both features with the right eye's first: each eye took the
other's vectors (860 showed eye 0 without usable depth and eye 1 with it),
and a Virtual Desktop restart, which starts the order afresh, put them back.

### Measured eye order of two DLSS streams (V546)

A game that runs one DLSS feature per eye, with each eye in a swapchain of
its own, both inputs at the same offset and no camera (Star Citizen), gave
the resolver nothing to tell the two streams apart, and it took the one it
had seen first as the left eye. Which one that was came from the order the
capture iterated its features: right in a session that started with Hybrid
on, the eyes' vectors swapped after Hybrid was switched on in game - double
edges on every sideways head move, fine with the head still.

The order is now measured. While a pair is undecided, each eye's
synthesizer compares, every second vector texel where the two streams'
vectors part by more than a pixel, the current eye image's pixel with the
previous image read at the end of each vector, and counts which lands on the
same surface. After 24 measurements and 20000 pixels, the order with half as
many votes again as the other is the pair's for the session; until then the
first-seen order stands. A still head in a still scene gives no votes.

- `embedded_configuration` 867, a measurement, the first 64 and every 100th
  after: `a` the eye `<< 56` or the assigned stream's low 28 bits `<< 28` or
  the other's; `b` the pixels that favoured the assigned stream `<< 32` or
  the other; `c` the pixels compared.
- 868, the pair's order, once a session (kept through the log's wraps): `a`
  the left stream, `b` the right, `c` the votes for the lower stream as the
  left `<< 40` or for the higher `<< 16` (24 bits each) or the measurements.
- 869 (V547), how the pair went to an eye at its release, the first 16 and
  every 600th after: `a` the eye `<< 40` or the path `<< 32` or the chosen
  stream's low 32 bits; `b` the count of path 0 (undecided, both
  evaluations before either release) `<< 32` or path 1 (undecided, one
  between the releases); `c` path 2 (decided, both before) `<< 32` or path
  3 (decided, one between). V546 applied the decided order on path 2 only;
  on path 3 each eye took the newest evaluation before its own release,
  whichever stream it was, which swapped Star Citizen's eyes after a live
  switch to 3X on Virtual Desktop. V547 applies it on both. V548: `a` bit
  44 set where the eye took its stream's previous evaluation rather than
  its newest, and the count of those so far in `b`'s top 16 bits (path 0's
  count is then 16 bits). The later of the two eyes' release watermarks now
  bounds both eyes: the two bridged releases run back to back at the
  game's xrEndFrame, and an evaluation the second eye's wait for the game's
  queue completed is this frame's for the first eye too; V547 gave that eye
  its previous evaluation, vectors one frame older than the image.

### The diagnostic tint (V539)

Two boxes at the top of the tray's "Frame generation tuning (live)" window
(expert mode) tint generated frames in a running game - the real frames
stay as they are, so the tint flickers at half rate. Never saved: unticked
at every tray start. 813's last field records it.

By what Hybrid did at each pixel: **red** the optical flow took it,
**yellow stripes** a mix of flow and vectors; otherwise the vectors' answer -
**blue** covered background (the previous frame alone), **cyan** revealed
background (the current frame alone: the probes' check or the coverage
mask), **orange** the camera alone, **magenta** a motion edge found but not
verified (the current frame lerped with the blend by confidence - the
revealed-ghost path), **green** a second surface found, **white** the depth
search's nearer surface, untinted the plain vectors warp.

By the coverage mask's verdict at the pixel's previous-frame sample, and
what became of it. Where the mask says another surface covered it there
(revealed): **green** its answer, the current frame alone, was kept;
**white** kept, then replaced by a surface search; **orange** not acted on
because the camera alone explains the pixel; **blue** not acted on because
the pixel is covered background; **red** not acted on, a repeated capture.
Otherwise **magenta** nothing of the current frame came from there,
**grey** no mask bound, untinted the pixel's own surface or the mask not
consulted (static pixels). Stripes instead of a solid colour: the optical
flow was mixed in.

V543: the mask's band (the tuning window's "Mask band", off by default):
each moving surface's footprint is drawn along its own motion, either way,
by that share of it and at least a texel, so background a fast edge
uncovers is found although the picture sits slightly off its vectors -
R.E.A.L. VR's eye between two of its evaluations, render-resolution
vectors, an anti-aliased fringe. Off by default: in Cyberpunk 2077 it did
not reach the ghost left at fast edges (that one shows with the optical flow
alone too), and it costs about 0.2 ms a pair at 20%, more moving faster.
V543-V554 recorded it as 866 (the tuning fields past 813's sixteen); since
V555 the window has fourteen fields and 813 carries them all.

V560: a third box tints generated frames by the still mask for head-locked
HUDs (V559), with Hybrid on or off: **magenta** a pixel held by its age (the
same stroke at the same place for enough pairs), **cyan** held by a
head-motion confirmation alone (the loose tolerances), **yellow** a warped
sample refused for crossing the mask. 866 is back for the tuning fields
past the sixteenth, the still mask's: a their values, 8 bits each from bit 0
(still angle, still age, still vs moved), b the window's sequence.

The second-surface search (Edge surface solves) accepts a surface only
where the coverage mask does not give its previous-frame spot to another
surface (V539): white bands in the mask tint were the search painting the
object back over revealed background on dark or flat walls.

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
| 829 | an evaluation of a feature created before the layer hooked, once: the same fields as 825 without a stream; the feature is then registered from its evaluation and an 821 with `a` = type + 100 follows; since V551 its `b` bit 33 says the parameter object's Width/Height and OutWidth/OutHeight were the other way round (an output smaller than the input on both axes) and were swapped - No Man's Sky's evaluation carries its 6144 x 3264 output as Width and its 3564 x 1893 render size as OutWidth, so with Hybrid switched on mid-session the output read 3564 x 1893 and the side-by-side split never matched |
| 826 | every 1000th evaluation: `a` evaluations, `b` publications, `c` skipped unknown `<< 40` or missing `<< 20` or nested |
| 827 | a publication the registry refused, first eight: `a` invalid publications so far, `b` snapshot failures so far, `c` vector rect width `<< 32` or height |
| 828 | the resolver's totals every 300 resolves: `a` matched `<< 32` or stale pairs, `b` queue mismatches `<< 32` or missing streams, `c` pairs used `<< 32` or hybrid pairs |
