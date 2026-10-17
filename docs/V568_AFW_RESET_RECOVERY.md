# V568: fresh AFW provider state across producer and output resets

Parent: sealed V567 b7de5812d0bb380716ab489c738f9ee5982815f8. Native V566
meter and18trial benchmark, V567 OptiScaler delegation retained unchanged.
Owner reports corrupted images after leaving the DOOM game menu and during
an automatic switch late in Crimson's benchmark; Crimson required no menu.
This concerns game content changes, not closing the OFXR F10 surface.

## Evidence and reproduction

Owner source CSVs: Crimson V566 run5668990958264 PID15032, DOOM V567
run5807423510584 PID22584. Both have18complete trials,21FPS samples per trial,
leading3seconds discarded. Local owner-sheet adapter maps S18:BF18 and S24:BF24.
Source/flight logs frozen locally with hashes. Full flight logs retain later
segments (Crimson detailed segment starts around576s; DOOM around643s); startup
metadata lists earlier benchmark changes, but these logs do not show the
actual corrupted image or establish its exact first frame. No false claim
that a game screenshot was reconstructed from telemetry.

Detailed DOOM segment contains producer resets and reduced-quality Combined
falling back to Alternate, then resuming Combined. Code reset the origin and
called InitFrameWarp again on an already warmed beta6 renderer. Beta6 has
module-global lookup state, which that call does not fully reset.

Controlled reproduction uses the shipping AFW implementation, its real shaders
and byte-identical selected beta6 provider on an actual D3D12 GPU. Change the
producer epoch mid-sequence, admit new native history, then resume Combined at
2x2. Before this fix the D3D12 debug layer reports on frame13:
ClearRenderTargetView uses m_otherEyeLookupDesc in shader-read state0xC0,
but requires render-target state0x4. The1x1/other-rate cases cannot be assumed
safe from a single pass; test0/4/8 explicitly. This reproduces a concrete
provider-state fault consistent with the report, not proof of the owner's
exact visual incident.

## Focused fix

After the existing provider completion fence has retired previous GPU work,
a warmed provider crossing a producer-origin/lifetime reset or a color-output
geometry change is replaced with a fresh private byte-identical provider
module. This is the same isolated recreation already used on quality changes.
Retain the newly established camera origin, calibrated units and producer key;
new provider lookup resources and temporal history start together. Failure
returns an error through existing real-frame fallback; never publishes a
partial new context. Diagnostics1045producerreset,1046geometry;1044quality stays.
Cold initialization and steady rendering do not gain a recreation or new wait.

No changes to depth subtraction, motion interval, eye ownership, source capture,
provider binaries/ABI, quality settings, OFXR routes, pacing, meters, menus,
swapchain counts, benchmark mapping or controls. This is the Autonomous AFW
consumer path only; delegated OptiScaler rendering is unchanged.

## Validation

Extend the existing independent rotated-plane reconstruction test with a new
producer epoch and separately a full-to-odd-output geometry transition at
1x1/2x2/4x4, using the real bundled provider and D3D12 state validation. These
exercise all3AFW modes, both native intervals and full/odd raster fixtures.
Existing live quality0->4->8->0, interleaved guides, terminal ownership and
post-AFW packets are retained. See V568_VALIDATION.json for final check counts.
Headset/menu recovery remains owner-pending; no new F8 is required before the
first verdict. Re-enter the game menu at AFW2x2/4x4 and retry automatic switches.
