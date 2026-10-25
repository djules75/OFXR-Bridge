# V455 validation

Windows x64 MSVC optimized Release,2026-10-10. Fresh isolated checkout from
sealed V452 3accd73e6bc43c1f4864dc5a067a333072192aa2 with V454 queue fix cherry-picked.
V453 native GPU profiling is absent. See docs/V455_RETURNAL_PROVIDER.md.

## Actual provider regression

Real Returnal nvngx_dlssg.dll1.0.3.0 initializes the NGX core first and advertises
FG available/create success. Its actual seed Evaluate rejects InvalidParameter.
Compiling the new fixture against the unchanged V452/V454 native implementation
(with only the matching private ABI45501) fails: no actual packed interpolation.
The corrected native implementation passes, with two non-generated refusal
receipts, validated private310.9.1.0 recovery, successful private seed/normal
Evaluate, enabled GPU decision, correct stereo midpoint pixels, unchanged
original game core availability answers, and no D3D12 debug errors.
Before/after logs, exact flight traces and hashed regression receipt are packaged.
An additional per-eye-feature run passes the same recovery/pixel checks.

## Complete suites and artifact checks

Complete new139/139 (90.79s), djules156/156 (96.92s), none skipped.
All eight runtime binaries match across both configurations; new is restored
and hashes checked again before packaging. No tests or assertions were relaxed.
The new real older-provider GPU regression runs in both complete suites.

Package helpers require clean matching source and complete suite success,
inspect all runtime imports, bind source archive/checkpoint/catalog hashes,
and verify extracted file checksums and the packaged split-eye layer loader.
Exact final receipt is produced by Verify-V455.ps1 after packaging.

The V452 result/counter, private missing-provider/late-discovery, invalid guides,
FSR, route switching, logging, Luke correction and lifecycle guards remain covered.
V454 per-VkQueue serialization/regression is retained in both complete suites.
No shader, camera/reset threshold, guide gate, cadence or GPU transport change.

## Owner validation still required

The laboratory test proves compatibility recovery with Returnal's actual old
provider and the real RTX4090. It does not certify Returnal in-game/headset
behavior or performance. The V447 green counter alone was not a valid proof of
interpolation; its diagnostic event limits are recorded separately.
NMS performance recovery, Returnal FSR rotation artifacts and Detnoir native
performance optimization remain open. No game DLL replacement, live tray
activation/registration, game launch, shared port or push was performed.
