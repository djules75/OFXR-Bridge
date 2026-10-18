# V592: physical display slots and retained packet buffering

The owner rejects the remaining V591 produced/presented gap and requests a
different analytical method. A separate read-only background OpenVR observer
records full timing snapshots, QPC/vsync and frame revisions without injecting
into the game or calling WaitGetPoses/Submit.1329frame IDs match the in-process
observer exactly;77zero-present records never become presented later. Across
the39.61s New-route window,2927frames arrive and189are never shown. The worst
200-record window has28zeros; one second receives75and shows62. The earlier
13/200sample was a short tail, not the maximum loss. Full evidence stays local.

The existing director promised more than it guaranteed. Its minimumPinterval
was measured from each actual End; late GPU preparation moved that phase to
copy completion. There was no physical-vsync phase input. Source-only refill
also omitted later private-copy service. Cumulative phase displacement was
not a stable per-packet input-to-presentation latency. This does not attribute
every zero to GPU lateness: some losses occur with preparation already early.

## Absolute physical slots

Use IVRSystem's physical vsync anchor and counter, refreshed on the existing
application observer path at most4Hz, including F9/UI-off. A nonblocking lock
attempt skips busy refreshes. Publish a consistent atomic snapshot; the
consumer reads it in bounded attempts without locks or OpenVR calls.

Each output gets an absolute hardware counter and deadline at the physical
vsync edge. A late packet stays in FIFO and uses a later slot; it does not
replace the clock's phase with its copy-completion timestamp. Empty elapsed
slots require no invented frame or discarded input. Native Wait stays on its
own worker; GPU preparation stays independent; the consumer only releases
completed targets and performs serialized Begin/End. Normal source/storage,
provider and interpolation decisions are unchanged across the SteamVR routes.

The target grid advances by exact physicalP. Actual CPU entry can be late; its
next interval may recover up to min(100us,P/100) toward the unchanged grid.
At90Hz the minimum remains11.011ms. This deliberately replaces a strict
lastActual+P floor: imposing that floor on every late wake would move the grid
again. Unique hardware slots prevent double delivery to one refresh. The2us
edge guard is a fixed slot offset, not an increment accumulated each frame.
No11.1ms oscillator, guessed refresh rate or period shortening is introduced.

## A buffer delay actually applied to packets

Measure unfinished-source fence work and actual private-copy service. Exclude
FIFO waiting, real-image acquisition and retained-index alias gates. Hold the
maximum observed service time, bounded by the existing(capacity-1)P reserve.
Every FIFO head must reach acceptedAt+heldLatency before its physical slot.
A service increase of0.7ms adds0.7ms to that hold, not a whole frame. Actual
display slots remain discrete. A ready packet is never silently superseded.
The finite75fps case keeps all packets and simply leaves unused display slots.

There are no additional images, XRswapchains or producer credits. Full-pool
backpressure remains permitted. Before the runtime supplies a physical phase,
V591 timing remains available. First anchored delivery adopts the prior actual
handoff to avoid a startup transition burst. VDXR, AFW/vector math, provider
presets, UI, INI and FPS-meter formula are unchanged.

## Evidence and next comparison

Deterministic tests cover exact retained GPU hold, per-packet eligibility,
absolute-slot recovery after late copy/CPU entry, bounded pause, and1000finite
75fps packets on90/120/75Hz grids. ActualGPU/mockedXR tests verify24changing
FIFO pixel values in both D11 and D12 while preparation is deliberately late,
plus safe activation when the physical clock appears after startup. Exact
V591 production code fails the new physical-phase check; V592 passes. All33
focused checks pass. This proves the new contracts in those tests, not headset
performance. V592's independent-observer comparison is still pending.

F9 adds5729eligibleAt/heldLatency,5730deadline/physical counter,5731service/added
hold and5732requested/actual layers plus shouldRender. Existing5712c identifies
packet eligibility latency on the physical path, cumulative phase on fallback.
No optional GPU profiling is enabled. Read-only4Hz physical-clock refresh is
core pacing and therefore operates with diagnostics off too.

Use the same Valhalla scene,New0.3.0 2X,LowerVRAM/Neural off. Enable F9 only in
the level for about20seconds, disable it, then close normally. No benchmark
orF8. Keep the external observer to compare the same frame IDs and worst
windows. This local/runtime-pending candidate is not installed or published.
