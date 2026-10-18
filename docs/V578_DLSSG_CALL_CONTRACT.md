# V578: record the actual native DLSS-G call contract

This is a diagnostic candidate on V577, not a visual fix. The owner observes
propeller ghosting only with native DLSS-G in MSFS. Both recorded eyes already
contain the defect in NGX output; final composition preserves those RGB bytes.
Original application colors and sampled guides do not show a gross motion
sign/scale error. The supplied0.3.0/FSR F8s contain original colors, not their
generated outputs, so they are not a direct generated-pixel quality control.

A local unit-MV-scale proposal failed the unchanged native GPU pixel-quality
controls and was discarded. An aligned camera transform was geometrically
consistent with sampled guides but displaced NGX content to the wrong time;
it was also discarded. NVIDIA's SDK/header descriptions alone did not override
these actual GPU results. No rejected interpolation code enters this build.

The local replay now uses the captured10-bit aligned previous color, 8-bit
current color, motion and depth, plus the signed310.9.16/modelC provider whose
SDK banner matches the game. Core and direct evaluation reproduce the same
offline pixels, but the replay still differs at the ghosted silhouette. The
next missing observation is the reset's retained guides and the actual SDK
options, rather than another change to the working SteamVR distributor.

## Full F8 output

`native-dlssg-evaluations.tsv` records each claimed reset/pair SDK call:
resource identities, input/feature formats, exact rectangles, MV scales,
jitter, camera/projection/clip matrices, flags, NGX result, output count/index,
and depth/object-separation heuristics. Reset and ordinary evaluations have
their own original guide publications; join resource identities to manifest
native stages, rather than assuming the two rows have the same publication.
Flags bits0..5: inverted depth, camera motion included, dilated vectors, HDR,
orthographic projection, not-rendering-game-frames. Raw pointers are local
resource identities, never XR swapchain handles or reusable cross-run IDs.

Extra images `dlssg_seed_motion` and `dlssg_seed_depth` snapshot the guide
resources actually bound during reset, before B's guides overwrite them.
These can contain older contents; the old optimization writes aligned A's
color only and advertises a64x64 guide rectangle for its reset evaluation.
Neither that optimization nor any scalar/matrix supplied to NGX is changed.

Only a full plain F8 pair claim enables this work. Existing two-stream claim,
48-image/1.25GiB budget and GPU completion/state restoration remain. At most16
evaluation records are retained. Added seed readbacks may displace later
ordinary images within that same budget; missing image status remains explicit.
No extra GPU copy/wait/query, parameter readback or sampling runs normally or
with the F9-only rolling ring. No queue/FIFO/phase/AFW/VDXR/benchmark change.
Control IPC57701 and provider ABI45601 remain compatible with V577.

## One next owner observation

Restart tray and MSFS with V578. Same scene, native DLSS-G2X, AFW/Neural/debug
off; reproduce the propeller ghost while moving and press plain F8 once.
Wait about10 seconds for saving, then close the game normally. No F9 or full
benchmark sweep is required. Do not score performance during readbacks.

12 focused checks pass, including actual NGX pixel/viewport/reseed/late-provider
controls with unchanged quality limits; F8 raw completion, exactSDK metadata,
TSV columns/row counts and inactive bypass; private2X/3X/LowerVRAM/switch,
VDXR00/11 and loader. Headset cause/fix is still pending the observation above.
