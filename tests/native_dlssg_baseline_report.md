# Native DLSSG V440 hardware baseline

The exact imported Elliot helper from commit
`135185a033453ae7f844b867c7ef34ae26d40ab3` was compiled into a separate local
Release executable. Its source came directly from `git show`; the unchanged
native HLSL/generated shaders, official NVIDIA SDK 310.9.1.0 runtime and current
deterministic stereo fixture were shared with the autonomous test. This target
contains one native helper and the existing GPU fixture; it does not build a
second copy of the old product or an OptiScaler runtime.

On the actual RTX adapter the unmodified upstream helper reproduced the initial
autonomous result exactly: eye 0, uncropped UNORM, midpoint MAE `6.95494` against
blend MAE `1.02443`, with the best matching horizontal displacement exactly
the expected four pixels. A readback probe localized all interior error to rows
16 through 20; row 21 onward matched the expected motion pattern byte for byte.
The saved image shows NVIDIA's version indicator text in rows 0 through 20.

A bounded read of native 64-bit
`HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\ShowDlssIndicator` found DWORD
`1024`. NVIDIA's SDK documents this driver indicator; this is existing user
state and applies to production libraries as well. The SDK runtime does not
show a confidential or do-not-distribute watermark. No registry or game files
were changed. The inherited optional `DLSSG.ShowDebug=0` parameter did not
suppress the driver-controlled indicator in an isolated temporary probe.

The strict fixture retains its original MAE threshold, expected eye motion,
stereo separation and cropped exterior checks. It defines a content ROI only
when both the exact enabled registry value and actual black/white indicator
readback are present. The excluded vendor overlay and tested content rows are
printed with the test result; an unrecognized header keeps the original ROI.
The resulting eight eye/cropped/sRGB checks pass on the actual adapter with
MAEs approximately `0.00002` to `0.00032`; native reseed assertions also pass.

Local baseline evidence is under ignored
`src/neural/build/native-baseline/`: `hardware-baseline.log`,
`hardware-image-probe.log`, and `native-output.png`. These diagnostics are
excluded from the production build and package. The upstream A/B proves the
autonomous lifetime fixes did not cause the initial numerical failure.
