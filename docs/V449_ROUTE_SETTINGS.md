# V449 route preferences and live logging

V448 used one flat set of generation preferences. FSR and DLSS-G forced that
same motion-source field to game vectors, and the djules/DLSS-G multiplier
controls edited the same boolean. Returning to OFXR therefore retained the
wrong route's values. Two real ImGui regressions fail on the V448 behavior,
then pass after this correction, using both desktop and headset contexts.

V449 stores six separate preference banks: OFXR FidelityFX, NVIDIA reference,
NVIDIA new, NVIDIA djules, FSR and DLSS-G. Each owns motion source, flow
quality/resolution/direction, Lower VRAM, Prefer FPS, multiplier and generated
resolution. Switching commits the outgoing edits and restores the target bank.
FSR and DLSS-G still require game guides; their requirement does not alter OFXR.
Enable frame generation remains the common master switch. Luke, NR and Display
keep their existing separate controls; logging remains global to the session.

All banks persist as route sections in autonomous.ini. The old flat profile
migrates its selected route's explicit values; other routes start at their own
defaults (optical flow for OFXR, 2X, Lower VRAM off). Values already overwritten
by an old release cannot be reconstructed. Modern route sections take priority
over redundant active flat fields. Both clients merge per-field changes into
the original route bank, so an edit from a stale headset draft cannot overwrite
the newly selected desktop route's preferences. The shared mapping has a new
V449 identity and ABI 44901; use the matched V449 tray and layer together.

The inherited djules flight logger already supports a live INI switch. The
standalone IPC writes that switch during xrEndFrame. V449 removes the incorrect
restart instruction and publishes the actual logger-enabled state, distinct
from the requested checkbox. Its bounded poll and stable-setting observation
can take roughly half a second. Enabling starts a new distinct log and restores
cached startup metadata; disabling drains the asynchronous writer. GPU
instrumentation has its own existing switch and is not changed by this control.

## Regression evidence

- Real shared ImGui desktop/headset controls reproduce OFXR motion-source loss
  and coupled DJ/DLSS-G 2X/3X before the fix, then pass after it.
- Six complete route profiles survive serialization and selection; a new
  process with a fresh shared mapping restores inactive DJ and DLSS-G choices
  from the actual persisted file.
- Stale edits remain attached to their original bank, without changing the
  active route; legacy migration and malformed inactive values are checked.
- The actual Release layer is loaded into an isolated OpenXR/GPU fixture.
  Standalone IPC toggles logging off/on/off/on/off within that same session.
  Runtime state matches each request, two distinct files retain negotiation
  metadata and record live frame calls, and generation continues.
- The existing logger concurrency/epoch tests retain producer safety across
  switches. Complete new/djules suites and package verification are recorded
  in VALIDATION.md and the package evidence directory.

V448's signed FSR API 1.1.3 provider, native guide refusal/recovery and mandatory
Luke depth-aware camera/stereo correction remain in place. This change does
not establish Returnal rotation quality; the owner's game/headset retry remains
pending. No live game installation or activation is part of candidate creation.
