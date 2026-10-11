# V464: exact FSR color/guide eye association

Base: sealed V463 `05385dee0eeec02a14f58b0e8f6e14490221a5fb`. Product0.464.0, IPC46401, private native provider ABI45601. The owner requests this targeted fix after the V460 MSFS two-run audit; V460 through V463 remain immutable.

## Reproduced defect and evidence limits

In the legacy one-swapchain-per-eye fallback, equally sized NGX streams with equal input offsets were assigned left/right by first-publication order. Right-first discovery therefore assigned right-eye guides to left-eye XR color. Owned WARP publications in the actual release/refresh layer fixture reproduce this exact wrong-eye baseline.

The supplied defective MSFS log began late. Different jitter values, independent serial counters and opposite depth-array assignment correlate with the two sessions but are not proof of their exact eye provenance. The fix addresses the known resolver defect, without asserting that the scalar log proves the entire reported visual failure. Good-run native GPU decisions confirm actual DLSS-G interpolation, not a green counter alone. Original audit/evidence: lab `work/shared/ofxr-autonomous/diagnostics/v460-msfs-fsr-two-runs`.

## Correction

Ordinary native-D3D12 FSR records the publication watermark at each actual XR color release and refreshes only the guide association after both releases. Color tickets are not recaptured at EndFrame. Exact chosen snapshots keep existing submitted producer fence, freshness, stream epoch/reset and same-eye serial continuity rules.

When both evaluations precede both releases, equal input offsets no longer authorize a first-discovered-left guess. FSR reuses an association verified by prior distinct releases within the same stream epoch, or uses distinct spatial input offsets. Unknown/ambiguous equal-offset startup remains in real-frame bypass until identity can be established; temporal_mismatch is the current diagnostic reason. Shared mono streams with indistinguishable eye publications are also refused in this case. The retained identity contains only scalar stream/epoch, not a held GPU snapshot. Eye/layout changes clear it; a reset epoch cannot reuse it. Route switches preserve an already verified identity.

## Boundaries

Only native-D3D12 FSR guide ingress/selection changes. FSR provider1.1.3, configure/prepare/dispatch code, shader math, camera matrices, jitter values, motion sign/scales, reset interpretation, GPU ownership and scheduler are not modified. All25 V463 shader bytecodes are identical. D11 receipt and Luke/AFW routes retain their existing association. Packed/array stereo remains on its existing resolver. OFXR, optional Hybrid and native DLSS-G retain the default resolver policy. R19 checkbox remains global/live/default OFF; native Vulkan vectors remain suspended.

## Owner-requested Hybrid menu simplification

Hybrid fallback is now fixed to FidelityFX optical flow both before guides arrive and when pairs are rejected. The shared desktop/F10 menu removes Fallback engine and its repeated NVIDIA quality/resolution/bidirectional controls. Existing V463 saved hybrid_backend values are normalized to0; the legacy key remains readable for profile compatibility but cannot select another engine. Initial runtime INI and live setting application both force FidelityFX for Hybrid independently of the saved OFXR engine. Hybrid has its own Lower VRAM profile as before; other routes retain their controls. Guided quarter-axis Hybrid composition and its shaders are unchanged.

## Regression

`xrfg_three_slot_director_tests fsr-release-identity` compiles the actual layer. It creates real WARP color histories, owned NGX guide copies and submitted fence receipts, publishes right first with distinguishable eye jitter, and calls the actual swapchain release/refresh. It proves the old wrong-eye assignment, ambiguous startup bypass, correct interleaved right-first association, subsequent both-before-release reuse, actual FSR/native/FSR setting switches, reset-epoch rejection and layout invalidation. Original release-bound resolver tests continue to cover shared streams, evicted/ambiguous pairs and cross-queue exact fence ownership. Automated validation is recorded separately in V464_VALIDATION.json / VALIDATION.md.

Live MSFS/Returnal headset confirmation remains pending. No installation, activation, game launch, registry mutation, push or publication is performed. This build does not resolve or claim to resolve the separate NMS residual hitch investigation.
