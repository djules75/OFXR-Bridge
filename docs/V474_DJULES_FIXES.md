# V474 selective Djules V439–V443 fixes

Based on sealed V473 `9710d5271a5f61a1a34b5ab707ebc07e5d3ed6da`. The owner
selected Djules-only V439/V441/V442 and shared V440/V443 after the archive review.
No wholesale upstream scheduler or provider replacement is made.

| Import | Scope | Result and risk |
| --- | --- | --- |
| V439 | Ordinary Djules, SteamVR | Do not hold a submitted prime for one presenter frame. Pair holds/capacity/ownership remain; other runtimes retain their prior hold. |
| V440 | Shared ordinary D11 bridge | On the observed EndFrame thread, delay the whole valid bridge release until the top of EndFrame. Copy, history capture, downstream release and ownership bookkeeping use the completed drawing together. Native D12, Vulkan, direct D11 and Luke/AFW keep their release policy. |
| V441 | Ordinary Djules input history | A GPU-busy ring slot may wait one observed physical display period, unknown fallback11.111ms, maximum requested20ms per capture. Shared API default is no wait. Never wait for an unsubmitted CPU lease or for the new copy. Timeout/failure refuses reuse. Stereo captures can accumulate waits; OS scheduling can exceed timer wake deadlines. |
| V442 | Ordinary Djules, Virtual Desktop | A presenter stopped by a bypass can return after a frame-loop handoff latched split mode. Fresh split sessions stay inline. Starts consume the receipt; session reset, manual stop, route departure and failure demotion clear it. Hard direct-D11 split-thread restrictions remain. |
| V443 | Shared resource lifecycle | Remove only the creation-triggered one-second quarantine. Resource locks, outstanding work drain, repeat invalidation, projection-first lazy arming, first-use passthrough and all other structural/error quarantines remain. |

The V440 delay accepts only a valid release structure without extension pointers
and valid tracked waited ownership. Before the EndFrame thread is known, and on
another release thread, it runs immediately. Next acquire/wait flushes a pending
release first; destruction drops it. A downstream deferred failure is logged,
discards the uncommitted capture, and does not manufacture a new history ticket.
The earlier application release already returned success, as in the upstream
design; no later error is retrospectively returned from that call.

Route gating also excludes native DLSS-G and Hybrid even when their saved backend
is Djules. Shared history callers and AFW final history keep default zero wait.
No settings or IPC/provider layout is added: IPC47101/provider45601 retained.
Existing FSR/AFW/NGX providers, guide retention, shaders, camera/R19, V472 black-bar
prediction, meter/metrics and V473 display application fix remain.

Flight logs add deferred-bridge stage4, busy-history budget/elapsed and Djules VD
start/bypass/demotion records602–604. Success stage4 is per-frame, excluded from
startup metadata so logging-off does not retain it each release. Existing operation
IDs are preserved; the new history wait operation is appended.

## Evidence and provenance

The D: archive V23058/V075 bundled the old timer with resource lifetime and
continuity protections. Those notes still required headset validation and do not
isolate the creation timer as a proven crash fix. The recorded V23052 vector-switch
device hang was premature snapshot retirement, a separate issue. Exact archive
evidence/hashes and original upstream patches/authorship are retained in
`docs/imports/djules-v439-v443/`.

New production-path regressions cover scoped routing, actual create/drain/repeat
lifetime and surviving quarantine, an actual VD presenter bypass/stop/restore,
real D11/D12 late-paint runtime/history pixel readback and pending release ordering,
and real history consumer fences (completion, timeout, stale event, CPU lease).
Final full suite/package results are in `V474_VALIDATION.json` and `VALIDATION.md`.
Headset/game performance and stability confirmation remain pending. This candidate
does not claim to solve the outstanding NMS activation overhead or all hitches.
