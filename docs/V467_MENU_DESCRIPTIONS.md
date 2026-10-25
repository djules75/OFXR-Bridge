# V467 menu descriptions

Base: sealed V466 (1f37cb326d14773a5b60c8f9430482d2298f1066).
Shared desktop/F10 menu captions and explanatory text only. Frame generation entries describe relative speed and DLSS input requirements. OFXR engine display order is FidelityFX, Nvidia 0.2.1, Nvidia 0.2.13 (DJules), Nvidia 0.3.0. Persisted backend IDs remain 0,1,3,2 respectively, preserving profiles and route behavior.
Luke Ross alternate/previous entries are marked for debug. Clear/Ignore motion/Depth dilation are always visible, without the Framewarp options fold. Ignore motion and depth dilation explanations wrap below their controls to fit both surfaces. Lower VRAM and the R19 camera checkbox have owner-requested descriptions.
Renderer, shaders, providers, FSR guides/math, scheduler, telemetry, defaults and IPC46501/provider ABI45601 are unchanged. Relative performance/compatibility captions are user-provided guidance, not new measured guarantees. Tray Enable/Disable remains the V466 toggle.
Only shared widgets, logger version metadata, layer and tray targets are rebuilt against verified V465 static renderer libraries; six provider/queue DLLs and INI are reused byte-for-byte. No GPU or game tests. Existing CPU-only widget/navigation checks cover display-order mapping and independent preferences; package checks cover hashes/imports/catalogue.
