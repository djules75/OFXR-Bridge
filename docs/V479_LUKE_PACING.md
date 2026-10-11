# V479: Horizon fresh-content pacing

Parent V478 54a9ffd9324d263faa0ddd116258cb0dff4c0aa3; V474 fallback retained. No V475/V476 import. IPC47101/provider45601 unchanged.

Owner reports Luke/game 40 FPS versus OFXR104 with AFW/OFXR both Off, plus Horizon-only persistent 60 ->40 FPS after mode trials. The V478 PID34504 run and final INI are preserved under work/shared/ofxr-autonomous/diagnostics/v478-horizon-pid34504. The original reader accepts Crimson only, so HFW cannot suppress original-source duplicates. Luke detection also keeps warm capture paths running with both generators Off.

## Changes

HFW 2609.18.2.5162 only: pin PE header, selection instruction bytes, HFW vtable and stable double-read of selected original AER source. Release latches source even without a history capture. Repeated or unavailable source skips private history/composition capture and synthesis gates while preserving Acquire/Wait/Release ownership. At End, one new source is admitted across all four AFW/OFXR combinations; repeated sources consume only their virtual app ticket. Source identity survives menu switches. AFW held output pairs are not resubmitted. No retained real/generated output is presented periodically.

App polling is virtual after the first runtime prediction, at most one poll per display period. Physical Wait/Begin/End opens lazily only for a fresh output, adopts already-open internal cycles, and assigns only the display slot: pose/FOV/space/rectangle remain attached to their original image. No new presenter thread. HFW cannot promote into a repeat presenter. FPS is capped by accepted unique outputs rather than runtime reuse of the last image. The runtime may hold/reproject the last accepted image between new productions; OFXR cannot prevent headset refreshes at a higher rate than the game.

Both Off: stop new private history/composition and NGX snapshots. The D11 native DLSS ingress bypasses its motion/depth/color/output interop transport before GPU copies, while still calling original game DLSS and recording input availability. NR remains an independent consumer. Retained native slots are not unsafely freed because recorded game command lists can still reference them. Re-enable retains the normal epoch reset and drained ownership.

Other titles retain existing pacing. All AFW camera/game fixes, FSR/Hybrid/DLSS-G shader/provider math and Vulkan guide suspension remain. No live game install, registry/tray activation or push.

## Verification and owner test

Optimized x64 Release;26/26 focused checks passed,8.92s, zero skips. Actual layer Wait/Begin/Release/End fixture verifies duplicates in all four states, release ownership, warm Off path, unknown-source skip, unique next-source delivery, source metadata and adoption of an already-open internal cycle. Pinned-reader signature/race/fault tests and installed PE layout receipt pass. Existing Luke GPU/depth/guide/packet/context/ABI and loader checks pass. No shipping test switches.

This proves local API/ownership behavior, not Horizon headset smoothness or FPS recovery. Restart Horizon with V479, begin AERV2 1/2 with both Off, then try AFW, OFXR and both; return to both Off and compare the game FPS with the initial scene. F9 logs source admission/drop (luke_filter9 policy enabled,10 admitted,11 repeated,12 unavailable). F8 remains bounded image capture; Ctrl+Alt+F6 debug should be Off for visual comparison.
