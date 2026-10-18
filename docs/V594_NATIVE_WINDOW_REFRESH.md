# V594: bounded renewal of short native windows

V593 improves Valhalla, but the owner still observes sporadic drops near 62 FPS
while produced real/generated counts remain stable. Full-run external SteamVR
receipts confirm missing content presentations: the worst one-second intervals
receive 74/show 65 for Djules,75/66 for Reference, and78/70 for Fidelity. These are
delivery losses, not evidence of falling source production. The detailed F9
tail covers only 1.983 seconds, so it cannot identify every earlier loss cluster.

## Change to the common director

V593 renews already-expired unsubmitted native clocks. A prediction can remain
future at our handoff yet leave less than one physical period of native window.
With a valid physical clock, V594 asks for one fresh native cycle in this case,
through the existing Wait worker and sole serialized Begin/End consumer. The
same source packet, images, metadata, leases and FIFO position remain owned.

The preventive request is bounded to one attempt per packet. If the replacement
is still current but short, use it rather than demand an ever-longer horizon:
the runtime need not supply that horizon on demand. Existing V593 hard-expiry
recovery, error/stop handling and nonrenderable behavior remain. Without a valid
physical clock, only that earlier hard-expiry recovery applies. This full-period
coverage guard is a measured scheduling policy, not an OpenXR requirement.

No new fixed latency or shorter-period oscillator is introduced. Recompute the
same absolute physical handoff after native work. The producer, private pool,
queue credits, images and XR swapchain budget are unchanged. Native renewal
can take time, and the existing full-pool backpressure still applies. No AFW,
depth/vector, interpolation, meter-formula or VDXR pacing change is involved.
The common SteamVR endpoint includes D3D11 Luke Ross, D3D12 and existing
translated paths, with AFW on or off. V593 clock-capability negotiation with
diagnostics off is retained.

## Actual runtime evidence

The local standalone probe uses the production private runtime in an actual
SteamVR/OpenXR session at 90 Hz, paired 37.5 Hz inputs and75 content submissions/s.
All source submissions are associated uniquely with OpenVR frame IDs through
second-Submit timestamps inside the native End call (30us alignment tolerance).
Only unrelated initialization receipts are excluded; no source frame is omitted.

| Controlled trial | Source frames | Shown | Unshown |
| --- | ---: | ---: | ---: |
| V593,512px | 901 | 863 | 38 |
| Bounded candidate,512px | 901 | 898 | 3 |
| V593,4096px | 901 | 877 | 24 |
| Bounded candidate,4096px | 901 | 898 | 3 |
| Final V594 source,4096px repeat | 901 | 900 | 1 |
| Bounded candidate,15+15 at 512px | 361 | 360 | 1 |

The single loss in the 15+15 trial is its first source packet. The final-source
repeat exercises 174 preventive renewals, never more than one per packet. These
are sequential controlled trials: they support this change but do not prove
loss-free Valhalla/Luke gameplay or steady 90. Isolated real losses remain.
The probe, raw captures, experimental variants and private symbols stay local.

## Focused checks and diagnostics

33 focused checks pass. New D11/D12 GPU tests require the preventive request,
its one-attempt bound, exact FIFO pixels and progress when a replacement stays
short. Exact V593 fails that new request assertion. The initial full suite
revealed a test synchronizing on cumulative native Begin count, which now
includes renewals. Synchronize on the begun input ordinal instead; the existing
no-native-margin, acquisition, pixel and timing assertions are unchanged.

F9-only 5735 records the packet ID, raw prediction QPC and requested handoff plus one period.
5733 still identifies hard expiry;5734 records the final native prediction and
renewal count. Do not infer a fixed offset from packet ordinal to native frame
ID across renewals; associate the actual Submit with its native End interval.

For the owner comparison: same Valhalla level, 0.3.0 2X, Lower VRAM and Neural off,
F9 enabled throughout about 30 seconds in the level, then close normally. No F8
or benchmark needed. External read-only observation can capture those same
receipts. Include the actual drops, not only the final seconds. Luke Ross is
in scope but requires a separate gameplay comparison. Keep the shared online
reference unchanged until acceptance.

[OpenXR xrBeginFrame contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html)
