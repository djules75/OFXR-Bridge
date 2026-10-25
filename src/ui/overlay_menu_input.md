The headset input adapter borrows the accepted OptiInput algorithms from
OptiScaler V33034 (canonical source 6c36e598):
`menu/input/input_system_cursor.cpp`, `input_system_raw.cpp`,
`input_system_messages.cpp`, `input_system_window.cpp`,
`input_system_directinput.cpp`, and `native_key_state.h`.
It contains no OptiScaler initialization, configuration, or renderer dependency.

The renderer owns its ImGui context and calls `apply_imgui_input()` before
`NewFrame()`. Mouse movement changes a bounded virtual cursor directly in the
1024 by 768 menu framebuffer. Legacy input contributes client-coordinate deltas;
desktop origins and DPI do not define the position of the headset quad. The
renderer draws the same cursor position and does not load a Win32 ImGui backend.

Only the actual foreground root HWND owned by the host process can open the
menu. F10 toggles it, Escape closes it, and Insert passes to UEVR while closed.
Raw mouse/keyboard delivery, capture, and clip requests are restored to their
latest game-requested values. Foreground loss releases confinement instead of
restoring a background game's clip. No focus is stolen or restored. The last
controller restores its WndProc if it remains the current procedure; later hook
chains are preserved and their calls pass through.

While open, temporary mouse capture belongs to that foreground HWND. It keeps
mouse delivery on the HWND under the Win32 capture contract; it cannot prevent
the user activating another application. Game capture/release requests
are deferred until close, and loss of the menu's capture closes it. This is
separate from cursor confinement: no desktop rectangle positions the headset
cursor, and the desktop pointer is not clipped to the menu quad.

V442 serializes capture acquisition/restoration through explicit phases and
revision-tagged controls on the HWND thread. Restoration retains ownership
until all Win32 calls return: a nested game WndProc cannot start another menu
acquisition inside ReleaseCapture. Notifications queue a close instead of
reacquiring capture in the notification itself, as required by
[WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged).
Frames become open after acquisition completes. Obsolete posted controls cannot
close a newer opening, and last-owner teardown restores the WndProc only after
resource retirement. Game callbacks do not inherit the adapter's internal API
bypass. Raw registration and DirectInput routing remain the V441 behavior.
The frame exposes `requested_open` and `transitioning`: a renderer-thread poll
can report an acquisition still waiting for the HWND thread. The compositor
keeps the menu hidden and clears its ImGui input, but must not turn that pending
frame into another close request. Explicit focus/disarm/error closures cancel
the request. Repeated already-closed calls are idempotent.

`offset_pointer()` countershifts a consumed drag in the same framebuffer. Only
the current open owner can use it; it rebases queued positions and preserves
later relative motion, key/button events and native desktop cursor state.

Raw packets are read before masking, with a cached decision shared by the
WM_INPUT and GetRawInputData routes. Consumed WM_INPUT calls DefWindowProc for
system cleanup. Ordered button/key events keep clicks between XR frames.
Keyboard and mouse releases for presses already owned by the game pass through.
Loaded DirectInput 8 A/W system device vtables are probed so existing devices are
covered too; GetCapabilities and GetObjectInfo classify them authoritatively.
Buffered reads preserve owed button releases and event metadata, and DIGDD_PEEK
uses a copy of ownership state. Gamepad input is unchanged.

`overlay_menu_input_tests` checks production cursor/mask algorithms, custom
DirectInput offsets and PEEK ownership, bounded production raw buffer parsing,
actual foreground windows and capture/clip/raw-registration lifecycle, native
DirectInput devices created before controller startup, ordered input and ImGui
cursor alignment, actual reentrant ReleaseCapture callbacks, 32 native-input
reopen cycles and 47 render/UI-thread acquisitions. It generates no global keyboard or mouse input and activates
no game. Nonempty buffered Raw Input packets are portable production-parser
fixtures, not a claim of hardware input injection.

These input regressions address independent reentrancy and ownership defects.
They do not attribute the reported Planet Crafter crash to input: the owner's
runtime dump identifies ImGui ItemAdd/BeginCombo, covered by the separate
renderer/context fix and its validation.
