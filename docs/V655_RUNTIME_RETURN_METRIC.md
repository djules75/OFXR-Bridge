# V655: observed cycle through successful native OpenXR End return

Owner chooses the verifiable handoff to OpenXR/compositor, excluding subsequent
display prediction/encoding. Endpoint is successful RETURN from the actual
runtime xrEndFrame, not its entry, a virtual bridge success, panel scanout or
producer's later return. Origin stays the same retained source's app Wait entry.
Any work/blocking performed synchronously inside native End remains measured;
its internal cause cannot be separated by an external API-layer observer.

Cycle to runtime = native successful End return QPC - same cycle Wait-entry QPC.
Private endpoints use their already published successful nonempty REAL receipt;
pending/failed/empty receipts remain unavailable, never virtual acceptance.
Legacy direct runtimes use the existing successful End return timestamp from
the FPS wrapper. No predicted-display target, hypothetical generated-frame slot,
constant11ms or producer tail is added. Disabled FG and AFW-only use the same rule.
The metric works without a display-time conversion extension or synthetic frame.

Scope is measurement/UI/export globally: SteamVR and VDXR Legacy/new/private,
Luke/non-Luke, all backends. No graphics binding, capture, resource, frame math,
queue/capacity, clock, scheduling, End/Wait call or provider change from654.
All654 delivery/production policies remain, including scoped2LukeD11SteamVR
credits; VDXR Legacy does not acquire the private bridge as a side effect.

OSD/F10 third value is renamed Cycle to runtime. CSV metric rows explicitly use
Cycle to runtime min/median/max ms. Benchmark selection uses the same cached
measurement. Historical cycle_estimated_* transport fields/log slot remain only
for layout compatibility;655 is the semantic boundary, not a hidden offset.
Internal owner-sheet adapter accepts old CSVs and new labels without mixing them.
Its conversion remains local, not bundled in the DLL/package. New and old latency
numbers have different endpoints and must not be compared as an optimization.

Cycle length remains producer-inclusive, so can be higher when app End has a tail
after native handoff. OFXR time remains CPU call-union time. Delivery partition
now terminates at observed native return, generation/preparation unchanged.
No claim of full engine/input-to-photon latency or actual panel/encoding time.
Existing successful-return clocks/receipts are reused: no new polling or waits.
