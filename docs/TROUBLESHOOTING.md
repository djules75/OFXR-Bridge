# OFXR Bridge troubleshooting

> [!IMPORTANT]
> **First, make sure you are on the latest version: 0.2.14.1.** Every
> release fixes problems reported here, so check the
> [releases page](https://github.com/djules75/OFXR-Bridge/releases) before
> anything else. The tray's menu shows the version you are running.

> [!IMPORTANT]
> **Start OFXR before the game.** Run the tray first, so OFXR is armed when
> the game starts; if you arm it while a game is running, restart the game.
> This matters most for **Hybrid**: OFXR has to be running when the game
> sets up DLSS.

> [!TIP]
> **Pimax headsets:** OFXR uses less VRAM through SteamVR with
> [sboys3's native SteamVR driver](https://store.pimax.com/blogs/blogs/sboys3-native-steamvr-driver-setup-guide)
> than through Pimax OpenXR, so we recommend it, especially in MSFS 2024.
> [Details](#how-much-vram-ofxr-uses).

## Low frame rate or stutter?

Try, in this order ([what these settings do](#optical-flow-settings)):

| Step | Change |
|---|---|
| 1 | **Optical flow backend** FidelityFX |
| 2 | **Optical flow resolution** 25% |
| 3 | **Hybrid: depth search** off |
| 4 | **Hybrid** off |
| 5 | Lower the per-eye resolution |

Still not working? Click **Reset to default** in the tray, and restart the
game. If the problem is still there, carry on below.

## Step 1: is OFXR running in your game?

Turn on the FPS overlay: right-click the tray icon, **FPS overlay**, pick a
corner. Start the game and look at that corner in the headset.

| You see | It means | Go to |
|---|---|---|
| No number at all | OFXR is not loaded in the game | [Table A](#a-ofxr-is-not-loaded) |
| A **red** number | OFXR is loaded but not generating frames | [Table B](#b-ofxr-is-loaded-but-not-generating) |
| A **green** number | OFXR is generating frames | **Reset to default** (above) if you have not yet, then [Table C](#c-ofxr-works-but-it-does-not-feel-better) |
| **Two orange bars** before the number | Frame generation is paused | Choose **Resume frame generation** in the tray menu, or press the pause key (Ctrl + Alt + F7 unless you changed it) |
| **HYBRID KO** in red above the number | Hybrid is on but cannot work in this game | [Table D](#d-hybrid-shows-hybrid-ko) |

## A. OFXR is not loaded

| Cause | What to do |
|---|---|
| The game uses **OpenVR**, not OpenXR | OFXR only works with OpenXR games. Some OpenVR games can run through OpenXR with [OpenComposite](https://gitlab.com/znixian/OpenOVR). |
| The game runs **as administrator** | Run the game, and Steam or its launcher, normally. Never as administrator. |
| The tray was not armed when the game started | Start the tray first; it arms by itself. Then start the game. If you disarm and re-arm, restart the game. |
| The game runs under another Windows user | Run the tray and the game under the same Windows account. |
| The game is 32-bit | Not supported. |
| The game's anti-cheat blocks it | Star Citizen works with Easy Anti-Cheat, because OFXR is signed. Other anti-cheats may still block OFXR. Using OFXR with an anti-cheat protected game is at your own risk. |
| You play on Linux | OFXR runs under Proton: follow the [Linux guide](LINUX.md). |
| The game crashes at start | Try without ReShade and other OpenXR layers, then send a flight log (see below). |

## B. OFXR is loaded but not generating

| Cause | What to do |
|---|---|
| You are in a menu or loading screen | Normal. Generation starts once you are in the 3D world. |
| The game uses OpenGL | Not supported. |
| Too many swapchains for SteamVR (some UEVR games, often with depth submission on) | Turn off **Prefer FPS over latency**, or turn off depth submission in UEVR. |
| You disarmed OFXR from the tray | Arm it again, then restart the game. |
| You paused frame generation, from the tray or with the pause key (the FPS number shows two orange bars) | Choose **Resume frame generation** in the tray menu, or press the key again. No restart needed. |
| Generation stops and starts by itself while you play | The pause key (Ctrl + Alt + F7 by default) is also a key your game or a mod uses. Choose another under **Current key binding** in the tray menu. |
| Something else | Send a flight log (see below). |

## C. OFXR works, but it does not feel better

| Cause | What to do |
|---|---|
| The game already runs near your headset's refresh rate | Nothing to gain: OFXR helps games that can't hold full rate. Raise resolution or settings until the game sits around half your refresh rate. |
| The game runs **below half** your refresh rate (under 45 FPS at 90 Hz) | Turn on **3X Frame Gen** in the tray (for games around 30 FPS at 90 Hz), lower the refresh rate (72 Hz needs 36 FPS), or lower settings until the game holds half. |
| **3X Frame Gen** is on for a game that can hold half your refresh rate | Turn 3X off: it holds the game to a third of the refresh rate, and 2X looks better. |
| Your VR software's own frame smoothing is on | Turn it off, it fights OFXR: **SteamVR** Motion Smoothing and "fixed frame rate at half" (per-application video settings), **Meta** ASW, **Virtual Desktop** SSW, **Pimax Play** Smart Smoothing, **Pico Streaming Assistant** Frame Interpolation. |
| Your GPU runs out of VRAM | OFXR needs extra video memory on top of the game (see [How much VRAM OFXR uses](#how-much-vram-ofxr-uses)). Signs: stutter, sudden drops, or a crash, often after a few minutes. Lower the game's settings or the per-eye resolution first. |
| 3X doesn't switch while you play | The game was started with **Prefer FPS over latency** off. Restart the game. |
| 3X switches back to 2X, and the tray says the game uses R.E.A.L. VR | That mod only works in 2X. See the [game compatibility guide](GAME_COMPATIBILITY.md#real-vr-games). |
| With **Hybrid** on, the frame rate drops or moving objects look worse | Turn **Hybrid: depth search** off, then **Hybrid** off if needed. |
| You play MSFS 2024, Skyrim VR, Fallout 4 VR or a R.E.A.L. VR game | Check the [game compatibility guide](GAME_COMPATIBILITY.md) for the settings that game needs. |
| The number stops short of the refresh rate (110 to 115 at 120 Hz, for example), although the game runs above half of it without OFXR | Generating frames costs GPU time too, so the game needs some headroom above half the refresh rate. Lower the refresh rate, the per-eye resolution or the optical-flow resolution. Example: No Man's Sky ran at 94 FPS without OFXR; OFXR gave an even 100 at 100 Hz and 113 to 115 at 120 Hz, because a real frame plus a generated one took about 17.7 ms and 120 Hz allows 16.7. |
| Choppy or below half your refresh rate with **Optical flow resolution** above 50%, **NVIDIA Slow**, or **NVIDIA bidirectional consistency** on | These cost a lot of GPU time (see [Optical flow settings](#optical-flow-settings)). Click **Reset to default** in the tray and restart the game. |
| Dips only in heavy scenes | Disarm OFXR and play the same scene. If the dips are still there, lower the per-eye resolution: OFXR can't make up frames the game doesn't render. |
| The overlay shows your full refresh rate in green, but the headset looks no smoother (a very high resolution, often MSFS 2024 with 3X Frame Gen) | The GPU is short of time for the generated frames: choose **FidelityFX** and **25%** optical flow resolution, or one step down in your headset's render resolution. |
| You want to know whether a problem comes from OFXR | **Disarm** and restart the game. **Pause frame generation** is not enough: a paused game still runs through OFXR, it only stops making extra frames. |

## D. Hybrid shows HYBRID KO

The number in brackets says why. Leave **Hybrid** off in games where it
cannot work; the [game compatibility guide](GAME_COMPATIBILITY.md) has the
settings for the games we know.

mbucchia's [Quad-Views-Foveated](https://github.com/mbucchia/Quad-Views-Foveated)
layer breaks Hybrid: turn it off to use Hybrid.

| Code | Meaning | What to do |
|---|---|---|
| 702 | The game's graphics API is not supported | Leave Hybrid off. |
| 820 | The game is not using DLSS | Turn DLSS on in the game, or enable its DLSS mod. Games without DLSS cannot use Hybrid. |
| 821 | DLSS is installed but not running | Choose DLSS, not FSR or another upscaler. Some games turn DLSS off in VR (Assetto Corsa with CSP). |
| 824 | DLSS runs, but OFXR could not read it | Send a flight log. |
| 827 | OFXR read DLSS, but could not keep its data | Send a flight log. |
| 828 | The DLSS image does not match the eye image | Usually Quad-Views-Foveated, or an upscaler mod's foveated setting: turn Quad-Views-Foveated off, or see the compatibility guide. |
| 815 | Hybrid could not use DLSS's data | Send a flight log. |

## Optical flow settings

To make an in-between frame, OFXR first works out how every part of the
image moved between two game frames: the optical flow. These tray options
change how much GPU time that takes. The defaults are the best choice for
most setups.

| Setting | Default | What it does | GPU cost |
|---|---|---|---|
| **Optical flow backend** | FidelityFX | How the motion is worked out. FidelityFX's quality is similar to NVIDIA's, in particular with Hybrid on | NVIDIA costs more: about 4 ms more at very high resolutions |
| **Optical flow resolution** | 50% | How finely the motion is worked out, as a share of your per-eye resolution on each side | 100% costs about four times as much as 50%, for an image that looks almost the same; 25% is the lightest |
| **NVIDIA bidirectional consistency** | Off | Checks the motion both ways: slightly cleaner edges on moving objects | Doubles NVIDIA's optical-flow cost |
| **Hybrid** | Off | Adds the game's own DLSS motion, for cleaner moving objects (DLSS games only) | Adds GPU time to every generated frame |
| **Hybrid: depth search** | Off | Keeps thin nearby objects solid, such as cockpit frames | Adds more on top of Hybrid, more at high resolutions |

If your game has little headroom, any of these can cost more than the extra
frames give back. For example, NVIDIA Slow at 100% on a Pimax Crystal Light
at 120 Hz took about 26 ms per frame pair, three times a refresh, and played
choppier than at 50%.

Per-game advice is in the
[game compatibility guide](GAME_COMPATIBILITY.md).

## How much VRAM OFXR uses

OFXR needs some video memory on top of the game's. It grows with your
per-eye resolution; 2X, 3X and the optical-flow backend cost the same.

Extra VRAM, both eyes, on SteamVR, Virtual Desktop and the Meta runtime:

| Per-eye resolution | D3D12 | D3D11 | Vulkan |
|---|---|---|---|
| 2064×2208 (4.6 Mpx, Quest 3 class) | 0.45 GB | 0.55 GB | 0.66 GB |
| 3030×2971 (9.0 Mpx) | 0.90 GB | 1.11 GB | 1.33 GB |
| 4172×3268 (13.6 Mpx, Crystal Super) | 1.36 GB | 1.67 GB | 2.00 GB |
| 5040×3948 (19.9 Mpx) | 1.99 GB | 2.44 GB | 2.93 GB |
| 5884×4608 (27.1 Mpx) | 2.72 GB | 3.32 GB | 3.98 GB |

A game rendering 16-bit colour roughly doubles these figures.

**Lower VRAM (may cause stuttering)**, in the tray and on by default, keeps
fewer working images; the table assumes it. Turning it off costs about 15%
more, and only helps if you see stutter with VRAM to spare.

> [!WARNING]
> **Pimax OpenXR uses about half again as much**, because Pimax's runtime
> keeps its own copy of every image. If VRAM is tight, run your Pimax
> through SteamVR with
> [sboys3's native SteamVR driver](https://store.pimax.com/blogs/blogs/sboys3-native-steamvr-driver-setup-guide),
> especially for MSFS 2024.

### Know your real per-eye resolution

It is what your VR software asks the game to render, not the panel's
resolution or the "100%" figure. In SteamVR, the global resolution and the
game's own resolution **multiply**. On a Pimax Crystal Super:

| SteamVR global | Game's own setting | Pixels rendered | Per eye |
|---|---|---|---|
| 100% | 100% | 1× | about 4096×3206 |
| 200% | 100% | 2× | about 5793×4534 |
| 200% | 200% | 4× | 8192×6412 |

The `view_configuration` record near the top of a flight log shows the real
per-eye size. If you run out of VRAM, lower the per-eye resolution first: it
lowers both OFXR's usage and the game's.

## Still stuck? Send a flight log

1. Tick **Bridge flight recorder** in the tray.
2. Start the game, reproduce the problem, play for a minute or two.
3. Use **Open bridge logs** and send the newest file, with your headset, VR
   software (SteamVR, Pimax Play, Virtual Desktop...) and the game's name, on
   [GitHub Issues](https://github.com/djules75/OFXR-Bridge/issues) or Discord.

You can also tick the recorder while the game is already running: it starts
within a moment, and the log still begins with the game's start-up
information. Untick it to close the file. The game hitches once each time.

The recorder keeps only the last part of a long session, so switch it on
shortly before the problem, or reproduce the problem early.
