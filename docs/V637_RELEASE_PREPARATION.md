# OFXR-BRIDGE 0.3.0 (v637)

Local laboratory candidate on V636. No commit, tag, push, release publication,
active installation or driver-setting change is authorized by this checkpoint.
The public version remains **OFXR-BRIDGE 0.3.0 (v637)**.

## Configuration ownership

The common directory is `%LOCALAPPDATA%/ofxr-bridge`.
`ofxr-bridge.ini` holds global tray choices and the desktop rendering template.
`Profiles/<executable filename>.ini` holds each game's rendering preferences;
for example `Profiles/KCD2.exe.ini`. Matching is by executable filename, so
installations with the same executable filename deliberately share a profile.
New game profiles inherit current main rendering settings, including inactive
route preferences and the requested DLSS-G preset. Tray settings, benchmark
enablement and last-game discovery metadata are not copied.

Profile selection happens before the first application OpenXR instance and
before session resources are created. AFW Only and Swapchain budget fix are
latched from that game's profile. Multiplier/pipeline initialization also uses
the game settings rather than an older generated runtime INI.
A profile setup failure refuses initialization rather than writing game choices
back into the main INI. First creation and each settings save are serialized;
file replacement is atomic. Per-profile IPC prevents different executables
sharing live settings, benchmark state or visibility leases.

F10 edits the game profile. Desktop Settings follows the active game, including
while paused, and shows its filename. After disconnect it edits Global defaults.
Unpublished edits from a previous target are discarded instead of being saved
into the next target. The benchmark recorder retains its original profile for
stop/disconnect restoration. Benchmark CSVs always go into the common
`Benchmarks` folder, independently of game profiles and diagnostic logging.

Reset to defaults restores factory rendering defaults in the current target:
OFXR 0.3.0, NVIDIA optical flow, 50% flow resolution, AFW Only off, basic mode,
and default inactive route preferences. The requested DLSS-G preset returns to
A for the next launch; no NVIDIA profile is silently rewritten by Reset.
The main rendering template is not used as the reset source. Global tray
preferences and benchmark enablement are preserved.

## Tray and startup choices

Pacing is a global, restart-required transport selection:
- Auto: original VDXR transport on VirtualDesktopXR; SteamVR-style private
  delivery transport on every other runtime.
- Force VDXR: original VDXR transport on any physical runtime.
- Force SteamVR: private delivery transport on any physical runtime.

This does not falsify the actual runtime identity. SteamVR compositor queries
remain specific to SteamVR; VDXR remains identified as VDXR in diagnostics.
The separate in-game Pacing combo remains Framework / Director /
Framework+director and is first below Enable frame generation.
AFW Only moved from the tray into the Luke Ross tab and is per-game.
The tray keeps Pause key, Crash debug, Enable benchmarks, and Open folder.
Pause/Resume FG and the preparation queue menu are removed. The preparation
queue is Separate by default and remains INI-only:

```ini
[tray]
steamvr_separate_queue=1
pacing=auto
```

## Basic and advanced UI

```ini
[ofxr]
AdvancedMode=0
```

The key is INI-only. Set it in the main INI for new profiles or in an existing
game profile, then reopen that context/restart the game. Basic mode hides Motion
source and forces optical flow for OFXR engines. It hides Clear before framewarp
and Display's Experimental features. AdvancedMode=1 restores those controls.
FSR, Hybrid and DLSS-G retain their own native-guide contracts.
Display no longer exposes Open INI or Output CSV. Benchmark output is always on;
Enable benchmarks still gates the suite buttons and sampling.

## Validation boundaries

Automated tests cover isolated configuration and real D3D12 GPU resources with
a mocked downstream XR runtime. This does not assert headset validation of
forced cross-runtime combinations or physical display scanout. No AFW motion
math, provider model, depth subtraction or pacing algorithm is redesigned here.
The V636 default-off crash-debug capability remains available independently of
F9; reports now identify v637. No Horizon device-hung prevention fix is claimed.
