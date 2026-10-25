# OFXR-BRIDGE 0.3.0 (v641)

Local laboratory candidate. Close the previous tray and restart the game with
this folder's OFXRBridgeTray.exe. No active installation is changed automatically.

Display / Automatic tests contains Auto pick best one. It defaults off and is
saved per game. When enabled, a completed benchmark applies and saves a measured
configuration; the completion message identifies the result. Stop restores the
original settings. A manual edit cancels automation and is kept.

Benchmark durations, CSV columns, profile ownership and the V640 rendering paths
remain unchanged. OFA preferences remain saved independently for each route.
The new build simplifies the UI and updates automatic selection internally.

Common directory: %LOCALAPPDATA%/ofxr-bridge. Game profiles: Profiles/<game.exe>.ini.
Global tray settings remain in the main ofxr-bridge.ini. Reset restores factory
rendering defaults, preserving global tray choices. Benchmarks work without F9.

Automated validation is recorded in V641_VALIDATION.json; headset acceptance is
pending. No Git commit, tag, merge, push, publication or driver change.
