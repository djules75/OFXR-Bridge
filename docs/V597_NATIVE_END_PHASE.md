# V597: actual native EndFrame phase lead

Owner authorizes trying an earlier xrEndFrame after the MSFS2024 analysis.
This is a comparison build based on exact V596, whose source-lifetime fix stays.

## Why this test

The V595 MSFS detailed tail has 1978 uniquely joined native submissions and24
unshown frames;23 lie in two bursts. Copies are already complete before End,
raw native predictions are future and CPU calls follow the physical cadence.
During those bursts the compositor update phase moves from about6.9ms to0.1ms
relative to its own frame/vsync reference. Native End starts around0.03ms;
the second-eye Submit completes around0.3-0.5ms. This supports testing final
submission headroom, but is not proof that a1ms shift fixes every loss.
NewFrameReadyMs is a CPU second-Submit observation, not GPU completion.

The previous additional1ms budget opens Wait/Begin early. The final End still
waited for the unshifted physical edge. Advancing only the preceding calls
did not provide a1ms lead for the actual final handoff.

## Implementation and scope

PrivatePhysicalSchedule stores each raw physical anchor at raw_at minus1ms.
It keeps the raw counter and period. All claimed End slots therefore lie on
that fixed earlier grid; the consumer's actual xrEndFrame follows the shifted
deadline. The native Wait lookahead follows the same handoff phase, retaining
its existing service budget. No request is made to change headset frequency.

Apply the offset once per raw anchor, never to an already shifted deadline.
Refreshing the same clock cannot accumulate advance. Packet readiness, held
work latency, pending native targets and FIFO order still constrain slot
selection. A packet too late for the advanced slot remains owned and uses a
later physical slot; no early/unready handoff or catch-up burst is authorized.
Existing bounded phase correction for late CPU entry is unchanged.

This applies to the common SteamVR D3D11/D3D12 private director with a valid
headset phase, including the existing Vulkan translation and Luke Ross path.
The fallback without a valid physical phase is unchanged. VDXR, AFW, image and
swapchain budgets, meter, INI and benchmark layout are unchanged.

## Validation

All33 focused checks pass. Exact V596 source plus its exact physical-clock
headers fails the new actual native End phase assertion. V597 passes D11/D12
with actual GPU resources and copies. The native mock spends350us inside End
before recording a receipt, which must still precede physical vsync with at
least300us headroom. Every source pixel and per-packet metadata is retained.
This is an actual call-entry/receipt test, not merely a Wait/Begin timestamp.

Pure-clock coverage runs10000 refreshes at75,90 and120Hz, including repeated
raw-anchor observations, requiring exact original periods and a constant1ms
phase. Existing physical late-copy, fallback-to-physical transition, source
reuse/generations, route switches, flags, native failure/stop and bounded FIFO
checks pass. Hardware compositor behavior still requires an owner game run.

## Owner comparison

MSFS2024, same scene/settings/headset refresh as V595/V596. Use0.3.0 2X,
Lower VRAM and Neural off, F9 on throughout approximately60seconds in the
level, then normal close. No F8 or full benchmark is required. Compare actual
presented output and sporadic drops; do not infer improvement from production
counters alone. Candidate stays local until owner acceptance.
