# V462 ? opt-in R19 synthetic camera

Owner authorization: on-the-fly R19 checkbox for every generation route; V461 is the base. Default OFF. No Hybrid in this candidate.

Ported camera changes: ec54383c26879a15f12896df3be51700b2f6cb05 (pose math), c175b1e348d2e978002820161693b2972102e4eb (composition), a76bbcb70bfc5efe4f380ce3ed42d7bb76877a60 (native composition), 767fb00105c427986e90b26b26e89320b9b1af40 (OpenXR pose pairing). Live control intent of 3632f7f89e2804c28d61e110fde3bc5ab5df40b2 is implemented through existing Autonomous IPC instead of INI polling.

The shared desktop/F10 checkbox ?R19 synthetic camera (experimental)? persists in the autonomous profile and applies at the next pair. An applied atomic CPU policy is copied into queued camera snapshots. The toggle does not reconfigure the generator, drain queued frames, or alter their already selected poses. Each synthetic receives its own interpolation fraction (including 3X outputs). Recenter over 45 degrees or translation over 0.5 metres keeps the complete stereo pair in B's camera. Native skipped/copy output retains B metadata; GPU confirmed generated-content accounting remains independent of camera selection.

OFXR optical-flow and game-vector composition rotate the output ray into B and use A at a newly revealed edge where available. Native DLSS-G applies the equivalent change to its final composition. FSR SDK inputs, preparation and provider are untouched: only its existing final reprojection destination changes when ON; the cinema final quad rotates its corners to the selected camera. OFF retains B-camera validation and the existing path. Provider ABI45601 is unchanged; product IPC46201 isolates the new control layout.

No R19 scheduler, flow limit, quarantine, Agility SDK, extrapolation or mesh imports. V457 Vulkan guide ingress suspension is retained; no Vulkan game vectors are restored. Existing FSR provider1.1.3/AFW ownership, same-eye history, fences and release-bound guide rules remain. The R19 upstream camera warp is rotational; metadata position follows interpolation as upstream, without new depth-based translation compensation.

Validation: full existing suites plus actual GPU target-camera ray-direction comparisons for FidelityFX/NVIDIA/game vectors; B real copies remain bit-exact. Actual-layer fixtures test camera metadata coupling, native skip fallback, immutable queued snapshots, teleport/recenter stereo rejection. Live checkbox persistence and concurrent settings merge are tested. Automated validation does not establish visual quality in the user's game/headset.

Additional all-view GPU R19 fixture: flow/vectors/NVIDIA 2X and 3X, native2X at100/67 percent, all pass with zero black scene pixels. Native3X is hardware-gated and unavailable on this adapter. The captured NVIDIA SDK banner is confirmed by registry value1024 plus actual monochrome text and excluded only in its scaled upper band. Visual evidence is retained.

FSR reset output is a copy of B (confirmed by exact RGB readback). For R19 ON only, its observed SDK reset receipt keeps B camera/pixels; ordinary generated outputs use their interpolated target. The wrapper records the existing reset value, without changing any FSR configure/prepare/dispatch inputs. OFF FSR composition is unchanged. FSR steady-scene quality remains game/headset validation.
