# V605: retire unused generation and auxiliary outputs

The owner requests cleanup on every runtime and route, explicitly including
Quest/VDXR, Luke Ross and Vulkan. This supersedes the earlier SteamVR-only
scope. Base is sealed V604; this is a local, headset-pending build.

## Confirmed trigger

High on Life 2 V604, UEVR Synched Sequential, has two submitted eye color
sources. Its recorded application resources also include two depth sources,
three other color sources and one static source. Upstream UEVR names its three
auxiliary color allocations UI, UI_RIGHT and FRAMEWORK_UI; the game flight
record itself does not contain those names. Their application handles remain
application-owned, including when absent from the current projection.

Normal generation requests two current and two synthetic output swapchains per
source: eight for two separate eyes, versus four for one packed stereo source.
That per-source allocation is unchanged across inspected V527, V557, V558,
V570, V577, V585, V597, V599 and V604. Lower VRAM requests one of each per source.
These are handle counts, not the runtime's images per swapchain.

The run retains XR_ERROR_LIMIT_REACHED while creating the right-eye output.
It also proves FSR attempts output creation for three sources outside the
submitted eye projection. Its failed eye admission then survives subsequent
memory-control changes. The former projection-only control/retry policy was
restricted to native D3D12 OFXR on SteamVR. Exact instantaneous native live
handle count and an independent queue/fence fault were not established.

## Changes

- All ordinary providers and bindings defer generator output creation to actual
  projection use, on every runtime. Enumeration alone cannot allocate generator
  output rings for UI/depth/static sources. Outlaws retains its required
  pre-frame initialization rendezvous; that resource use is not redundant.
- Drained control changes retire outputs for sources no longer in the submitted
  projection. New projection admission also retires the older unused output
  sets before spending replacement capacity. Source images, history and exact
  release captures survive so a source can return later.
- AER may alternate eye source identities. Previously observed/unknown eye
  sources remain temporally useful; only proven auxiliary-only AER sources
  qualify for this retirement. No eye selection or AFW math changes.
- With both OFXR and AFW Off, interpolation outputs are retired. The independent
  AFW-only Off transition is observed even without an OFXR control revision.
  OFXR Off alone cannot retire outputs still used by AFW.
- Explicit route/storage/enable revisions can reopen a refused valid source for
  vector providers as well. The actual projection still decides which sources
  may allocate. Another refusal retains bounded bypass; no per-frame retries,
  weaker queue/device guards, hidden Lower VRAM or output-capacity reduction.
- Unreferenced auxiliary output cleanup applies to all providers/runtimes and
  D3D11/D3D12 transport, including Vulkan's existing translated path. It runs
  without requiring an enabled FPS overlay and handles empty layer submissions.
  Leased snapshots, currently submitted layers and pending GPU reads survive.
  Luke's private snapshot tokens are never passed to xrDestroySwapchain; its
  real shared publication sink retires only after its last owner/read is gone.
- Existing FPS/meter/F10 idle retirement remains in force and is checked again.

The normal output count and distributor cadence do not change. Removing waste
does not prove that every Synched Sequential normal allocation now fits: the
necessary eight outputs still compete with application resources and active
overlays. No game FPS gain or independent DLSS-G queue mismatch repair is claimed.
No new memory query, always-on diagnostic, live install or online publication.
The control ABI is unchanged (60401); match the packaged tray and game anyway.

## Verification

Actual D3D12 sources and production control/admission code exercise the three
nonprojected targets, all generator modes, SteamVR/VDXR, exact normal/reduced
handle counts, Off cleanup, independent AFW Off, source preservation and bounded
vector-provider budget recovery. The existing poisoned-source stereo pixel
oracle runs after cleanup. D3D11 auxiliary fixtures test leases, visibility,
all generator selections, both runtime policies and real sink/token cleanup.
Final check results and qualifications are in V605_VALIDATION.json.

Owner test: fresh V605 tray/game, High on Life 2, same Synched Sequential mode.
Compare Normal and Lower VRAM, including FSR and DLSS-G, then switch routes and
Off/On. F9 must be enabled before the failure to capture its detailed cause.
No F8 or complete benchmark is required for the initial admission check.
