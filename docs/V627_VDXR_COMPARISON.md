# V627: VDXR Legacy versus common private director

Base V626: 6cfc5f85624055a3f438e44e2a05b5ce1087ac37. Local candidate.

## Session selection

The tray-only Legacy VDXR checkbox persists ofxr.vdxr_legacy in the existing
ofxr-bridge.ini and in the armed runtime's generated startup configuration.
It defaults ON. A changed checkbox applies to the next game session; it never
replaces an active graphics binding. No new settings file or migration is added.

Legacy ON retains the original VDXR native session, runtime queue, provider
scheduling and memory controls. Director choices remain unavailable there.
Legacy OFF installs the existing private endpoint on VDXR and its dedicated
native delivery queue. The Framework choice uses ordered runtime-paced bypass;
Director bypasses the eligible producer pacing; Framework+director retains both.
Those three choices change live on the same private session after a drain.
SteamVR continues using its existing endpoint regardless of this VDXR flag.
Other runtimes remain unchanged. AFW only overrides either VDXR choice and
remains a native AFW terminal path, with Neural Rendering independent.

The opt-in path shares producer-publication ownership, native-reference overlap,
FSR scheduling, Fast 3X capacity, post-AFW snapshots and sole-director gates.
Native capture ordering changes apply only to opt-in VDXR or existing SteamVR,
never Legacy VDXR or Luke guide identity. AFW depth/camera subtraction, image
quality, capture contracts and V626 marker recovery remain unchanged. This is
not a fix for the separate high-VRAM investigation.

## Native clock and measurement

VDXR remains identified as VirtualDesktopXR, never SteamVR. Its real OpenXR
predictions supply the private consumer period/phase when no OpenVR physical
clock exists. No synthetic SteamVR compositor statistics or fixed 90Hz clock
are substituted. The new GPU fixtures use 120Hz predictions and preserve FIFO
pixel/pose identity, including actual startup and live policy changes.

VDXR output telemetry records successful nonempty native EndFrame completions.
The private mode counts at the real consumer, not at producer enqueue. The
legacy direct path counts the completed direct End. This has the explicit
runtime_accepted_delivery source and is not evidence of physical headset
scanout. Luke's distinct source/AFW identity counter remains authoritative.
No GPU timing poll or flight logging is enabled by the comparison itself.

## Benchmarks and owner spreadsheet

Display keeps the existing three suite buttons, 10 seconds per trial, leading
3 seconds discarded, remaining samples retained; VRAM is sampled near the end.
No preset override is introduced. Runtime/LegacyVDXR columns identify sessions
in the detailed CSV. IPC62701 requires matching tray/game; provider45601 remains.

Legacy VDXR uses the sheet's actual G:AC result block; C:F are manual indicators
and game-only references. For non-Luke tests, Fidelity, 0.2.1, Djules 2X and
0.3.0 each use Fast once; Djules 3X and each available vector engine compare
Normal/Fast. This yields 7 optical or 13 complete trials. Luke runs AFW alone
plus four OFXR engines for each of 1x1/2x2/4x4: 15 trials, no 3X or vectors.

Opt-in VDXR uses the same 31 optical / 49 complete / 39 Luke matrix as SteamVR.
The new Quest result block is AD:CN. The owner adapter validates headers and
writes 63 cells for this block or 23 Legacy cells, preserving blanks and n.a.
Unsupported/untried routes remain n.a., not zero. SteamVR's block is discovered
from its marker because adding Legacy columns shifted its start to CS.
The personal sheet adapter remains local and is not bundled with the DLL.

## Verification and first comparison

The shipping GPU-layer fixtures cover original VDXR startup versus private
startup, unchanged runtime identity, live three-policy switches, all seven
native generator families, 3X, sole-director paths and four Luke paths at120Hz.
Existing SteamVR, Outlaws, AFW-only, UI, launcher and benchmark checks remain.
Their native runtime is mocked; Quest smoothness, performance, latency and
resource-budget acceptance require the owner's hardware comparison.

Close the prior tray/game and load matching V627. Keep AFW only OFF for OFXR
matrix tests. Start VDXR with Legacy ON and run the relevant suite. Close the
game, uncheck Legacy VDXR and relaunch for the new suite. Preserve scene,
resolution, refresh, Neural and DLSS settings. Do not mix the two CSV sessions
or infer a useful default until that comparison is complete. No live install,
active owner configuration change, push or release accompanies preparation.
