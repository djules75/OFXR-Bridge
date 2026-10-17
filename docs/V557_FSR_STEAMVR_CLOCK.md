# V557 ? FSR native SteamVR presentation clock

Development scope of the owner-authorized V557 shared endpoint. See V557_RELEASE.md for the current publication status. Based on local V556bf80531c2ac1669cd87294d10736055828aa7add.
The owner reports that V556 looks much better but FSR remains poor. Frozen owner
run and measured review:private diagnostics/v556-owner-delivery. Settled final
FSR00 output55.06 versus82.42 accepted submissions/s; FSR A56.97/84.74. The
counted-release556 probe IS active in FSR. No claim that simply enabling it
again would solve this. Djules00 about89/90 and NewA89.31/89.77. A short New00
window remains54.11/89.92,so do not call every V556 configuration resolved.
Detailed GPU probes exist only in the late FSR portion. Pending S afterEnd is
not sufficient to identify a lost frame; exact source-to-compositor mapping
remains unavailable.

## One scheduling change

FSR's existing TwoOutputSchedule has no measured SteamVR phase acquisition and
reanchors a slow End to its return plus a full period. Its native D12,two-ring,
SteamVR consumer now uses the existing DjulesAbsoluteSchedule and bounded
DjulesPhaseAcquisition (3ms target band,real-output samples only). Physical HMD
period adoption now admits that FSR scope too. An unavailable compositor sample
leaves phase step0; no renderer failure follows. The absolute grid handles loop
cost/long End without changing raw OpenXR prediction timestamps.

This is the same clock policy already used on Djules,not V452's non-SteamVR
producer-phase controller and not a restored V555 distributor. It changes FSR
consumer CPU pacing/phase; no new GPU completion wait or producer release rule.
All FSR guide pairing,depth/motion math,FFX contexts,generation,copies (including
B deferred after S),handover/readiness/ownership/ring sizes remain V556. No new
XR handles,textures,queues,controller settings or hidden flags. Existing finite
backpressure remains. Do not claim improved game FPS or reduced latency from
synthetic tests.

Scope:FSR,native D12,SteamVR,Boff,A either. Bsingle-ring,VDXR,D11/Vulkan,Luke
and all non-FSR routes retain V556 scheduling. The dedicated FSR selector never
changes djules_native_scheduler/backend identity,so cannot accidentally enable
Djules deep/3X or different upscaler contracts.

5570 presenter_clock_anchor:old/new physical period.
5571 presenter_clock_anchor:real-output sample,error from3ms band,phase step.
5572 presenter_clock_anchor:output sequence,CPU grid slot ns,HMD period ns.
Existing5490-5493 readiness probes,556 admission and measured Output remain.
No output FPS cap or fallback to submitted frames is added.

## Validation and first owner trial

New scope/physical-period check covers missing/invalid samples and excluded
VDXR/bridges/Luke/one-ring/nonFSR. Two actual-GPU FSR fixtures,00 andA,verify
correct eye/HUD pixels,FIFO/full dispatch counts,unchanged raw XR slot spacing,
physical call order and actual consumer5572 activation. Existing clock policy,
GPU-progress/A-B/publication,FSR recovery/refusal,route-switch,meter,Luke and
VDXR checks are retained. These do not reproduce SteamVR's exact compositor
and cannot establish a headset gain.

First trial only FSR:MSFS carrier,same scene/keyboard strafe/head still,B/NRoff,
F9 on,about15s Aoff then15s Aon. Compare actual measured Output FPS and doubling.
No F8 or whole benchmark sweep. The owner now authorizes publication; broader retesting remains planned.
