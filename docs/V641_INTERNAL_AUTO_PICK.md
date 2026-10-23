# V641 internal selection policy

Internal laboratory notes. Do not reproduce these formulas in product UI,
quick-start help or public-facing benchmark descriptions.

Changes on V640:

- Remove both selection-policy explanation paragraphs from Display. Keep the
  Auto pick best one checkbox, measured completion result and operational
  start/stop/restore guidance.
- Exclude requested 3X candidates when any eligible non-3X candidate reaches
  at least 66% of its measured display Hz. Apply this after invalid/FidelityFX
  and Luke resolution exclusions, before performance ranking. Baseline Off is
  eligible. Failed measurements cannot block 3X. Unknown refresh does not
  authorize choosing 3X. Trial execution/counts/CSV layout stay unchanged.
- Preserve the ordinary global 1 FPS group. If that group includes optical
  results, native-vector candidates may enter from as much as 3 FPS below the
  best optical median. Their latency must be no more than 12 ms above the
  lowest optical median in the ordinary group. Select among qualifying vectors
  using the existing 1 FPS / 2 ms / stability rules. This widened tolerance
  applies only across optical/vector classes, not within the vector class.
- All existing Luke quality/AFW-only priorities, FidelityFX exclusion, actual
  vector-confirmation requirement and error/manual-edit restoration remain.

Requested 3X is copied from the actually tested settings into the private
score. A discarded 3X result cannot re-enter via vector preference. No new
per-frame sampling, rendering, AFW, delivery or runtime pacing changes.
IPC/discovery64101, provider ABI45601, PureDark beta6 unchanged.

Tests cover exact 66% at90/120Hz, below-threshold permission, failed/unknown
measurements, 3 FPS /12 ms boundaries, no vector-class tolerance drift, no
3X re-entry, public UI omission and complete automatic profile application.
Headset acceptance remains pending; evidence in V641_VALIDATION.json.
