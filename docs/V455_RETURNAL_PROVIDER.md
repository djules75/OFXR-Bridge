# V455: private native FG after incompatible core evaluation

Fresh isolated checkout from sealed V452 3accd73e6bc43c1f4864dc5a067a333072192aa2,
with V454 queue synchronization cherry-picked locally. V453 profiling is absent.

## Evidence

The owner Returnal V447 PID29344 reports GUI bypass with green ~114FPS. Retained
last10s deliver1171 physical slots (585 real/586 nominal synthetic), while the
application completes585 frames. V447 cannot confirm interpolation: copied B
was labelled synthetic. The V452 confirmed-content counter is retained.
This trace alone cannot certify every earlier V447 run or name its refusal.

The closed V453 Returnal PID32180 log records1494 core evaluations failing
0xBAD00005/InvalidParameter. Loaded-module evidence identifies the game's own
FG1.0.3.0; caps and feature creation succeed. The bundled provider is310.9.1.0.
The same real older provider reproduces InvalidParameter in the isolated GPU
fixture. Exact rejected SDK parameter is not identified; no guessed parameter,
guide relaxation, game DLL replacement or core shutdown is used.

## Recovery and ownership

Previously only FeatureNotFound discovery could open the validated private
forwarder. Now a core-owned feature's actual InvalidParameter evaluation latches
one recovery attempt for that native context. The rejected pair still copies B
and has a non-generated receipt. On the next eligible pair, the existing private
requirements/init/device-capacity checks validate the bundled provider; this
pair also stays in real-copy bypass. If validation fails, ordinary refusal
continues without repeated provider initialization or a false availability flag.

After successful validation, the feature's provider owner participates in its
existing resize condition. The current completion fence must have passed before
its old core handle is released or private-owned handles are created. No new CPU
wait, same-list retry, global core reset, game-handle mutation or shutdown occurs.
New neural histories are seeded from the original exact same-eye guides. Actual
private capacity controls generated outputs; shader/camera/stereo/guide gates and
accepted Luke correction remain unchanged. Each handle retains its creator for
Evaluate/Release. Per-context ownership avoids changing already-recorded handles
when another swapchain recovers on the same device.

Native provider event stage8 records evaluation-triggered recovery: result1/0
means private validation succeeded/failed; b=actual capacity, c=1. Existing events
record the initial core InvalidParameter, private requirements/init, subsequent
direct=1 evaluations and completed GPU interpolation decisions.

## Real GPU regression

Optional CMake input XRFG_TEST_LEGACY_DLSSG_PROVIDER is a real older provider,
kept separate from packaged runtime. The fixture initializes the game's core
with that path before OFXR, requires available=1/init success and confirms that
exact module was loaded. In the same context it requires two refused receipts,
then successful interpolation through the adjacent modern private provider.
Actual packed stereo midpoint pixels must match opposite translations in both
eyes, outperform blending, and report an enabled GPU decision. The game's
original core capability answers remain available/success after recovery and
D3D12 debug validation must remain clean.

The same fixture compiled against V452/V454 native implementation fails before
the fix. V455 passes on RTX4090: core0xBAD00005, private seed/normal result1,
completed decision mask1/disable0. Both eyes midpoint MAE~0.00002/0.00004,
versus blend~0.994/1.009. Final complete new/djules suites and runtime hashes are
bound in VALIDATION.md/BUILD_PROVENANCE.json. These are laboratory results;
owner Returnal/headset validation remains pending. NMS throughput, Returnal FSR
rotation quality and native Detnoir performance optimization remain separate.
