# V583: selected-preset benchmark and early handoff comparison

Base V582 `0addef6f4ee3d8338203e12de316c78950d379ff`. Owner replaces the earlier C-only benchmark policy.

## Preset policy

The single user ofxr-bridge.ini stores `[dlssg] preset=1` (A) when no explicit
choice exists. Existing obsolete benchmark-forced C profiles are corrected to
that preference by the new tray before the remembered game's next launch.
Explicit dropdown choices C(3) or game default(0) are persisted in that same
section and preserved when remembering another game or saving other settings.
Tray-only startup/discovery applies the preference to that game's profile,
skipping a write if already correct. No global driver profile is changed.
C rejection retains the existing explicit A fallback. Selecting a preset still
requires a game restart; changing a running game's profile cannot change the
already loaded model. A new game's first discovery may therefore affect only
its next launch. Build/tests do not modify the owner's live profile or INI.

The complete suite now starts with A, C or game default without modifying the
NVIDIA profile, requiring C or forcing a restart. Labels say DLSS-G, not C.
Detailed CSV snapshots the immutable startup request and records A, C or
game-default/unverified truthfully. This is not proof of the active NGX model.
No A/C model switch is performed mid-suite. Optical/Luke test plans, Lower VRAM,
durations/trim, VRAM sampling, CSV column order and restoration are unchanged.
The default-off `[benchmark] enabled=0` gate remains in the existing INI.

## Paired comparison

V582 retains the exact V581/V579 distributor. V583 starts from V582 and adds
only the V580 preparation change: metadata/optional trace work100us before the
unchanged final deadline, the2us guard in the final spin and saved-edge
bookkeeping/tracing after native End. No shortened period, cumulative advance,
FIFO/backpressure/pose/AFW/interpolation change. It does not guarantee100us
relative to an unexposed compositor cutoff. The VDXR path is unchanged.

Close game and previous tray before starting either version, allowing the
remembered game's A preference to be applied before startup. Compare the same
scene/route/preset/flags/refresh. V582 and V583 share the same user INI.
The difference is early preparation only; keep the better measured result.

Focused tests cover full A/C/default runs without restart enforcement, truthful
CSV, preference/target preservation in one INI, real offline ImGui controls,
profile validation and tray/loader behavior. V583 additionally checks common
SteamVR cadence/ownership and generator routes in D3D11/12. Exact passing set
is recorded in V583_VALIDATION.json. Headset validation pending. Local only.
