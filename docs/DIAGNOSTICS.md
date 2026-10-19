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
| 821 | DLSS modules hooked, but no DLSS evaluation: the game is not running DLSS (DI2: the F8 FSR2 toggle) |
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
- `presenter_transition` 752, once: one DLSS evaluation feeds both eyes,
  each in a swapchain of its own (No Man's Sky), so both take that
  evaluation: `c` 1 whole (each swapchain holds the whole side-by-side image,
  its view picks its half), 2 halved (each half at its swapchain's origin).
  `a` the evaluation's output width << 32 | height, `b` the view's.
- 824 reasons 7 (the capture could not start, `c`: 1 the Vulkan loader, 2 the
  device functions, 3 the queue layer not loaded), 8 (a command buffer not
  the session device's, or a resource not an image view), 9 (the transport
  failed, `c` its HRESULT).

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
