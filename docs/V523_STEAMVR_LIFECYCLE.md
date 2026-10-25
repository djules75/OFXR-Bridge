# V523 local candidate: observed V522 failure boundaries

Owner rejects V522: unchanged headset symptoms. Original sealed V522 remains
immutable and local. Frozen Valhalla pid27440 log SHA256:
`7faa881b9c903f21e66c52090197c3e0688fbe95822252f6577f0e11e2173116`.

The measured internal failures changed despite the same visible symptoms:

- Six F10 creations return XR_ERROR_LIMIT_REACHED while New is running. Game
  loading/auxiliary outputs remain retained after those layers disappear.
- Djules now creates both eyes successfully, then emits 444 synthesis-busy
  decisions with no synthesis calls. The old presenter's stopped flag remains
  true after its threads are joined; staged bootstrap treats that flag as an
  active interruption before it can prime and start the new owner.
- Reference submits 346 real/generated pairs but SteamVR reports about 41 FPS.
  Its EndFrame calls often cost 14ms on an 11.11ms display. The next real can
  overwrite a generated output immediately after that slow call returns.

Changes:

- Staged output wait/abort respects stop/failure only while a presenter is
  active. A joined old presenter cannot reject an idle bootstrap prime.
- SteamVR native D3D12 Reference uses the existing schedule's completed-call
  boundary after an End spans the next slot. Ordinary jitter retains the grid;
  a real endpoint leaves the stalled synthetic one physical exposure.
- Before UI drawing, ordinary native D3D12 SteamVR OFXR retires only auxiliary
  outputs absent from current application layers, with exclusive CPU ownership,
  idle XR image ownership and completed own-copy GPU fences. Source captures
  and application handles survive. Referenced, leased and GPU-pending outputs
  remain intact; there is no GPU drain or wait in this retirement pass.
  `composition_publish` result S_FALSE records retirement (source/output/fence).

New's renderer/schedule, AFW, special Outlaws initialization, Vulkan transport,
camera/depth/vector contracts and paused HUD experiments are unchanged.

Focused tests include an actual-GPU SteamVR bootstrap from the stopped state,
preserving eight real frames through promotion; an auxiliary-output retirement
test preserving a live lease and reusing its source when a quad returns; and a
25ms Reference runtime End stall. V522 fails the independent exposure fixture;
the fixed implementation passes. Initial harness probes are retained locally:
an inline-unless-pipelined probe was not the SteamVR runtime policy and was
corrected; auxiliary poisoning now honors synchronous borrowed lifetimes, and
the restart oracle accounts for the established single promotion re-prime.
One transient test PDB link write error passed on retry without source change.

Final optimized Release: 24/24 targeted checks pass in 39.34 seconds, zero
skips. These include KCD/Indiana bundled quality, DOOM/Indiana Vulkan bootstrap,
Outlaws startup, Reference queue/VDXR/slow runtime, route handoff/shapes and
D3D11/D3D12 menu checks. Existing invalid default CPU director fixture remains
excluded; no full-suite or headset verdict is inferred from these checks.

This is not owner headset validation. Publication remains local until accepted.
Test with fresh V523 tray/game, same Valhalla settings, F9 before loading. After
world load reopen F10 and try New -> Djules -> Reference -> New. No dump needed.
