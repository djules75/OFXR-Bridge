# V451: Detnoir packed guides and late native FG discovery

Based on sealed V450 57586efb86c232b3fffc7272dabfd77cba242c79.
Reference: Elliot release 0.2.13.19-dlssg-V439, exact tag commit
f7ece4d721cae5158dca4b0f1f46d9287e8be798.

## Owner evidence and guide comparison

Closed Nobody Wants to Die (detnoir-Win64-Shipping) PID 33380, V450:
private evidence work/shared/ofxr-autonomous/diagnostics/v450-detnoir-rerun-pid33380.
The flight log's projection_view_rect events 22/23 show one array slice, two
3072x3264 views at X=0/3072. NGX capture independently shows mono outputs
3072x3264 with local output origins 0, full-resolution R16G16_FLOAT motion,
and render-resolution 1536x1632 depth. V450 receives those inputs; a missing
upscaler or missing motion-vector DLL is not the explanation for this layout.

The exact r19 source maps independent mono producer color coordinates to each
half of double-wide XR and selects game-motion guides/descriptors by logical
view, independently of the texture's physical slice. V450 detected two streams
but retained both output origins at zero. Its OFXR game-motion pair/descriptor
checks still required guide eye_count == DepthOrArraySize (2 != 1), and both
views sampled guide index 0. This deterministic rejection explains OFA fallback
for the recorded packed geometry. Logs do not contain a per-pair guide rejection
code, so the final owner game/headset validation remains distinct from this
source/evidence match and GPU reproduction.

V451 selectively ports the packed color mapping and uses the logical stereo
eye count for OFXR validation, guide descriptors and view selection. Real color
slice and RTV remain physical slice 0; motion/depth rectangles remain native
producer coordinates. Snapshot leases, resource/device/fence ownership,
serial/epoch/reset rejection and mandatory Luke corrected Normal motion remain.
Shader-visible extra descriptor blocks are populated explicitly, not copied
from shader-visible heaps. No scheduler, hybrid, extrapolation or r19 synthetic
pose changes were imported. r19's default DLSS/FidelityFX hybrid is a different
mode from the Autonomous OFXR game-vector selection.

## Native FG: recover the already initialized core's missing feature

V450's same run records successful NGX init and capability query, followed by
FrameGeneration_FeatureInitResult=0xBAD00004 (FeatureNotFound), available=0,
capacity=0, both at startup and native selection. The core was loaded before
layer ingress. The earlier two-module GPU diagnostic reproduces that failure
when the first caller initializes without the provider path; adding a late path
or merely loading the DLL does not refresh the core's discovery.

Only that FeatureNotFound response, with no required driver update, permits
V451 to try its adjacent signed nvngx_dlssg.dll through the new small matching
nvngx.dll_dlssg.dll forwarder. ABI 45101 and all required exports must match.
The actual NVIDIA adapter and the snippet's successful GetFeatureRequirements
with FeatureSupported==0, initialization and device-populated positive capacity
must all agree. Unsupported/missing/unknown providers stay in real-frame bypass.
The provider is opened by exact adjacent path. No game DLL, proxy replacement,
NGX shutdown/reset, forced availability or game handle is used.

The snippet validates the module owning its return address. Its calls retain a
return frame inside the NGX-named forwarder even in optimized Release; unchecked
tail dispatch would incorrectly report PlatformError. Official SDK parameter
methods and full FG helpers are used. The C helper's null progress callback is
dispatched through the snippet's actual canonical callback export; a non-null
C callback is refused rather than reinterpreted. OFXR uses its own stable custom
application identity. Capability parameter objects and feature handles are
private. Each feature retains its direct/core owner through deferred retirement,
evaluation and release. Modules/devices stay alive for the process, with no
provider shutdown that could invalidate the game's or another swapchain's work.

Passive native flight events retain existing stages 1 init, 2 capability query,
3 core feature flags, 4 create; new stages 5 direct requirements (b support bits),
6 direct init/population, 7 actual recovered capacity (b frames, c direct=1).
The core may still report FeatureNotFound while stage 7 proves the private route.
3X remains gated by the actual provider's device capacity; the RTX 4090 reports 1
(2X). All renderer inputs, camera mapping and generation ownership stay intact.

## Regression evidence

The three new packed WARP/NVIDIA and late native GPU regressions fail against
V450's actual implementations. The packed cases reject the vector route, and
the native case refuses late recovery. Restored V451 consumes both eyes' opposite
motion, scales from native input resolution and reuses work slots with no D3D12
errors. The late test uses a new empty physical application directory, records
real FeatureNotFound, then supplies the real signed provider. It generates packed,
cropped and sRGB stereo with real GPU pixel readbacks and reseed checks, then
verifies the original core's capability answer is unchanged. WARP refusal and
the existing genuine missing-provider recovery/whole-real-frame guards remain.
Complete final suite results and eight runtime hashes are in VALIDATION.md.

## Package and remaining game validation

Use the matched V451 tray/layer (IPC 45101). Both native FG DLLs are included and
staged by Activate beside the layer. NVIDIA's NR model remains external; its
existing forwarder is unchanged. V450 source/checkpoint/package are preserved.
Detnoir OFXR game-vector selection and DLSS-G recovery need an owner run with
V451; Returnal FSR headset rotation quality remains pending. No live installation,
activation, registry change, game launch, canonical/shared port, push or publication
was performed to create this candidate.
