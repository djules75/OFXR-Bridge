# V613: three corrected meters and live Framework bypass

The owner stops the incomplete engine-start measurement experiment and asks
to restore the three established timings. The owner also requests Framework
as a bypass inside the installed SteamVR bridge, permitting live pacing changes.

## Timings

F10, desktop settings and the Elliott meter again show:

- **OFXR time**: union of application-thread intercepted OpenXR call intervals,
  including their waits. This is CPU wall time, not pure GPU generation cost.
- **Cycle length**: application Wait entry through the same real frame's
  successful native delivery completion or later producer return.
- **Cycle (estimated)**: the same Wait origin through the confirmed real native
  predicted display target, or a later native/producer return.

The V607/V608 native delivery receipts and V612 stable benchmark origin remain.
Neither cycle stops at the private bridge's early acknowledgement. FidelityFX
and Hybrid therefore retain their corrected final-delivery coverage; restoring
the old labels does not restore the old incomplete approximately15ms estimate.
The prediction horizon is still an estimate, not physical scanout or a full
engine/input latency measurement. Pending/failed native receipts, unknown
clocks and expired observations remain unavailable rather than fabricated.

The experimental UEVR engine pre/post callbacks, per-session registration and
LocateViews/frame-origin tracking are removed. No FPS-derived Game estimate is
introduced. Existing internal optional stage fields/helpers remain ABI-compatible;
they are no longer displayed. Benchmark ordering, CSV/sheet layout, FPS and
cached VRAM calculations are unchanged.

## Framework bypass on SteamVR

The private endpoint and final binding queue are now installed at session
creation for all three choices. Native session, swapchains, private textures,
producer leases, copy fences and the native Wait/Begin/End owners stay in place.

- **Framework (0)**: preserve each framework's upstream policy; bypass the
  final director's refresh floor, phase, work floor, adaptive hold, headroom,
  clock-renewal policy and timed lookahead. Once a packet is copied and ready,
  use the runtime's own Wait/Begin/End and submit it in FIFO order. Native Wait
  starts when prepared input exists, without a director deadline. Runtime/GPU
  blocking remains legitimate synchronization, not an artificial pacing delay.
- **Director (1)**: retain V610's sole-final-director behavior.
- **Framework+director (2)**: retain the previous combined policy and default.

All three switch live. First drain the framework's queued handoffs and all
accepted native packets. Keep the generator/history for a pacing-only change.
Serialize the policy update against packet acceptance, stamp each packet with
its policy, and clear old phase/latency state before re-enabling the director.
Retarget an unstarted next Wait job to remove an old director deadline. The
consumer remains the sole native Begin/End owner throughout; no competing
native frame caller or session teardown is introduced.

Bypass is not identical to removing the bridge. Private images, the existing
bounded FIFO/backpressure, virtual app prediction and final copy remain. A frame
can still wait behind native runtime or GPU work, but no director deadline is
imposed. No claim of equal performance or zero overhead is made.

VDXR and other runtimes keep their existing Framework path. Their connected UI
shows Framework with director choices disabled; a saved SteamVR preference is
preserved without pretending to enable a VDXR director. Fast mode retains its
existing behavior and still has no effect in Luke Ross.

## Validation

The new actual-GPU D3D11/D3D12 checks verify ordered pixel/pose identity,
delivery faster than a refresh floor with an unpaced fake runtime, restored
director cadence, unchanged handles/counts and a blocked native-End transition
that drains without losing its frame. Shipping session tests cover Framework
startup and all live policy choices without recreating the endpoint. Existing
all-framework live-policy tests also traverse Framework during real generation.
The validation JSON records the exact complete selected result and limitations.

CMake0.613.0; shared layout remains IPC61101/V611 and provider45601. Restart tray
and game together to load this new DLL once; subsequent pacing changes do not
require a game restart. Owner headset validation remains pending. No live
installation, active INI/registry mutation or online publication is performed.
