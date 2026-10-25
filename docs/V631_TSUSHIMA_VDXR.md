# V631: Tsushima native motion on VDXR

V629's owner F8 contains Quest 3 / VirtualDesktopXR, not SteamVR. The existing
Tsushima correction was gated on SteamVR, so both VDXR fields were declared
N-1 although they match same-eye N-2. The two captured field fits have median
errors 0.203/0.174 pixels for N-2 versus 9.766/9.728 pixels for N-1.

Declare Tsushima's measured native interval independently of XR runtime.
Horizon remains SteamVR-only; generic, Crimson and KCD capture contracts keep
their own existing rules. Preserve explicit scoped motion-span precedence.
Depth-aware subtraction/reconstruction, shader/provider code, crop, scalar
values, pacing, counters and allocation are unchanged. Base V629; no V630
queue or latency trial is included.

Offline replay retains the captured color, cameras, provider, mode and gains.
The second evaluation reproduces captured output at RGB MAE 0.0138/255 with
the original inputs. Replacing only plugin motion/depth with the existing N-2
native crop reduces central debug-green coverage from 60.70% to 2.54%.
First replay evaluation includes bootstrap and is not an exact matched control.
Original F8 payloads are read-only and are not packaged.

Eight focused checks pass: real native capture on both runtimes, Horizon
runtime scope, generic/Crimson/KCD controls, and the bundled full AFW shader
and provider quality test. Runtime/headset confirmation is still pending.
See [validation](V631_VALIDATION.json). No live install or public/private push.
