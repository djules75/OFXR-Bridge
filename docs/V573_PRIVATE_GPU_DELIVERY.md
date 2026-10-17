# V573: isolate final OpenXR GPU delivery from future producer waits

Local candidate on sealed V572 `dc0666e01f6415cc9f8b32b5417a7e6805a7b398`.
Actual headset recovery remains pending. No SteamVR defect is asserted.

## V572 owner run

All 13,435 accepted private packets reached real End and returned success, with
no terminal transport error. Settled compositor records still contain losses.
Approximate real-run rates (production versus compositor) are Reference59/35,
Fidelity60/58, Djules62/58, New62/59, Hybrid60/50, FSR59/49. This is not only a
Reference issue. Per-route windows have different lengths, and rolling meter
medians need not exactly equal whole-window presentation rates.

The new probes measure real Wait returning in approximately0.03ms median.
On Reference, Begin plus final image preparation/copy costs4.92ms median,
8.69ms p90, after an already-ready private head and distributor pacing.
These probes do not isolate texture bandwidth from queue dependencies.

Source inspection shows that private preparation and final runtime delivery
still used the same D3D12 queue. After the exact head fence completed, a later
producer could enqueue an unrelated app/synthesis fence wait on that queue.
The consumer's later copy then inherited that future wait. CPU thread and
texture detachment alone did not isolate this GPU dependency.

An actual D3D12 fixture confirms the mechanism: enqueue a blocked producer wait
AFTER the consumer observes head completion. V572 cannot finish the ready head
until the future work is unblocked. In V573 the shared-queue negative control
still blocks, while the split-queue case presents the same image before that
future fence is signaled. This proves the dependency mechanism, not that every
live zero-present compositor record has that exact cause.

## Change and boundaries

SteamVR D3D12 session creation binds the real runtime to a dedicated final
delivery queue. Existing application/synthesis publication and private image
preparation retain their existing queues. Virtual End signals its packet's
ready fence on the PRIVATE producer queue. After observing that exact fence,
the consumer copies on the real runtime's dedicated binding queue; no producer
publication wait is placed there.

The real frame sequence remains Wait -> Begin -> copy/release -> End, using the
exact raw predictedDisplayTime. Distributor spacing, FIFO3/backpressure, image
leases and synthesis algorithms remain unchanged. No additional swapchain or
image pool is allocated by this change. The new object is a D3D12 command queue.

This applies to the common SteamVR D3D12 endpoint, including existing D3D11 or
Vulkan routes translated to D3D12. Native D3D11 immediate-context delivery is
unchanged. The existing first-graphics-binding substitution boundary is retained;
if an unsupported leading session extension prevents substitution, the previous
queue path remains and the dedicated-queue diagnostic bit is absent.
VDXR and AFW motion/depth/camera/provider contracts are unchanged.

The tray startup choice now explicitly says "SteamVR preparation queue": it
chooses whether PRIVATE preparation uses its own queue or the game queue.
Final consumer isolation is required by the private distributor in either
choice. A remains incorporated at the SteamVR private boundary, B retains its
ring-allocation choice. No live preferences are changed by preparing the ZIP.

steamvr_queue_configuration b now also has bit4 set when the dedicated final
queue is installed; bits1/2 keep the existing preference/preparation meanings.
The existing5714/5717/5716/5715 probes still separate GPU-ready, pacing, Wait
and final copy cost. The compositor meter is unchanged.

## Validation and first trial

See V573_VALIDATION.json. Tests use actual GPU resources and production code,
with a mocked downstream XR runtime. The new session test enters production
xrCreateSession and verifies that the runtime receives the dedicated consumer
queue while application binding metadata and private preparation remain distinct.
An initial session-fixture exit crash was a null endpoint in fixture teardown;
the fixture now retains the actual created endpoint, and the final run includes
that case. It did not require a renderer change.

Restart tray/game with V573, same Ghostwire location, Neural unchanged, 2X,
F9 enabled. First compare Reference0.2.1 then Fidelity, about15s each. If these
improve, extend to Djules/New/Hybrid/FSR; no full benchmark is needed initially.
No automated live installation, activation or publication is performed.
