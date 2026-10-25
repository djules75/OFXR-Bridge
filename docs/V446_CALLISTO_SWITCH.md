# V446: resume generation after a paused output-shape change

The owner reported that The Callisto Protocol initially generated with V445,
then stayed in bypass after changing options. This supersedes the brief report
that everything worked after checking Enable frame generation.

## Exact evidence

The V445 PID 12384 flight trace starts generation at 14.080 s, disables it at
18.742 s, changes the output route while disabled at 54.532 s, and enables it
again at 55.726 s. Configuration application succeeds, but preparation reports
missing_generation (reason 4) from 55.727 s until the end of the trace at 131.708 s.
The saved final preference is enabled=1. UEVR's log identifies Callisto.

## Cause and replacement

apply_embedded_control drains and destroys already-admitted private output
swapchains when their layout changes, including direct djules extra-output
capacity and Lower VRAM staging. While generation is disabled, it correctly
does not allocate replacements. However, V445 left generation_eligible_pending
false after removing the old outputs. A later enable with the same route tuple
therefore could not create outputs, while reporting successful configuration.

V446 sets generation_eligible_pending after that owned shape retirement. On
resume, the existing creation path can allocate the current layout. Provider
refusal, session budget, source capture and producer-fence gates remain in
force. A successful allocation clears pending admission as before. Source
history and release captures stay owned, and configuration application clears
the previous projection so fresh input primes before a generated pair.

This is a local control-lifecycle fix on V445, without further upstream ports.
Shader quality, native guide rules, AFW correction, defaults and scheduler
behavior remain those of the V445 base. UI/runtime identities are numbered V446.

## Regression

xrfg_paused_route_shapes_gpu uses the actual D3D12 layer and GPU providers with
only the XR runtime mocked. For Lower VRAM off and on, it pauses, switches
reference/djules/new routes, and resumes repeatedly. It checks admission,
output layout, retained history/captures and retired temporal anchors; then the
actual release/EndFrame/director path must deliver eight real and seven
synthetic frames with pixel, XR-cycle, resource and D3D12 debug accounting.
The test fails before the fix with "paused output replacement lost generation
admission". Final execution and complete suite results are in VALIDATION.md.

Owner Callisto validation of the exact V446 package remains pending. No game
installation, runtime activation or global registration was performed.
