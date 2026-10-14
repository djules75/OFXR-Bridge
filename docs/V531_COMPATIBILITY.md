# V531: selective Elliott compatibility changes on V530

> **Owner verdict (2026-10-14): V531 rejected.** C1/R19, C2/R26 and selected C3/R35 return to TODO; none counts as a completed import. This report retains the rejected attempt and its historical evidence. See the [current audit](../../docs/ofxr-autonomous/ELLIOTTTATE_AUDIT_CURRENT.md) for the work to redo. No specific failure cause has been supplied.

The owner requests these three compatibility changes on the published V530
endpoint, production-path tests to decide the R26 import, and private online
publication of the resulting build and updated English audit. Dedicated tests
in the named games are not requested. Automated GPU/ownership evidence is not
a claim that Beat Saber, Control or every VR mod has been validated.

Base: shared main `71233ef12ab8448a3655816ace2a4d305d1a48ea`, retaining V530
DLSS-G A/C controls and the final benchmark exporter methodology. No pacing
controller, automatic multiplier, hitch/repeat policy, Hybrid shader-quality
rewrite, fitted camera, controller-menu redesign or extrapolation is added.
IPC52601 and provider45601 remain unchanged.

## C1 / R19: broken host Agility configuration

Adapted from Elliott Tate's
[0c994d780a601f4f833cca323652aebbf5f61baa](https://github.com/elliotttate/OFXR-Bridge/commit/0c994d780a601f4f833cca323652aebbf5f61baa).
Only an initial `D3D12_ERROR_INVALID_REDIST` takes the private same-adapter
system-D3D12 factory fallback. The ordinary device path and the game's global
SDK selection are unchanged. Bridge-owned root signatures use the actual
device's serializer if the global serializer has that same failure. Consumers
include synthesis, the existing guide composer, native NGX packing, AFW,
Neural Rendering, optional menu rendering and Vulkan bridge-owned work.

The prebuilt FidelityFX Optical Flow backend still uses the global serializer.
If that serializer is unusable, refuse that backend before dispatch rather
than construct an incomplete context. This does not blanket-disable FSR or
claim FidelityFX becomes functional in a broken host.

The local hardware reproduces the broken host exports, but its system core
does not expose a usable `ID3D12DeviceFactory` (`E_NOINTERFACE`). The fixture
therefore verifies clean unavailability, null output objects, unchanged host
configuration and the precise global-serializer guard. **The recovered-device
GPU branch is not exercised on this system.** The same fixture executes that
branch on systems supporting the factory. A separate healthy-host fixture
verifies same-adapter creation, root-signature creation, real GPU pixel readback
and normal FidelityFX initialization. No recovered Beat Saber result is claimed.

## C2 / R26: import justified by an actual V530 negative

Adapted from Elliott Tate's
[272a07bd5b8b06dde60361f5b092b90e3071b994](https://github.com/elliotttate/OFXR-Bridge/commit/272a07bd5b8b06dde60361f5b092b90e3071b994).

The actual layer fixture creates a real D11/D12 bridge and history, releases an
eye, publishes another eye's guides, paints late and flushes the deferred
release. On unmodified V530, the native-D11 exact receipt stays at publication1,
but the remaining global-publication path changes original3 to guide4. This
is a demonstrated residual association defect, so the conditional port is made.

V531 freezes the original publication boundary and, when applicable, the exact
native-D11 owner/stream/publication receipt at the existing deferral point.
Copies, GPU ordering and runtime release remain at their original flush point.
No early copy, new CPU fence wait or producer-delay policy is introduced.
With vectors disabled, the new metadata queries are bypassed. Luke/AFW,
capability8 direct/native ownership and Vulkan deferral policy stay unchanged.

Fixed production-path tests preserve original1,3,5, including mutation of the
texture's current receipt before flush. Real runtime/history readback still
contains the late-painted value127/128. Additional guide tests reject invalid
shape, retired streams and stale owner generations; existing acquire/wait flush,
failed runtime release and destruction tests are retained. Missing original
identity never licenses a lookup of a newer unrelated guide.

## C3 / R35: ordinary array history for the existing R22 composer

Selectively adapted from Elliott Tate's
[2a3e600458c279f0e40c0a1b325238f77d366c74](https://github.com/elliotttate/OFXR-Bridge/commit/2a3e600458c279f0e40c0a1b325238f77d366c74).
The remainder of that mixed UI/diagnostics/settings commit is not imported.

Ordinary alternating-eye array pairing now retains raw previous2/previous
guide links before assigning same-eye serials or reordering eyes. Actual older
producer completion is checked through the existing GPU ordering contract.
The existing composer and work-slot input retention are reused. Strict FSR
does not acquire bridge links or the extra composer dependency; Luke's separate
eye-count resolver and same-eye native reconstruction are unchanged.

The V530 fixture fails because the array lacks its raw bridge links. Fixed
tests verify serial3/4 -> bridge2/3, right-first eye ordering, reset epochs,
absence of recursive links, strict-FSR/Luke isolation and the existing real-GPU
cross-eye/same-eye composer readbacks. The conditional composer pass can become
reachable for affected arrays; no zero-cost or performance claim is made.
The upstream Control artifact report is not a confirmed game fix.

## Validation and delivery

See `V531_VALIDATION.json` for final selected Release checks, source/shader/provider
retention and package identities. Original negative logs and applicable checks
are preserved with the release evidence. The English current audit lives at
`docs/ofxr-autonomous/elliotttate/audit-current.html` in the shared repository
and is also supplied as a standalone HTML release asset and a complete audit ZIP.
Previous V529 analysis and V530 package/tag identities remain immutable.
