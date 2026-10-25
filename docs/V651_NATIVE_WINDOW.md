# V651: native prediction coverage follows the actual display edge

Exact V650 source plus one physical-clock renewal policy correction.
All V648 queue isolation, V649 HFW worker/decoder and V650 dependency fixes
remain. No AFW/provider/guide/marker/depth-subtraction or synthesis changes.

## Captured cause

Horizon SteamVR V650 PID4596 has a good retained 3.48s trace and two slow
7.40s traces, all Djules Director/AFW1x1. Good: ~45 real productions/sec,
output90FPS, worker median20.36ms, buffer wait median3.56ms. Slow: ~40 real
productions/sec, worker still20.33-20.35ms, buffer-wait p90~14.7ms.
The V650 runtime gate is no longer blocking (maximum0.057ms in slow windows).

Every physical output gap>16ms in both slow windows coincides with exactly
one preventive native-clock renewal:62/601 and66/597 retained output packets.
All128 are stage25, NONE an expired-clock stage23. Their original native
prediction covers the planned handoff by~9.25ms (minimum~9.21ms),
yet the code demands coverage for a full additional11.111ms physical period.
Most renewal transactions take~11.06ms. Learned native-entry lead rises from
~1.6ms in the good trace to~17ms in slow traces, reinforcing early entry.
These are CPU/native protocol observations, not isolated GPU-time claims.
The retained AFW-Off-generation baseline is67FPS median; owner observes~69.

## Correction and scope

The physical handoff is1ms before headset vsync. A native prediction covering
that physical edge plus the existing100us guard is sufficient; it need not
cover another complete refresh after handoff. Use this exact threshold for
the existing bounded preventive renewal. The expired-cycle recovery loop,
one-attempt preventive bound, saved image packet, raw runtime clock, physical
grid, period, input/output FIFO capacity and backpressure are unchanged.
No frame is dropped, repeated, timestamp-invented or delivered early by this
change. Runtime prediction coverage must still be checked in headset trials.

Common physical-clock private endpoint: every SteamVR framework using it,
not HFW-only or one NVIDIA route. VDXR Legacy and AFW-only/FG-Off bypass do
not enter this policy. No new setting, allocations or continuous diagnostics.

## Checks

CPU checks use captured9.226/9.250/9.260ms margins and test expired,
before-display and exact guarded boundaries. Existing10000-refresh clock,
FIFO ownership and variable-Hz checks remain. Actual D11/D12 endpoint tests
retain expired-clock recovery, failure/stop balancing, immutable GPU pixels
and logging-Off recovery. Quiet-clock fixture now explicitly injects a truly
expired clock; it must not rely on unnecessary full-period renewals to pass.
The old short-window test still verifies at-most-one preventive attempt and
exact pixels/order, without requiring a needless attempt on a valid window.

Final76 focused CTests PASS100.68s: full D11/D12 endpoint ownership/clock/
stale recovery and cadence aggregate, NVIDIA/FSR/Hybrid/DLSS-G/3X transports,
V650 dependency fixtures, HFW worker changes, VDXR Legacy/bypass controls,
native capture contracts and AFW beta6 quality/lifecycle. No failing V651
runtime/test variant. Final results and packaged loader evidence accompany it.
Steady90FPS and compositor presentation acceptance remain PENDING, not a
claim derived from mocked endpoint or CPU tests. V650 package remains intact.
