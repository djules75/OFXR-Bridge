# V450: recovery after a native provider refusal

Based on sealed V449 ddd74dac2699ea6eb9a2eb32854277eefaa946bd.
Owner report: Nobody Wants to Die / detnoir-Win64-Shipping, native DLSS-G
unavailable; enabling live logging appeared to prevent other generators too.

## Observed run

Private, closed PID 1020 flight log: 6,859,831 bytes. Logging begins at session
311,659.741 ms, already in generation cooldown. There are three later
DXGI_ERROR_UNSUPPORTED / 0x887A0004 control reconfigurations. The projected
packed stereo swapchain repeatedly lacks generation buffers (1,663 events),
while three other swapchains initialize successfully after route changes.
FSR stereo input checks transition to ready, yet generation remains missing.
The original provider refusal happened before disk logging and is absent.
Logging caused the initial failure is not proved; no synthesis pairs occur
in the recorded final portion. Private source/profile copies are retained under
work/shared/ofxr-autonomous/diagnostics/v450-detnoir-logging, outside the ZIP.

## Cause and recovery

V449 marks failed creation as generation_declined, regardless of why. Output
shape changes can retire the already-admitted buffers before a missing native
provider refuses their replacements. Later provider changes respect that flag
forever, even though the application's history is still valid.

V450 records the exact refusal reason/detail. An explicit control
reconfiguration reopens only synthesis-initialization refusals, once per
revision, retaining exact source history/captures. Structural/interop/private
runtime swapchain refusals and exhausted session budgets stay closed.
There is no per-frame initialization retry or GPU ownership bypass.

The shared diagnostics report the provider and actual initialization HRESULT,
with real-frame bypass. Successful reconfiguration clears the previous error.
A bounded metadata cache retains swapchain eligibility and passive native NGX
initialization/capability/feature-creation answers for late logging, so the
next closed log can distinguish missing feature discovery from missing guides.
Native provider events use a=stage (1 initialization, 2 capability query,
3 capability answer, 4 feature creation). Stage 3 result is the NGX feature init
code, b=available and c=generated-frame capacity. No NGX state is reset.

## Regression

The actual layer code and real NVIDIA/D3D12 providers run in an isolated
executable directory deliberately without nvngx_dlssg.dll. NGX genuinely
refuses native initialization; no injected GPU/provider failure is used.
The regression fails before the fix: 'provider refusal poisoned optical-flow
recovery'. After the fix it checks two native refusals, precise bypass status,
actual live logging on/off without a generation-control revision, retained
native failure metadata, successful OFXR buffers, structural/budget refusal
gates, then eight real/seven synthetic stereo submissions with pixel and
XR-cycle accounting and no GPU debug errors.
Existing loaded-layer live logging and complete new/djules suites remain
required; final results are in VALIDATION.md.

## DLL routing and remaining limit

The package's ofxr/nvngx_dlssg.dll is staged beside the activated layer. Its
native NGX initialization explicitly supplies that module directory as a
feature search path. A game-directory copy is not additionally required.
The owner says Detnoir has both copies. Their presence does not prove NGX
recognized the feature. A first NGX initialization preceding UEVR injection
can prevent later DLSS-G discovery; the prior two-module RTX 4090 probe proved
this limit, and this candidate does not reset/shut down the game's NGX.
The actual Detnoir initial NGX failure code was not recorded, so its precise
cause remains pending the new native provider diagnostics.
No native-DLSS-G game success is claimed. Owner headset/game retry, Returnal
FSR rotation quality and external NR model requirement remain pending.
IPC ABI/mapping identity 45001 / V450; use the matched package.
