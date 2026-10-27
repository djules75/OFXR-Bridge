# OFXR Bridge

**Frame generation for VR: a smoother experience, at your headset's refresh
rate.**

OFXR Bridge creates extra frames between the ones your game renders, so you
reach your headset's refresh rate even when your PC can't: a game at 45 FPS
on a 90 Hz headset gets close to 90, with 3X Frame Gen a game at 30 FPS too,
and with 4X a game at 30 FPS on a 120 Hz headset.

It runs from a tray icon: start it before your game and it works in the
background, no game files touched. Generating frames costs a little GPU
time, and fast-moving objects can show small artifacts. It works with OpenXR
games only (see below).

Under the hood, OFXR Bridge is an OpenXR layer that uses optical flow to
create the in-between frames. In games with DLSS, its **Hybrid** mode also
uses the motion the game itself calculates, for cleaner results.

## 🛠️ Not working, or not feeling smoother?

### ➡️ [Read the troubleshooting guide first](docs/TROUBLESHOOTING.md)

It checks in one step whether OFXR is running in your game, then lists the
usual causes and fixes: OpenVR games, running as administrator, the game's
frame rate, frame smoothing in your VR software, and running out of VRAM.

**More guides:** the [game compatibility guide](docs/GAME_COMPATIBILITY.md)
(the games we tested, and settings for MSFS 2024, Skyrim VR, Fallout 4 VR,
R.E.A.L. VR games and Alien: Isolation) · [OFXR on Linux (Proton)](docs/LINUX.md)

---

> [!IMPORTANT]
> **OpenXR games only.** OFXR works with games that use **OpenXR**, whatever
> your headset or VR software. Games that use **OpenVR** (SteamVR's older
> system) are not supported, but many run through OpenXR with
> [OpenComposite](https://gitlab.com/znixian/OpenOVR). With **UEVR**, choose
> its OpenXR option. See the [games we tested](docs/GAME_COMPATIBILITY.md#tested-games).

> [!IMPORTANT]
> **Works on most recent graphics cards:** NVIDIA GeForce GTX 10-series and
> newer, AMD Radeon RX 400-series and newer, and Intel Arc. On NVIDIA RTX
> cards and the GTX 1660, you can also choose NVIDIA's own optical flow in the
> tray.

> [!TIP]
> **What to expect:**
>
> - **2X:** a game at about half your refresh rate reaches the full rate. 45
>   FPS on a 90 Hz headset gives 90.
> - **3X:** a game at about a third of your refresh rate reaches it too. 30
>   FPS on a 90 Hz headset gives 90.
> - **4X:** a game at about a quarter of your refresh rate reaches it too. 30
>   FPS on a 120 Hz headset gives 120.
> - Results vary by game, GPU and resolution. OFXR cannot make up frames the
>   game does not render.

## Features

**Frame generation**

- **2X, 3X and 4X Frame Gen**: one, two or three generated frames for every
  frame the game renders, switchable while you play, from the tray or with a
  key (Ctrl + Alt + F12 by default).
- **Hybrid mode** for DLSS games: combines the game's own motion with OFXR's
  optical flow, for cleaner moving objects, with an optional **depth search**
  that keeps thin nearby objects solid.
- **Optical flow**: AMD FidelityFX by default, or NVIDIA's Optical Flow
  engine (Fast, Medium, Slow), at 100%, 75%, 50% or 25% resolution.
- **Prefer FPS over latency** (on by default): one frame of extra latency in
  exchange for reaching full frame rate from half, with smoother dips.
- **HUD and markers** in UEVR games move smoothly with the generated frames
  instead of appearing twice.
- **HUD fix**: a translucent HUD, such as Star Citizen's helmet HUD and
  cockpit instruments, no longer leaves double images on stick turns.

**Games and platforms**

- D3D12, D3D11 and Vulkan games, with a pipeline built specifically for
  SteamVR's compositor.
- **32-bit games** in D3D11, such as Alien: Isolation with MotherVR, on
  NVIDIA cards with NVIDIA's optical flow.
- **Star Citizen**, with Easy Anti-Cheat: OFXR's files are signed.
- UEVR, OpenComposite, Luke Ross's R.E.A.L. VR mods and Cheeky Foveated
  DLSS.
- **Linux**, in games under Proton.

**Tray and overlay**

- A tray icon that arms the bridge as soon as it starts. Most settings change
  in a running game, without restarting it.
- **Pause frame generation** while you play, from the tray or with a key
  (Ctrl + Alt + F7 by default).
- **Lower VRAM** (on by default) and **Reset to default**, which puts every
  setting back to its factory default in one click.
- **Tooltips** on the settings that cost GPU time or need a condition: hover
  over them in the tray menu.
- **Games run as administrator** (not recommended): lets OFXR work in games
  that have to run as administrator.
- An in-headset **FPS overlay** that counts only new frames, with the latency
  OFXR adds and Hybrid's status, in three sizes and four corners.
- A **flight recorder** for diagnostics, switchable while you play.
- A live status file for other tools, such as
  [MSFS AutoFPS](https://github.com/ResetXPDR/MSFS_AutoFPS).

## Installation and use

1. Download the latest release archive from GitHub Releases.
2. Extract the complete archive to a writable folder.
3. Run `OFXRBridgeTray.exe`. The bridge arms itself straight away. If it
   cannot, the tray shows why; select **Arm bridge until manual disarm** to
   retry.
4. Right-click the tray icon to choose the optical-flow backend and options.
   Hover over a setting to see what it costs. The optical flow, Hybrid, the
   HUD fix, the Frame Gen multiplier, **Prefer FPS over latency**, the FPS
   overlay and the flight recorder change in a running game; **Lower VRAM**
   applies the next time the game starts.
5. Start the game normally, **after** the tray: OFXR has to be armed when the
   game starts, especially for Hybrid. For injectors such as UEVR, leave the
   tray armed while the VR mod is injected.
6. Select **Disarm bridge** or close the tray application when finished.

On Linux under Proton the tray cannot stay running beside the game: see
[OFXR Bridge on Linux (Proton)](docs/LINUX.md) for arming it with
`--arm-and-exit` instead.

### Pause and resume

To compare with and without generated frames while you play:

- **Pause frame generation** in the tray menu, or press **Ctrl + Alt + F7**
  in the game. The FPS number shows two orange bars while paused. Do the
  same again to resume.
- **Change the key** with **Current key binding** in the tray menu. Pick a
  key your game and mods do not use: the game still sees it.
- **Pausing is not disarming:** a paused game still runs through OFXR. To
  rule OFXR out of a problem, disarm it and restart the game.

> [!IMPORTANT]
> **Don't run the game as administrator.** OFXR does not load in it, with no
> error and no FPS number, unless you turn on
> [Games run as administrator](#games-run-as-administrator).
>
> - Check the game's `.exe`, shortcut and launcher: Properties →
>   Compatibility → "Run this program as an administrator" must be off.
> - A launcher or mod manager running as administrator starts the game as
>   administrator too.
> - The UEVR injector is fine as administrator.

Keep the default settings to start with: **FidelityFX** optical flow at
**50% resolution**. They give a good performance boost with minimal
visual-quality loss.

The tray and bridge require the [Microsoft Visual C++ Redistributable
x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist),
and [32-bit games](#32-bit-games) the x86 one as well. OFXR Bridge does not
replace your active OpenXR runtime.

### FPS overlay and flight recorder

**FPS overlay:** in the tray menu, **FPS overlay** puts a frame counter in a
corner of your headset view. It can also show the latency OFXR adds and
Hybrid's status, in three sizes.

> [!NOTE]
> **Use OFXR's own FPS number.** Other FPS tools (fpsVR, SteamVR's frame
> timing, counters in the game or VR mod) can misreport while OFXR runs,
> too high or too low. OFXR's number counts only the new frames that reach
> your headset. See [FPS overlay details](docs/FPS_OVERLAY.md).

**Bridge flight recorder:** in the tray menu, it writes a diagnostic log
of what OFXR does, which is what we need to look into a problem (see
[Reporting problems](#reporting-problems)). It can be switched on and off
while you play. While it runs, OFXR also draws two small squares next to
the FPS number, so you can check which frames reach your headset: a purple
one on the generated frames and a green one, just to its right, on the
game's own frames. Turn the recorder off when you are done.

| In the headset | It means |
|---|---|
| A **red** FPS number | OFXR is loaded but not generating frames |
| A **green** FPS number | OFXR is generating frames and handing them to your VR software |
| A **purple** square (flight recorder on) | The generated frames are shown in your headset |
| A **green** square, right of the purple one (flight recorder on) | The game's own frames are shown in your headset |
| Both squares, flickering | Generated and game frames alternate, as they should |

### Hybrid

Tray option **Hybrid: game motion vectors + optical flow (DLSS games)**,
**off by default**. Inspired by Eliott's and
[tig3rmast3r](https://github.com/tig3rmast3r)'s work. In games that use DLSS,
OFXR reads the motion the game calculates for DLSS and combines it with its
own optical flow, so moving objects keep their shape with less warping. OFXR
adds two improvements of its own: the background revealed behind a
fast-moving object no longer shows a ghost of it, and **Hybrid: depth
search** (off by default) uses the game's depth to keep thin nearby objects,
such as cockpit frames and railings, from breaking up.

- **Start OFXR before the game**: it has to be running when the game sets up
  DLSS.
- **The FPS overlay** shows **HYBRID OK** in green when Hybrid is working,
  or **HYBRID KO** with a code in red when it cannot work in that game. The
  [troubleshooting guide](docs/TROUBLESHOOTING.md#d-hybrid-shows-hybrid-ko)
  says what each code means.
- **GPU cost:** Hybrid adds GPU time to every generated frame, and the depth
  search adds more, especially at high headset resolutions. Turn the depth
  search off first if your frame rate drops.
- **Switch it while you play**, without restarting the game.
- Some games need a setting changed: see the
  [game compatibility guide](docs/GAME_COMPATIBILITY.md).

### HUD fix

Tray option **HUD fix**, **off by default**, with Hybrid on or off. A
translucent HUD drawn over the scene, such as Star Citizen's helmet HUD and
its ship's cockpit instruments, can leave double images in the generated
frames when you turn with the stick, more at 3X and 4X. The HUD fix keeps
it still instead.

- **It depends on the game:** it was made for Star Citizen. Try it if a HUD
  smears or doubles when you turn.
- **GPU cost:** it adds some GPU time, more at 3X and 4X.
- **Switch it while you play**, without restarting the game.

### Star Citizen

OFXR's files are digitally signed, so Easy Anti-Cheat lets OFXR load in Star
Citizen. 2X, 3X and 4X work, Hybrid works with the game's DLSS, and the
[HUD fix](#hud-fix) keeps the helmet HUD and cockpit instruments from
doubling on stick turns. Start OFXR before the game. The best settings are in the
[game compatibility guide](docs/GAME_COMPATIBILITY.md#star-citizen).

> [!CAUTION]
> OFXR is an independent project, not endorsed by Cloud Imperium Games or
> Easy Anti-Cheat. Using it with an anti-cheat protected game is at your own
> risk: we accept no responsibility for any account action, ban or other
> consequence. Check the game's terms of service before you play.

### 32-bit games

OFXR also works in 32-bit games, such as Alien: Isolation with the MotherVR
mod. The tray arms the 32-bit layer, in the `ofxr_x86` folder, together with
the 64-bit one: nothing to choose, each game loads the one it needs.

- **D3D11 games only.** A 32-bit game in D3D12, Vulkan or OpenGL runs
  without OFXR.
- **NVIDIA optical flow only**, on NVIDIA RTX cards and the GTX 1660.
  FidelityFX is not available in 32-bit games: OFXR uses NVIDIA's optical
  flow whatever the tray says, and on other cards it generates no frames.
- **No Hybrid:** DLSS does not exist for 32-bit games.
- **The 32-bit Visual C++ Redistributable** is needed too (see below).
- Most of these games use OpenVR: run them through OpenXR with the 32-bit
  [OpenComposite](https://gitlab.com/znixian/OpenOVR).

See [Alien: Isolation](docs/GAME_COMPATIBILITY.md#alien-isolation) in the
game compatibility guide.

### Optical flow

To make an in-between frame, OFXR first works out how the image moved
between two game frames: the optical flow. Keep the defaults unless you need
more performance. These settings change while you play.

| Setting | Default | Notes |
|---|---|---|
| **Optical flow backend** | FidelityFX | Similar quality to NVIDIA's, and lighter on the GPU. NVIDIA's options work on RTX cards. |
| **Optical flow resolution** | 50% | 25% is lighter, for very high headset resolutions. 100% costs about four times more for almost the same image. |
| **NVIDIA bidirectional consistency** | Off | Slightly cleaner edges, at twice the cost. |

### Prefer FPS over latency

This tray option is **on by default**. The bridge holds each generated frame
back by one display refresh, so the optical flow gets a whole refresh to
finish instead of the gap the game leaves between frames.

- **Gain:** a game that only sustains about half your headset's refresh rate
  can reach the full rate. A game holding 45–50 FPS on a 90 Hz headset is
  likely to reach 90. Dips are also much smoother.
- **Cost:** one frame of extra latency, about 11 ms at 90 Hz.
- **Turn it off** when your GPU has budget to spare. Try it: turn the option
  off and play the same scene. If you still hold your full frame rate, leave
  it off and save one frame of latency. If you lose frames, turn it back on.

It changes in a running game, with a short pause. 3X and 4X need it, so
choosing either turns it on.

### 3X and 4X Frame Gen

Tray menu **Frame Gen multiplier**: **2X** by default, **3X** or **4X**. 3X
makes two in-between frames for every game frame, for games at about a third
of your refresh rate (30 FPS at 90 Hz); 4X makes three, for games at about a
quarter (30 FPS at 120 Hz). Use the lowest multiplier your game can hold: if
it holds half the refresh rate, 2X looks better.

- **Change it while you play**, with one short hitch at the switch, from the
  tray or with the multiplier key: **Ctrl + Alt + F12** steps 2X, 3X, 4X and
  back to 2X. Change the key with **Current key binding**, under **Frame Gen
  multiplier** in the tray menu. In some games, for example with **Lower
  VRAM** off, the tray asks you to restart the game for a higher multiplier.
- **Best results** in DLSS games where Hybrid works (**HYBRID OK** on the
  overlay): turn on **Hybrid** and **Hybrid: depth search**, if your GPU has
  the headroom.
- **Expect more artifacts than 2X**, mostly in sideways motion, more at 4X,
  and a little more latency.
- **R.E.A.L. VR games stay at 2X**: see the
  [game compatibility guide](docs/GAME_COMPATIBILITY.md#real-vr-games).

### Lower VRAM

Tray option **Lower VRAM (may cause stuttering)**, **on by default**. The
bridge keeps fewer working images, which saves about half a gigabyte of VRAM
at high resolutions. On most cards it costs nothing; if you see stutter with
VRAM to spare, untick it. It takes effect the next time the game starts.

### Reset to default

Tray entry **Reset to default...**, in its own section below **Open bridge
logs**. After you confirm, it puts every OFXR setting back to its factory
default: optical flow **FidelityFX** at **50%** resolution, **Prefer FPS
over latency** on, the Frame Gen multiplier at **2X**, **Lower VRAM** on,
**Hybrid** and its depth search off, the **HUD fix** off, the FPS overlay big
in the upper right with the latency, multiplier and Hybrid status shown, the
flight recorder off, the pause key back to **Ctrl + Alt + F7** and the
multiplier key to **Ctrl + Alt + F12**. Settings only reachable by editing
`tray.ini` go back to their defaults too. **Games run as administrator** is
not a setting and stays as it is.
If OFXR runs worse than you expected and you have changed any settings,
start here. **Lower VRAM** applies the next time the game starts; the rest
changes in a running game at once.

### Games run as administrator

Tray entry **Games run as administrator (not recommended)**, below **Reset to
default**. Run your games normally whenever you can: a game run as
administrator can change anything on your PC, and so can every mod it loads.
Use this only for a game that has to run as administrator.

- **Turning it on** asks for Windows administrator rights once. It installs
  a copy of OFXR for every user of the PC, in
  `%ProgramFiles%\OFXR Bridge\Elevated`.
- **It only works while the tray is armed**, with your usual settings and
  logs.
- **While it is on,** OFXR's small Vulkan layer loads into every Vulkan
  program, even when OFXR is disarmed. It does nothing there.
- **After you update OFXR,** the entry shows **update needed**: click it to
  update the installed copy.
- **Turn it off** to remove the installed copy.

### Cheeky Foveated DLSS

OFXR works alongside [Cheeky Foveated DLSS](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS),
including its eye-tracked foveation, and Hybrid works with it.

### For the best results

On every headset and VR software:

- **Turn off your VR software's own frame smoothing**: it fights with
  OFXR's. That is **SteamVR**'s Motion Smoothing and "fixed frame rate at
  half" (per-application video settings), **Meta**'s ASW, **Virtual
  Desktop**'s SSW, **Pimax Play**'s Smart Smoothing and **Pico Streaming
  Assistant**'s Frame Interpolation.
- Leave **Prefer FPS over latency** on, then try the same scene with it off.
  If you still hold full frame rate without it, keep it off for one frame
  less latency.
- Pick a refresh rate close to double your game's frame rate, triple it
  with **3X Frame Gen** or quadruple it with **4X**. If that is still short of
  the refresh rate, your VR software fills the gap with repeated frames and
  the image judders.

### When the number stops short of the refresh rate

Generating frames costs GPU time too, so your game needs some headroom above
half the refresh rate. If the number stops short (110 at 120 Hz, for
example), lower the refresh rate, the per-eye resolution or the optical-flow
resolution. See the
[troubleshooting guide](docs/TROUBLESHOOTING.md#c-ofxr-works-but-it-does-not-feel-better).

### How much VRAM the bridge uses

See the VRAM table in [Troubleshooting](docs/TROUBLESHOOTING.md#how-much-vram-ofxr-uses).

### Other tools: MSFS AutoFPS

The tray writes OFXR's live state to a small file, `status.json`, once a
second. [MSFS AutoFPS](https://github.com/ResetXPDR/MSFS_AutoFPS) reads it,
so it knows when frame generation is running and at which multiplier, and
adjusts the simulator's detail to keep your frame rate at its maximum. Other
tools can read it too: see [status.json](docs/STATUS_JSON.md).

### What the tray changes on your PC

When the tray arms the bridge (at start-up, or when you select **Arm**), it copies the versioned OFXR layer and its
configuration into `%LOCALAPPDATA%\OFXR Bridge`, creates an absolute-path
OpenXR implicit-layer manifest and registers that manifest for the current
Windows user, with a second one for the 32-bit layer. It does not inject a
DLL into the game, replace game files or replace the active OpenXR runtime.

**Games run as administrator**, when you turn it on, also installs the
OpenXR layer and OFXR's Vulkan layer into `%ProgramFiles%\OFXR Bridge\Elevated`
and registers them for every user of the PC. They stay installed until you
turn the option off; while the tray is disarmed they do nothing.

Selecting **Disarm** removes the exact OpenXR registrations. Closing the tray
also disarms it, with a watchdog providing cleanup if the tray exits
unexpectedly. After disarming and closing the application, no active OFXR hook
or OpenXR registration remains on the system. Versioned cache files, settings
and diagnostic logs may remain under `%LOCALAPPDATA%\OFXR Bridge`, but they are
inert and may be deleted manually at any time.

## Reporting problems

Please report both working and non-working games, rendering problems, freezes
and crashes in [GitHub Issues](https://github.com/djules75/OFXR-Bridge/issues)
or on the [Flat2VR Modding Discord](https://discord.gg/flat2vr).

Reported results are collected in the
[OFXR Bridge Compatibility Chart](https://docs.google.com/spreadsheets/d/1lhaJm1wzt29exmx4tZbxdwf82RcrlLcyZJ850GcTf1w/edit?usp=sharing);
the games we tested ourselves are in the
[game compatibility guide](docs/GAME_COMPATIBILITY.md#tested-games).

Before reporting a problem:

1. Right-click the tray icon and enable **Bridge flight recorder**.
2. Start the game and reproduce the problem once. You can also switch the
   recorder on while the game is running; the game hitches once.
3. Close the game, then select **Open bridge logs** from the tray.
4. Attach the newest `ofxr-bridge-flight-*.log` file to the issue.
5. Make sure OFXR works on your system in at least one other game before
   reporting that it does not work in this one.

Please also include:

- game name and version
- VR mod or injector, if any
- headset and OpenXR runtime
- GPU and driver version
- Windows version/build and OFXR build number
- selected OFXR backend and options
- exact steps and the observed result

If no OFXR log was created, report that too: it usually means the layer was not
loaded or the process stopped before the recorder could start. Game logs and a
crash dump are also useful when available.

The recorder is independent from game and mod logging. It records OpenXR
negotiation, resource eligibility, frame-generation stages, recovery events
and potentially blocked call boundaries. It does not record video or replace a
native crash dump.

## Building from source

See the [Windows build instructions](docs/BUILDING.md).

## License

OFXR Bridge is licensed under [LGPL-3.0-or-later](LICENSE). Third-party
components retain their respective licenses.

## Support

If you find OFXR Bridge useful and want to support its development:

- Created by tig3rmast3r, the original author of OFXR:
  [ko-fi.com/tig3rmast3r](https://ko-fi.com/tig3rmast3r)
- 0.2.X version maintained by Djules:
  [ko-fi.com/djules](https://ko-fi.com/djules)
