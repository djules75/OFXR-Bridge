# V461: V460 interface with V457 Vulkan guide suspension

The owner explicitly selected V460 as the starting point, superseding the previous
V452 baseline instruction for this candidate. Only V457 native Vulkan guide-capture
suspension is ported; no V458/V459 code or new stutter correction is included.

The shipping layer links vulkan_native_ingress_disabled.cpp instead of the native
Vulkan ingress/command-stream implementation. It installs no native NGX Vulkan,
command/device/image hooks and creates no private guide transport. Original sources
remain preserved for isolated transport fixtures. Active XR Vulkan-to-D3D12 color
transport and per-queue external synchronization stay unchanged.

Vulkan OFXR falls back to optical flow. FSR/native DLSS-G remain real-frame bypass
when they cannot obtain exact stereo guides; Vulkan NR ingress is suspended. Both
V460 settings surfaces now report this suspension alongside actual Vulkan detection.
D11/D12 FSR, complete synthesis/shader sources and providers remain as in V460.
The private native provider ABI 45601 remains paired; the UI shared-mapping ABI is 46101.

V457 already recovered sustained NMS throughput in the owner's run. Its residual
intermittent hitches are still under investigation. V461 is the requested combined
candidate, not a claimed stutter fix. Live NMS and UI/headset validation is pending.
