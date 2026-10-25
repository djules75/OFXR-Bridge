# V482: Horizon one-second circular diagnostic capture

Parent V481 0c31b6224fe8fb2314016a13f355a5fd45815463, including startup black-screen correction. V474 fallback; V475/V476 excluded. V480 clock correction retained: its1ns defect is absent in the real PID32164 run, but owner confirms visible flicker is unresolved. No new AFW/pacing/camera-math correction is claimed.

## Controls

In Horizon, Ctrl+F8 arms/stops the circular buffer (default OFF). Arm at least one second before the trial. F8 while armed saves the previous second to a fresh folder and keeps recording. Press F8 multiple times for independent snapshots, including during a previous disk write. Ctrl+F8 again stops capture. F9 live logging and F10 menu remain. F8 with the ring OFF retains the older next-two-evaluations capture. No registry/game-file/live installation action is performed.

Snapshots are under %LOCALAPPDATA%/OFXR Autonomous/RuntimeLayer/v482/captures/rolling-TIMESTAMP-pidPID-N (runtime module's captures directory). Each contains trace.jsonl and per-eye PPM images. The success beep follows completed save. No capture folder/disk stream is created merely by arming.

## Data and ownership

Records original release images, actual AFW-published terminal D11 eye images, source receipts/admission/drops, per-eye application poses, actual physical OpenXR output poses/result and AFW camera contracts/timeline. Records bind image capture IDs, render/object identity, guide publication, serial, private swapchain and image index. Includes position/orientation/FOV/rectangle, source/destination matrices and native motion camera transforms. Runtime views describe submitted images, not a measurement of the headset compositor displaying them.

Images sample a regular1/8-resolution grid to keep a second practical; full decoded RGBA pixels still contribute to a128-bit fingerprint per slice. This detects pixel changes outside the visible sample grid, but the reduced image may miss a fine pixel artifact. Fingerprints do not by themselves identify an engine frame. No depth/MV full-resolution readback is added to rolling capture. Legacy two-evaluation capture remains available when the ring is OFF for detailed pixel diagnosis.

Each F8 freezes a QPC interval exactly one second long, ending when the app thread observes the key. Private image copies are queued before application/runtime release, and Map happens only after the diagnostic fence completes, with DO_NOT_WAIT. A save request includes its pending GPU samples and is dispatched after those readbacks become available, without waiting on the rendering thread. The writer compacts samples and writes snapshots separately while capture continues. Metadata includes GPU/CPU writer/save drops and capacity evictions; early arming reports full_second_available=false.

Bounds:256MiB compact CPU ring,128MiB incoming queue,8 pending save requests and4 source/output streams, with bounded existing GPU snapshot slots. Two seconds of temporary CPU retention allow late completed readbacks; exports contain only the requested previous second. Slow GPU/disk or unusual resolutions can cause explicitly reported drops; missing data is not silently reported as a complete frame sequence. Arming adds diagnostic GPU copy/hash work and can affect performance; use ordinary logging with capture OFF for pacing comparisons. Default OFF performs no capture copies/readbacks/writer work.

## Verification

Optimized x64 Release:29/29 focused checks pass11.21s, no skips. New GPU test captures source eyes then overwrites originals, verifies private copies, fence-before-map, shader-state restoration inherited checks, exactly-one-second QPC filtering, old/future samples excluded, two independent save presses and continued recording, correct1/8 image dimensions/eye colors, and off/default gating. V481 actual startup composition/all-four-state gameplay filtering, clock negative/fix, Luke GPU/context/depth/guide retention and ABI/loader checks remain passing. All27 generated production shaders and seven ancillary runtime files remain byte-identical to V481; only capture's private runtime-compiled diagnostic shader has an optional8-pixel tile. AFW/provider math and game scheduling are unchanged.

Owner test: start Horizon, check menu/loading visibility, choose the bad AFW state with debug OFF, Ctrl+F8, wait one second, rotate as usual. At each flicker press F8; repeat as needed. Ctrl+F8 stops. Retain the run and new rolling directories. Visual diagnosis and startup headset confirmation remain pending.
