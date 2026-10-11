# V448 validation

Isolated Windows x64 MSVC optimized Release candidate, 2026-10-10, based on
V447 `267d5bf72b1e524e20fd057d16ba821f69871921`. The selected private FSR provider
now matches the signed SDK runtime used by shared embedded OptiScaler GPU tests:
API provider 1.1.3, file version 1.0.1.41314, SHA256 12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6.
V447 used the different modular provider API 3.1.6. Direct ffxQuery enumeration
succeeded on both source dependency DLLs. See docs/V448_FSR_PROVIDER.md/json.

The generator, FSR parameters, shader math, scheduler and native guide ownership
are unchanged. Passive per-eye input diagnostics now also cover ordinary VR at
the existing once-per-120-pair rate; validity bits expose native camera/matrices.
V447's actual Streamline 1.x/2.x detour fixtures remain in the complete suites.

## Complete final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 132/132; none skipped | 79.38 s |
| djules | 149/149; none skipped | 86.68 s |

All seven shipped runtime binaries match across both fixture selections; the
new fixture configuration is restored before packaging. Complete verbose logs
and runtime hashes accompany the package. Actual selected-provider tests cover
FSR stereo/packed generation, private queue ordering, Lower VRAM, missing-guide
bypass and recovery, repeated pause/output-shape switches, GPU transport and
mandatory depth-aware Luke correction. They retain native DLSS-G evaluation
on RTX 4090, IPC, window capture and menu reopening checks, without skips.
Import resolution, extracted checksums, previous catalogue hashes and the
packaged split-eye loader probe are verified by the numbered tools. The separate
verification receipt binds the package to its clean source commit. The original
V447 validation is preserved byte for byte under docs/history/.

## Limits and owner validation

The owner ran V447 Returnal and rejected FSR rotation quality with both mouse
and headset; the comparison is embedded OFXR FSR in OptiScaler. The provider
selection difference is proved, but visual causality is not. No image/vector
capture establishes a per-pixel source domain error. The exact V448 Returnal
mouse/headset comparison is pending; automated generation does not certify
artifact-free output or performance. Lower VRAM remains default off. External
NR model evaluation and the late first-NGX discovery limit remain separate.

No game launch, live DLL installation, tray activation, layer registration,
canonical port, push or publication occurred. Previous numbered sources,
checkpoints, packages and migration receipts remain preserved.
