# V447 validation

Isolated Windows x64 MSVC optimized Release candidate, 2026-10-10, based on
V446 `ee00317beba0f59756422ae0817bbbdd87d5af71`. Production changes distinguish
Streamline 1.x/2.x camera API declarations and refuse unknown/inconsistent ABIs.
Renderer, scheduling, native guide ownership and mandatory Luke correction
retain the V446 behavior. See docs/V447_RETURNAL_STREAMLINE.md.

## Crash evidence and regression

Four fresh Returnal V446 dumps bind the access violation to the same evaluate
hook RVA 0x5ba90. The exact sealed DLL matches the original build DLL byte for
byte; dump image timestamp and SizeOfImage also match. The original linker map
and disassembly identify an integer legacy frameIndex read as a modern inputs
array. Returnal's Streamline interposer is 1.3.3.0. The sanitized receipt is
shipped in evidence/V446_RETURNAL_CRASH_SYMBOLIZED.json; full private dumps and
logs remain under the laboratory diagnostics/v447-returnal-streamline folder.

The physical 1.x DLL regression produces SegFault with V446's unconditional
modern detours. The typed fix passes five DLL cases: 1.x, 2.x, unknown major,
version/export contradiction, and missing version resource. It checks the
actual SafetyHook boundary, SDK-sized guard-page Constants, original call
arguments and results, exact frame/viewport admission, nested scope restoration,
failed constants, wrong frame/viewport and modern viewport ambiguity. There
is no latest-camera or opposite-eye recovery. The original calls still run.

## Complete final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 132/132; none skipped | 81.74 s |
| djules | 149/149; none skipped | 87.79 s |

All seven shipped runtime binaries match across both fixture selections;
the new fixture configuration is restored before packaging. Complete verbose
suite logs, pre-fix/fixed regression output and runtime hashes are shipped as
evidence. Import resolution, extracted checksums, all previous catalogue hashes
and the packaged split-eye loader probe are verified by the numbered tools;
the separate verification receipt binds the package to its clean source commit.

The suites retain actual native DLSS-G GPU evaluation on RTX 4090, FSR stereo
ownership, missing-guide native/FSR switching bypass, capture-busy recovery,
V446 paused output-shape admission, Luke depth/camera correction, D11/Vulkan
transport, per-eye mapping, shader quality, IPC and menu reopening checks.
The original V446 validation is preserved byte for byte in docs/history/.

## Limits and owner validation

Exact V447 Returnal startup, headset quality and performance remain pending
owner verification. CPU ABI reproduction and automated GPU checks do not prove
the complete game session. V446 Callisto option-switch game confirmation remains
pending. External NR model evaluation and the late first-NGX discovery limit
remain separate. Lower VRAM stays default off and native scale stays 100%.

No game launch, live DLL installation, tray activation, layer registration,
canonical port, push or publication occurred. Previous numbered sources,
checkpoints, packages and historical migration receipts remain preserved.
