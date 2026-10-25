# V444 settings, guide diagnostics and native FG startup

Parent: V443 `6cf534a05aed76d0251228aa86796f5c5729b90f`.

## Lower VRAM

The three-way Output storage selector becomes one Lower VRAM checkbox in the
shared tray/headset widgets. Off is the source/profile/session/runtime-INI
default. Old -1 route-default profiles normalize to 0, while explicitly saved
1 remains enabled. Direct outputs are used unless the flag is on; there is no
backend-dependent automatic enable. No live owner profile is edited by preparation.

## Origin of OFXR game-motion interpolation

Full local Git history identifies `8a84fa3162d79bcfb1c46dede1d54574daaf880d`,
2026-09-15, "Integrate adaptive frame synthesis and runtime safeguards", as the
introduction of the DLSS guide ingress and `record_game_motion_pair`. This is
an ancestor of the selected native-dlss-fg base `135185a033453ae7f844b867c7ef34ae26d40ab3`.
V441 `3f2329a08827135210c618e6d4b399287d89f9d7` exposed Motion source in the
shared settings widgets. These facts establish source provenance, not owner
authorization or proof of the original implementation's in-game quality.

This is separate from FSR FG: valid SR/RR motion guides feed direct OFXR
interpolation and bypass OFA. If guides cannot be used, ordinary OFXR calls
its selected optical-flow backend. Existing real D3D12 tests feed opposite
per-eye translations, verify selection/use counters and shader readback, and
exercise NVIDIA work-list reuse. No accepted owner Planet Crafter result exists
for this alternate path. Optical flow remains the fresh-profile default.

## Owner V443 native DLSS-G crash

Owner session PID 22836, 2026-10-09 19:58-20:03 local, ran until generation
revision 19 selected native DLSS-G. Both swapchains initialized successfully;
the flight trace stops inside the first `synthesis_prime`. The Unity crash
handler wrote `Crash_2026-10-09_180310911/crash.dmp` (UTC timestamp).
Exception: read of address zero, `0xc0000005`, layer RVA `0xceb7a`.
The matching V443 Release map places it in `D3D12FrameSynthesizer::Impl::reset_work_slot`
at offset `0xba` from RVA `0xceac0`. No SR/RR evaluation/publication appears in
the captured NGX log. That establishes lack of observed guides, not a claim
that the game can never produce them.

Native mode deliberately does not allocate an OFA synthesis command list.
`reset_work_slot` nevertheless tested the selected NVIDIA backend and
dereferenced its absent allocator. It now tests the actual list. The native
GPU suite primes and retires/reuses five work slots with NVIDIA selected,
checks exact real stereo pixels, and refuses guide-less primes without an
anchor. A layer regression hot-switches eight times between native DLSS-G
and FSR without motion or depth, preserving all real eye/HUD frames and
runtime Wait/Begin/End ownership. It then verifies OFXR optical-flow recovery
and visible fallback. The existing capture-busy and FSR stereo tests remain.

## Runtime diagnostics

The generation tab always shows passive typed native DLSS SR/RR input
observations, motion/depth presence, recency, and published/XR-matched/used guide
counters. Optical flow does not enable GPU guide copies for this observation.
Only creation-observed or exact typed SR/RR calls enter it; DLSS-G/NR calls
cannot masquerade as an upscaler. D12, D11 and Vulkan interception report these
inputs. FSR/XeSS source-upscaler adapters remain absent and are named in the UI.

Actual generation status distinguishes optical-flow fallback from real-frame
bypass, missing motion from missing/unknown depth, mono/shared stereo inputs,
invalid frame/device/camera identity, discontinuous history, output/queue
mismatch, provider/runtime availability, provider evaluation failure,
fresh-history priming and GPU backpressure. Applied settings alone never label
the route as generating. These observations and counters do not certify a
game's visual quality or promise interception of calls made before injection.

Owner game/headset retry remains pending. Preparation does not activate a tray,
register a layer, install a game DLL, publish or push this candidate.
