# V621: OFXR OFF bypasses added pacing

Base V620, source4a1462d80b8853577c93362f4ff43494d312fc7c.
Owner observes AFW-only performance/timing changing with OFXR pacing selection.
The selected mode is a preference, not permission to pace while OFXR is OFF.

V620's private transport policy checked only pacing_mode!=Framework. Its
single-director selector similarly ignored applied enable state. Thus disabling
generation left final director cadence/adaptive hold active. Frozen V620 runs:
Horizon PID40760/run27898734616376 and Indiana PID35472/run27938300922512.
Indiana's detailed OFF+AFW interval contains1209 director slot observations;
Horizon retains OFF configurations for modes0/2, but no usable detailed OFF
transport observations in that log window. Do not infer absent samples.

V621 gates the startup transport policy, live pacing-only policy and effective
sole-director selector on applied OFXR enable state. Full control transactions
set final presentation policy after their generation result is known, so a
failed or disabled configuration uses bypass. The selected dropdown preference
is retained and restored when OFXR is enabled again. Every OFF selection uses
the existing ordered native-runtime-paced bypass; no final director deadline,
adaptive hold, physical slot floor, clock-window renewal or added end advance.
This applies to the common private SteamVR endpoint across bindings/titles,
including AFW-only. VDXR has no private endpoint and retains its existing path.

Keep the existing session, binding, private image handles, FIFO ownership and
required GPU readiness/copies and native Wait/Begin/End calls. This is not a
zero-copy or zero-cost claim. Buffer-full backpressure and actual native runtime
blocking remain safety/ownership bounds. No new wait or swapchain is added.
AFW generation, motion/depth, quality and benchmark layout are unchanged.

The actual session/control/transport fixture starts OFF with combined preference,
cycles all stored modes with AFW selected, checks effective bypass, exact FIFO
GPU pixel/pose identity and absence of an artificial refresh floor on a mock
unthrottled runtime, then tests repeated ON/OFF restoration. AFW temporal anchor
survives pacing-only and enable changes; native binding/allocation count stays
constant. The fake native compositor cannot validate headset smoothness/FPS.
Existing D11/D12 bypass/drain tests and enabled route hot-pacing tests cover
reference/new/Djules/Fidelity/Hybrid/FSR/DLSS-G behavior.12 checks pass.

Owner check: restart with matching V621 tray/game. OFXR OFF, AFW ON, same point:
cycle Framework/Director/Framework+Director. AFW-only should now ignore that
selection. Then turn OFXR ON to verify the chosen policy resumes. No full
benchmark or F8 is required initially. Candidate stays local/runtime-pending.
