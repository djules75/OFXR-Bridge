# V662: retire a stale Luke synthesis queue before native ownership attachment

Base: V661, with the accepted V659 scheduling and V660 benchmark behavior.

KCD2 V661 PID18320 rejects both D11/D12 transport synthesizers at 43483/43499ms.
Initialization stage1, argument mask256 is the sole rejection. The selected
synthesis queue reports device2101929939904; the authoritative allocation
device is2100839492656. The queue was created at21450ms and retained after the
late native device attachment at43407ms. Backing-state equivalence between a
REAL VR device alias and its native owner did not satisfy exact COM ownership.

Before any active color history exists, late native attachment now checks the
existing synthesis queue's reported device against the confirmed device's
IUnknown identity. It retires an incompatible queue and creates the separate
synthesis queue on the confirmed allocation device. Compatible early queues
are retained. Active histories and queues are not migrated or reset. Existing
strict synthesizer validation is unchanged, including cross-device refusal.
This is common Luke D11 attachment logic, independent of the sink-sharing flag.
It adds no per-frame GPU work, pacing, fence, buffer, marker or vector change.

Horizon V661 PID27204 and21908 still record both transport refusals, reason13,
E_INVALIDARG. Their retained flight windows contain no initialization guard
payload. The logs confirm the failure class, not the exact KCD cause. Negative
initialization events now survive normal flight-log truncation as startup
metadata, so a failure captured with F9 enabled remains diagnosable later.
No new diagnostics are enabled without F9.

Regression proof: the new actual-layer fixture injects a queue reporting a
verified REAL VR alias before native attachment. It fails on the unchanged
V661 production branch, then passes with V662. Existing compatible, active-
history and missing-helper attachment tests must continue passing. A real-GPU
isolation test checks that parked OFXR does not block AFW/capture or corrupt
stereo pixels. A log-rotation fixture preserves the failure payload.

Hardware acceptance remains pending. Restart matching tray and game. Test KCD2
with Swapchain budget fix OFF and Horizon with ON, the reported failing cases.
F9 before entering the level is sufficient; no F8 or full benchmark is needed.
The switch remains per-game and is not forced to either state.

No Git metadata, push, live installation/profile, runtime/driver, registry,
paused HUD or other laboratory changes are part of this candidate.
