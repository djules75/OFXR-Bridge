# V483: Luke AFW stereo continuity and signed native projection

Parent V482 5288c88c6cd18b4259a1ef54ece837e7491ba79e. V474 fallback; V475/V476 excluded. No live installation or publication.

## Horizon evidence and correction

Owner explicitly reports flicker while stationary, resembling the other eye or a shifted image. Do not attribute this report to headset rotation. PID25572 has two complete previous-second captures. All 244 final AFW eye samples are present, and runtime output poses exactly match the AFW camera poses. Diagnostic GPU drops affect original-image samples, not missing AFW output samples. There are also25-40ms submission gaps; capture overhead means those are not an uninstrumented performance baseline.

Seven transitions skip one NGX publication and repeat the same accepted source eye:783->785,802->804,813->815 and1251->1253,1266->1268,1277->1279,1299->1301. Image feature correspondence shows stereo disparity collapsing at those transitions, recovering on the next opposite eye. The native guide parity stays correct. This is not a global left/right swap.

The real bundled PDAFWPlugin and the read-only game-local Horizon provider both reproduce the repeated-eye error in a512x512 static GPU fixture: combined mode moves the reconstructed peer's stripe to the source-eye position (26pixels); the corrected transition preserves the opposite-eye position and the next alternating evaluation resumes normal combined mode. Both clear-before-warping OFF (owner's setting) and ON pass.

For requested previous/combined AFW, use current-source/depth AlternateEyeWarping for bootstrap, missing-middle-publication or repeated-eye evaluations. Resume the requested mode on the next contiguous alternating publication. Desired settings are preserved; no added output, repetition, hidden frame or game-frame drop. Update continuity only after successful GPU submission. Stage952 records the actual effective mode and previous accepted publication. Existing AFW Alternate mode is unchanged. This reproduces and corrects one captured defect; headset confirmation and any remaining flicker remain pending.

## Ghost of Tsushima / Crimson Desert

Preserved V482 PIDs27152/38668: native input and color-token resolutions do exist, but AFW rejects native camera history with reason3 thousands of times. With AFW selected, its rejection also bypasses the downstream OFXR consumer. This is not proof that every Luke input is absent. The guard previously accepted only perspective W=-1. Native camera reprojection now accepts exact signed W=-1 or+1 and converts physical forward into the matching view Z independently for each camera. Invalid/zero W, nonfinite camera, reset, jittered vectors and nonconsecutive identities remain rejected.88 GPU shader scenarios cover both handedness contracts, projection offsets, object/camera movement and invalid inputs; existing RH behavior is retained. The two game logs do not record reason3's individual scalar, so actual game recovery is pending rather than claimed. Stage953 adds bounded once-per-second scalar failure flags/W/token evidence if rejection persists, with no GPU readback.

## Capture and preservation

Horizon rolling capture reserves its four image chains for original logical left/right and final AFW left/right. Exclude auxiliary color chains from ring image sampling; legacy paired capture is unchanged. Ctrl+F8 arm/stop and repeatable F8 previous-second saves remain. The buffer is optional and diagnostic.

FSR/Hybrid/DLSS-G providers, OFA, all27 generated shaders, pacing, counters, startup passthrough and Vulkan guide suspension are unchanged. Seven ancillary runtime components are byte-identical to V482. No opt-in flag or extra GPU resource/copy is added to the normal frame path.

Optimized Release focused30/30 checks passed in11.71s, no skips. Real provider repeated-eye regression plus retained Luke GPU/depth/history/context/clock/startup/ABI/loader checks. Full other-route suites are intentionally not repeated. Owner game/headset checks pending.
