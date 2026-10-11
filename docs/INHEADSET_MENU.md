# V444 in-headset controls

The menu uses the same OpenXR end-frame composition owner as the FPS counter.
A private 1024x768 color swapchain supplies a 4:3 head-fixed VIEW-space quad,
clamped to the common eye FOV. It is independent of the small numeric FPS
texture, so turning the counter off does not remove interactive controls.
Application layers, frame next chains and display times are preserved. Empty
submissions remain empty. A menu with no available compositor slot immediately
releases input; on a one-spare-slot runtime it has priority over the FPS quad.

F10 opens/closes; Escape closes. Keyboard navigation and a drawn mouse
cursor use the menu framebuffer. Relative raw input moves that cursor directly;
absolute-device input contributes normalized movement and legacy client mouse
messages contribute deltas. Neither the mirror's position nor its letterboxing
is used as a headset hit-test rectangle. Mouse drawing and widget hit testing
therefore have one coordinate system. Desktop and XR focus loss, runtime frame
failure, manual disarm and teardown close capture and clear ImGui active input.

Drag any empty area of the menu to move the entire headset quad. Native ImGui
window/child movement ownership distinguishes these presses from controls,
scrollbars, disabled items and popups. The drawn cursor stays on its grabbed
texel during the drag; the input producer rebases its snapshot and queued
positions by the same consumed displacement. The fixed texture and hit-test
coordinates do not change when the quad moves or changes physical size.

The footer remains outside the scrollable settings and exposes Interface size
(0.50x to 1.25x) and Reset position / size. One times retains V441's size; 1.25x
fills the limiting common FOV dimension while preserving the 4:3 aspect ratio.
Bounds keep the complete quad visible. Changes
preview while held and save on release or close through the existing delta IPC.
Profiles without these three new keys retain the centred, one-times defaults.

V442 corrects the real V441 Planet Crafter reopen crash: hidden menus must clear
navigation ownership with FocusWindow(nullptr), not only SetNavWindow(nullptr).
The latter left a live widget NavId with a null NavWindow. See
[the crash receipt](CRASH_V441_PLANET_CRAFTER.md) for the actual dump and before/
after regression. Capture acquisition/restoration is also serialized on the
window thread; pending acquisition intent survives an earlier render poll.

The tray and headset physically share one widget implementation. Frame
generation, Luke Ross, Neural Rendering and Display tabs include all autonomous
V440 options. Edits merge only changed fields into current IPC state; a stale
tray draft cannot overwrite an independent headset edit. Requested/applied
revisions and runtime messages report progress. Resolution changes commit on
mouse release or close. This menu exposes no bypass for the accepted Luke
depth-aware camera/stereo correction and same-eye interval reconstruction.

V444 replaces Output storage with the Lower VRAM checkbox, default off. The
generation tab always shows native SR/RR input observations, motion/depth
presence and owned/XR-matched/used guide counters. Requested settings and
actual generation are separate: unavailable guide-driven FG reports real-frame
bypass and a reason, while OFXR game-motion refusal reports optical-flow fallback.
See [V444 source origin and crash evidence](V444_SETTINGS_AND_GUIDES.md).

GPU resources belong to the overlay. The D11 backend swaps a private driver context-state object and restores the
complete application state, including SINGLETHREADED devices.
The D12 implementation adapts the accepted V33034 immutable renderer to private
lists, per-image meshes/atlas/descriptors and exact queue/fence receipts. It
does not assume sequential swapchain image indices. Busy images retain ordered
input events and skip draws without a frame-thread GPU wait. Teardown obtains
a fresh owner-queue receipt; an unproven live-device completion retains resources
instead of freeing an executing draw. A shared context mutex serializes ImGui
across multiple XR session threads.

Win32, raw-input and DirectInput keyboard/mouse routes are intercepted only for
the foreground window in the layer's process while the menu is open. Prior
game-held releases, raw registration changes, clipping and capture ownership
are handled explicitly. DirectInput probes the loaded system implementation,
covering mouse/keyboard devices created before injection. Controller/XInput
input is outside this keyboard/mouse menu. The layer remains resident until
normal process teardown; closing the menu does not unload it.

The GPU fixtures prove real text/cursor pixels, color formats, state restoration
and retirement. The OpenXR fixtures prove composition, timeout ownership,
focus closure and layer limits. The actual Win32/DirectInput fixtures prove
coordinate and capture behavior. These checks do not replace a headset/game
trial for perceived alignment, game-specific input paths or performance.

This UI does not repair late native DLSS-G provider discovery or unobserved
native guide-resource ownership. V442 retains the known late NGX startup limit;
see VALIDATION.md and TESTING_IT.md.
