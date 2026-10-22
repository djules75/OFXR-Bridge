# V635: Horizon AFW source admission

## Observed loss

The owner's V634 Horizon Quest/VDXR Legacy run, PID15568, retains about the
last 34 seconds of detailed flight events. There are 1157 AFW evaluations:
1110 Combined and 47 Alternate re-primes. Across 26 publication discontinuities,
27 source publications are absent from AFW. Every discontinuity follows a
committed XR source capture rejected by the Horizon cross-eye render-counter
filter as a duplicate. No new F8 or saved pixel of the flash is available.

The first example is exact: publication 557 / eye 0 was evaluated. The other
eye is then captured with Luke render counter 5316, equal to the globally last
admitted render. xrEndFrame discards that call. At the next admission the actual
CRC-decoded eye colors contain tokens 559 and 558, but only newest 559 is used.
The V634 protection correctly re-primes 559 and 560; it cannot recover the
already discarded 558. The other 25 discontinuities follow the same captured
release / duplicate-filter pattern. This establishes a false admission loss;
it does not establish that every perceived flash has this single cause.

## Narrow change

Inside the existing pinned Horizon source filter only, an AFW-selected cycle
with a committed fresh projection-eye capture may proceed despite an equal
global render counter. The downstream CRC color-origin resolution, publication
freshness and eye ambiguity checks remain authoritative and unchanged. Missing
identities, invalid eyes and cycles without a fresh capture remain duplicates.
AFW Off retains the original filter exactly. Other games do not enter this
Horizon-specific filter and are unchanged.

Per-eye release filtering, captured-color ownership, depth/camera subtraction,
native N-2 selection, provider, V634 stereo re-prime, pacing, metrics, swapchains
and thresholds are unchanged. No readback, GPU copy, allocation or wait is added.
Existing work for a genuinely fresh capture now reaches AFW rather than being
discarded before its color-token check.

## Verification and limits

The origin GPU test checks a fresh color-carried token through the corrected
admission policy, while retaining duplicate and ambiguous token rejection.
CPU controls cover both eyes, AFW Off, no fresh release, invalid eye, missing
identity and ordinary new raw source. The twelve focused checks also retain
Horizon/Ghost SteamVR and VDXR ingress, Crimson/KCD, motion geometry, depth GPU,
real-provider continuity and bundled AFW quality controls.

The source admission loss is supported by the flight trace; this is not a
replay of the missing pixels. Owner headset confirmation is pending. No further
sporadic-flash F8 is required to try this candidate. Exact first bad revision
remains unknown. Candidate stays local; no live install or push.
