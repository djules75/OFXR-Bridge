# V581: refresh the desktop DLSS-G target

Base V579 `832705c577eb7e0158a522f9245ae8b5f2891db4`. The V580 early-handoff
experiment is intentionally excluded; keep that version for its own comparison.

The desktop cached an empty game target when first opening the DLSS-G controls.
If the tray subsequently learned the executable while the window remained
open, the selector could stay disabled after game exit. Refresh only queried
the cached path, so it could not recover. The owner's canonical INI already
contains the valid MSFS executable; no live configuration repair was performed.

Refresh now resolves the current/remembered executable before reading its
NVIDIA profile. Visible desktop controls also check once per UI second so they
recover without closing/reopening settings. A target change triggers a fresh
profile read; unchanged targets do not cause repeated driver-profile queries.
The in-game compact UI retains its current-process target and no periodic
file polling is added to the rendering/frame paths. Detection does not write
NVIDIA profiles; choosing a preset remains the explicit write operation.

A first game discovery is still necessary to identify a per-game profile.
No EXE picker, global profile override, pending silent preset or legacy INI
import is added. One user ofxr-bridge.ini and existing A/C restart/fallback
behavior remain. AFW, interpolation, swapchains and pacing are V579-identical.

The actual ImGui regression fixture fails against V579 at manual Refresh after
late target discovery. Final focused checks cover manual/automatic discovery,
cold offline restore and opening the real preset popup while disconnected,
profile validation, single configuration and the shipping packaged loader.
The test advances UI time for polling; it does not sleep or write a driver
profile. Results are in V581_VALIDATION.json. Owner desktop test pending.
Local preparation only, no automatic live installation or publication.
