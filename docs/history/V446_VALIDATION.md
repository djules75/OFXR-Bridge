# V446 validation

Local isolated Windows x64 optimized Release candidate, 2026-10-10. Parent
V445: `1c8e6fb509f2a39227b06d0d4b701b8887a30776`. The production change preserves
pending generation admission after retiring an output shape while paused.
No additional upstream renderer or scheduler changes are imported.

## Regression before and after

The new xrfg_paused_route_shapes_gpu test fails before the fix in 0.67 s with
"paused output replacement lost generation admission". With the fix, it and
the existing live route-shape regression both pass (2/2).

The actual D3D12 layer repeatedly pauses, selects reference/djules/new and
resumes with Lower VRAM off and on. GPU output layout, source history and
release captures stay valid; previous projection history is retired. After
the last resume the real release/EndFrame/worker/director path presents eight
real and seven synthetic frames. Existing readback assertions reject poisoned
source images, duplicate/lost frames, incorrect XR ownership, leaked private
swapchains and D3D12 debug errors. Only the runtime is mocked.

## Complete final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 127/127; none skipped | 79.19 s |
| djules | 144/144; none skipped | 86.05 s |

All seven shipped runtime binaries match byte-for-byte between new/reference
and djules fixture configurations. The new fixture configuration is restored
before packaging. The pre-fix failure, focused checks, complete suites, detailed
test output and runtime hashes are shipped as evidence. Import, extracted
checksum and packaged split-eye loader verification are performed by the
numbered packaging tools and recorded in the verification receipt.

The suites retain capture-busy recovery, missing-motion/depth native/FSR hot
switch bypass, actual native DLSS-G GPU evaluation, FSR stereo ownership,
accepted Luke camera/depth correction, D11/Vulkan transport, per-eye guide
matching, shader quality, IPC and menu input/reopening checks from V445.
The original V445 validation receipt is retained in docs/history/.

## Owner game evidence and limits

The owner V445 Callisto trace identifies a successful enable followed by
missing_generation throughout the remaining session. Its diagnosis and exact
trace hash are retained in evidence/V445_CALLISTO_DIAGNOSIS.json; full raw logs
remain in the laboratory diagnostics/v446-callisto-switch directory. The code
path and failing GPU regression support the admission-loss diagnosis.

The owner's initial "everything works" report was superseded by the report
that option changes stop generation. The exact V446 package still requires
Callisto/headset confirmation; automated fixtures do not prove in-game visual
quality or timing. Planet Crafter validation remains pending. External NR model
evaluation and the first-NGX provider-discovery limitation remain distinct.
FSR/XeSS guide adapters, arbitrary upscaler substitution, hybrid/extrapolation,
upstream pacing/promise integration and Assetto CSP's producer are absent.

Lower VRAM stays default off and native resolution stays 100%. Existing source,
numbered packages/checkpoints and the historical migration marker are retained.
No game launch, live DLL installation, tray activation, registration, push or
publication was performed during preparation.
