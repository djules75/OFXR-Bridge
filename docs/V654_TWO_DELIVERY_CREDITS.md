# V654: two admitted Luke/D11/SteamVR deliveries, three images retained

V653 PID39832 is a measured no-gain result: median Estimated97.856ms, output90.
The work margin really decreased2P->1P, but accepted->native handoff remained
33.059ms (V65233.052ms). Three outstanding packets at589/593 measured acceptance
points; prepared->handoff22.040ms. A lower work-margin floor alone does not
remove a saturated FIFO's extra owned packet. No static meter subtraction fixes
actual residence. Trace evidence .local/diagnostics/horizon-v653-latency-pid39832.

V654 is exact653 plus one runtime admission change: for its existing opted-in
Luke native D11/real SteamVR endpoint, Options.capacity=2 instead of3. Storage
still has three images per private swapchain; no image/swapchain/ring reduction.
One packet may be presented while the second is prepared. The third End waits
for a genuinely retired FIFO credit. No old/new packet is replaced or discarded.
This can lower steady queued residence by one90Hz refresh (~11.11ms), but source
production/backpressure and headset delivery must be validated in the title.
Do not claim lower latency or unchanged FPS from a small-texture fixture.

Same eligibility as653: common Luke D11 private endpoint on actual SteamVR.
Framework admission is also bounded; Director alone controls physical cadence.
VDXR/nonLuke/native D12 and Vulkan-translated endpoints remain at three credits.
AFW-only tray bypass unchanged. FGOff still uses the native bypass policy.
All V653 per-image/fence/auxiliary/readiness checks remain; all V652 separation
and V651 native-window fixes remain. No provider/AFW/depth/subtraction/marker/
shader/source-continuity/pose/display-time/meter/pacing-formula change.

Automated validation includes actual D11 pixels/poses, two-credit backpressure
against original three-credit control, three-image enumeration, producer wake
on real retirement, Framework bypass/Director cadence, and stop while a producer
waits for credit. Existing source-ready proof/overlay/rewrite checks and focused
Luke loading/worker/quality/native lifecycle controls must pass before packaging.
The initial admission fixture blocked native End20ms with an11ms fake prediction;
Framework bypass correctly does not renew Director clocks, so the fixture's
freshness assertion fired. Give this controlled-stall fixture a100ms XR horizon,
retaining its11.111ms physical period and strict current-cycle assertion.
No renderer correction or weakened readiness check was made for this test issue.
Headset acceptance PENDING. Accepted652 source/package remain intact.
