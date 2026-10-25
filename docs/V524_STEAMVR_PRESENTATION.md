# V524: SteamVR presentation accounting and native D3D12 recovery

The owner reports Valhalla on Meganex/SteamVR generating about 38 real plus
38 synthetic submissions per second, with a smaller Output FPS number and
less fluid movement than plain AER. V523 is not accepted as resolving cadence.

Two accounting errors invalidate the earlier numerical loss estimate. The
flight diagnostic requested only 16 compositor records every 250 ms, missing
records at 90 Hz. It now requests 64. The overlay also deducted variable
`m_nNumMisPresented` from records with positive `m_nNumFramePresents`.
OpenVR defines mispresentation as presentation on a different predicted vsync,
so that deduction incorrectly classified shown frames as lost. A rolling
64-record estimator now counts presented records once using compositor time,
ignores pending zero-present tails, and excludes settled zero-present records.
Repeated scanouts count once; index gaps do not invent extra frames.
`steamvr_delivery` field b now reports an informational mispresented-record
count rather than a purported lost-share estimate. Game/Generated counters
remain successful submissions. Compositor records do not identify scene images;
correcting this measure is not proof of a visual improvement.

The previous run also has a concrete New 0.3.0 second-eye XR_LIMIT refusal.
Ordinary native D3D12 SteamVR OFXR New admission may retry once using the
existing owned output textures and two runtime handles for the refused eye.
An already admitted direct eye remains direct. A genuine refusal still latches
the existing bypass, with no per-frame reallocation. AFW, Outlaws, Hybrid,
native DLSS-G, FSR, D3D11 and Vulkan are excluded.

For Reference 0.2.1 only, the queued SteamVR consumer retains the exact
synthesis fence/value for each owned S/B output and waits before xrWaitFrame.
S waits for its synthetic completion, B for its whole-pair completion. This
moves unfinished synthesis outside the runtime frame deadline without waiting
on the producer or on a synthesizer's later submission. Runtime queue ordering,
image leases, midpoint interpolation and the V523 stalled-exposure protection
remain. Actual in-game presentation benefit is pending, not established by
the GPU fixture.

Validation: V523 production fails both new independent GPU fixtures: six-handle
budget recovery and entering XR with a delayed exact Reference output.
V524 passes them, with eight real/seven generated stereo pixel outputs and
poisoned auxiliary-image ownership. The readiness fixture also verifies the
producer returns while GPU work remains blocked. Accounting tests cover all
shown frames with variable mispresentation, 76/90 cadence, 45 FPS repeated
scanouts, settled omissions, overlapping polls and unsettled tails. The focused
Release regression suite and packaged loader results are bundled as evidence.

Owner test: fresh V524 tray/game, identical Valhalla resolution and graphics.
AFW/Neural off. Compare New, Reference and Djules in the same location, checking
fluidity as well as Game/Generated/Output FPS. F9 from before the comparison
provides the complete compositor window; no image capture is needed.
The candidate stays local pending headset acceptance. The HUD investigation
remains paused and the accepted KCD603/Indiana701 contracts are retained.
