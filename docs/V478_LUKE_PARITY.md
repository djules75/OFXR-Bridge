# V478 Luke host parity against owner-selected V33018

Parent V477 6ae73fbbfb724f683f2cbb098192cbc4cc6b1186, itself from V474. V475/V476 rejected latency trials remain excluded. Owner confirms OptiScaler V33018 f8183be19cc070569d575846ff8e1957bb971966 as the nearest working Luke baseline. V33034 is a later endpoint, not an interchangeable proof of V33018 visual behavior.

## Evidence and narrow corrections

Horizon V477 PID34904 performs 7049 actual AFW evaluations. The retained two-evaluation GPU capture has normal original/NGX/plugin input color and green plugin outputs with debug=1 in both contracts. The run contains 2494 debug-on and 4555 debug-off evaluations. The old bare F6/Num6 shortcut mutated embedded AFW state independently of Autonomous UI/profile. Debug now uses explicit Ctrl+Alt+F6 and the same profile transaction as the menu. This explains the captured green tint; it does not prove the entire rotation artifact is only debug.

The V33018 physical Forward helper multiplied Forward by sign(projection W). The later V33034 default treats supplied rows as view rows unchanged, with a separate DOOM right/down/forward contract. HFW uses projection W=-1 in the actual capture. V478 restores the exact V33018 transform for the active RealVR.ini HFW section. Right/Up stay unchanged. Later accepted DOOM and other title contracts remain, including the independent captured Streamline-history test; no global axis reversal. This is a source-parity trial against the owner's working baseline, pending game validation.

The host also preserves game-directory ofxr_bridge.ini eye phase (Horizon has odd_serial_is_left=0; standalone previously defaulted to1) and prefers the game's existing PDAFWPlugin.dll, as the combined host did. The current game plugin is534528bytes/643281327028d156c83d7ed701a6c7176a8fdf46544b513da960fa12e5dfe259; bundled beta5 is519680bytes/efc3dd3df78e5a0e487454e7098512514a65764d4479ae10a11f6f140fac1ef3. Difference is observed, not proof of causality. When no local provider exists, the same bundled DLL is used. Files are never overwritten. Game WorldScale remains read-only.

Luke-only NGX rectangles now match OptiScaler's FLOAT MV_Offset_X/Y and clamped available texture extents; invalid frame delta retains16.6667ms fallback. Other guide ingress is unchanged. The Horizon capture has zero offsets, so this is not claimed as its primary cause. Capture-only original pre-DLSS color improves comparison; no extra GPU work while capture is inactive. Shaders, AFW algorithm, depth calibration, protected history, FSR/Hybrid/DLSS-G providers, schedulers and timing formulas remain unchanged. Vulkan vectors remain suspended.

## Diagnostic keys

- F8: bounded two-AFW-evaluation/eight-XR-frame image capture in the game directory, legacy folder name ofxr-capture-v23286-PID-timestamp. Includes settings.txt, guide-contracts.tsv and xr-timeline.tsv, raw images with padded row strides, up to48 images/1GiB. Capture temporarily adds readback cost.
- F9: live logging toggle through the existing shared UI/profile; allow about half a second for file logger polling. Native NGX logging now follows live logger state instead of caching its first disabled value.
- F10: existing menu only; opening it no longer starts large GPU readbacks.
- Ctrl+Alt+F6: explicit AFW debug toggle synchronized with UI/profile. Bare F6/Num6 no longer silently tint outputs.

Raw PID34904 logs and the original capture are retained in work/shared/ofxr-autonomous/diagnostics/v477-horizon-pid34904; compact analysis ships in evidence. No game or registry mutation, live tray launch, push or publication.

## Validation and owner check

Optimized x64 Release,24/24 focused checks,8.88s, zero failures/skips. Camera golden regression uses the captured Horizon rows and exact V33018 behavior; existing real captured view-row/DOOM history checks remain. Luke original queue/bootstrap, output packets, protected guides, depth GPU, host config/phase/provider selection, key-edge behavior, live logging and five Streamline ABI variants pass. Additional actual local-provider initialization and V477 negative comparison receipts are in evidence. Initial test-only linking dependency introduced by a diagnostic snapshot was removed; per-evaluation actual state already exists in the capture contracts. Initial selection regex serialization was corrected before final tests. No test stubs enter shipping runtime.

Test Horizon with AFW debug OFF, same settings as working OptiScaler, first AFW then AFW+OFXR. Confirm rotation and real generated pixels, not only a green counter. If artifacts remain, F9 then F8 captures the exact bad pair without restarting. Visual 1:1 and all-game acceptance remain pending owner testing; V474 remains the selected fallback.
