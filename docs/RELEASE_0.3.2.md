# OFXR-BRIDGE0.3.2: local candidate series V746-V749

Base: Djules nightly V735 b95478841c8db7b9665332599764bc16075c9bfe.
His V733 live camera-list tracking, V734 producer-device Execute hook and V735
OSR near-object band fill remain unchanged in ancestry with their authors.

Four independent functional commits, in this order:

- V746: recover expired native OpenXR cycles in Framework bypass.
- V747: retire GPU temporal anchors after quarantine/presentation-domain changes.
- V748: place native DLSS-G guides into resized XR viewports by verified cameras.
- V749: stabilize Director clock phase with Increase buffer. LAST AND OPTIONAL.

Compare V748 against V749 to isolate the Director change. Except version stamps,
that final commit changes only the phase policy and its regression tests.
The default buffer capacity remains2; phase stabilization applies with Increase
buffer (capacity3). No added pair or historical latency floor is introduced.
The Framework expired-cycle correction is present in BOTH comparison builds.

No RIFE UI, code, models, Python, worker, resource rings or runtime locator is
included. V740 experimental worker-failure diagnostics and the separate Witcher
HUD-mod guard are also excluded. RIFE work is in ofxr-rife-development.

Local candidates, not published. Game/headset validation remains necessary.
F10 still also toggles Witcher Cinema Mode; use desktop Settings for the initial
full-VR vector-generation comparison. Installed mod and hotkeys are unchanged.
