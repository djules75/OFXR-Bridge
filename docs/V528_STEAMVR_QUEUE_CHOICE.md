# V528: explicit SteamVR queue choice in the tray

The owner requests testing shared and separate runtime queues, including active
generation: recovering the Disabled baseline does not establish which topology
has better generation stability. Keep both choices available. Do not infer an
OFXR regression from30FPS: the owner subsequently measured30 without OFXR too.

The tray context menu now contains **SteamVR queue (restart game)** with
**Separate from game** and **Shared with game**. The choice is saved in tray.ini
and emitted as `[ofxr] steamvr_separate_queue=1/0` in the runtime configuration.
Only the tray exposes it; no Settings/F10 control or IPC ABI field is added.
Separate is the default when no preference exists; explicit Shared persists.
An armed change writes only that runtime key, retaining current renderer/debug
settings, and sends no live control transaction. Restart the game to apply it;
no session is destroyed or recreated behind UEVR.

For ordinary native D3D12 SteamVR sessions, the startup choice now determines
the runtime graphics-binding queue independently of the initial backend,
generator, Hybrid selection or Enabled state. Shared binds the original game
queue. Separate creates the binding queue and release/reacquire fences already
used by the previous separate route. The synthesis queue and its priority,
required ownership transfers, output/pacing policies, A/B allocation flags and
PreferFPS remain unchanged. VDXR and established Luke/D11/Vulkan transport
selection remain outside this native SteamVR option. Queue creation failure
retains the existing fallback and records the actual result.

New startup/replay flight receipt `steamvr_queue_configuration`: result is queue
creation HRESULT (S_FALSE when not requested); a=session; b=requested bit0,
actual separate bit1; c=initial backend low8, generator next8, requested Hybrid
bit16, Enabled bit17. `session_binding` also retains actual capability16 for a
separate binding queue. Requested and actual allocation must not be conflated.

Engineering validation checks native route/Enabled/Hybrid combinations against
the actual production initializer and real D3D12 queues. A blocked runtime queue
also blocks a later game marker in Shared mode; in Separate the game marker
completes independently. Restoring the old route-dependent policy makes this
regression fail. Production tray persistence, armed-key preservation, unchanged
live-control revision, save-failure rollback, launcher roundtrips and startup
flight recorder checks accompany the existing output/compatibility regressions.
The fixture retires its GPU device before process-static layer teardown; its
initial early-return lifetime error was corrected in test code only.
Final optimized Release validation:66/66 applicable checks pass in58.08s,
including24 transport combinations and six3X cases. Packaged loader, AMD64
runtime identities and matched symbols are checked during delivery sealing.

Owner comparison: close game, choose the queue in V528 tray, launch the same
Nobody scene/settings/Hz with F9 before changes. Compare Disabled and the same
active generator/settings, then close/relaunch for the other queue. A/B, Neural,
AFW, multiplier and PreferFPS must remain constant between runs. Measure both
actual presentation stability and cycle metrics, not Disabled FPS alone.
Candidate is local/runtime-pending; no stability/latency/FPS benefit is claimed.
No live install, private push or HUD changes.
