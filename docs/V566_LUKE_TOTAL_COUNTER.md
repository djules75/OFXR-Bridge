# V566: restore Luke real/generated counters and correct the total

Local candidate from V565 b9a860ba. Owner rejects V565: all meter rates N/A.
Owner clarifies that real/generated values during OFXR looked correct; only
its total needed correction. Preserve those established OFXR counters.

## Actual failing run

Frozen V565 Crimson PID26432: 7735 unique submission probes, 682 matched and
7053 unmatched. Of the unmatched calls, 6954 had both valid OpenVR probes but
no synchronous current-record change. A 64-record window could never obtain
complete source attribution. A changed current record alone also cannot prove
that it belongs to this specific OpenXR submission. The V565 measurement is
withdrawn from the live meter, not relaxed into an assumed presentation.

## Measurement boundary

Luke total is explicitly labelled **New submitted FPS** in desktop/F10 and
Elliott meter. It counts distinct image identities successfully accepted by
nonempty downstream xrEndFrame, over an exact rolling one-second window.
Terminal AFW production serials identify held pairs; raw identity uses the
already verified CPU source reader. Repeating an identity never adds FPS.
Native generated outputs are admitted only after existing nonblocking GPU
confirmation, retaining their original successful acceptance time and output
index. Failed, empty, held and unconfirmed submissions never add images.

Real submitted / Gen submitted keep the pre-V565 OFXR counters. AFW-only/raw
held-source counts use identities because an XR call is not a new game image.
Unknown source identity affects only the total; it cannot erase the known
real/generated counters. Unsupported raw identity layouts may still have an
unknown total. No guess from compositor rate, refresh, or generated multiplier.
All detected Luke paths share this accounting; it is not tied to SteamVR and
works without diagnostic logging. The small number uses the same total.

**Accepted delivery is not physical presentation.** New source enum13 records
this boundary. Presentation validity remains false for this source; separate
output validity allows the meter and benchmark to consume accepted unique
images without calling them displayed images. Existing compositor delivery
still measures compositor records for other paths. The runtime may drop an
accepted image; no one-to-one content/physical scanout attribution is claimed.
This corrects both V564 repeated-scene inflation and V565 permanent N/A without
pretending to have solved the compositor observability limit.

No pacing, generation, image processing, swapchain, guide or AFW shader change.
V565 per-EndFrame OpenVR probe calls removed from the meter hot path. All
existing source stamps retained. Luke suite stays18 trials, no AFW-off pass.
CSV keeps six metric rows and actual V566 build. Owner adapter unchanged/local.

## Focused validation and next trial

Test the exact asynchronous/absent timing interface case through D3D11/D3D12
composition wrappers, failed-send admission, repeat exclusion, confirmed native
results, preserved real/generated counts and metric resets. CPU model tests
cover90 sends /25real+25generated+40holds ->50 new images and47 source frames
resubmitted90times ->47. Run the existing benchmark CSV/source/timing tests.
Headset result remains pending. Short F9 run with AFW-only and one OFXR is
sufficient; no new F8 required and no full benchmark needed before verdict.
