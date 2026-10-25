## V624 local: Luke publication option and stable user data

The Luke Ross tab adds **Swapchain budget fix** (required for some games, e.g. SW Outlaws; restart game) below AFW
and above Details. It is read before native session creation and remains fixed
for the session. Default off for other Luke games; Outlaws retains its mandatory
V623 sharing. Compatible generated outputs share only the final native sink per
original source. Auxiliary publication, private work images, pacing and AFW math
are unchanged. AFW-only and VDXR do not use this option.

Details retains the AFW counter above the inventory, then shows Total swapchains,
OFXR, Game / Luke Ross, and unassigned native outputs when observed. Native
publication handles are deduplicated: independent private tokens do not inflate
the total. Problem identity/allocation/fallback warnings remain. Counts describe
observed session resources, not hidden runtime allocations or invented module
ownership.

All new user data is rooted at `%LOCALAPPDATA%/ofxr-bridge`, independent of build:
`ofxr-bridge.ini`, `Benchmarks`, `NGX`, and the existing versioned `RuntimeLayer`
cache underneath that root. Display adds **Open benchmark folder**, available
even with sampling disabled. Existing INI filename and benchmark CSV format stay.
No legacy autonomous.ini/tray.ini reader is introduced. Future per-game profiles
will live under this root; no profile loader is claimed in this build. Resolve a
future game's profile before session creation so startup-only choices remain
available before resource allocation. Old builds keep their historical paths.

IPC62401 requires a matching tray and game DLL; provider45601 is unchanged.
Headset validation pending. No automatic activation, registry update or push.
