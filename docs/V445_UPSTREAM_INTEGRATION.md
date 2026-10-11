# V445: selected native-dlss-fg integration

V445 retains V444's native work-slot crash fix, whole-stereo missing-guide
bypass, capture-busy recovery, passive input diagnostics and default-off Lower
VRAM checkbox. The source parent is V444 `46d0a69067a3e776616c24e74e5ff39ee4259ae6`.

The upstream branch was fetched from https://github.com/elliotttate/OFXR-Bridge
on 2026-10-10, at `16b709610d6e181eded1786d515baf86988cb6e8`. The audited
range begins after our upstream base `135185a033453ae7f844b867c7ef34ae26d40ab3`.
Every commit and its disposition is recorded in `V445_UPSTREAM_AUDIT.json`.
This is a selected port, not a full merge or a new upstream base.

## Imported renderer changes

- `b8f25b8`, `751010a`, `bdf8ba6`, `62f4e5a`, `d4dcab3`: game-motion
  interpolation handles foreground/background occlusion, stationary overlays
  drawn after DLSS, and smooth fades. Non-uniform motion selects surfaces.
- `4e94e95`: optical flow compares its explanation of both frames with the XR
  camera's explanation; a tie retains flow. Bidirectional consistency remains.
- `e1eb825`, `231e85e`: guide eye order follows original input positions;
  direct output identity wins, and a mono swapchain in a two-view projection
  selects its eye rather than the newest evaluation for both images.
- `1c7bf00`, with the small parameter plumbing from `2f1887f`: native DLSS-G
  uses NGX linearized-depth scale 0.1 and explicit default depth heuristics.

## Autonomous adaptations

Upstream snapshots crop guide rectangles; Autonomous retains full resources,
native offsets, snapshot leases, producer command recordings and cross-queue
submission fences. The new source-offset fields therefore record the original
positions without changing the owned snapshot rectangles. New resolver exits
join the actual producer fence before exposing another queue's guide.

Projection eye learning applies to the ordinary D3D12 resolver. Luke AFW's
token-bound camera, same-eye history and depth-aware Normal-motion correction
remain separate. No new extrapolation scheduler or presenter owner is imported.
Learning occurs at EndFrame and affects subsequent release captures; it does
not reconstruct guide observations missed before injection. First discovery
or a changed eye assignment discards the release's previous guide association,
using the existing fallback/bypass until a capture resolves the new identity.

The existing pure-head-turn GPU regression caught a direct upstream shader
regression: zero scene vectors overrode the exact XR camera result (MAE 12.39).
The adapted shader retains the camera result when zero vectors explain the
pair worse than that camera mapping. The original threshold remains unchanged.
New occlusion/HUD/fade tests retain upstream limits of 2/12/1 MAE per eye.

Per-eye tests additionally discover the right stream first, retain a double-wide
input's full snapshot/offsets, reverse evaluation order, check direct identity,
and refuse a right eye that has only the first left-eye publication. The layer
projection test checks a two-view layer with one swapchain per eye and excludes
the double-wide swapchain from mono-eye labeling.

## Deferred upstream features

Hybrid vectors plus FidelityFX, gather/mesh extrapolation, the new release-phase
and display-time promise scheduler, frame recording/replay and the PC benchmark
UI are not included. They intersect Autonomous's accepted scheduler and AFW
ownership rather than applying as independent fixes. The upstream change of
native resolution default to 67% is also deferred; Autonomous keeps 100% and
preserves saved preferences. The menu adds no new options in V445.

Upstream recorded-game measurements describe upstream runs, not this build's
performance. V445's automated validation is in `../VALIDATION.md`; Planet
Crafter and headset quality/performance validation remain pending.
