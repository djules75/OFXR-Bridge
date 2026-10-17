# V567: Autonomous bootstrap for the embedded OptiScaler owner

Parent is sealed V566, source 25935d1767298e9453e56b397c5c530de7e9c6f6.
The owner requests V567 after V566, restoring OptiScaler bootstrap. No renderer
or counter change is included. V566 Luke accepted-image accounting and its
18 AFW-quality benchmark trials remain intact for the Autonomous route.

## Trigger and ownership

Assetto with OptiScaler V33036 now starts, but the actual V565 implicit layer
kept the OpenXR callbacks. Its independent destination registry could not arm
OptiScaler's CSP-owned SR path. Native equal-size DLSS evaluations continued;
that did not prove full XR-resolution upscaling or embedded OFXR activation.

V567 consumes the existing OptiScaler V2 identity (size, version, permanent
magic, guide capability), rather than a DLL filename or product version.
It selects one already loaded compatible host and retains that module for
the lifetime of returned callbacks. Ambiguous owners, invalid identity,
activation failure, failed negotiation, exceptions or invalid callbacks fall
back to Autonomous. Failure restores embedded activation to zero. Ownership
is fixed at the first valid negotiation and remains stable for later instances.
Stock OptiScaler without this identity does not qualify.

The original V232 bootstrap supplies the discovery/retention/activation
pattern. Here the registered name is XR_APILAYER_OFXRAutonomous, while the
embedded owner expects XR_APILAYER_XRFrameBridge_diagnostic. Negotiation uses
the embedded name; a creation trampoline shallow-copies just our loader entry
and substitutes that name. The host alone advances the chain. Original next
entries, downstream callbacks and application inputs remain unchanged.

Process exclusions and loader validation run before discovery/activation.
Delegation returns before Autonomous's NGX profile setup, capture hooks,
session resources or settings application. There is one renderer, owned by
OptiScaler. Autonomous does not re-export retired OptiScaler producer APIs.
Flight event negotiation/result5670 records successful delegation. No second
implicit-layer registration is required.

## Controls and trial

With delegation, use the **OptiScaler in-game menu** for SR, Neural Rendering
and embedded OFXR. Autonomous tray settings, its F10 menu, benchmark controller
and V566 meter are not forwarded to the embedded renderer. They continue to
serve native Autonomous games. This change restores the bootstrap, not a
cross-product settings or telemetry adapter. The external NR model remains a
separate existing OptiScaler requirement and is not packaged here.

The candidate is not live-installed or globally activated. After selecting
V567 and restarting tray/game, verify Assetto enters VR, OptiScaler reports
the embedded layer active, SR actually changes render/output resolution,
and then test NR and OFXR individually. Loader fixtures are not headset,
image-quality, DLSS5/NR or pacing validation. Keep V566 and V33036 sealed.

## Verification

Shipping optimized DLL tests cover identity/capability rejection, activation
rollback, structured exceptions, invalid callbacks and concurrent negotiation.
Successful fake-host and real sealed V33036 tests exercise the actual create
adapter through to a downstream runtime refusal, verify one chain advancement
and unchanged caller entries, malformed-input refusal and host retention.
Native negotiation, process exclusion, call chain, provider API and V566 meter
and benchmark regressions are checked separately. Exact results are recorded
in V567_VALIDATION.json and package provenance.
