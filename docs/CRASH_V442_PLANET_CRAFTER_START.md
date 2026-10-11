# V442 Planet Crafter startup failure and V443 correction

Owner report, 2026-10-09: V442 closes Planet Crafter immediately after VR
attachment, before F10. Two fresh sessions at 19:28 and 19:29 Europe/Rome
show the same causal sequence in OFXR's flight log. The latest Unity Player.log
identifies V442, D3D12, VirtualDesktopXR 1.0.10 and RTX 4090. This game uses its
Unity OpenXR VR mod; the report does not establish UEVR injection.

## Observed failure

The 19:29 flight log records history captures returning 0x800700AA
(HRESULT_FROM_WIN32(ERROR_BUSY), signed -2147024726) for both eye resources.
The device-status records report S_OK. Source release succeeds but clears the
corresponding last_released_capture because no new history ticket was committed.
The next stereo frame is nevertheless handed to the synthesis worker. Preparation
returns missing_capture (reason 5), and the worker calls
fail_pending_presenter_submissions_locked with XR_ERROR_RUNTIME_FAILURE.
The following application xrWaitFrame returns -2 while the director's own
downstream xrWaitFrame still succeeds. Unity catches this error and shuts down
the XR experience. There is no new native crash dump for these sessions.

The 19:28 session shows the same failure after one eye's capture is refused.
This is distinct from V441's ImGui ItemAdd access violation on menu reopening.
The source permits nonblocking capture to return ERROR_BUSY when its three-slot
history ring or allocator is still owned by GPU work or a consumer lease.
The failure was treating the resulting missing ticket as a fatal worker error.
The logs do not identify which individual slot fence or lease caused backpressure;
V442 does not change the release-time history implementation from V441.

## Physical correction

Before the ordinary three-slot handoff, V443 checks all mapped projection
resources for committed release captures. If any eye lacks a ticket, the entire
frame uses the existing borrowed real-frame path. It retires the temporal anchor,
drains pending owned outputs, submits through the current physical frame-call
owner and keeps that same generation presenter active. The next complete stereo
capture primes both eyes; only the following compatible frame can synthesize.

Release capture remains nonblocking. No stale capture is substituted, no eye is
generated independently, no busy slot is overwritten, and genuine GPU/worker
failures retain their existing error handling. AFW's completed private output
and depth-aware Normal correction are unchanged.

## Regression

The real D3D12 fixture holds an actual committed history consumer lease on the
next capture slot. Frame 4 refuses only the left capture; frame 6 refuses both.
It executes production release, EndFrame, worker and director code, with a mocked
OpenXR runtime and the real GPU. The original handoff fails the regression;
V443 preserves all eight real frames in order and emits exactly three synthetic
frames (2, 3 and 8), with a fresh prime after each refusal. Pixel checks cover
both eyes and static/dynamic auxiliary quads; D3D12 errors remain forbidden.

Preserved owner logs are in the lab's shared diagnostics directory
`diagnostics/v443-planet-crafter-start/`. Final suite results are in VALIDATION.md.
The owner still needs to test V443 startup and the changed-options F10 reopening
sequence in Planet Crafter; this fixture is not a game/headset validation.
