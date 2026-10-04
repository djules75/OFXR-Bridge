# OFXR Bridge troubleshooting

> [!WARNING]
> **Pimax headsets: use SteamVR, not Pimax OpenXR.** Pimax Play's OpenXR
> runtime keeps an extra copy in VRAM of every image it is handed, so OFXR
> costs about **1.7 times** more on it than on SteamVR, and the copies stay
> until the game closes. We have reported it to Pimax so they can fix their
> runtime; there is nothing OFXR can do about it in the meantime. Until it is
> fixed, run your Pimax through SteamVR with
> [sboys3's CustomHeadsetOpenVR driver](https://github.com/sboys3/CustomHeadsetOpenVR)
> and make SteamVR the active OpenXR runtime (SteamVR → Settings → OpenXR).
> This matters most in MSFS 2024, which is already close to the VRAM limit
> at Pimax resolutions. Details in
> [How much VRAM OFXR uses](#how-much-vram-ofxr-uses).

## Step 1: is OFXR running in your game?

Turn on the FPS overlay: right-click the tray icon, **FPS overlay**, pick a
corner. Start the game and look at that corner in the headset.

| You see | It means | Go to |
|---|---|---|
| No number at all | OFXR is not loaded in the game | [Table A](#a-ofxr-is-not-loaded) |
| A **red** number | OFXR is loaded but not generating frames | [Table B](#b-ofxr-is-loaded-but-not-generating) |
| A **green** number | OFXR is generating frames | [Table C](#c-ofxr-works-but-it-does-not-feel-better) |

Vulkan games (No Man's Sky, for example) show no FPS number. For those, use
the flight recorder instead: tick **Bridge flight recorder** in the tray, play
for a minute, then **Open bridge logs**. If no new log file appears, OFXR is
not loaded (Table A).

## A. OFXR is not loaded

| Cause | What to do |
|---|---|
| The game uses **OpenVR**, not OpenXR | OFXR only works with OpenXR games. Some OpenVR games can run through OpenXR with [OpenComposite](https://gitlab.com/znixian/OpenOVR). |
| The game runs **as administrator** | Run the game, and Steam or its launcher, normally. Never as administrator. |
| The tray was not armed when the game started | Start the tray first; it arms by itself. Then start the game. If you disarm and re-arm, restart the game. |
| The game runs under another Windows user | Run the tray and the game under the same Windows account. |
| The game is 32-bit | Not supported. |
| The game's anti-cheat blocks it | Not supported. |

## B. OFXR is loaded but not generating

| Cause | What to do |
|---|---|
| You are in a menu or loading screen | Normal. Generation starts once you are in the 3D world. |
| The game uses OpenGL | Not supported. |
| Too many swapchains for SteamVR (some UEVR games, often with depth submission on) | Turn off **Prefer FPS over latency**, or turn off depth submission in UEVR. |
| You disarmed OFXR from the tray | Arm it again, then restart the game. |
| Something else | Send a flight log (see below). |

## C. OFXR works, but it does not feel better

| Cause | What to do |
|---|---|
| The game already runs near your headset's refresh rate | Nothing to gain: OFXR helps games that can't hold full rate. Raise resolution or settings until the game sits around half your refresh rate. |
| The game runs **below half** your refresh rate (under 45 FPS at 90 Hz) | Turn on **3X Frame Gen** in the tray (for games around 30 FPS at 90 Hz), lower the refresh rate (72 Hz needs 36 FPS), or lower settings until the game holds half. |
| **3X Frame Gen** is on for a game that can hold half your refresh rate | Turn 3X off: it holds the game to a third of the refresh rate, and 2X looks better. |
| Your VR software's own frame smoothing is on | Turn it off, it fights OFXR: **SteamVR** Motion Smoothing and "fixed frame rate at half" (per-application video settings), **Virtual Desktop** SSW, **Pimax Play** Smart Smoothing, **Pico Streaming Assistant** Frame Interpolation. |
| Your GPU runs out of VRAM | OFXR needs extra video memory on top of the game (see [How much VRAM OFXR uses](#how-much-vram-ofxr-uses)). Signs: stutter, sudden drops, or a crash, often after a few minutes. Lower the game's settings or the per-eye resolution first. |
| 3X doesn't switch while you play | The game was started with **Prefer FPS over latency** off. Restart the game. |
| Dips only in heavy scenes | Disarm OFXR and play the same scene. If the dips are still there, lower the per-eye resolution: OFXR can't make up frames the game doesn't render. |

## How much VRAM OFXR uses

The bridge needs extra video memory on top of what the game uses. How much
depends on your per-eye resolution, the game's graphics API, and whether
**Prefer FPS over latency** or **3X Frame Gen** is on. The optical-flow
backend makes no real difference: NVIDIA and FidelityFX are within 0.01 GB of
each other.

Total extra VRAM, both eyes:

| Per-eye resolution | D3D12, 2X | D3D12, 2X Prefer FPS or 3X | D3D11, 2X | D3D11, 2X Prefer FPS or 3X | Vulkan, 2X | Vulkan, 2X Prefer FPS or 3X |
|---|---|---|---|---|---|---|
| 2064×2208 (4.6 Mpx, Quest 3 class) | 0.42 GB | 0.52 GB | 0.52 GB | 0.63 GB | 0.83 GB | 1.03 GB |
| 3030×2971 (9.0 Mpx) | 0.83 GB | 1.04 GB | 1.04 GB | 1.24 GB | 1.64 GB | 2.04 GB |
| 4172×3268 (13.6 Mpx, Crystal Super) | 1.26 GB | 1.57 GB | 1.57 GB | 1.87 GB | 2.48 GB | 3.09 GB |
| 5040×3948 (19.9 Mpx) | 1.84 GB | 2.29 GB | 2.29 GB | 2.73 GB | 3.62 GB | 4.51 GB |
| 5884×4608 (27.1 Mpx) | 2.51 GB | 3.12 GB | 3.12 GB | 3.72 GB | 4.94 GB | 6.15 GB |

"2X" alone means **Prefer FPS over latency** off. D3D11 means through the
D3D11 bridge, which is on by default. The figures are worked out from what
the bridge allocates and assume a game rendering 8-bit colour; a game
rendering 16-bit colour roughly doubles them. Measured in MSFS 2024 on
SteamVR they hold within 10%. Above the table, count about **0.12 GB per
megapixel per eye** (0.09 GB with Prefer FPS over latency off).

> [!WARNING]
> **Pimax OpenXR costs about 1.7 times the table.** Pimax Play's runtime
> keeps an extra copy of every image it is handed, made the first time the
> image is shown and kept until the game closes, so each of OFXR's private
> images costs twice on it: measured **7.0 GB against 4.2 GB on SteamVR** at
> the same 29 megapixels per eye. SteamVR does not do this; the other
> runtimes have not been measured yet. We have reported it to Pimax so they
> can fix their runtime, and there is nothing OFXR can do about it for now:
> run your Pimax through SteamVR with
> [sboys3's CustomHeadsetOpenVR driver](https://github.com/sboys3/CustomHeadsetOpenVR)
> instead, especially for MSFS 2024.

**Know your real per-eye resolution.** It is what your VR software asks the
game to render, not the headset's panel and not the "100%" figure. In
SteamVR the global resolution and a per-application resolution **multiply**:
200% global with 199% for the game gave 8192×6412 per eye on a Crystal
Super, four times the 100% figure, and OFXR paid for all of it. The flight
log says what was really asked: the `view_configuration` record near the
top holds the per-eye size.

If you run out of VRAM, lower the per-eye resolution first; it moves every
column of this table and the game's own usage with it. Turning **Prefer FPS
over latency** off also saves about 20%, but costs the smoothness it buys.
Since V408, OFXR stays out of **Pimax Home**, which takes the headset
whenever a game leaves VR and used to hold OFXR's buffers at full resolution
the whole time the game was out of VR; earlier versions let it.

## Still stuck? Send a flight log

1. Tick **Bridge flight recorder** in the tray.
2. Start the game, reproduce the problem, play for a minute or two.
3. Use **Open bridge logs** and send the newest file, with your headset, VR
   software (SteamVR, Pimax Play, Virtual Desktop...) and the game's name, on
   [GitHub Issues](https://github.com/djules75/OFXR-Bridge/issues) or Discord.

The recorder keeps only the last part of a long session. Start the game with
the recorder already on, and reproduce the problem early.
