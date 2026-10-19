# OFXR Bridge on Linux (Proton)

OFXR Bridge runs under Proton: a Windows VR game, its OpenXR loader and the
OFXR layer all run inside Wine, and Proton's `wineopenxr` hands the frames to
SteamVR for Linux.

```
game -> OpenXR loader -> OFXR layer -> wineopenxr (Proton) -> SteamVR (Linux) -> headset
```

This was first made to work in
[issue #34](https://github.com/djules75/OFXR-Bridge/issues/34), with MSFS 2024
on Proton Experimental, SteamVR for Linux, Steam Link and a Quest 3, on an
RTX 5080. That is the only setup tested so far. The maintainer has no Proton
setup, so reports from other games, GPUs and headsets are very welcome.

## What you need

- An OFXR build with Proton support: version V536 or later. The first line
  of a flight log tells you which version ran (`op=logger result=536` or
  higher).
- A Proton version with `wineopenxr` (Proton Experimental was used in #34).
- SteamVR for Linux.
- [protontricks](https://github.com/Matoking/protontricks), to run the tray
  inside the game's Wine prefix.

## Why the tray cannot stay running

On Windows the tray stays open while you play. Under Proton it must not:
protontricks and Steam each start their own Wine server for the game's
prefix, and with the tray still running the game hangs at start-up (Steam
shows it as running, the game never appears).

Instead, `OFXRBridgeTray.exe --arm-and-exit` arms the bridge with your saved
tray settings and exits straight away. The layer stays registered in the
prefix, and the game picks it up when it starts.

## Setting it up

1. Extract the OFXR archive to a folder of your own. If you use the Flatpak
   version of protontricks, make sure it can read that folder.
2. Find the game's Steam App ID: `protontricks -l` lists your games with
   their IDs.
3. Run the tray once in the game's prefix to choose your settings:

   ```
   protontricks-launch --appid <AppID> /path/to/OFXRBridgeTray.exe
   ```

   For a first try, pick **FidelityFX** as the optical flow backend (the
   default since V532). Turn on **Bridge flight recorder** if you want a log
   to check or to report with. Then select **Exit** in the tray menu.
4. Arm without staying open:

   ```
   protontricks-launch --appid <AppID> /path/to/OFXRBridgeTray.exe --arm-and-exit
   ```

   Nothing is shown; it exits on its own. It refuses to arm while a tray is
   running, since that tray owns the registration.
5. Start the game from Steam as usual.

## Changing settings

Starting the tray normally removes the arm that `--arm-and-exit` left. To
change a setting, repeat steps 3 and 4: open the tray, change the setting,
exit it, and arm again with `--arm-and-exit`.

## What does not work under Proton

Without a running tray there is nothing to talk to the game while it plays:

- the pause hotkey and **Pause frame generation**;
- live changes in a running game (backend, Hybrid, 3X Frame Gen, the FPS
  overlay position, the flight recorder). Settings apply the next time you
  arm and start the game;
- **Disarm** while the game runs.

Not yet tested under Proton: the NVIDIA optical flow backend (it needs
dxvk-nvapi with NVIDIA's optical flow library available in the prefix),
Hybrid, and runtimes other than SteamVR.

## Checking that it works

In the headset, the FPS overlay appears (upper right by default) and the
motion gets smoother. With the flight recorder on, the log is in the game's
prefix:

```
~/.steam/steam/steamapps/compatdata/<AppID>/pfx/drive_c/users/steamuser/AppData/Local/OFXR Bridge/RuntimeLayer/v<version>/
```

In that log:

- `op=arm_control result=0 a=1` means the bridge was armed by
  `--arm-and-exit` and the layer saw it was running under Wine. That is
  the expected line.
- `synthesis_pair` records throughout the session mean frames are being
  generated.
- `generation_prepare result=17` on every frame means the layer stayed Off:
  arm again with `--arm-and-exit` and make sure no tray is running.
- `swapchain_eligibility result=3` on every swapchain was the `wineopenxr`
  problem fixed in V536; seeing it means an older build ran.
- `steamvr_delivery_attach result=-2 a=126` is harmless: OFXR cannot read
  SteamVR's frame timings from inside Wine. Generation is not affected.

## Removing it

Start the tray normally in the game's prefix (step 3) and select **Exit**.
That removes the arm, and the game starts without OFXR from then on.

## Reporting

Please post results, working or not, in
[GitHub Issues](https://github.com/djules75/OFXR-Bridge/issues) with the
game, your Proton version, GPU and driver, headset and runtime, and the
flight log from the session.
