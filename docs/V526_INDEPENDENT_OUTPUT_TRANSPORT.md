# V526: independent synthesis storage and XR allocation

Owner requests two independent controls on all applicable engines and OpenXR
runtimes, including VirtualDesktopXR (VDXR), to isolate the V525 Ghostwire
Lower VRAM improvement. This is a local experiment; headset acceptance is pending.

## Controls

| Separate synthesis | Reduce XR swapchains | Behavior |
| --- | --- | --- |
| Off | Off | Compute directly into the full XR output allocation |
| On | Off | Compute into owned textures; copy into the full XR allocation at delivery |
| On | On | Owned textures, half the XR output swapchain handles |
| Off | On | Direct output, half the XR handles, bounded 2X delivery |

Both choices are saved separately for each generator/engine profile. Existing
profiles without `separate_synthesis` migrate it from their explicit old
`lower_vram` value. New profiles default both off. The runtime INI keys are
`separate_synthesis` and `single_swapchain_rings`; the shared tray/layer IPC ABI
is 52601. Close the previous tray and game before starting V526.

Native D3D12 output storage implements both flags for Reference, New, Djules,
FidelityFX, and compatible native DLSS-G/FSR generation, independently of
SteamVR/VDXR identification. Luke AFW, D3D11 and Vulkan interop retain their
existing transport and do not use these experimental native storage controls.
AFW camera/depth subtraction, guides and Neural processing are unchanged.

Reference/New have two real plus two synthetic XR handles per source; reduction
selects one of each. Djules has two real plus four synthetic handles; reduction
selects one real plus two synthetic handles. This is exactly half the handles;
the runtime still chooses the number of images in each swapchain. Previous
coupled Lower VRAM reduced Djules to one real plus one synthetic handle.

Separation keeps the existing two owned real textures and two synthetic textures
(four synthetic textures for Djules). Full XR allocation plus private textures
can consume more VRAM than direct/full. No extra producer frames are admitted.
Copy readiness fences, immutable history leases and output reuse fences remain.
Private synthesis removes the XR destination dependency from the synthesis
queue; acquisition and safe delivery remain necessary on the consumer.

Reduced direct output serializes reuse through the existing fresh-pair owner;
New/Djules cannot use the asynchronous three-slot owner or deep/3X repeat with
that allocation. The UI disables 3X for that combination. Turning separation on
allows the normal staged 3X capacity. No interpolation shader or motion method
changes. These storage choices intentionally replace hidden SteamVR staging
and budget fallbacks: full allocation means full allocation. A genuine runtime
budget refusal remains visible; choose reduction explicitly to test recovery.

## Evidence and owner comparison

V525 Ghostwire Reference went from roughly 61 presented FPS and 65 ms cycle
with direct/full storage to roughly 89 presented FPS and 43 ms with coupled
Lower VRAM. XR CPU Acquire/Wait costs were small; GPU queue dependency is the
working explanation, not a separately measured GPU timestamp attribution.
No hard 44/55 ms latency cap is implemented or promised.

V526 uses real D3D12 synthesis with a fake XR runtime to test all 24 combinations
of three NVIDIA engines, SteamVR/VDXR policy, and the two flags. Pixel readback
checks eight real/seven generated stereo outputs, poisoned released sources and
auxiliary layers, handle count, ordering, XR-cycle accounting, debug errors and
teardown. Live profile switches test independent storage shape replacement
without replacing source histories. Automated checks do not establish headset
latency, compositor timing or vendor-runtime allocation limits.

For the owner test, start with Ghostwire UEVR, AFW/Neural off, 2X, same scene,
resolution and refresh rate. Compare On/Off with On/On first; then Off/Off and
Off/On. Record Cycle length, Cycle estimated and Output FPS after settling.
Repeat on VDXR (not SteamVR through Virtual Desktop). Keep F9 logging consistent
between compared samples. These cycle fields are not motion-to-photon latency.
