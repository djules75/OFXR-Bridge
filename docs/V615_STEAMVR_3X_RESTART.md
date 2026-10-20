# V615: SteamVR 3X restart and Fast capacity

Based on V614. The owner found two failed Valhalla benchmark trials: Djules3X,
Fast off, Director and Framework+director. Both recorded zero generated FPS
and about41-42 delivered FPS. Framework3X Normal reached90. Fast3X in all
three policies actually delivered one synthetic per real, despite a3X request.
The retained detailed game flight did not cover the two failing windows; the
compact benchmark log and main/details CSVs do. No claim of an exact game GPU
failure trace is made.

## Reproduced restart failure

An actual D3D12 shipping-layer GPU fixture replays the six benchmark transitions,
with16 real inputs per configuration. On exact V614 renderer code, after
Framework3X Fast -> Director3X Normal, it delivers16 real outputs and no generated
outputs. The trace alternates successful prime and `synthesis_busy` before pair
submission. After a presenter route changes, its old worker is joined and
`presenter_stop_requested` remains latched. Inline3X preparation must acquire
its two synthetic leases before a replacement presenter can be promoted. That
lease gate incorrectly interpreted the retired owner's stop flag as a live
cancellation. Priming repeatedly succeeds; every attempted pair is refused.

For the private SteamVR bridge only, direct publication/extra-synthetic lease
waits now treat that stop flag as cancellation only while the presenter is
active. Live cancellation and error propagation remain; queued GPU fences,
lease ownership and the later presenter promotion remain intact. Staged and
nonprivate/non-SteamVR behavior is unchanged.

The corrected replay delivers46 outputs from16 real inputs in the previously
failing Director Normal phase (including restart warm-up), versus16 before.
The same replay checks Framework+director Normal after the next Fast transition.

## Fast must retain actual3X

An older reduced-output guard disabled3X whenever direct storage had one B and
one S ring, turning selected3X into actual2X. For private SteamVR,3X now retains
separate A/B and S1/S2 output capacity. Fast3X must preserve these minimum buffers;
it may therefore save little VRAM compared with Normal3X. Halving B as well was
rejected by the GPU ownership test (a private release failed); that attempted
layout is not shipped. No same-handle synthetic alias is permitted.

Fast2X still uses one current and one synthetic ring. Live multiplier changes
reconcile both ring counts, retire the old descriptors first, and release extra
capacity when returning to2X. VDXR's existing staged3X contract remains one XR
current/synthetic ring with its existing private staging stride. Resource-shape
checks and scope tests explicitly distinguish those cases. No VDXR director
experiment or pacing changes are introduced.

## Meaning of Framework

Framework in the current dropdown is framework pacing over the new private
SteamVR transport, with final director cadence/phase/hold bypassed. It retains
private textures, FIFO, native call ownership, the isolated binding queue and
backpressure. It is not the historical pre-director direct-runtime path. This
explains why delivery can differ from old builds even in Framework mode.
Framework+director is the existing combined policy, not a new3X limitation.

## Validation and next owner check

The GPU replay checks all six3X configurations, distinct output capacity, then
live3X ->2X Fast and resource reduction. Existing private3X, blocked-consumer,
pooling, live policy, ordinary Fast2X, shared control/GUI and VDXR regressions
are listed in V615_VALIDATION.json. No renderer test substitutes for Valhalla
headset acceptance. CMake0.615.0; IPC61101/provider45601 unchanged.

Load matching V615 tray/runtime. Re-run the optical benchmark in the same
Valhalla scene. F9 is optional for CSV capture; leave Output CSV enabled. The
owner postponed spreadsheet export until3X works. The new spreadsheet snapshot
has moved the SteamVR block to BV:EF; the separate local adapter now locates
and validates the Meganex/SteamVR block instead of assuming AI. No sheet writes.
