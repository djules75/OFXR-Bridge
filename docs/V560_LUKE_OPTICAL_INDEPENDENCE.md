# V560: Luke optical-flow independence; Crimson AFW investigation

Base: V558 (`cf7d31960abf020b9f15bde7211aee32609c9fd6`). The withdrawn V559
spreadsheet-format experiment is excluded. The six-row V558 CSV contract and
benchmark automation are unchanged.

Plain Luke optical-flow OFXR with AFW off now disables native guide snapshot
and barcode recording. Observation hooks remain available for native device
discovery and subsequent AFW activation. Neural rendering and explicitly
guide-dependent backends retain their own input requirements.

AFW rejecting an input with `DXGI_ERROR_UNSUPPORTED` or `ERROR_NOT_READY`, before
a successful AFW evaluation and with both scratch acquisitions released, is
classified separately from a synthesis/device/publication failure. Existing
colour-only OFXR fallback can consume the original XR captures in that case.
Previously that local AFW refusal could block OFXR and enter its failure
cooldown. Device removal, GPU backpressure and failed image release retain
their existing handling. No AFW result or native motion is fabricated.

## Owner run evidence and remaining work

The V558 Crimson SteamVR run (PID 29492) contains protected colour tokens that
decode successfully. Resolver diagnostics nevertheless reject interleaved
native SR handles: for example one handle has serials 525 -> 526, while global
publications advance 3536 -> 3538 (reason 8). Merely accepting that gap would
use a different native history contract; this build does **not** do so.

Two resolved inputs also reach AFW and fail its guide-window validation
(reason 20, outside output). The logged native projection has focal values
2.74748, whereas the XR horizontal FOV is approximately -0.77576/+0.77576
radians. Native output is 3088 square, depth/motion 1544 square, and the eye
colour at that rejection is 3088 x 2829. Widening the admission guard alone
would not establish correct colour/guide alignment. An F8 capture of the
actual scene, depth and vectors has been requested. AFW remains unresolved.

The logged sequence is AFW-only, AFW off/OFXR disabled, AFW-only, then a short
DLSS-G attempt with AFW. It does not establish failure of a plain 0.2.1,
0.2.13 or 0.3.0 AFW-off segment. That distinction does not invalidate the
owner's visual report; it limits what this recording proves.

## Validation

Optimized Release build and 7 focused checks pass (4.43 seconds): new actual
two-eye D3D11-to-D3D12 optical pipeline with zero native guide publications,
snapshots or colour markers; missing-AFW-guide fallback; AFW scratch slot;
post-AFW immutable packet; protected guide ownership; HFW pacing and clock
continuity. The optical GPU test verifies ordered real/generated delivery,
distinct eye pixels, auxiliary ownership and completed publication receipts.

AFW mathematics, native history selection, shaders, providers, swapchain
counts, A/B controls and presentation pacing remain unchanged. No Crimson
headset success, FPS gain or AFW fix is claimed. Candidate stays local.
