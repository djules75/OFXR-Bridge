# V608: production, preparation and delivery timing

The owner prioritizes latency measurement. High on Life 2 Synched Sequential
Hybrid/FSR motion ghosting is recorded but intentionally deferred. This change
does not alter vectors, pixels, producer pacing, output cadence, queues, copies,
swapchain counts, AFW or Neural Render.

## What the four numbers mean

All intervals belong to the retained source frame, using the same QPC domain.
They are observed wall-clock stages, not isolated GPU kernel costs:

| Meter | Definition |
| --- | --- |
| Game time | Source app/UEVR production wall time before its EndFrame entry, minus the union of intercepted XR calls during that interval. Concurrent eye calls count once. |
| Generation time | Source EndFrame entry to readiness of the first confirmed generated native output, after the existing GPU preparation and private-to-native copy. Includes queue/history/preparation time. Zero if no synthetic was delivered. Nonprivate runtimes use synthetic acceptance as their observed boundary. |
| Delivery time | The rest of the complete estimated cycle: intercepted XR calls before source End, plus time after first-output preparation until the real target. Contains bridge/runtime waiting, output pacing, subsequent-output preparation, copies and the prediction horizon. With generation disabled it still includes all intercepted XR waits and source End to target. |
| Cycle (estimated) | App WaitFrame entry to the real output's native predicted display target, or a later native/producer return. No added refresh on SteamVR. |

The three stages partition each individual complete cycle. Each visible value
is independently median-filtered over one second; the three displayed medians
need not add exactly to the displayed cycle median. A missing, failed or empty
native real cannot publish a cycle. Missing generation confirmation/readiness
keeps the stage partition N/A, independently of the valid real predicted cycle.
No synthetic is required to measure a real-only/disabled baseline.

The disabled baseline still passes through the loaded private SteamVR transport.
It is **not** an unloaded-DLL reference or a direct native passthrough. Its
Generation time is zero, while Delivery time retains transport/runtime cost.
Game time excludes observed intercepted XR calls, but is not an independent
measurement of the engine's asynchronous GPU work. End-to-end input latency,
panel scanout and late compositor reprojection are not measured here.

## High on Life 2 V607 audit

The owner made two full suites in PID5260: first Synched Sequential, then Native
Stereo. The Native CSV is OFXR-V607-20123299812492.csv. Its accepted sample
medians include Fidelity normal76.013ms, Djules2X normal97.899ms, New0.3.0
normal97.823ms and Djules3X normal119.882ms. These are complete predicted
cycle ages, not generation-only cost. The native estimated endpoint already
includes physical output slots; adding an extra11.111ms would double count.

The disabled Native compact trace has measured cycle median37.262ms during
the baseline revision. V607 did not publish a baseline **estimated** cycle
because it unnecessarily required a synthetic receipt. That37ms measured
handoff cycle is not interchangeable with the predicted-display endpoint of
the enabled76/98/120ms results. The old exported baseline remains N/A; no
constant or guessed value is backfilled into its CSV.

The retained detailed ring starts at471771ms, after most Native test routes.
It cannot retrospectively divide the earlier90FPS Fidelity/Djules sections
into generation and delivery. Existing compact metrics retain their original
values. The new stage measurements are for a fresh run.

OpenXR defines predictedDisplayTime as a predicted display timestamp, not an
observed scanout measurement:
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html

## Implementation and compatibility

The existing private packet prepared_at timestamp travels in its optional
delivery receipt and is atomically published after native End. No new runtime
call, GPU query, fence wait, copy or handoff-edge callback is added. Existing
F9/UI/benchmark demand gates remain. Additional compact benchmark_stages
records are F9-only; CSV collection also works with F9 off.

Both F10 and the Elliott meter show the four stages. Runtime state remains
visible outside the timing cells. Shared UI ABI is60801 with a V608 mapping
name; restart tray and game together. Provider ABI remains45601.

CSV layout and the owner-only spreadsheet adapter stay unchanged. The baseline
now contributes Cycle estimated min/median/max whenever a same-frame actual
prediction is available, including nonprivate runtimes with clock conversion.
Missing conversion stays N/A. The internal owner-sheet script is not bundled.

The candidate requires owner headset validation. No game installation or
publication is performed automatically.
