# V626: Outlaws source-marker preamble recovery

Base V625: ca07e6bc1df2d6940846bb22d40f64c7e4123697. Local candidate.

The owner reports generation stopping during the benchmark and subsequently
remaining unavailable even at 1x1. The closed V625 run keeps publishing DLSS
captures; the reader's copy/signal/fence stages succeed, but marker decoding
returns ERROR_NOT_FOUND. This is distinct from the V624 warm-output retirement
fixed in V625. The first failed benchmark retained here requests 2x2 while the
owner associates the failure with a 4x4 change; requested and active quality
must not be inferred to be the same. The later F8 independently exhibits the
reader defect without requiring that transition explanation.

The later failing F8 includes eight original XR scene eye images. Replaying
its actual compact 263x12 RGBA8 readback area through the shipping V625 CPU
reader yields no token in any of those images. Independent Manchester decoding
recovers sync 0xd3a5, tokens 6098 through 6105, and matching CRCs. The images
therefore still contain exact source identity. The third XR source is a REAL VR
FPS overlay, not another scene eye. No absent NGX color dump is claimed.

The barcode overlays a nonuniform dimmed/background region. Its final sync
midpoint can merge with a background gradient and move away from the nominal
position; the old bit0/bit15 pitch estimate and mandatory 16-of-16 edge fit
then reject a still readable marker. V626 also derives pitch from bit14's
opposite-direction midpoint and permits one missing edge in the sync fit.
Every sync bit is nevertheless decoded and checked exactly afterward, along
with the complete token CRC. Conflicting tokens still reject the result. This
is geometry recovery, not partial identity acceptance or a guide fallback.

A synthetic blended-edge regression fails against V625 and passes V626.
Corrupted payload and recovered-versus-other-token conflict cases must reject.
All eight captured scene images recover the expected exact token with no
ambiguity. Captures/full logs stay local; no game pixels are committed or
bundled. The common reader applies consistently to supported Luke paths;
writer geometry and guide registry selection remain unchanged.

No GPU copies, waits, allocations or logging are added. The CPU candidate
search also examines rising sync edges; the local replay cost is recorded
separately and is not a headset performance claim. V625's output lifetime,
V624 UI/storage, all director pacing, AFW 1x1/2x2/4x4 processing and mandatory
depth/camera motion subtraction are retained. IPC62401/provider45601 unchanged.

Restart tray and Outlaws with matching V626 files. Initially check AFW at 1x1,
then 2x2 and 4x4 in the same level. A full benchmark is unnecessary until that
sequence remains engaged. Runtime acceptance remains pending; no live install,
active configuration/registry change or publication accompanies this package.
