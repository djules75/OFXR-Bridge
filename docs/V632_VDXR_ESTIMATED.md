# V632: retain the real prediction in Legacy Cycle Estimated

Owner Quest 3 / VDXR Legacy Ghostwire V631 run34804036623008 records FSR
103.779FPS /9.991ms and Fidelity119.880FPS /9.454ms. Their estimated branch
used CPU synthetic acceptance plus a refresh slot, ignoring the same real's
already converted predicted display target whenever a synthetic was present.
That omitted the runtime prediction lead, including queued GPU work.

The nonprivate generated estimate now takes the maximum of that real target,
the existing ordered pair-slot bound, producer return and real acceptance.
These are endpoints in the same QPC domain, not durations to add together.
Already spaced native targets receive no extra refresh. Native-required
SteamVR/private delivery, generation, queues, frame scheduling and all FPS
arithmetic remain unchanged. No extra GPU query, fence or logging dependency.
Existing UI and benchmark consumers receive the corrected one-second median.
The telemetry source identifies a converted prediction when it dominates.

The new unit regression fails V631 (CPU-slot10ms versus real target34.814ms)
and passes V632. Existing2X/3X, baseline, missing-clock and native no-double-add
checks pass. Actual D3D11/D3D12 overlay tests publish a42ms future real target,
not the short CPU-slot estimate or a52ms double-add. Four distinct checks have
passing final outcomes; see [validation](V632_VALIDATION.json) for the initial
fixture-demand expiration and retained negative-control evidence.

The old CSV has no per-frame real target and cannot be retroactively assigned
corrected latency values. A fresh owner headset run remains pending.
V631 Tsushima correction is retained; V630 latency trial is excluded.
