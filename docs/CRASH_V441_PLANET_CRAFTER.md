# V441 Planet Crafter reopen crash

Owner runtime report, 2026-10-09: the first F10 menu opening worked; reopening
after changing options crashed. The current Player.log and Windows dump both
identify an OFXR menu draw on the game's D12 rendering thread.

The real fault is access violation 0xC0000005 at module RVA0x17FA7B, reading
address0x438 through a null pointer. The V441 release had no source symbols;
a separate diagnostic relink generated a map without changing its preserved
binary. The relinked .text section and code RVAs are byte-identical to the V441
checkpoint. The resolved stack is:

`ImGui::ItemAdd+0xDB -> ImGui::BeginCombo -> ImGui::Combo -> generation ->
render_settings_widgets -> draw_menu -> render_d3d12 -> render_menu ->
application_frame -> layer_end_frame`.

Unity's repeated OFXR_SetOverlaySettingV1 label was its nearest exported name,
not the actual function in that stack.

## Cause and physical correction

The old close reset called SetNavWindow(nullptr). This removes the navigation
window but deliberately does not clear the selected widget NavId. Hidden OFXR
menus run no ImGui frames, so reopening submits the same window and Combo IDs
without a normal disappearing/appearing focus transition. ItemAdd matches the
surviving NavId and dereferences the null NavWindow.

V442 physically replaces that call with FocusWindow(nullptr), the vendored
ImGui complete focus transition. It coherently retires navigation ID, scope,
layer and requests. There is no ImGui null-check patch, fallback, GPU/atlas
rebuild or frame-generation algorithm change.

## Reproduction and limits

A fresh real WARP D12 process using the V441 reset, two focused Combo warm
frames and reset/reopen exits0xC0000005. The same fixture passes after the fix.
Expanded D11 normal/SINGLETHREADED and D12 coverage exercises144 reopen cycles
across four colour formats, edited options, popups, dynamic atlas changes,
draw callbacks and exact GPU retirement. Final counts are in VALIDATION.md.

V442 separately serializes capture acquisition/restoration on the HWND thread
and preserves pending acquisition intent across a render poll. Actual nested
ReleaseCapture callbacks and repeated cross-thread input fixtures verify that
guard; it is not the attributed cause of the ItemAdd fault.

No game was launched or live DLL replaced during this repair. The corrected
candidate still needs the owner's Planet Crafter test, especially its same
changed-option sequence and mouse alignment while moving/scaling the menu.
