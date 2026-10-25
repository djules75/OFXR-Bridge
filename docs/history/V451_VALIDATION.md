# V451 validation

Windows x64 MSVC optimized Release, 2026-10-10, based on preserved V450
57586efb86c232b3fffc7272dabfd77cba242c79. See docs/V451_DETNOIR_GUIDES.md.
Exact r19 reference: f7ece4d721cae5158dca4b0f1f46d9287e8be798.

## Before/after GPU regressions

The actual V450 implementations fail all three new regressions: packed game
vectors are rejected on WARP and NVIDIA, and real late NGX recovery is refused.
Restored V451 passes both packed vector routes with opposite eye motion,
output-resolution scaling, slot reuse and no GPU debug errors. The matching
provider reads color from the correct logical eye while both RTVs use slice 0.
A fresh physical empty application directory produces actual FeatureNotFound
before the signed provider is supplied. V451 recovers through real provider
requirements/init/device capabilities and actual Create/Evaluate/Release,
then passes packed/cropped/sRGB/reseed pixel readbacks on RTX 4090. The original
NGX core's capability answer remains unchanged afterward. No shutdown is called.
Unsupported WARP remains refused; the earlier genuinely absent provider/route
recovery, missing-guide whole-real-frame bypass and structural/budget guards pass.
The provider reports capacity 1 on this RTX 4090, so native 3X stays gated to 2X.

## Final complete suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 137/137; none skipped | 88.37 s |
| djules | 154/154; none skipped | 94.72 s |

All eight packaged runtime hashes match across both configurations. The new
fixture configuration is restored before packaging and the restored shipped
binaries match those fully tested hashes. Additional final detailed regressions
and live IPC logging are preserved in evidence/. All existing menu preferences,
live logging off/on/off/on/off, provider/guides/stereo guards, native FG, FSR,
GPU fence/snapshot ownership, Streamline ABI, output transitions and mandatory
Luke depth-aware corrected Normal motion tests remain. Signed FSR API 1.1.3 and
the NR forwarder are unchanged. V450 validation is byte-exact in docs/history/.
Extracted checksums/imports, catalog preservation and packaged loader checks
have a separate verification receipt tied to the clean source commit.

## Scope and pending owner tests

Detnoir PID33380 V450 provides the exact packed geometry and captured DLSS
inputs matching the confirmed source defect, plus native FeatureNotFound at
startup and selection. New game-vector consumption and native late recovery
are GPU-tested; owner Detnoir/headset validation on V451 remains pending.
Returnal FSR rotation quality remains pending. This is a selectively scoped
packed-guide/native-discovery fix, with no r19 hybrid/scheduler/pose port.
Use the matched V451 tray/layer, IPC 45101. Both FG DLLs are included. The
NVIDIA NR model stays external. No live installation, activation, registry
change, game launch, shared/canonical port, push or publication occurred.
