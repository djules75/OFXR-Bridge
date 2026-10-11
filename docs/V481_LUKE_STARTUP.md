# V481: Horizon startup composition passthrough

Parent V480 4b8928cc40808f1c83cb950cd327cbd662e57267. V474 fallback; V475/V476 excluded. IPC47101/provider45601 unchanged. Sealed predecessors retained.

## Owner result and evidence

Owner confirms that V480 still flickers in Horizon. The earlier correction that the run was V479 was superseded by this explicit V480 result. Preserved PID32164 logs/INI at work/shared/ofxr-autonomous/diagnostics/v480-horizon-pid32164. The clock correction works in the log: no application prediction advances by the former1ns floor, including new-source admissions. It does not solve the reported visible problem.

All2891 downstream outputs succeed; source admissions have AFW enabled/OFXR disabled. Startup drops737 unknown-source submissions before the first original AER source at9190.519ms. This explains missing menu/loading output: V479/V480 treat an unavailable producer source as a gameplay duplicate before the source stream exists.

The final30-55.522s interval contains89 output gaps above25ms, largest34.540ms,26 source increments of2, no failed OpenXR outputs. One End at39.340s takes29.272ms; between synthesis prime and source-origin readback stage122 are28.570ms, while physical End takes33us. The next admitted source advances2662->2664. These are measured stalls/skipped source observations, not proof that a particular eye image or pose caused the visible flicker. V481 does not remove this readback wait or claim a flicker fix.

## Narrow startup fix

Pinned Horizon source filtering is eligible from session creation but initially unarmed. Every actual application menu/loading composition is copied and submitted, even without an AER source identity. Private generation/AFW history is bypassed until two receipts from the same pinned source object show an increasing original-render counter. A stationary receipt cannot arm it. This is evidence that original level production started, not a private engine-level loaded flag or a timer.

The advancing receipt is admitted exactly once. After arming, V479/V480 duplicate and unknown-source suppression, all four AFW/OFXR menu combinations, fresh-only physical ownership, original image metadata and both-Off warm bypass remain. Transient missing gameplay receipts do not disarm the filter. The clock fix is retained because its specific defect is absent in the real V480 run. Other titles/routes, shaders, camera math and providers stay unchanged.

Diagnostic luke_filter result15 marks startup pass-through;14 marks the one transition to source filtering;10/11/12 retain new/duplicate/unavailable gameplay meanings. No new menu or persistent setting.

## Verification and next capture

Optimized x64 Release:28/28 focused checks pass9.42s, no skips. The new test calls actual application Wait/Begin/Release/End with unknown receipts in all four control states. Its strict fake runtime checks that the real projection, pose, space and output slot are submitted with balanced physical calls. Repeated stationary startup receipts pass; an advancing receipt arms; gameplay repeats/unknowns then produce no extra output, and the next source is admitted. Existing clock negative/fix, fresh ownership, Luke GPU/guide/depth/context and ABI checks remain passing.

First check that menus/loading are visible. For remaining rotation flicker, use the existing F8 capture while the bad state is happening, AFW debug OFF and the same working OptiScaler settings. It records the next two AFW evaluations with original/guide/plugin-output images, source tokens, eye associations, camera matrices and XR timing under the game executable directory (legacy name ofxr-capture-v23286-PID-timestamp). If the captured pair is clean, repeat F8 during the bad state; two samples cannot establish a rare transient they missed. Capture readbacks temporarily add GPU cost, so use the ordinary noncapture log for pacing measurements. F9 toggles live logging. No extra capture work is enabled by default.

No live game install, tray/registry action, push or publication. Startup and visual headset acceptance await owner testing.
