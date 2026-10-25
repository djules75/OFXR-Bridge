# V610: SteamVR sole-director experiment

V609's native DLSS-G selection fix is retained. This separate candidate adds
**Single director (SteamVR)** immediately below Lower VRAM, in both settings
surfaces. It is global, independent of Lower VRAM, persists as
`[autonomous] single_director=0/1` in the existing ofxr-bridge.ini, and defaults
OFF. Benchmark route changes preserve it. Restart tray and game together when
switching numbered builds: shared-control ABI61001/mappingV610 differs from609.

## One presentation clock

The flag is effective only when the session already has the private SteamVR
OpenXR endpoint. Other runtimes, including VDXR, retain their existing policies
even if the saved checkbox is ON. No VDXR private director is introduced here.
SteamVR's final private FIFO, native consumer, cadence, phase/headroom,
work-floor estimator, adaptive latency and three-output-credit limit are
unchanged. This experiment does not move those settings at the same time.

Several old route sleeps were already bypassed by the private endpoint. Simply
disabling them again would not remove the remaining upstream real-frame hold
or admission counts. V610 addresses these remaining policies:

| Framework | Flag ON |
| --- | --- |
| FidelityFX | Skip legacy presenter phase/bias schedule maintenance; release the producer on completed handoff to the private endpoint instead of virtual presenter tick counts. |
| OFXR0.2.1 | Keep the ordered S/R transfer executor and output leases; remove counted presentation admission and acknowledge the owned batch when the endpoint accepts it. No additional route timer. |
| OFXR0.2.13 | Deliver a ready real immediately instead of retaining it until the next real. Keep only an independent temporal source identity for pairing, together with the existing capture history. Skip the unused upstream schedule updates. |
| OFXR0.3.0 | Same early real delivery and independent temporal identity. The worker still generates asynchronously. Its ready descriptor slots are a bounded transfer batch, not a wait for a future application frame or another display clock. |
| Hybrid | Same handoff-only policy as FidelityFX, retaining native-guide validation and its existing optical fallback. |
| FSR | Remove counted-release admission; ordered S/R handoff acknowledges endpoint acceptance. Keep deferred current-copy ordering, FSR context/history and required GPU readiness. |
| DLSS-G | Use V609's canonical benchmark presenter, with its legacy phase/bias maintenance bypassed and producer acknowledgment tied to private endpoint acceptance. Model A/C, resolution and native generation are unchanged. |

The advanced worker retains one current generation/transfer batch until it has
entered the final FIFO, so it does not build an additional ready-pair backlog.
Its descriptor buffer no longer determines when a real is allowed to present.
In2X the output order is realA, syntheticAB, realB;3X keeps the existing two
synthetics in their original order. After a continuity reset, the next source
primes normally. Merely emptying the output queue must not erase the temporal
anchor and disable all subsequent generation.

Producer waits now concern endpoint capacity or safe generation/resource
ownership. They do not wait for a real native EndFrame merely because it is
still in progress while the endpoint has spare credit. A full final FIFO still
applies backpressure; no output is overwritten or skipped to make the producer
appear faster. Native source captures, GPU fences and current-copy dependencies
are not redundant presentation pacing and remain required.

## Preserved paths and measurement

AFW's depth/subtraction method, AER fresh/duplicate-source filtering, guide
matching, stereo/eye interpretation, image algorithms, model presets, Neural
Render and existing resource-count policy are unchanged. In particular Lower
VRAM still has no effect for Luke Ross. Input polling bounds for duplicate AER
cycles remain source filtering, not an output presentation clock. Legacy paths
without a private endpoint retain their existing scheduler.

Changing only the checkbox drains the old transfer batch and preserves the
generator's history, options and GPU resource states. It does not go through
an unrelated backend reconfiguration or re-prime FidelityFX's packed input as
a fresh UAV. Returning to an advanced route's original retained-real policy
may require its ordinary first real prime. Changes to other generator settings
retain their existing full control transaction. The same physical endpoint
survives the switch. OFF restores the V609 route policy.

F9 is optional for ordinary operation, meters and benchmarks. No new GPU query,
fence, texture copy or per-frame file write is introduced for this experiment.
The existing F9 configuration event records requested/effective bits40/41.
The detailed CSV appends `SingleDirector`; the spreadsheet-layout CSV keeps its
existing columns and six statistics/four memory rows. Benchmark sampling rules
and the four timing endpoints are unchanged.

Local GPU/fixture checks cannot establish headset smoothness or latency gains.
Compare OFF/ON in the same scene, route, refresh, AFW and Lower VRAM settings.
All native3X/Lower VRAM provider limitations remain as before. High on Life2
Synched Sequential Hybrid/FSR guide ghosting is outside this pacing change.
See V610_VALIDATION.json for exact checks, outcomes and retained qualifications.
