# V522 local candidate: SteamVR output budget and Reference display clock

V521 Valhalla/Meganex owner verdict: New 0.3.0 works (45 to 68 FPS),
Reference 0.2.1 turns green without visible gain, Djules 0.2.13 stays red;
F10 eventually stops appearing. This is a local candidate, not runtime acceptance.

Frozen V521 pid42268 log SHA256:
`9f824c42ff4834f549168ff171961a2e69ece09e676915525b01faf4ea9bfd4d`. Diagnostics remain local.

Two Djules selections fail the second eye's synthetic private swapchain with
XR_ERROR_LIMIT_REACHED. Reference completes and submits real/generated pairs,
but SteamVR reports roughly 41–45 delivered FPS. After an Off/On sequence its
presenter holds up to 20–21 ms: the adaptive application's prediction can be
22.22 ms on a 90 Hz display. New reports approximately 64 delivered FPS in the
last segment and remains the owner-confirmed working route.

Changes:

- Ordinary Djules OFXR on native D3D12 SteamVR uses the existing staged output
  implementation: retained owned GPU textures keep full 2X/3X capacity while
  only two private runtime swapchains per eye are created. This adds the
  existing handover GPU copies; performance needs owner confirmation.
- Reference's native D3D12 SteamVR presenter takes the physical display period
  from the existing SteamVR observer. The adaptive game prediction no longer
  controls the full-period output schedule. New's midpoint schedule is unchanged.
- A transient menu XR_ERROR_LIMIT_REACHED no longer permanently latches F10
  initialization off. A new explicit opening retries after input is released.
  Menu creation, close and failing acquire/wait/fence/draw/release/EndFrame
  boundaries now have `menu_status` flight records. The original menu failure
  is unproven: V521 has no menu boundary records.

AFW, Outlaws special initialization, camera/depth/vector math, Vulkan transport,
native DLSS/FSR policies and paused HUD experiments are outside these route
changes. Original KCD V603 and Indiana V701 acceptance remains separate.

Validation: before the renderer change, the stricter eight-output actual-GPU
fixture fails V521 during Djules projection admission. The fixed fixture also
checks Reference's 22.22-to-11.11 ms display-clock adoption, rejects invalid
measurements and protects New/Djules from the Reference clock policy. Existing
eight-real/seven-generated stereo pixel ownership oracle remains in that test.
Menu D3D11/D3D12 fixtures now refuse the first menu creation and prove a later
opening creates, draws and releases the menu without leaking a failed handle.
Final checks and artifact hashes are recorded in the local sealed receipt.

Optimized Release: 21/21 focused checks pass in 36.66 seconds, zero skips.
The first fixed run exposed the test's eight-handle cap also blocking the
subsequent auxiliary-quad oracle. The cap now ends after the isolated eye-output
admission phase; the unchanged full stereo/quad pixel oracle then runs. No
production change was made to accommodate that fixture failure. Initial and
negative logs are retained. Existing invalid default CPU director fixture is
excluded; no claim of a full 185-test suite or headset validation is made.

Owner test: fresh V522 tray/game, same Valhalla resolution/stereo. OFXR on,
AFW/Neural off; F9 before switching New -> Reference -> Djules -> New. Close and
reopen F10 after each route. No capture dump is required. Do not publish until
the owner accepts the headset result.
