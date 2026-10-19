# Hybrid: game compatibility

Hybrid uses the game's own DLSS motion vectors. Some games need a setting
changed before Hybrid can use them. The FPS overlay shows **HYBRID OK** when
it works, or **HYBRID KO (code)** when it does not.

## Games

- [Fallout 4 VR](#fallout-4-vr)
- [Skyrim VR](#skyrim-vr)

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
