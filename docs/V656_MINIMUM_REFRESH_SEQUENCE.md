# V656: observed completion with the minimum distinct-output refresh constraint

Owner correction: V655 raw runtime return is not sufficient when a Legacy/VDXR
runtime accepts S then B1ms apart. Retain the known minimum refresh separation
for a sequence in which both outputs are displayed distinctly. Do not restore
the runtime's speculative future display horizon, or add another P to outputs
which already took that time. This is a sequence lower constraint, NOT feedback
confirming the compositor displayed those images. A dropped synthetic invalidates
the assumption about all accepted outputs being shown; accepted != presented.

P is the observed physical display period. With S1..Sn then B:
    endpoint = max(observed native B return, max_i(Si entry + (n-i+1)*P))
    Cycle minimum = endpoint - same retained source app Wait entry
At90Hz P11.111ms; at120Hz P8.333ms. One generated output supplies one remaining
interval;3X two. Late S2 contributes S2+P, without rounding onto a fabricated
phase grid anchored at S1's CPU return. Actual native return already later than
that floor is unchanged. Source timestamps remain same-cycle; no producer-tail
or predictedDisplayTime term, fixed offset, extra Wait/sleep or GPU work.

Use native completed, pixel-confirmed synthetic receipt ENTRY for private paths,
not earlier virtual acceptance. End may block until after a synthetic was already
shown, so its later RETURN cannot be used to invent another full display wait.
Legacy receipts retain existing native-wrapper submit entry, including deferred
NGX confirmation; polling cannot retimestamp it. Pending/failed/unconfirmed native
synthetics do not become successful generated slots. Unknown/mixed physical period
with successful generated output remains unavailable rather than guessing11ms.
FGOff/AFW-only without OFXR synthetic output remains its observed native B return.

Scope: GLOBAL METRICS/UI/CSV ONLY, SteamVR/VDXR, all backends/Legacy/private.
Label Cycle minimum; same cached value in F10/Elliott/benchmark/auto-pick/CSV.
Cycle length still includes later producer return; OFXR time unchanged. Old
cycle_estimated_* telemetry/log slot retained only for layout;656 names/metadata
explicit. Internal sheet adapter supports older labels without retroconversion.
Rendering, pacing, runtime selection, bindings, provider/capture/AFW math,
swapchain/ring counts, two-credit LukeSteamVR admission and direct VDXR all
remain655. FPS wrapper changes only preserve an existing timestamp for metrics.
No real-time compositor confirmation, panel scanout or encoding measurement.
