# V521: SteamVR output budget recovery on V520

Owner requests a local fix on the published V520 base using the existing
Valhalla/Meganex SteamVR run. The supplied run actually loads sealed V603:
logger603 and cached DLL SHA256 match the original V603. The affected
budget/creation/control functions are byte-identical to V520. Logging starts
at174.505s, but retained critical records preserve three earlier private
swapchain refusals: two current rings at120.414/120.418s and a synthetic ring
at134.320s, all XR_ERROR_LIMIT_REACHED. The recorded63.872s contains2394
missing-generation decisions,439 cooldown decisions,2393 primes and zero pairs.
No exact physical GPU-memory ceiling or switch timestamp is inferred.

The control path creates deferred outputs for every eligible enumerated target,
including targets absent from the current stereo projection. Replacing one
admitted output shape while other old shapes remain can therefore compete
with those unused outputs. A private-ring refusal remains latched; subsequent
route revisions cannot rearm it.

For native D3D12 OFXR on SteamVR, control revisions now leave pending output
allocation to the existing validated projection path. All changed admitted
shapes retire before that path creates the current projected eyes. Source
histories and this frame's captured images survive. An explicit changed route
or Off-to-On revision reopens a valid refused source once. An unchanged
revision cannot retry each frame; another genuine refusal retains the existing
real-frame/session budget gate. Depth/static/protected/invalid sources stay
ineligible. No format, resolution reduction or alternative interpolation is
introduced. AFW and the special Outlaws initialization path are excluded from
this change; their validated guide/color/camera contracts remain.

The new independent runtime-ceiling fixture uses the actual native GPU
history/synthesis and control code with two active eyes plus two unused
enumerated targets. Original V520 fails because the control change allocates
unsubmitted targets. Fixed code switches Reference/Djules/New under the same
ceiling, preserves source ownership, recovers from a real XR limit refusal
after an explicit route or enable change, and never retries an unchanged
revision. The unchanged pixel oracle then verifies eight real/seven generated
stereo submissions and static/dynamic quads, with no GPU debug errors.

V521 is local/runtime-pending. The actual Valhalla/Meganex game verdict is
still required. Original V603 remains accepted for KCD and original V701
for Indiana; their acceptance is not a combined V521 verdict. No HUD work,
live install, tray activation, game INI change or remote publication.

Final verification: optimized VS2022 x64 Release builds; 15/15 targeted checks pass in21.45s, zero skips. Covers the bounded native SteamVR GPU recovery (both per-eye synthetic-ring and session-wide refusals), route/paused shapes, native provider recovery, Reference and VDXR queues, both presenter handoffs, D11/native guide compatibility, Outlaws startup, both Vulkan bootstraps and bundled KCD/Indiana quality. The first paused-shape timing attempt failed, then the isolated check and final suite passed without production changes. An expanded harness setup initially requested the same provider while expecting a new revision; corrected to an actual route change. Original V520 negative control still proves unsubmitted outputs were allocated. No hardware game/FPS verdict is inferred.
