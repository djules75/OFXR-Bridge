# OFXR Bridge

OFXR Bridge is an experimental OpenXR API layer that inserts an optical-flow
generated frame between two rendered frames.

Current release: **v0.2.4 (internal build V312)**.
See the [release notes](docs/releases/0.2.4.md).

> [!WARNING]
> This is experimental software. It may not work with your game, VR mod, GPU or OpenXR
> runtime. It may produce visual artifacts, fail to activate, freeze the game
> or cause a crash. Use it at your own risk.

> [!IMPORTANT]
> The NVIDIA backend requires an NVIDIA Turing-generation GPU or newer with
> Optical Flow hardware support. TU117-based cards, including the GTX 1650,
> are not supported. RTX 20/30/40-series cards and GTX 1660-family cards are
> supported with a compatible NVIDIA driver. Older Pascal cards such as the
> GTX 10 series are not supported. FidelityFX remains available on other GPUs.

> [!TIP]
> A typical real-world result is a **30–50% frame-rate increase** when using
> FidelityFX, or NVIDIA Medium with optical flow resolution at 50%. Actual results
> vary by game, GPU, resolution and base frame rate.

The current build provides:

- AMD FidelityFX Optical Flow (default)
- NVIDIA Optical Flow with Fast (test), Medium and Slow presets
- 100%, 75% and 50% NVIDIA optical-flow calculation scales
- a tray icon that arms the bridge as soon as it starts, with manual
  Arm/Disarm
- **Prefer FPS over latency** (on by default): one frame of extra latency in
  exchange for reaching full frame rate from half, with smoother dips
- a pipeline built specifically for SteamVR's compositor
- an optional transparent in-headset FPS number with four corner positions;
  green means recent synthetic submissions and red means inactive generation
- an optional bridge flight recorder for diagnostics

OFXR Bridge uses color-only optical flow. It does not receive game motion
vectors or depth, so artifacts around moving objects, disocclusions and head
rotation are still possible.

## Installation and use

1. Download the latest release archive from GitHub Releases.
2. Extract the complete archive to a writable folder.
3. Run `OFXRBridgeTray.exe`. The bridge arms itself straight away.
4. Right-click the tray icon to choose the optical-flow backend and options.
   Most options take effect the next time the game starts.
5. Start the game normally. For injectors such as UEVR, start the tray before
   the game and leave it armed while the VR mod is injected.
6. Select **Disarm bridge** or close the tray application when finished.

If arming fails at start-up, the tray shows the reason and stays disarmed;
select **Arm bridge until manual disarm** to retry.

For supported NVIDIA GPUs, the suggested starting configuration is **NVIDIA
Medium** with **50% optical flow resolution**. It should provide a decent
performance boost with minimal visual-quality loss. Running the optical flow at
100% resolution is usually too expensive and often produces only a small or
negligible net performance gain, so it is not recommended for normal use.

> [!NOTE]
> Some FPS counters, including xrFPS in certain setups, measure the original
> application frames upstream of OFXR. While frame generation is active, they
> may therefore display roughly half the frames actually being submitted to
> the headset. This does not necessarily mean that OFXR is inactive.

The bridge's own optional FPS number counts accepted nonempty OpenXR
submissions. It is a diagnostic indicator rather than proof of physical headset
scanout; see [FPS overlay details](docs/FPS_OVERLAY.md).

When both the **Bridge flight recorder** and an FPS overlay position are
enabled, OFXR draws a small purple rectangle into synthetic frames near the FPS
counter. Its purpose is to verify whether generated frames are actually
reaching the headset: if the rectangle is visible there, the synthetic output
has reached the displayed presentation path. The green FPS number alone only
confirms accepted submissions. Disable the flight recorder after testing to
remove the marker.

The tray and bridge require the [Microsoft Visual C++ Redistributable
x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
OFXR Bridge does not replace your active OpenXR runtime.

FidelityFX is the most performing one but will produce artifacts during headset rotation in dark areas, this is known and cannot be avoided.

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

The change applies the next time the game starts.

### SteamVR users

SteamVR headsets (Pimax and others) get a pipeline built specifically for
SteamVR's compositor. The bridge sends two frames for every game frame, and
they must arrive one display refresh apart. Other runtimes, such as Virtual
Desktop, pace this by themselves. SteamVR does not, so the bridge times each
frame against SteamVR's compositor directly. For D3D12 games, the bridge also
gives SteamVR a GPU queue of its own, so the game's next frame can no longer
make a finished generated frame look unready.

For the best results on SteamVR:

- Leave **Prefer FPS over latency** on, then try the same scene with it off.
  If you still hold full frame rate without it, keep it off for one frame
  less latency.
- Pick a refresh rate close to double your game's frame rate. If double is
  still short of the refresh rate, SteamVR fills the gap with repeated frames
  and the image judders.
- If you get dips, disarm the bridge and play the same scene. If the dips
  remain, lower SteamVR's per-eye resolution; the bridge cannot recover
  frames the game does not render.

### What the tray changes on your PC

When the tray arms the bridge (at start-up, or when you select **Arm**), it copies the versioned OFXR layer and its
configuration into `%LOCALAPPDATA%\OFXR Bridge`, creates an absolute-path
OpenXR implicit-layer manifest and registers that manifest for the current
Windows user. It does not inject a DLL into the game, replace game files or
replace the active OpenXR runtime.

Selecting **Disarm** removes the exact OpenXR registration. Closing the tray
also disarms it, with a watchdog providing cleanup if the tray exits
unexpectedly. After disarming and closing the application, no active OFXR hook
or OpenXR registration remains on the system. Versioned cache files, settings
and diagnostic logs may remain under `%LOCALAPPDATA%\OFXR Bridge`, but they are
inert and may be deleted manually at any time.

## Reporting problems

Please report both working and non-working games, rendering problems, freezes
and crashes in [GitHub Issues](https://github.com/tig3rmast3r/OFXR-Bridge/issues)
or on the [Flat2VR Modding Discord](https://discord.gg/flat2vr).

Reported results are collected in the
[OFXR Bridge Compatibility Chart](https://docs.google.com/spreadsheets/d/1lhaJm1wzt29exmx4tZbxdwf82RcrlLcyZJ850GcTf1w/edit?usp=sharing).

Before reproducing a problem:

1. Right-click the tray icon and enable **Bridge flight recorder**.
2. Start the game and reproduce the problem once.
3. Close the game, then select **Open bridge logs** from the tray.
4. Attach the newest `ofxr-bridge-flight-*.log` file to the issue.
5. Make sure OFXR has worked on your system on at least another game before claiming that is not working for the game you are reporting

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

If you find OFXR Bridge useful and want to support its development, you can
[support the project on Ko-fi](https://ko-fi.com/tig3rmast3r).
