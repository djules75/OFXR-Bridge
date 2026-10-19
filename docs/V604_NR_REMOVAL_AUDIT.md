# V604: NR crash evidence and display labels

## Owner requests

Audit the Witcher 3 OptiScaler device-hung protections against Autonomous,
classify the latest Ghost of Tsushima DLSS5 incident, and prepare the next
candidate. Subsequent requests add explicit framework labels and a cached
VRAM percentage to both F10 and the Elliott panel. V600-V603 are already
immutable KCD checkpoints, hence this next main-line candidate is V604.

## Latest incident: known facts and limits

Ghost V599 completes all 18 Luke benchmark trials with Neural enabled. The
later failure occurs during ordinary play, with no setting/menu transition
(owner confirmation). REAL VR records DEVICE_REMOVED and removal reason
0x887A0006 / DEVICE_HUNG at 21:36:53. The game then deliberately writes that
HRESULT to address zero. Subsequent Streamline DLSS-G access violations occur
during teardown; their faulting module does not identify the initial GPU fault.
Last regular game memory sample is about 16,079 MiB against a 23,370 MiB budget;
the reported temperature is 49 C. The V598 accumulating AFW-allocation pattern
is absent from this completed V599 run. This is not proof of every possible
resource-lifetime defect, nor proof that NR alone caused the GPU hang.

The mini dumps contain the NR ring pointer but omit its heap pages. No DRED
report was persisted. Therefore this evidence cannot identify the failing GPU
operation, or establish that the identical HRESULT has the same root cause as
a prior Witcher hang. **V604 is not a demonstrated device-hung prevention fix.**

## Port audit

Compared the accepted V33034 NR extraction and published Witcher V23244 lineage.

| Protection | Autonomous finding |
|---|---|
| Active pre-SR extent / separate zero-origin inputs | `NrBeforeSrContract.h`, byte-identical |
| Source camera-cut reset OR pending local reset | `NrHistoryContract.h`, byte-identical |
| Per-feature retirement, never backend-global eye waits | retained; `NrRetirementCadence.h` byte-identical |
| Typed and typeless private depth/motion snapshots, V23235 | `NrGuideSnapshot.h`, byte-identical |
| Complete forwarder ABI and native motion scaling | `NrForwarderContract.h`, byte-identical; scales retained |
| Streamline native command-list identity | `NrCommandListIdentity.h`, byte-identical |
| Distinct eye/handle histories and scratch | retained in `NrPerHandle` |
| Mutable evaluation tables owned by command-list identity | retained, with exact prior native submission retirement |
| Detach recording serial before Execute; signal after real Execute | retained in native ingress; stale callbacks cannot retire a new recording |
| UINT64_MAX removed-device fence is not successful retirement | retained and tested |
| Separate feature creation and first evaluation submissions | retained |
| Immutable codec descriptors, resources, constants | retained per native handle/device; HLSL source byte-identical |
| Debounced tuning and shape/format rebuild | retained |
| Delayed release of features/resources/capture readbacks | strengthened: exact recording tokens replace guessed 32-evaluation delays |
| Shutdown with outstanding work | retains state until proved retired |
| NR diagnostic work gated off without F9 | retained |
| Device-removal notification and persistence of NR/DRED evidence | **missing in V599; restored in V604** |

The rejected V23226 global retirement policy is not restored. The legacy Luke
exception that skipped submission retirement is not imported: Autonomous now
observes real native submissions. Old README references to guessed frame delays
or private model initialization queues are not accepted-source requirements.
The separate Witcher host renderer is outside this repository; its applicable
scene-only export consumer remains in the Autonomous D3D12 synthesizer.

## Restored removal observer

Port the accepted `DeviceRemovalDred::Monitor`. One shared observer per native
NR device uses a private never-signalled fence plus Windows threadpool event
notification. It does not poll, Execute, Signal a GPU queue, or wait for GPU
completion on the producer thread. On removal it writes the retained NR RAM
ring first, then available DRED breadcrumbs/contexts and current/recently freed
allocation records to `ofxr-dred-V604-...-deviceN.log` beside the runtime DLL.
Writes flush independently of normal logger shutdown. Owners keep the monitor
alive, and healthy teardown retires only its CPU callback.

Arming, markers and persistence are diagnostic-only. F9 can arm an already
running NR handle. The DRED settings call affects subsequently created devices;
it cannot retroactively enable full breadcrumbs on an existing game device.
The report still preserves NR recording/submission/reuse evidence and clearly
records unavailable DRED queries. No registry, game files or driver settings
are changed. No vendor model, NR math, AFW contract or director scheduling is
changed. A startup-disabled observer is not created at all.

## Display changes

Replace Generating with FidelityFX, OFXR 0.2.1, OFXR 0.2.13, OFXR 0.3.0,
Hybrid, FSR or DLSS-G. Preserve active AFW/Neural suffixes, waiting/fallback
reasons and green/error colours. These labels require fresh confirmed generation.

Show `VRAM nn%` in the top centre of F10 and the Elliott panel. It is the last
existing game-process LOCAL CurrentUsage divided by its DXGI Budget, **not global
GPU occupancy**. Publish the results of the existing query sites; no added GPU
query, polling frequency, RAM query or benchmark sample. Without an available
sample/budget or an active game, show N/A. With F9 and benchmarks off there is
no new sampler: the display remains the last existing sample, if any.
The F10 tooltip states this. IPC adds this cached value under ABI60401 with a
new mapping name, so V599 and V604 peers cannot corrupt each other's channel.
Restart both tray and game for a matched pair. User INI values/layout stay intact.

## Validation and next game check

See V604_VALIDATION.json for actual check results. The imported WARP removal
fixture covers normal/disabled lifetime, real software-device removal without
Present, complete independent reports surviving child termination, second-device
rearming, breadcrumb ring wrap and partial-list priority. It never removes a
physical/game GPU device. Existing NR contracts/codec, UI and runtime-route
checks are retained without relaxed thresholds.

Owner check: use the V604 tray and game, keep F9 on while playing Ghost with
DLSS5 as usual. No F8 or complete benchmark is required. If the hang recurs,
the new automatic report is the missing discriminator between a resource
lifetime/barrier fault, a native SR/NR list stall and an OFXR output fault.
The actual prevention repair remains pending that evidence; do not publish this
as a solved DLSS5 crash. V599 reset-memory and V598/V597 ingress/pacing fixes
remain present. This package is local, with no automatic install or online push.
