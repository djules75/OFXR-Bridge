# V448: align the embedded FSR runtime

The owner reports dirty ghosting/artifacts in Returnal V447 when rotating with
both mouse and headset. The working comparison was FSR inside the OFXR panel
of OptiScaler, not OptiScaler's independent native FG integration.

## Proved difference

The V447 FSR generator and record_fsr_pair implementation match accepted shared
V33034. However, shared embedded GPU test packaging copies the signed
FidelityFX-SDK/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll to the private OFXR alias.
Autonomous had instead prepared the modular SDK-v2 framegeneration DLL.
Source dependency hashes and direct ffxQuery version enumeration identify API
providers 1.1.3 versus 3.1.6; file versions are 1.0.1.41314 versus 4.0.1.2740.
See V448_FSR_PROVIDER.json. These provider identifiers are not marketing names.
No installed game/layer DLL was hashed, backed up or replaced.

V448 supplies a separate prepared copy of the same signed SDK runtime as the
shared embedded tests. CMake explicitly selects runtime/fsr-optiscaler/; the
previous prepared provider and all numbered candidates remain intact.
The staged private alias is still ofxr_amd_fidelityfx_framegeneration_dx12.dll.
It requires no OptiScaler proxy or checkout at runtime or in the prepared build.

## Input observations and diagnostics

Closed Returnal OptiScaler sessions from 2026-10-08 contain actual FSR context
creation at display 3072x3264 and guides 1536x1632, flags 8 (inverted depth).
V447's retained window contains distinct eye streams/publications and the
zero/invalid native-camera hash 9091909026564522765. The earlier sessions also
contain that hash: missing native camera is not established as a new regression.
No pixel/vector-field capture establishes whether their numeric values are
correctly projected in the game. Guide presence alone cannot prove quality.

FSR input diagnostics previously ran only in cinema projection. They now run
in ordinary VR too, at the existing once-per-120-pair rate, only with logging
enabled. Existing eye binding/resource/rectangle/scale/jitter/time/depth/FOV/pose
records remain; motion flags additionally report native camera validity (bit 3)
and matrix validity (bit 4). This is passive logging, with no correction bypass.

## Validation limits

This is a provider compatibility candidate. The provider selection difference
is proved, but it is not a proved explanation of the owner's visual artifacts.
Automated stereo generation, ownership and recovery tests are engineering
checks, not Returnal visual acceptance. The exact mouse/headset comparison on
V448 remains required. No shader, native vector scaling, camera fallback,
scheduler, FSR parameters or Luke correction is altered. V447's Streamline ABI
fix and all other functional guards remain. No live activation or publication
is part of preparation. See VALIDATION.md for final checks.
