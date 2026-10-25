# V611: pacing choices and source production timing

Local candidate based on V610. The owner reports all routes appear functional
in Ghostwire with V610, but the Game/Delivery partition is misleading. The
owner defines Game as observed frame-production start through handoff to OFXR,
followed by Generation and Delivery; disabled generation has only Delivery
after handoff. This is a chronological wall-clock measurement, not GPU busy
cost, input latency, or exclusive attribution of API blocking to a component.

## Pacing menu

The shared desktop/F10 Generation tab replaces the V610 checkbox with:

- **Framework** (`pacing_mode=0`): no common private SteamVR endpoint/director
  or dedicated final delivery queue is installed when creating the session.
  The existing framework/native transport runs, as on the VDXR path, retaining
  runtime-specific compatibility behavior.
- **Director** (`1`): V610 Single director enabled. Preserve source/history and
  GPU ownership; bypass the same upstream presentation holds and counted/bias
  policies, using the existing final private director.
- **Framework+director** (`2`): V610 Single director disabled, preserving the
  previous combined policy. Default remains this pre-existing behavior.

VDXR retains its Framework transport regardless of the saved SteamVR choice.
The native OpenXR graphics binding and swapchain ownership are session-scoped.
Crossing between Framework and either director transport therefore requires a
**game restart**. A selection is saved without replacing an active session or
pretending the transport changed. Both the dropdown and status warn while a
restart is pending. Director <-> Framework+director remains a drained live
policy change that preserves the generator, history and GPU resource states.

The existing `ofxr-bridge.ini` stores the global integer; it is not a per-route
preference. A V610 checkbox in that same INI migrates ON to Director and OFF to
Framework+director. Explicit `pacing_mode` takes precedence. IPC is61101 with a
V611 mapping; tray and game must use matching versions. Provider ABI45601 stays.
Compact configuration keeps requested/effective sole-director bits40/41 and
adds requested pacing mode in bits42-43. Detailed CSV appends PacingMode instead
of the old boolean label; suite ordering and sheet layout are not redesigned.

## Fast mode

Lower VRAM is renamed **Fast mode (lower vram, lower latency, may reduce
performance)**. The combined private-texture/reduced-ring behavior is unchanged.
INI/profile field names remain unchanged. It still has no effect with Luke Ross.
The owner will decide benchmark layout/pacing comparisons separately.

## Production boundary and scope

V608's Game subtracts intercepted XR calls inside Wait-to-End. In the owner's
Ghostwire V610 run, disabled median is0.287ms Game and53.639ms Delivery. This
is not a complete engine production observation: engine work can precede Wait,
and asynchronous rendering cannot be inferred from the short CPU API burst.

The first producer adapter uses the existing public UEVR SDK2 engine-tick
callbacks. With measurement demand (visible meter/settings, active benchmark
or F9), it timestamps the pre-engine-tick callback and clears that thread's
active production token at the post-engine-tick callback. This observes the
start of the original game-engine Tick body after UEVR's earlier pre-tick
handlers; it does not claim to timestamp earlier input/simulation/mod work.
No engine patch, GPU query, new GPU wait, or producer pacing change is added.
Callbacks unregister when the last session releases observation. An SDK that
is not ready at session creation can attach on a later measured Wait.

A stereo LocateViews made inside that tick records its owner, serial, QPC
start, requested display time, space and both returned poses in a fixed ring.
The submitted projection must match the exact time, space and both poses.
A newer tick with the same stationary poses but a different target cannot
replace an older source. Reused identical targets with distinct matching tick
identities are ambiguous and stay N/A. Changed poses, multiple scene
projections, stale origins and absent callbacks also stay N/A; no frame period,
previous End return, latest tick or arbitrary residual is substituted.

This is intentionally an initial **UEVR adapter**, not a universal game-start
hook. Native OpenXR titles and Luke Ross currently have no engine-start
adapter here. Their Game and complete Cycle estimated remain N/A, while
source-handoff Generation/Delivery and existing FPS remain independently
available. A changed prediction time or transformed projection can also prevent
an unambiguous UEVR association. Owner validation in the real game is pending.

For a matched production start:

- Game: observed engine start -> application source EndFrame entry.
- Generation: source End -> readiness of the first confirmed generated output,
  retaining the existing preparation/copy readiness boundary. Zero without a
  delivered synthetic output.
- Delivery: the remainder **after source handoff** through the real predicted
  display target (or a later native/producer return). Without generation,
  this is the whole post-handoff interval.
- Cycle estimated: observed engine start -> that same real endpoint.

The three durations partition each source's full observed cycle. Pre-handoff
XR calls remain inside Game under the owner's chronological definition.
Independent one-second displayed medians need not sum exactly. The prediction
horizon is included in Delivery and is not proof of exclusive OFXR overhead or
physical panel scanout. Unknown engine origin does not invalidate independent
Generation/Delivery. Compact benchmark_stages result bit0 marks those two
stages valid; bit1 marks Game valid. Missing full-cycle origins export as N/A
rather than continuing the previous incomplete Wait-origin estimate.

## Validation boundary

Automated checks exercise the actual production callback/LocateViews/End hooks,
late Wait, retained frame identity, independent missing-origin delivery,
stationary-pose ambiguity, disabled baseline, stereo mismatches, expiration,
three menu values and old-INI migration. Actual session creation checks that
Framework installs no director, leaves the active transport intact when the
selection requires restart, and preserves the pre-existing director bindings.
The inherited V610 private endpoint, all-framework, hot-switch, Lower VRAM,
3X, pooled, D3D11/D3D12, Vulkan bootstrap, VDXR and GUI/control checks remain.
See V611_VALIDATION.json for exact final results and earlier fixture failures.

No live game installation, user INI mutation, registry change or publication is
part of candidate preparation. No vector, depth-subtraction, AFW/NR algorithm,
physical director schedule or interpolation kernel change is included.
