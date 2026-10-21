# Game compatibility

What to know, and what to change, for OFXR to work well in specific games.
Hybrid uses the game's own DLSS motion vectors, and some games need a
setting changed before Hybrid can use them: with Hybrid on, the FPS overlay
shows **HYBRID OK** when it works, or **HYBRID KO (code)** when it does not.
mbucchia's [Quad-Views-Foveated](https://github.com/mbucchia/Quad-Views-Foveated)
layer breaks Hybrid: turn it off to use Hybrid.

## Tested games

Games the developer has run with OFXR. Other OpenXR games may work too: the
[OFXR Bridge Compatibility Chart](https://docs.google.com/spreadsheets/d/1lhaJm1wzt29exmx4tZbxdwf82RcrlLcyZJ850GcTf1w/edit?usp=sharing)
collects players' reports.

| Game | How it runs in VR | Hybrid |
|---|---|---|
| Microsoft Flight Simulator 2024 | Native OpenXR | Yes, [settings below](#microsoft-flight-simulator-2024) |
| Star Citizen | Native OpenXR | Yes, [settings below](#star-citizen) |
| DCS World | Native OpenXR | Yes |
| No Man's Sky | OpenComposite | Yes |
| Hogwarts Legacy | UEVR | Yes |
| Returnal | UEVR | Yes |
| Atomic Heart | UEVR | Yes |
| Dead Island 2 | UEVR | Yes |
| The Callisto Protocol | UEVR | No: the game has no DLSS |
| Cyberpunk 2077 | R.E.A.L. VR | Yes, [2X only with AERv2](#real-vr-games) |
| Ghost of Tsushima | R.E.A.L. VR | Yes, [2X only with AERv2](#real-vr-games) |
| Kingdom Come: Deliverance | R.E.A.L. VR | Yes, [2X only with AERv2](#real-vr-games) |
| Avatar: Frontiers of Pandora | R.E.A.L. VR | Yes, [2X only with AERv2](#real-vr-games) |
| The Last of Us Part I | R.E.A.L. VR | Yes, [2X only with AERv2](#real-vr-games) |
| Skyrim VR | OpenComposite | With a DLSS mod, [settings below](#skyrim-vr) |
| Elite Dangerous | OpenComposite | Yes, with the [EDVR mod](https://github.com/characterecho-sean/edvr-unofficial-patch), which adds DLSS |
| Assetto Corsa (CSP) | Native OpenXR | No: CSP turns DLSS off in VR |

## Games

- [Fallout 4 VR](#fallout-4-vr)
- [Microsoft Flight Simulator 2024](#microsoft-flight-simulator-2024)
- [R.E.A.L. VR games](#real-vr-games)
- [Skyrim VR](#skyrim-vr)
- [Star Citizen](#star-citizen)

---

## Fallout 4 VR

**Upscaler:** PureDark's Fallout 4 Upscaler, the same mod as in Skyrim VR.

**Issue:** its default foveated scale (0.8 × 0.6) runs DLSS on the centre of
each eye only, so Hybrid cannot use it (**HYBRID KO (828)**).

**Fix:** in `F4SE\Plugins\Fallout4Upscaler.ini`, set:

```ini
mFoveatedScaleX = 1.000000
mFoveatedScaleY = 1.000000
```

Close the game before editing the file. Not yet tested with Hybrid.

---

## Microsoft Flight Simulator 2024

**Hybrid:** works with DLSS, including with foveated rendering. No setting
to change.

**AutoFPS:** [MSFS AutoFPS](https://github.com/ResetXPDR/MSFS_AutoFPS) reads
OFXR's live state, so it knows when frame generation is running and at which
multiplier, and adjusts the simulator's detail to keep your frame rate at
its maximum.

**At high headset resolutions,** keep some GPU headroom for the generated
frames. In the tray:

- **Optical flow backend:** FidelityFX (the default).
- **Optical flow resolution:** 25%.
- **Hybrid: depth search:** off (the default).

---

## R.E.A.L. VR games

**Mod:** Luke Ross's R.E.A.L. VR, for example in Cyberpunk 2077, Ghost of
Tsushima, Kingdom Come: Deliverance or Avatar: Frontiers of Pandora.

**Hybrid:** works.

**AERv2:** in the mod's settings, choose **AERv2** as the rendering mode,
not AERv2 1/2 or AERv2 1/3. The setting is only shown in expert mode: close
the game, add this line to `RealVR.ini`, then start the game again:

```ini
ExpertMode=1
```

**2X only:** the mod draws one eye per game frame and the other from an
older head position, so 3X's two generated frames between game frames
judder as soon as you move your head. 2X runs clean. OFXR switches to 2X by
itself when it sees R.E.A.L. VR, and the tray tells you why; 3X can be
chosen again for other games.

---

## Skyrim VR

**Upscaler:** PureDark's Skyrim Upscaler, for example the FUS list's
"DLSS (Nvidia RTX)" option.

**Issue:** its foveated upscaling runs DLSS on the centre of each eye only
(FUS sets a scale of 0.5). The DLSS image is then smaller than the eye image,
so Hybrid cannot use it (**HYBRID KO (828)**).

**Fix:** in `SKSE\Plugins\SkyrimUpscaler.ini`, set:

```ini
mFoveatedScaleX = 1.000000
mFoveatedScaleY = 1.000000
```

Close the game before editing: the mod saves this file when the game exits.
With Mod Organizer, the file is in the upscaler's own mod folder. Leave
`mFoveatedOffsetX/Y` as they are.

**Also:**

- Choose the DLSS option, not DLAA: DLAA does not start at Skyrim VR's
  resolution (**HYBRID KO (821)**).
- No DLSS mod enabled at all gives **HYBRID KO (820)**.

---

## Star Citizen

**Easy Anti-Cheat:** OFXR loads in Star Citizen because its files are signed.
Using it with an anti-cheat protected game is at your own risk.

**Best settings for frame rate:**

- **Frame Gen multiplier:** 3X.
- **Hybrid:** on.
- **Hybrid: depth search:** on.

In 2X, depth search costs more than it gives back. In 3X it removes a lot of
the artifacts the two generated frames would otherwise show, so it is worth
its cost there.

Start OFXR before the game, so Hybrid is ready when Star Citizen sets up DLSS.
