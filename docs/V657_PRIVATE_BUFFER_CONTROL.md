# V657: common private admission2 by default, Increase buffer selects3

Owner requests the two-credit default beyond Luke Ross, with an Increase buffer
checkbox restoring3. It belongs to the private bridge, not the Director alone:
Framework bypass/Director/combined on that bridge share the setting. Legacy
Framework transport is excluded, as is direct AFW-only. Runtime identity stays
unchanged: AutoVDXR/ForceVDXR remain Legacy; ForceSteamVR onVDXR opts into bridge.

Per-game Settings.increase_buffer defaultsfalse, persists in existing [ofxr]
INI and passes through the existing CPU embedded-control revision. It is one
preference shared by generation frameworks, independent of Fast mode/3X/AFW
quality. F10/desktop checkbox under Pacing; disabled on Legacy/direct AFW Only.
Active runtime buffer count is reported2/3, zero when there is no private bridge.

Every installed private runtime uses2credits unless selected3. Three private
image slots per chain and GPU resources/rings remain unchanged. A third output
in3X can wait for actual retirement; increasing admission is not required merely
to submit three outputs sequentially. Framework bypass still has FIFO ownership
but no Director pacing. Extra3 buffering may absorb service jitter at more latency.

Live change uses the existing safe policy boundary: hold new admission, let all
accepted packets finish through the ordinary consumer, update FIFO capacity in
place, then release producer. No packet drop/overwrite/flush or session/binding/
swapchain/engine/history recreation. Update accepted-policy epoch to reset old
work margin, retaining absolute cadence and last actual handoff; otherwise a
learned2P margin from3credits could survive the return to2. Work budget now follows
the selected2/3 capacity on all paths, removing the old Luke-only1P exception.
Set before session start is also supported. Stop/failure wakes a waiting change.

Control-only updates use the same fast path as pacing toggles: no OFXR reset,
AFW/publication loss, native guide rebuild or GPU allocation. Combined backend
changes apply capacity at the existing completed reconfiguration boundary.
FIFO setter changes future admission only and keeps outstanding ownership even
if called below current occupancy. Multi-credit waiter predicate checks count
against capacity before unsigned subtraction; resizing cannot underflow admission.

Benchmarks preserve chosen buffer preference and existing trial count/column
layout. Different requested budgets are different trials. Details CSV appends
BridgeBuffer (0Legacy/direct,2,3); flight config records actual bridge presence
and3credit state. Shipped parser retains that metadata. No automatic2vs3 sweep.
V656 minimum-cycle metric, AFW depth/subtraction/math/markers/provider, source
continuity, capture, synthesis and physical cadence algorithms unchanged.

Validation targets actual D11/D12 live2->3->2 and3-output sequences, exact pixels/
poses/FIFO, unchanged image count, pending drain, stop cancellation, pre-start
policy, INI persistence, no engine reset, DJules3X and Legacy exclusion. Hardware
game/runtime throughput/stability comparison remains owner's acceptance step.
