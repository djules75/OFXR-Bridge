# V607: measure complete SteamVR director delivery

Ghostwire DLSS5 V599 reported Fidelity Normal Cycle estimated median14.622ms.
The overlay sampled virtual bridge End success, before the director's native
queue/copy/End, and extrapolated one refresh from virtual synthetic acceptance.
Later retained FSR/DLSS-G packets show42-43ms between virtual acceptance and
native End, excluded from those old values. Fidelity's detailed native interval
was not retained; its actual latency cannot be recovered or corrected by adding
a constant. Old SteamVR estimated-cycle rankings require new measurements.

## Correct boundary

All SteamVR private-runtime routes now attach an optional immutable delivery
receipt to each measured fresh output. The existing final consumer publishes
native End entry, return and that packet's actual predicted display QPC only
after native End succeeds with nonempty content. Receipts use release/acquire
publication and retain no session/overlay/cycle references. They add no director
mutex, callback, wait, GPU query, runtime call or extra copy. Display QPC comes
from the conversion already used for this native displayTime before handoff.
No new clock query is inserted at the handoff edge; the optional return clock
and receipt publication happen after native End.

The original source cycle retains its own bounded receipts. Newer producer
frames cannot replace that identity. Virtual acceptance remains the producer's
completion contract and still returns independently of physical presentation.
Pending, failed, empty or abandoned native packets cannot publish a shorter
virtual latency as a physical result. Content confirmation for native DLSS-G
continues using its existing nonblocking result polling; copies do not count
as generated images. Repeats do not contribute new measured source cycles.

- OFXR time remains the union of app-facing CPU call intervals, unchanged.
- Cycle length ends at the later of the real native End return and app End
  return. It includes director queuing, GPU readiness/copy and native delivery.
- Cycle estimated ends at the later of that real packet's native predicted
  display time, native End return and app End return. It requires a confirmed
  generated packet from the same cycle to have reached native End first.
  The director already spaces those deliveries: no additional11.11ms (or two
  more periods in3X) is added. Missing display-clock conversion remains N/A
  independently of the measurable Cycle length.

These are WaitFrame-to-native-delivery/predicted-display cycle measurements,
not sensor-to-photon or measured scanout. Runtime/compositor prediction can
still differ from physical presentation. VDXR/non-private paths retain the
existing slot estimate. No minimum22ms clamp or guessed queue constant exists.

## Scope and compatibility

The observer is bound once to the session's common private endpoint; it covers
FidelityFX, OFXR0.2.1/0.2.13/0.3.0, Hybrid, FSR and DLSS-G, normal/Lower VRAM,
2X/3X, nativeD11/D12 and the existing translated transports. The Luke content
and private-auxiliary handling still runs before the same observed endpoint.
V606's separate-eye publication pooling remains intact. Application/public API
calls, FIFO limits, all native scheduling constants, production buffering,
source images, provider dispatch, AFW/NR math and FPS counters are unchanged.

Collection follows the existing F9/UI/benchmark measurement gate. Hidden/F9-Off
with no benchmark does not allocate or attach receipts. The director performs
no receipt clock/publication when a packet has none. The fixed interface ABI
and CSV headers stay unchanged; new native-source enum values identify the
corrected observations. The DLL and tray implementation is607 / CMake0.607.0;
IPC60401/provider45601 remain compatible.

## Validation and owner check

Unit tests reproduce the old virtual15ms value, require N/A while native output
is pending, then observe exact full-cycle and predicted-target values from
native receipts. They cover unconfirmed content, failed/empty delivery,
unknown display clocks, expiry and3X without adding display slots twice.
Shipping-generator fixtures run the actual overlay/content/transport stack
with native D3D GPU work and a mocked downstream runtime. All seven frameworks,
3X andD11 are included. Existing VDXR-style virtual slot tests remain unchanged.
Initial integration11/13 passed; DLSS-G exceeded the fixture's one-second UI
visibility lease. Its test now refreshes that lease during sampling rather
than disabling the production measurement gate. Initial evidence is retained.

Headset validation is pending. Restart tray/game withV607 and repeat the desired
Ghostwire normal/Lower benchmark; F9 is optional when the benchmark or meter is
active. Existing CSV/export layouts still apply. Compare latency using newV607
measurements rather than patched values from an old CSV. FPS/VRAM columns from
old runs are unaffected by this timing correction. No live installation or
online publication is performed as part of this local candidate.
