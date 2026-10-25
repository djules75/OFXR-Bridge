# Native Vulkan command continuation

The tracker and immutable submit-packet implementation are extracted from the
accepted OptiScaler V33034 source `6c36e598b9027a852ce2b759a1851378468a0938`:
`hooks/CommandBuffer_StateTracker.h`, `hooks/VulkanInteropSubmit.h`, and the
native `vkCmd*` signatures in `hooks/VulkanwDx12_Hooks.cpp`. The OptiScaler
licence is retained in `../neural/licenses/OptiScaler-LICENSE`.

The autonomous module forwards 262 native command entry points. Observed
command-pool allocation and recording epochs define exact device identity.
External-memory transfers use that observed pool's queue family, including a
native compute family different from the OpenXR binding's queue family.
Bindings, caller-owned push-constant bytes, descriptor dynamic offsets and
supported dynamic state replay into a new suffix after the transport copyback.
An active render pass, secondary command buffer, unknown allocation or state
that cannot be restored safely refuses capture before changing the recording.
These refusals leave the application's native rendering route available.

Each split retains one exact shared recording lifetime. Real successful native
submission activates deferred D3D12 capture work. Reset/begin/free/pool/device
teardown abandons only never-submitted work. Submit1 and Submit2 preserve the
application's original waits on the first prefix and signals/fence on the final
suffix. Multiple splits on one external timeline retain monotonically increasing
prefix/ready/completion values without duplicating the same signal semaphore.
Recorded split command buffers require a fresh recording for another execution.
Capture resources attach to the source recording before raw copy commands.
They remain retained if a later split fails and after successful submission,
until the application's exact reset/free operation following GPU retirement.
Deferred callbacks and retained-resource destructors run outside command locks.

`vulkan_command_tests.cpp` compiles and exercises the production module with a
deterministic driver dispatcher. The independent test requires only Vulkan
headers and is exposed by `cmake/vulkan_contracts.cmake`; it does not load an
OptiScaler runtime or proprietary NGX model. GPU transport verification belongs
to the native ingress producer, which owns the external resource allocations.
The fixture currently verifies 43 recording, state, packet and ownership
contracts, including failed unsplit captures and concurrent destructor access.
