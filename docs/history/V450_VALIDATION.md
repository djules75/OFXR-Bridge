# V450 validation

Windows x64 MSVC optimized Release candidate, 2026-10-10, based on sealed V449
ddd74dac2699ea6eb9a2eb32854277eefaa946bd. Provider-initialization refusal
recovery and passive native NGX diagnostics; docs/V450_PROVIDER_RECOVERY.md.

## Direct before/after regression

An isolated physical executable directory omits nvngx_dlssg.dll. Real NGX
refuses initialization with DXGI_ERROR_UNSUPPORTED. The pre-fix regression
fails at 'provider refusal poisoned optical-flow recovery'. Fixed runtime
passes two actual provider refusals and recovery, exact provider/HRESULT bypass
status, actual live logger on/off, no generation revision for logging, retained
native refusal metadata, structural/private runtime/session budget gates,
then eight real/seven synthetic stereo submissions with GPU pixels and XR-cycle
accounting, no GPU debug errors. No provider fault shim or assertion relaxation.

## Complete final suites

| Fixture route | Result | Time |
| --- | --- | --- |
| new | 133/133; none skipped | 85.85 s |
| djules | 150/150; none skipped | 94.24 s |

All seven shipped runtime binaries match across both fixture selections.
The new fixture selection is restored before packaging. Full logs and binary
hashes accompany the package. Existing real ImGui preferences, fresh-process
persistence, loaded-layer IPC off/on/off/on/off logging, provider/guides/stereo
refusal/recovery, native DLSS-G on RTX 4090, FSR packed/split stereo, GPU ownership,
Streamline ABI, output-shape transitions and mandatory depth-aware Luke
correction tests remain intact. V448 signed FSR provider API 1.1.3 is unchanged.
Extracted checksum/import/catalog and packaged loader verification have a
separate receipt binding the package to its clean source commit.
V449 validation is preserved byte for byte under docs/history/.

## Scope and limits

The owner Detnoir flight log confirms late logging active, repeated unsupported
reconfigurations and missing projected generation buffers. Its initial provider
refusal precedes disk logging; enabling logging caused the failure is unproved.
DLLs in package ofxr/ are staged beside the layer and supplied to NGX. A second
game-directory copy is not needed and cannot guarantee late NGX discovery.
This candidate does not reset the game's NGX or implement late first-init
feature discovery recovery. Actual Detnoir native DLSS-G success and the
initial NGX rejection remain pending the new provider diagnostics. Owner
headset retry, Returnal FSR rotation quality and external NR model remain pending.
Use the matched V450 tray/layer, IPC ABI 45001. No live installation, activation,
registration, game launch, shared/canonical port, push or publication was done.
