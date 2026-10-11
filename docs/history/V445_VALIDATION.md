# V445 validation

Local isolated Windows x64 Release candidate, 2026-10-10. Parent V444:
`46d0a69067a3e776616c24e74e5ff39ee4259ae6`. Nine upstream renderer fixes and
partial native parameter plumbing are ported; this is not a full merge.
See [the exact integration](docs/V445_UPSTREAM_INTEGRATION.md) and the complete
51-commit audit beside it. Upstream observed head: `16b7096`.

## Final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 126/126; none skipped | 78.04 s |
| djules | 143/143; none skipped | 84.83 s |

The seven shipped runtime binaries match byte-for-byte across both fixture
selections. The ordinary new/reference fixtures are restored before packaging.
Final logs, detailed test output, runtime hashes and earlier failed checks
ship in `evidence/`. Checksum, import and packaged loader receipts bind this
source and tested build without installing or activating it.

## Renderer and ownership regression

Real D3D12 WARP pixel readback checks occlusion edges, an overlay drawn after
DLSS with background vectors, and a moving fade, using unchanged per-eye MAE
limits 2/12/1. The existing head-turn threshold caught the direct shader port's
12.39 MAE regression; the adapted zero-motion camera fallback passes it without
loosening the test. Existing flow, cropped viewport, stereo motion, linear-light
colour and all NVIDIA-route checks remain in the suites.

Guide tests discover the right stream first with nonzero original input offsets,
retain full-resource snapshots and native rectangles, reverse evaluation order,
match direct output identity and resolve alternating shared-stream eyes. A
missing first right-eye publication is refused. The producer submission test
also checks the known-eye cross-queue fence handoff. The real layer/fake-runtime
projection test learns each mono swapchain's eye from one two-view layer and
does not assign a mono eye to a double-wide image. Identity changes clear old
release-time guide associations before the existing fallback/bypass path.

Actual native DLSS-G GPU evaluation, NVIDIA prime/reuse, missing-motion/depth
hot switches, capture-busy recovery, FSR stereo refusal/recovery, accepted Luke
depth/camera correction, D11/Vulkan transport, IPC and menu reopen/input/render
checks pass. The external proprietary NR model is not evaluated or packaged.
V444's original validation receipt is retained in `docs/history/`.

## Earlier failures and limits

Earlier new/reference suites failed the menu fixture's actual foreground-window
precondition. Its setup now temporarily joins the foreground input queue and
pumps messages during a bounded activation wait, keeping the actual focus
assertion and all production masking/capture checks. Three consecutive isolated
input runs and the final complete new/reference suite pass. The complete djules
suite had already passed, including its actual-focus input fixture, before the
test-setup correction. The runtime input code and shipped binaries are unchanged.
The earlier djules build also failed triple-inline pixel/cycle accounting and
two staged deep-readiness expectations. After the final eye-association
retirement change, all three pass in the complete 143-test suite. Those initial
failures are retained; this sequence does not prove their exact cause or rule
out timing sensitivity. No claim of a separately diagnosed djules fix is made.

Automated fixtures are distinct from real-game/headset quality and performance.
Planet Crafter attachment, option switching and F10 reopening still require
the owner's test. Native SR/RR ingress and first-NGX provider discovery limits
remain. FSR/XeSS guide adapters, arbitrary upscaler substitution and Assetto
CSP's special producer are absent. Native resolution remains 100% by default;
hybrid/extrapolation, new upstream pacing/promise logic and benchmark UI are
deferred. Lower VRAM remains default off, with saved explicit on preserved.

No game launch, live DLL installation, registration, tray activation, push or
publication was performed. Existing V444 and earlier checkpoints/packages and
the migration completion marker remain unchanged.
