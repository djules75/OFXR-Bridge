# V466: tray toggle on two rows

Base sealed V465 b16bcbb505021e192dc2e0e1e12033b89add4b9a. Owner requests only the tray menu change and explicitly asks to avoid unnecessary tests.

The tray shows separate **Enable** and **Disable** rows. Enable is checked when armed; Disable is checked when disarmed. Both rows send the existing toggle_arm command and toggle the same bridge state. Pause/resume remains the existing independent control. The arm/disarm handlers and registration lifecycle are unchanged.

This is a tray-only delivery: Release V466 executable links the identical verified V465 static libraries; all seven runtime DLLs and the runtime INI are copied byte-for-byte from V465. IPC remains46501 and private provider ABI45601. Existing settings/headset interface and flight logger keep their V465 version identifier. Renderer, scheduling, FSR, metrics, Hybrid and R19 are untouched.

No new tests or GPU suites are run. Validation is successful Release compilation plus diff review, unchanged-source/runtime hashes and package integrity. V465's143 new/160 djules checks remain inherited evidence, not new V466 tests. See VALIDATION.md and docs/V466_VALIDATION.json. No live activation or installation.

To reproduce the tray-only executable: configure the source with VS2022 x64 Release, reuse the seven static libraries listed in evidence/TRAY_BUILD.json from the sealed V465 build, and build OFXRBridgeTray.vcxproj with BuildProjectReferences=false. Copy V465 runtime outputs into Release before the post-build copy commands. The normal Build-Autonomous.ps1 builds all components from source; this package intentionally reuses the certified V465 runtime binaries and includes their original source archive separately.
