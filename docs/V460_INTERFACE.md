# V460 ? shared counters, timing and status

Base: sealed V456 `a1fcf74e123f825eefd998f558907cb702212718`.
Branch: `autonomous/v460`; isolated source `work/isolated/v460-interface`.
Private UI/telemetry mapping ABI: **46001**. The existing native FG forwarder
ABI remains 45601: this is a separate handshake and its provider behavior is preserved.

## Interface

Both surfaces use `render_settings_widgets`: Output FPS, Game FPS, Generated FPS,
Display Hz; then GPU render, OFXR delay, Presentation and the current generation
state in the fourth column of the same row. Vectors and wrapped Status follow;
the existing Frame generation / Luke Ross / Neural Rendering / Display tabs remain.
Full native input, guide, application revision, CPU production and refusal details
remain under Details. There is no guided setup or duplicate dropdown recap.

The sRGB palette comes from upstream `status_panel_model.cpp` at
`9579149c341c19e31f1fa9e48d56dc56b96e6872`: background 14/18/28, text 238/241/247,
dim 148/158/178, green 92/224/142, warning 255/192/72. The embedded ImGui font
is unchanged; larger sizes are baked by the existing dynamic atlas.
The headset retains 1024?768, F10/Escape, input ownership, blank-space dragging,
size/reset footer and menu priority when only one spare composition layer exists.
The corner counter remains independent, upper-right by default and configurable Off.

Luke detection comes from actual ingress statistics, not the selected AFW mode.
Vulkan comes from the original observed Vulkan binding even after its D12 bridge
substitution. Both can coexist. The primary blocking reason precedes detections;
duplicate messages are omitted, text wraps, and no-session/healthy status reads
No active game / OK. A real NGX evaluation refusal carries the original NGX result
into its immutable receipt and the UI. Existing provider initialization HRESULTs
are retained; missing error codes are not invented.
Vectors are labelled DLSS only for a recent typed SR/RR motion observation.
Selecting FSR FG cannot produce an FSR label. No FSR/XeSS adapter is added.

## Measurement contract

- GPU render: SteamVR `m_flPreSubmitGpuMs`, sampled through an independent accessor.
  It may include bridge work and GPU contention; it is not isolated game GPU cost.
- OFXR delay: application end-frame entry to first successful downstream real-frame
  send, including generation preparation and pipeline retention. The original
  frame identity and QPC stamps survive snapshot, AFW and owned/borrowed presenter
  handoffs. A failed send does not consume identity; repeat, empty and synthetic
  sends do not create samples.
- Presentation: predicted displayTime converted by the dispatch's actual
  `xrConvertTimeToWin32PerformanceCounterKHR`, less the real send QPC stamp.
  This is estimated, not measured scanout. Negative values read Late by ? ms.
  An absent/failed conversion makes only this measure unavailable.
- Details CPU production: return from application xrBeginFrame to entry into its
  xrEndFrame, in the same QPC domain. This is separate from GPU render.

These intervals are never summed or presented as a total roundtrip.
Each timing uses a median over samples in the last second. Publication runs at
4 Hz; the UI invalidates a publication after two seconds and displays N/A for
unavailable sources. Output falls back to distinct accepted submissions when
SteamVR delivery is unavailable; its tooltip and source distinguish scanout from
submissions. Game/generated rates use the original V456 content receipts: refused
native copies and repeats cannot become Generated FPS.

Visibility uses independent desktop/headset leases in shared memory, with no
profile writes, generation revision changes or reconfiguration. Normal close,
focus loss, minimization and teardown clear the relevant demand; an abandoned
lease expires after one second. New QPC timing collection, conversion and the new
SteamVR timing accessor are guarded by this demand. The timing accessor tries
its mutex and never consumes the presenter's cursors or closes its delivery window.
Existing corner-counter and pacing observations remain as in V456.
There are no new GPU timestamp queries, runtime command lists, fence waits or
mandatory logging. Test-only GPU readbacks use their own commands/fences.

## Scope and validation

Renderer, shaders, guide association/admission, synthesis parameters, frame-call
ownership, native provider discovery/recovery and pacing decisions remain V456.
The native provider change is one assignment of its existing refusal result into
receipt metadata. No V457/V458/V459 Vulkan change, hybrid route, extrapolation/ASW,
R19 camera/scheduler or V453 GPU profiling is imported.

Automated coverage includes timing identities, retries/failed sends, repeat and
synthetic exclusion, median expiry, independent missing clocks, late presentation,
actual FSR-selection/DLSS-source wording, simultaneous detections, all main refusal
states, visibility without settings changes, and real OpenXR D11/D12 composition.
Both menu GPU readbacks and a desktop preview are supplied as evidence. Populated
preview values are deterministic fixture data, not live game latency measurements.
Open/closed cost measurements are CPU application-frame costs in a WARP fixture;
these do not establish in-game GPU overhead or headset performance.

The final suite totals, timing and package checks are in VALIDATION.md and the
packaged evidence. The candidate remains disarmed and has not been installed,
registered or activated in a game. Owner headset/game verification remains pending.
