# V447: Returnal Streamline 1 compatibility

The owner reports that Returnal reaches Fatal Error with the autonomous tray,
whereas OptiScaler ran it. Four fresh V446 crash dumps on 2026-10-10 identify
the same CPU access violation before the first generated frame. The latest
process is 28916, RHI thread 25220. Returnal's interposer reports version 1.3.3.0.

## Exact cause

The loaded V446 image timestamp and SizeOfImage match the sealed checkpoint.
The original build DLL is byte-identical to that checkpoint. Its original
linker map places RVA 0x5ba90 in native_ingress::evaluate +0xf0. Disassembly
shows `mov rax, [rdx]` while iterating the modern inputs array. RDX holds
0x43ff in the latest crash, the frame index passed by the legacy caller.
The other three dumps fail at the identical RVA with different frame indices.

Streamline 1 calls evaluate(commandList, feature, frameIndex, viewportId).
Streamline 2 calls evaluate(feature, FrameToken&, inputs, count, commandList).
Their Constants layouts, setConstants arguments and return types also differ.
V446 installed the 2.x detours unconditionally. Consequently it dereferenced
a 1.x integer frame index as an array pointer. This is CPU ABI misuse, not a
missing motion-vector or provider-discovery failure.

The accepted shared OptiScaler source explicitly chooses typed 1.x versus 2.x
hooks by interposer major version. Autonomous had not carried that distinction.
The two NVIDIA Streamline 1.3 declaration headers are retained here with their
original notices and exact bytes from accepted shared source 6c36e598; the
clean public reference is ec96f42. This does not import its upscaler substitution.
Full dumps/logs remain local under diagnostics/v447-returnal-streamline;
the sanitized symbol/diagnosis receipt is shipped in package evidence.

## Replacement

Streamline camera observation now resides in its own production translation
unit, called by the same native ingress. A positive version resource from the
loaded image must agree with the presence of slGetNewFrameToken: 1.x without
the export binds legacy SDK declarations; 2.x with it binds modern declarations.
Unknown, missing or contradictory version evidence leaves the interposer
unmodified. A failed hook installation retires both hooks. Successful hooks
pin the original module before activation.

Each SDK's Constants are decoded through their actual field types, preserving
the original arguments and result. Both evaluate paths select only the exact
frame/viewport camera and restore the prior thread-local camera on return,
including nested evaluations. Modern ambiguous viewports remain refused.
No latest-camera or opposite-eye fallback is introduced. Colour optical flow,
owned native guide snapshots, mandatory Luke correction, NR submission fences,
renderer/pacing, settings defaults and V446 output admission remain unchanged.

## Regression and remaining validation

Five real DLL fixtures export the SDK functions as sl.interposer.dll. They
exercise 1.x, 2.x, unknown major, conflicting version/export and missing version.
The test calls GetProcAddress exports through the real SafetyHook detours,
with guard-page-sized SDK Constants, original arguments/results, nested scope,
failed constants, wrong frame/viewport and modern viewport ambiguity checks.
Reintroducing V446's unconditional modern binding produces SegFault in the
legacy caller. Typed binding passes all five cases. Complete optimized Release
suite and package results are recorded in VALIDATION.md.

Owner Returnal/headset validation of the exact V447 remains pending. No game
launch, live installation, tray activation, registration or publication occurs
as part of candidate preparation. V446 and all predecessors remain preserved.
