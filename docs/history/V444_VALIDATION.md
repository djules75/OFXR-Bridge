# V444 validation

Local isolated Windows x64 candidate, 2026-10-09. One optimized MSVC VS2022
Release layer and tray. Test-route selection changes fixtures, not the shipped
set of OFXR engines. Source parent: V443
`6cf534a05aed76d0251228aa86796f5c5729b90f`.

## Final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| NVIDIA djules / extra 3X policies | 142/142 passed; none skipped | 79.57 s |
| NVIDIA new/reference | 125/125 passed; none skipped | 73.21 s |

Full logs ship under `evidence/` in the candidate and are retained in
`work/shared/ofxr-autonomous/build-v444`. The runtime DLLs and tray are the
same optimized build for both fixture selections. The ordinary new/reference
fixture selection is restored after the djules suite.

## V444 settings and guide regression

The native/FSR hot-switch regression presents eleven exact real stereo frames
and two OFXR fallback synthetic frames. Eight requested native/FSR cycles lack
motion or depth; all retain real eye/HUD content and Wait/Begin/End ownership
while reporting the precise bypass reason. Switching back to OFXR with game
motion requested recovers optical-flow synthesis and explicitly reports fallback.
No GPU debug errors occur. The native GPU suite also exercises five prime/retire
cycles with the NVIDIA backend selected, checks exact real stereo copies and
refuses guide-less primes without retaining an anchor.

The owner's V443 null read is bound to the exact checkpoint by dump image
timestamp/size and the matching build DLL/map. Its RVA maps to the absent OFA
allocator reset. The symbolic receipt ships in evidence; the owner's raw dump
and logs remain local. See [the source origin and crash explanation](docs/V444_SETTINGS_AND_GUIDES.md).

Profile codec tests cover off defaults, migration from -1, preservation of
explicit on and 0/1 persistence. Actual shared ImGui checkbox tests publish
Lower VRAM from tray to headset and back. GPU route-shape and explicit output
tests check both flag states. Deep djules readiness/switch fixtures explicitly
cover deferred staged outputs and direct outputs independently: only the staged
variant schedules owned repeats while its GPU output is pending.

Typed D12/D11/Vulkan SR/RR hooks observe input presence before capture/transport
work. D12 monitoring also starts in optical-flow mode; capture retains its active
guard and creates no GPU guide copies for passive observation. Runtime diagnostics
separate observed input fields from owned XR guide matching and actual generation.

## V442 startup failure regression

Two owner sessions in Planet Crafter failed after a history capture returned
ERROR_BUSY, with a healthy D3D12 device. The missing ticket was admitted to the
worker; its preparation failure set XR_ERROR_RUNTIME_FAILURE, causing Unity to
close XR. No new native crash dump was produced. See
[the causal trace and physical correction](docs/CRASH_V442_PLANET_CRAFTER_START.md).

The retained V443 real D3D12 test holds consumer leases on the next history ring slots.
Frame 4 refuses the left eye; frame 6 refuses both eyes. It calls production
Release/EndFrame/worker/director code with a mocked OpenXR runtime. Before the
correction the regression fails. V444 delivers all eight stereo real frames in
order and three synthetic frames, retires the old anchor at each refusal, primes
fresh stereo input before resuming, and reports no D3D12 debug errors. Eye and
static/dynamic quad pixel checks retain their ordinary thresholds. Capture
remains nonblocking and busy GPU-owned slots remain protected.

## Coverage and limits

The passing suites include native NVIDIA DLSS-G evaluation on RTX 4090, real
Vulkan/D3D12 transport, real D11/D12 stereo generation and resource retirement,
DLSS guide ownership, the accepted depth-aware Luke AFW Normal correction,
WARP Neural Rendering colour-codec checks, and live IPC/lifecycle fixtures.
The proprietary external NR model itself is not evaluated or packaged.

They also retain the V442 ImGui reopen regression and input/capture tests,
movable/scalable panel controls, real WARP D11/D12 menu rendering and OpenXR
composition fixtures. V444 changes the shared generation widgets and runtime
diagnostics while retaining those menu ownership/rendering regressions.
No new headset capture or visual-quality claim is inferred from these fixtures.

All four OFXR engines, FSR FG, native DLSS-G, four Luke AFW modes and NR
before/after SR remain available. Assetto CSP's special producer and arbitrary
FSR/XeSS-to-DLSS substitution remain excluded. The known first-NGX-provider
startup limit is unchanged; late injection is not repaired by this build.

V444 still needs the owner's Planet Crafter native/FSR switch and changed-options
F10 reopen test, plus headset image-quality/performance validation. V442's reported
startup failure is preserved as an owner result, distinct from its passing
engineering tests and from V441's menu-reopen access violation.

No game was launched, live DLL installed, layer registered or tray activated
during preparation. No canonical port, push or publication was performed.

Package BUILD_PROVENANCE.json, PROVIDER_IMPORTS.json and SHA256SUMS.txt bind the
source archive, runtime artifacts, dependency import checks and suite evidence.
The local numbered checkpoint retains source, binaries and manifest separately.
