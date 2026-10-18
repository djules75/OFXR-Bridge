# status.json: OFXR Bridge's live state for other tools

OFXR Bridge's tray writes a small JSON file that other programs can read to
know whether frame generation is armed, paused or running, which multiplier is
active, and how many frames the headset is actually getting. It was added for
[MSFS AutoFPS](https://github.com/ResetXPDR/MSFS_AutoFPS), which adjusts the
simulator's level of detail to hold a target frame rate, and is meant to
replace parsing `tray-lifecycle.log`.

## Where and when

- **Path:** `%LOCALAPPDATA%\OFXR Bridge\status.json`, next to `tray.ini`.
- **Written by the tray** once a second while it runs, and immediately when it
  is armed or disarmed, frame generation is paused or resumed, or a setting
  changes from the menu.
- **Atomic:** each write goes to `status.json.tmp` first and replaces
  `status.json` in one rename, so a reader never sees half a file. If your
  program keeps the file open, the tray cannot replace it and tries again a
  second later, so open, read and close.
- **When the tray exits** it writes a last file with `"armed": false` and no
  session. If the file is missing, the tray has never run on this machine.

## Example

A game running in 3X:

```json
{
  "schema": 1,
  "armed": true,
  "paused": false,
  "multiplier": 3,
  "generating": true,
  "app_fps": 29.8,
  "output_fps": 89.4,
  "display_hz": 90.0,
  "updated": "2026-10-17T09:12:03.512Z",
  "ofxr": { "build": 488 },
  "session": {
    "process": "FlightSimulator2024.exe",
    "pid": 23456,
    "runtime": "SteamVR/OpenXR : steamvr",
    "deep_pipeline": true
  },
  "settings": {
    "multiplier": 3,
    "backend": "nvidia",
    "preset": "medium",
    "flow_resolution": 50,
    "bidirectional": false,
    "deep_pipeline": true,
    "edge_ghosting_reduction": false,
    "repeating_pattern_lock": true
  }
}
```

With no game running, the game's fields are `null`:

```json
{
  "schema": 1,
  "armed": true,
  "paused": false,
  "multiplier": 2,
  "generating": null,
  "app_fps": null,
  "output_fps": null,
  "display_hz": null,
  "updated": "2026-10-17T09:15:41.007Z",
  "ofxr": { "build": 488 },
  "session": null,
  "settings": { "multiplier": 2, "...": "..." }
}
```

## Fields

The top-level keys are the stable part. A program that only needs the basics
should read those and ignore everything else.

| Key | Type | Meaning |
|---|---|---|
| `schema` | number | Format version, currently `1`. Fields may be added within a version; a key will not change meaning or disappear without a new number. |
| `armed` | bool | The tray has OFXR armed: games started now load it. |
| `paused` | bool | Frame generation is paused from the tray (menu or pause key). |
| `multiplier` | number | Frames shown per game frame: `2`, or `3` in 3X. With a game running, the mode **actually running** in it (3X can be switched while it runs); otherwise the configured mode. |
| `generating` | bool or null | Frames were generated in the last second. `false` while paused, and during focus loss, loading or a brief quarantine after the game rebuilds its swapchains. `null` without a game. |
| `app_fps` | number or null | The game's own frames per second, one-second average. |
| `output_fps` | number or null | The frames per second the headset gets: the same one-second figure the in-headset FPS overlay shows, computed even with the overlay off. On SteamVR it comes from the compositor's own count, less repeats. |
| `display_hz` | number or null | The headset's refresh rate, once OFXR has measured it. |
| `updated` | string | When this file was written, ISO 8601 UTC. |
| `ofxr.build` | number | OFXR's internal build number. |
| `session` | object or null | The running game: `process` (executable name), `pid`, `runtime` (the OpenXR runtime's name) and `deep_pipeline` (Prefer FPS over latency). `null` without a game. |
| `settings` | object | What `tray.ini` asks for: `multiplier`, optical flow `backend` (`nvidia` or `fidelityfx`), NVIDIA `preset` (`slow`, `medium`, `fast`), `flow_resolution` in percent (`100`, `75`, `50`, `25`), `bidirectional`, `deep_pipeline`, `edge_ghosting_reduction`, `repeating_pattern_lock`. |

## Reading it reliably

- **Tolerate a missing or unreadable file:** the tray may not be running, or
  may be replacing the file at that instant. Keep your last good values and
  read again a second later.
- **Check `updated`:** if it is more than a few seconds old, the tray is not
  running any more (for example, it was killed). Treat OFXR as not armed.
- **A game that crashes** without closing its OpenXR session is dropped from
  the file three seconds after it stopped reporting.
- **One game at a time:** if two OpenXR games run at once, the file describes
  the one that reported last; `session.pid` says which.

## Telling when frame generation is struggling

`app_fps × multiplier` is what the headset should get when frame generation
keeps up; `output_fps` is what it actually gets. For example, in 3X with the
game at 28 frames per second, the headset should show about 84; an
`output_fps` of 40 means frame generation is delivering about half of that,
usually because the GPU is out of time, and lowering the game's load helps.
