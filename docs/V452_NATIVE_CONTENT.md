# V452 native DLSS-G content and refusal evidence

Based on sealed V451 fd374510eb062a11f147ff6299eb4ad0e090275b.

## Owner run and limits

Detnoir V451 PID34004 reports NGX Init/capability/Create success (result 1),
available=1 and generated capacity=1. The feature is 6208x3264 (two 3072x3264
eyes and a 64-pixel seam). Direct recovery is not used. Its logged tail has
157 successful synthesis pairs and 316 successful physical submissions over
4.55 s, about 69.23 Hz. It lacks per-record/evaluate and GPU disable decisions.
This proves discovery and submission, not interpolated content. Owner observes
46->66 and green while the panel reports bypass. The exact in-game refusal
cause remains unproved. Private evidence: diagnostics/v451-detnoir-pid34004.

## Content ownership and counter

A native record returning S_FALSE copies B into the synthetic output and still
returns S_OK to the physical presenter. V451 labelled that ticket as generated.
V452 retains a per-pair receipt for both record refusals and completed NGX GPU
disable-interpolation flags. A logical output is generated only if every eye's
flag is clear. CPU success alone is insufficient. The receipt caches its result
before its work-slot readback can be overwritten; queued frames retain their
own immutable decision through slot reuse and generator changes. Three 16-byte
readback resources are created with the native context; no per-frame GPU resource
allocation or CPU wait is added. Their completion fence names S, independently
of the whole-pair fence that may wait for B's deferred copy.

The counter resolves decisions after successful downstream EndFrame, and polls
bounded pending receipts without waiting. A delayed confirmation uses the original
submission timestamp; stale confirmations expire after one second. Repeated
native captures and copied S slots neither add unique content nor turn green.
B counts once. Genuine interpolation still counts. Physical slot selection,
queue/fence ownership, frame cadence, color/guide math and mandatory Luke motion
correction remain unchanged. Compositor delivery may cap unique native output
FPS but cannot inflate it with copied B slots. Existing non-native delivery
measurement is preserved. Green keeps its existing 0.5 s hysteresis.

The native status at presentation uses the receipt, including a distinct
NGX-disabled-interpolation bypass state. A native ready result cannot be replaced
by the process-global status of an unrelated auxiliary guide match.

## Ordinary live diagnostic events

No environment variable is needed in a game: enable Diagnostic logging.

- native_dlssg_record: result=Skip (0 recorded, 1 guides, 2 resize/busy,
  3 unsupported multiplier, 4 feature unavailable, 5 evaluate failed).
  a=guide publication, b=detail, c=logical eye.
- native_dlssg_evaluate: result=actual NGX code; a=reset/reseed, b=1-based
  generated output index, c=private direct provider versus core (1/0).
  Result 1 is NGX success; failure codes must be decoded as hexadecimal.
- native_dlssg_decision: result=generated output bitmask; a=publication,
  b=(eyes<<32)|outputs, c=per-eye disable bits (eye*2+output).
  Result 0 with successful evaluations means NGX disabled interpolation.

Guide details: 1 capture-only diagnostic mode, 2 missing/mismatched guide sets,
3 missing logical eye, 4 reset, 5 previous serial mismatch, 6 stream mismatch,
7 epoch mismatch, 8 depth convention change, 9 viewport size change,
11 wrong/missing motion or depth device, 12 motion rectangle/slice invalid,
13 depth rectangle/slice invalid, 14 motion format unsupported,
15 depth format unsupported, 16 invalid near/far, 17 invalid vector scale,
18 invalid jitter, 19 color viewport outside guide output, 20 repeated capture.
Detail 90 is a readback slot still owned by an unfinished prior GPU submission.
No guide validation gate is loosened to make a refused frame appear generated.

## Validation and pending game check

See VALIDATION.md for final suites and real GPU rejection/recovery/readbacks.
The test reproduces a previous-serial mismatch, verifies exact B copies and
non-generated content, then genuine native recovery and immutable slot receipts.
It also checks S's decision before B's deferred copy and real D11/D12 overlay
red pixels/counts. Existing native packed/cropped/sRGB/reseed interpolation and
late private-provider recovery remain tested. The cause of any Detnoir rejection
needs the owner's V452 run. This candidate does not certify DLSS-G in that game.
No live installation, activation, registry change, game launch or push occurs.
