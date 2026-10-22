# V636: opt-in crash debug

The tray adds **Crash debug (restart game)**, disabled by default. It persists
as `[tray] crash_debug=0/1` in the existing `ofxr-bridge.ini`; no second user INI.
This is diagnostic infrastructure, not a Horizon/DLSS5 crash prevention fix.
Rendering, AFW math, source admission, pacing, VRAM budgets and driver remain V635.

## Armed independently of F9

With Crash debug enabled, configure a bounded 16384-record NR RAM ring and a
device-removal observer without enabling normal diagnostic logging. Retain
recording, exact submission/fence reuse and model/stage receipts. F9 additionally
enables verbose per-barrier records and ordinary logs; turning F9 off does not
disarm crash debug. No image captures or GPU timestamp queries are enabled.
The observer's notification fence is never submitted or signalled on a GPU queue.

OpenXR session D3D12 devices are observed even with NR and OFXR disabled; a late
Luke native-device adoption adds its own lifetime observer. NR's native source
device retains the existing per-handle observer. DRED is a D3D12 facility: this
does not claim native Vulkan crash-dump coverage or guaranteed output for an
instant process termination before the observer callback executes.

Each observer immediately flushes a small armed receipt, build version, game
path, recorder mode and available preparation receipt. On device removal it
flushes the retained NR window and available DRED breadcrumbs/contexts and page
fault allocations. Files remain beside the active runtime DLL:
`%LOCALAPPDATA%/ofxr-bridge/RuntimeLayer/v636/ofxr-dred-V636-*.log`.
No continuous per-frame disk trace is added in crash-only mode.

## Startup DRED preparation

The tray invokes the shipped `scripts/Set-CrashDebug.ps1` hidden, on its desktop
thread, never the game/render thread. The helper uses the Windows D3DConfig
tool to request breadcrumbs, contexts and page-fault data before game device
creation. It targets the last known game before launch and registers newly
discovered games. Existing registrations remain prepared while the flag stays on.
For a game first discovered after it is already running, full DRED requires
one restart; it cannot be retroactively added to that device. The tray shows
prepared/restart, waiting-for-discovery or setup-failure status without popups.

Ownership, original settings and preparation/errors are recorded under
`%LOCALAPPDATA%/ofxr-bridge/CrashDiagnostics/`. The three DRED settings are shared
by D3DConfig's app list, so setup refuses conflicting unrelated registrations
rather than changing their settings. Existing debug-layer, GPU validation and
Watson settings are untouched. Partial setup keeps an original-state journal.

Disabling the flag removes only OFXR-owned app registrations and restores only
the settings still equal to OFXR's forced values. Foreign applications and
independent later edits survive. An already-running game's recorder/device
configuration lasts until game restart, as the tray label states. The helper
can also be run without `-Enable` to recover an interrupted setup. Default OFF
with no owned journal invokes no configuration tool and creates no recorder.

## Validation

Eight focused checks pass in 3.12 seconds. Actual WARP device removal through
the public configuration/observer API with NR disabled, Crash debug ON and F9
OFF produces an independently flushed `recorder_mode=crash-only` report, retained
submission receipt and successful DRED queries. The test removes only a software
device. Existing child-process persistence, ring-wrap/partial-node ordering,
codec and retirement checks remain. No physical GPU was deliberately removed.

Policy tests cover default OFF, multiple game registrations, foreign-state
protection, exact cleanup, independent edits and recovery after partial failure.
A real Windows D3DConfig enable/disable round-trip also passes, restoring the
original empty app list and all system-controlled settings. An early new failure
fixture recursively called its own mock; the fixture was corrected and bounded
with a timeout. The failed log is retained; production helper behavior was not
changed to satisfy that failure. Tray lifecycle, INI round-trip and Horizon
origin/motion controls pass. Owner game crash collection remains pending.

No live build installation, driver update, active INI edit, push or release.
