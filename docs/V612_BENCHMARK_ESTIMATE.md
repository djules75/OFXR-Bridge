# V612: restore the established benchmark estimate

V612 changes only the availability/origin policy of **Cycle estimated** from
V611. The initial UEVR Game-time adapter, pacing menu and Fast-mode label are
inherited unchanged from V611. No new game-family hooks or FPS-derived Game
estimate are introduced.

Cycle estimated again uses application `xrWaitFrame` entry through the same
real frame's confirmed predicted display endpoint, or a later native/producer
return. It does not require an observed engine-production origin. This restores
the established benchmark/owner-sheet metric on UEVR, native OpenXR and Luke
paths, including generation disabled, whenever the existing native receipts and
clock are valid. Pending/failed delivery, an unknown clock or expired samples
still remain unavailable. No constant or previous sample replaces missing data.

Game time remains observed engine start through source EndFrame entry. The
initial UEVR callback/stereo association has not yet been validated in a real
game. Missing or ambiguous association leaves only Game unavailable. Generation
and Delivery remain independent post-handoff measurements. Because Game can
begin before Wait, Game + Generation + Delivery need not equal Cycle estimated;
their origins now deliberately differ. The shared F10/Elliott tooltip explains
this distinction. This supersedes V611's full-cycle/N/A policy in its historical
documentation; it is not a claim of physical display or input latency.

Benchmark ordering, CSV schema, the owner-sheet adapter and FPS counters are
unchanged. There are no changes to pacing, director scheduling, buffering,
swapchains, copies, fences, interpolation, AFW, Neural or runtime selection.
CMake is 0.612.0. IPC61101/V611 mapping and provider45601 remain compatible.

## Checks and next owner test

The 64 selected checks pass, plus both added D3D11/D3D12 shipping-path cases
without engine origins: 66 distinct passing checks. The unit/benchmark case
holds Cycle estimated at the exact same 30ms before and after engine-origin
loss. Native receipt, overlay, all-framework, pooled/3X/Fast, Luke and VDXR
coverage is recorded in V612_VALIDATION.json. The package loader is verified
separately. Real-game validation remains pending.

For the proposed UEVR comparison, first record a short stable scene with OFXR,
AFW and Neural disabled and F9 active. Check that Game has valid paired samples,
then compare it against the real producer's frame interval (1000/game FPS),
not total/generated/headset FPS. A second load level can distinguish a useful
approximation from a coincidence caused by a cap or external pacing. No
FPS-derived fallback should be enabled until this comparison is measured;
one game's result cannot establish reliability for all engines/mod families.
