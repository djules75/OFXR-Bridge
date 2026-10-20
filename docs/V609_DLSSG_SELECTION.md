# V609: one DLSS-G selection contract

Manual DLSS-G selection used to retain the last optical-flow backend. That
backend also selected an upstream presenter, so selecting DLSS-G after 0.3.0
could use a different queue/real-frame hold from the automatic benchmark.
The benchmark's DLSS-G steps explicitly select backend0.

V609 normalizes native DLSS-G to backend0 both when selecting it and when
sanitizing UI, INI and IPC settings. It does not select FidelityFX as the
generator: native DLSS-G still supplies the generated image. Only the inherited
OFXR presenter classification is removed. Other OFXR profiles and native
resolution, multiplier, model preset and memory preferences are preserved.

The change applies to consistent selection on all runtimes. It does not change
the SteamVR private director, VDXR timing, vectors, AFW or Neural Render.
The separate V610 director experiment is not included in this build.

Validation includes every preceding OFXR backend, saved settings, benchmark
Normal/Lower VRAM equivalence, actual shipping GPU DLSS-G transactions in2X,
Lower VRAM and3X, and the retained resource/control/runtime regression set.
See V609_VALIDATION.json for results and qualifications. Owner headset testing
remains pending; no live installation or online publication is performed.
