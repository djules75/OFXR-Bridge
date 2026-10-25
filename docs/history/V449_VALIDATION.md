# V449 validation

Windows x64 MSVC optimized Release candidate, 2026-10-10, based on sealed
V448 `16835e045ea3155dbd4ae657aebefc2856e59359`. Six independent generation
preference banks and live logging status fix the owner's motion-source and
multiplier persistence reports. Details: docs/V449_ROUTE_SETTINGS.md.

## Direct regressions

Both real shared ImGui regressions fail before the fix, then pass after it:
returning to OFXR from FSR/DLSS-G restores Optical flow; DJ and DLSS-G keep
independent 2X/3X preferences across desktop/headset changes. The complete
widget executable passes 306 checks. Control tests pass 61 checks including
all six banks, malformed inactive values, legacy migration, stale route edits,
actual persisted file and fresh-process/fresh-mapping initialization.

The actual Release layer is loaded into an isolated OpenXR/GPU fixture.
Standalone IPC switches logging off/on/off/on/off in the same session. Its
actual logger state matches each revision, distinct files retain negotiation
metadata and record current frame calls; generation continues. Existing logger
producer/epoch/flush tests pass. No game or live runtime is needed for this test.

## Complete final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 132/132; none skipped | 83.64 s |
| djules | 149/149; none skipped | 89.09 s |

All seven shipped runtime binaries match across both fixture selections. The
new fixture configuration is restored before packaging. Complete verbose logs,
runtime hashes and before/after regression evidence accompany the package.
V448's signed FSR provider API 1.1.3 is retained byte for byte. The suites retain
stereo/packed FSR generation, guide refusal/recovery, output/paused-route changes,
private GPU ordering, native DLSS-G on RTX 4090, Streamline ABI fixtures,
mandatory depth-aware Luke correction and menu reopening, without skips.

Early complete new suites failed two FSR inline output-count checks. Both failures
also reproduced using the unchanged V448 test executable. The same V449 runtime
binaries subsequently passed both checks three consecutive times and the full
final suites; no FSR assertion, shader, parameter or scheduling rule was relaxed.
Count/statistics/per-frame failure diagnostics were added to the fixture. Early failure,
V448 comparison and stability logs are preserved. A first djules run also failed six GPU timing/output checks; all six passed
on an unchanged rerun before the final full suite. Concurrent game presence
was observed; GPU contention is a possible influence, not a proved per-frame
failure cause. These earlier failures remain part of the validation record.

Import resolution, extracted checksums, all prior catalogue hashes and the
packaged split-eye loader probe are checked by the numbered package tools.
Their separate receipt binds the package to its clean source commit. V448's
validation is preserved byte for byte under docs/history/.

## Limits

The owner game/headset check of V449 preferences and logging is pending.
Legacy files preserve their selected route; earlier overwritten inactive values
cannot be recovered. Use the matched V449 tray/layer (IPC ABI 44901). Returnal
FSR rotation quality remains pending; automated tests do not certify visuals
or performance. External NR model and late first-NGX discovery limits remain.

No live DLL installation, tray activation, registration, game launch, canonical
port, push or publication occurred. Prior numbered source/checkpoint/packages
and relocation receipts remain preserved.
