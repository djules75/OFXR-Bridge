# V460 validation

MSVC VS2022 x64 optimized Release. Isolated branch from sealed V456
`a1fcf74e123f825eefd998f558907cb702212718`; no V457?V459 import.

## Final suites and runtime

- new: **141/141**, 90.31 seconds.
- djules: **158/158**, 96.68 seconds.
- Zero final failures or skipped tests. Eight runtime binary hashes agree between
  both configurations. The build is restored to new before packaging.
- The real older Returnal NGX core compatibility fixture is included in both runs.
- Guide, FSR/OFXR/native GPU, release ownership, packed vectors, switch/recovery,
  Vulkan queue, scheduling and layer-call-chain regressions retain their assertions.
- 14 selected renderer/provider-preparation/guide/queue/schedule/shader files have
  unchanged Git blobs versus V456; see evidence/V456_PRESERVED.json.

## New coverage

47 timing/source/state checks cover successful real identity, failed retries,
empty/synthetic/repeat/copied-output exclusion, retained pipeline delay, late
presentation, independent absent clocks, one-second median expiry and two-second
UI freshness, simultaneous detections, principal refusals, FSR selection with
DLSS vectors, and independent UI demand without generation revision changes.

Production OpenXR D11/D12 fixtures test actual converted late clocks, failed
submission followed by successful retry, synthetic/repeat clock-query exclusion,
missing conversion and CPU clock, 4 Hz publication, hidden clock reads, and menu
closure while the independent corner counter continues. Menu/counter Off, focus,
reopen, size/position, blank-space drag and limited composition-layer priority pass.
Shared widget and control tests retain dropdown/slider and concurrent-edit checks.

Actual menu readback images are 1024?768 on D11 and D12 and have identical pixels.
The long-status image wraps above the tabs and shows Late by 24.5 ms. Desktop
preview is 674?861 and shows N/A / No active game. Populated images use explicit
fixture values and are not live measurements. See evidence/READBACKS.json.

## Cost and hidden behavior

100 frames per phase, 10 ms test interval, WARP synthetic-runtime fixture:

| Renderer | Menu open CPU per application_frame | Menu closed, counter on |
| --- | ---: | ---: |
| D11 | 0.1484 ms | 0.0129 ms |
| D12 | 0.5148 ms | 0.0140 ms |

Each open phase completed 100 menu draws; closed phases completed zero. Closing
the last UI stops telemetry publication and display-clock conversion; the new
compositor timing accessor is behind the same visibility guard. The hidden lease
probe measured about 3 ns/call on this host. This is CPU fixture evidence, not a
measurement of game/headset GPU overhead. No runtime timestamp query, fence wait,
new GPU readback command or mandatory logging was added.

## Initial failures and limits

The first UI fixture clicked the new Details header instead of blank space. Its
point was moved to blank space; the production drag logic was not changed.
The first djules full run had three failures (frame-capture readback, deep GPU
readiness, final capture-busy worker). All three then passed five consecutive
runs each with no assertion, timeout or pacing change. The final full suites above
passed; initial logs and repeated runs are retained in evidence.

See docs/V460_INTERFACE.md for measurement semantics. Presentation is predicted,
not total roundtrip; GPU render is SteamVR pre-submit time, not isolated game GPU
cost. SteamVR/game/headset validation remains an owner run. The candidate is
packaged disarmed, without live installation, activation, registration or publication.
Package import/checksum verification and its extracted split-eye loader probe are
recorded by tools/Verify-V460.ps1 in the delivery verification receipt.
