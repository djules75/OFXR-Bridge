# V586: deadline priority for final SteamVR delivery

Base sealed V585. Owner reports optical routes cannot hold their maximum FPS
in MSFS2024 despite approximately80% GPU usage. This is a local candidate;
actual headset recovery remains pending.

## Evidence and hypothesis

Compact V585 optical medians reach90fps. The retained detailed ring covers
only the final0.3.0 interval, not every route. Its prepared packets normally
have about22ms lead, with strict11.1132ms minimum handoff spacing. Some compound
preparation intervals spike to14-21ms and one to64.38ms; a native End also
spikes to14.03ms. There is no systematic CPU fine-wait oversleep in that tail.
Increasing the100us metadata lead alone cannot explain/fix those large stalls.

Code inspection finds synthesis explicitly uses a HIGH-priority queue while
the final SteamVR binding inherits the usuallyNORMAL optional binding queue.
Logical queue independence therefore does not imply equal GPU scheduling
priority. This asymmetry is corrected here. The observed compound interval
includes image acquisition/wait, recording, GPU completion observation and
release; the retained log does not identify their individual costs or prove
that GPU scheduling starvation alone caused the drops.

## Change and boundaries

For the common SteamVR D3D12 final queue, query DIRECT/HIGH support and request
HIGH when supported and above the inherited priority. A rejected HIGH request
retries the original descriptor on a new dedicated queue. Type, flags, node
mask and any already-higher inherited priority are preserved. A failure of
both attempts propagates the existing initialization error. Producer and
synthesis priorities are unchanged; no GLOBAL_REALTIME promotion is requested.

This affects native D3D12 and existing D3D11/Vulkan translations ending at the
same D3D12 binding. Native D3D11 context delivery and VDXR priority are unchanged.
V584's independent preparer, the three-credit pool, V585 automatic source
refill/native-cycle budget, physical cadence and100us lead remain unchanged.
No additional GPU image, XR swapchain, buffer credit or per-frame wait is added.

References: Microsoft documents the [priority levels](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_command_queue_priority)
and [per-type support query](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_command_queue_priority).
HIGH is a scheduling request, not proof that all workloads will meet a deadline.

## Validation and next comparison

33 focused checks pass. New cases verify priority support/rejection, original
dedicated fallback, descriptor preservation, inherited higher priority and
failure propagation. The shipping session fixture verifies the actual final
binding priority when the driver can createHIGH. GPU ownership, all routes,
strict cadence, independent preparation, backpressure and stop/restart remain
covered. Tests use actual GPU resources with mocked downstream OpenXR, not
the MSFS workload. See V586_VALIDATION.json for results and limits.

With F9 enabled, stages5722/5723/5724 separate target acquire/wait, copy submission
and observed copy-fence completion;5715 remains after real release. These CPU
timestamps are not GPU execution-duration measurements. No optional GPU query
resources are added, and all new substage sampling is bypassed with F9 off.

Restart game and tray, same MSFS location/refresh/settings. About20-30seconds
on0.3.0 with F9 is sufficient initially; no F8 or full benchmark is needed.
Compare delivered FPS and preparation tails. If the tail persists, use its
new substages instead of assuming another priority or timer change is needed.
No automatic publication or live installation accompanies this candidate.
