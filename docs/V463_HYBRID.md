# V463 - selectable Hybrid route

Owner scope: start from sealed V462 caa1eeda61d04d951b7b76e9f8e7b3e31e06f8e4. Add Hybrid after OFXR and before FSR in both desktop and F10; retain existing route behavior. R19 camera remains global, live and default OFF.

Hybrid keeps persisted mode ID 3 (OFXR=0, FSR=1, DLSS-G=2 unchanged), independent [route.hybrid] profile and fallback engine selection. Before native motion publications it uses that optical-flow engine. The first publication triggers safe existing generator reconfiguration even without a new settings revision. Valid exact stereo pairs combine game motion with FidelityFX flow, selecting per pixel by consistency between source frames. Hybrid flow inputs use 25 percent per axis. Missing/rejected pairs use optical flow and report the reason. Vulkan native guide ingress remains suspended, so Vulkan uses the selected fallback; this build does not restore Vulkan vectors.

Selective upstream import audit:
- 022b5290d62c102c8745f72773bddad63839f55f: Hybrid shader, guided FidelityFX pair and resource ownership.
- a21dea708278911c23d80e6baa126c5313662383: Hybrid documentation intent only; no extrapolation modes.
- 1d89ef3d44503832294b909758f7e7df86a45ffb / f28f78f99a3662dcc73a74583866969e65a0fbc1: tray selection/storage adapted to Autonomous IPC, shared UI and independent profiles.
- c7a2c4f2bef9dad8f8028db8b0fe7bc3790715ef: quarter input and filtered downsample, restricted to selected Hybrid. A separate shader retains all ordinary-route shader bytes.
- aa7eb119d853b3f26763a0956814266355c72d26: live selection intent implemented through existing IPC, not new INI polling.
- 58efca9762ad74ba4ef28f4cdc24ed973cd22a77: first-guide activation and correction to Hybrid fallback statistics. Its global Hybrid default is intentionally excluded because the owner requested an optional new route.
- R19 camera integration inherited from V462, including Hybrid's per-pixel camera fallback.

No scheduler, flow-cap, mesh or extrapolation changes. Existing provider ABI45601, FSR provider1.1.3, AFW same-eye/depth/camera ownership and GPU fences retained. Product0.463.0 and IPC46301 isolate the changed settings layout.

Validation includes both full existing suites, actual shared-UI selection and profile round-trips, unchanged existing shader hashes, real-GPU Hybrid composition and R19 target-camera output. The small 256x128 scene measures quarter Hybrid edge MAE11.30/7.66 versus same-pixel blend39.35/39.43; static-overlay MAE1.06/1.07; brightness-fade MAE7.96/7.84 versus blend45.76/45.81. These are test scenes, not predicted game quality. R6's original Hybrid threshold5 is not met by the integrated R15/R9 composition in this tiny scene; retained as an explicit limitation, not claimed as upstream pixel equivalence. Vectors-only thresholds are preserved. User headset/game comparison remains pending.

Initial full new run failed delayed-promotion split and foreground-sensitive input reopening. Both unchanged fixtures passed focused follow-up runs; sealed V462 also reproduced the promotion failure in the same environment. No promotion/scheduler/input production code was changed. Initial failures and reruns remain in package evidence.
