# V579: gaming configuration and explicit diagnostics

This candidate keeps the V578 interpolation, AFW math and private SteamVR
distributor. Owner requested a fresh configuration without legacy imports.

## One user INI

`%LOCALAPPDATA%/OFXR Autonomous/ofxr-bridge.ini` is the sole authoritative user
configuration. The Display tab has **Open ofxr-bridge.ini**. It holds rendering
preferences, per-route choices, tray options, the remembered game and benchmark
gate in separate sections. Atomic section updates share the IPC mutex and keep
the other sections. No autonomous.ini, tray.ini or old selection file is read.
Old files are not deleted. Generated RuntimeLayer activation INIs are internal
runtime data; edit the single user configuration instead. The ZIP no longer
contains the old editable-looking runtime INI; the tray generates runtime data.

First launch creates fresh defaults with Diagnostic log off and benchmarks off.
No renderer restart/queue/swapchain/pacing policy changes are included.

## Native DLSS-G preset

The tray remembers the actual connected game's path, even when settings are
closed. After one fresh game launch, desktop settings can change its profile
while the game is closed. No EXE file picker. Current connected game takes
precedence and becomes the next remembered game. Full UTF-8 path is local only.
The driver profile change remains an explicit preset selection, never an
automatic change while merely detecting/remembering a process.

A is labelled Full/quality, C Lite/faster. If C's profile write/verification is
rejected, retry A and show the actual saved choice. If both fail, show failure.
This does not claim a saved profile proves the loaded SDK model. In particular
an old vendor runtime may internally choose Full despite accepting a C request.
Complete tests remain C-only: an explicit A fallback does not get labelled C.
Restart the game after changing the preset. Avoid Complete tests during an A
comparison because that suite explicitly requests C.

The owner V578 MSFS capture is exactly reproducible in both eyes by enabling
Lite blending locally. Full reduces the captured propeller double. This is not
a new interpolation fix or a guarantee of every C/A headset result. No private
Lite parameter is added to the shipping runtime.

## Diagnostics and benchmark gate

F9 off disables flight events (including hidden startup metadata), RAM timing
recording, optional GPU instrumentation and new image/rolling capture requests.
F8 now requires F9 on. Existing in-flight captures drain safely. Generation's
required completion/disable receipts and visible user counters remain intact.
Full initialization diagnostics require enabling F9 before the relevant work.
Timing/RAM flags left in an internal INI cannot independently activate while
logging is off. Generated runtime defaults explicitly clear optional profilers.

In the **same ofxr-bridge.ini**:

```ini
[benchmark]
enabled=0
```

Set `enabled=1` to enable suite/CSV GUI controls and sampling. F9 can remain off.
Disabling during a suite stops it, restores prior settings and clears memory
requests. Normal 10s steps, leading3s trim, C-only complete suite,18-step Luke
suite and CSV format/owner adapter remain unchanged. No benchmark.ini file.

17 focused checks pass: actual cold offline preset popup, fresh/ignored legacy
configuration and section preservation, cross-process settings restart, suite
default-off/enable/stop/restore, actual rendering-adapter VRAM with F9 off,
logger/GPU live gating, RAM-off with stale flags, D3D11/12 timing when enabled,
native NGX capture/quality, private distributor cadence and packaged loader.
Headset A-versus-C validation remains pending. Local only; no automatic install,
driver-profile edit, publication or merge.
