# OFXR-BRIDGE Neural Rendering

The surviving model/colour implementation is extracted from accepted OptiScaler
V33034 at canonical source `6c36e598b9027a852ce2b759a1851378468a0938`.
It runs directly in the OFXR layer with no OptiScaler DLL or runtime dependency.
The original OptiScaler and RenoDX/clshortfuse licences are retained in `licenses/`.

`nr_feature.cpp` retains feature-18 creation settings, settled tuning rebuilds,
one temporal history per native SR handle, before/after-SR orchestration,
private native depth/motion snapshots, colour encoding and ratio composition.
`nr_codec.cpp` uses the accepted precompiled shader and immutable descriptor,
resource and constant bindings. `DlssNr_Codec.h` preserves its attributed source.
Only the NR-required path/logging/capability-block services are implemented by
`nr_host.*`; no broad proxy, upscaler replacement, GPU spoofing or game menu is
imported. The inactive driver-proxy experiment is physically absent.

The autonomous feature registry must admit only identified native SR/RR handles.
Caller parameter tables remain immutable. Colour or output is edited in place.
Separate zero-origin 2D inputs are supported; a nonzero subrectangle origin or
array layout is refused rather than sampling another eye. Submission receipts
detach **before** real native `ExecuteCommandLists` and complete after it.
For D3D11 and Vulkan, immutable ingress parameter adapters expose owned D3D12
resources on the application's device adapter. Vulkan prefix copy, D3D12
processing and native colour/output copyback are ordered by an imported
D3D12-fence timeline; recording alone never submits a wait to the D3D12 queue.
Actual Vulkan submission activates the corresponding capture work. Reset/free
of an unsubmitted recording cancels its exact detached receipts and recreates
vendor history whose initialization commands never executed.
Native list unwrapping retains Streamline identity. Every NR lane waits for the
same handle's exact prior submission before reusing vendor state.

Retired model features, parameter tables, scratch and diagnostic capture
readbacks retain their recording's queue/fence token. The source's guessed
32-evaluate resource delay and eight-frame readback delay are physically removed.
Failed vendor create/evaluate recordings also remain owned until retirement.
Shutdown retains pending GPU work rather than freeing it prematurely.
The colour codec belongs to the same native handle and D3D12 device. A changed
transport owner recreates that handle's model, scratch and codec after exact
prior-submission retirement. Immutable shader bindings reused in the new
recording remain owned; unused bindings from its retired prior recording are
released. Tuning or resize does not accumulate old resource graphs.

Build the matching `nvngx.dll_dlssnr.dll` forwarder beside the OFXR layer.
The model itself, `nvngx_dlssnr.dll`, is external and is not redistributed.
An initialized native NVIDIA NGX dispatcher is reused for private capability
blocks, or the installed driver NGXCore is discovered through its registry path.
The forwarder retains the accepted required-export and float-slot validation.

Independent optimized MSVC Release verification: three tests pass. Nineteen
contracts cover extent/reset, distinct handles, recording serials, reused lists,
removed-device fences and complete forwarder ABI. A real D3D12 WARP encode/resolve
test verifies 128 bit-identical pixels across changed source/constant recordings
at zero transfer strength and zero D3D12
validation errors. This does not establish proprietary model or game acceptance.
The production Vulkan dispatcher fixture verifies exact command recording,
state replay, repeated native splits, immutable Submit1/Submit2 metadata,
successful-submit callbacks, abandonment and explicit unsupported-state refusal.
Full integration uses `cmake/neural_rendering.cmake`; the local CMakeLists is a
small independent module build with `NR_SDK_INCLUDE` pointing to native SDK headers.
