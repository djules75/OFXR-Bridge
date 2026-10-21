# V620: Horizon SteamVR native motion interval

Base V619, source1fdeae3b4da3913f19e7376b06ecb384e4aa9c64.
Owner confirms V619 removed coloured contour edges, but intermittent flicker
and full-scene debug green remain. The new F8 is 1x1 and both eyes are affected.

The captured native RG16F field fits the preceding SAME-eye camera N-2,
although the Streamline clip-to-previous matrix advertises opposite-eye N-1.
The field and the camera-parameter receipt have different temporal endpoints.
On a fixed25point grid per captured frame, independent projective comparison
using original D24 depth yields median N-2 errors .0183/.0208px versus N-1
11.2204/11.8463px. The small numeric fixture retains those original inputs;
it does not use any generated provider image as its expected answer.

The existing offline actual-provider replay closely reproduces the second
captured debug output (full-image RGB MAE .000653 byte levels). Substituting
the native N-2 field and decoded native depth reduces green from80.60% to0.229%
of the full image, or86.30% to0.029% in the sampled central region. Debug stays
ON, ignore threshold stays2.5, and camera matrices/provider/motion gain are
unchanged. First-frame provider history seeding is approximate; no bit-exact
claim is made for that earlier evaluation or unobserved gameplay.

Extend the existing title/runtime interval selection from Ghost to Horizon
SteamVR. Mark native publications as N-2 before retaining history. The existing
mandatory depth/camera subtraction then uses the correct endpoint; same-eye
jitter is already present, and the alternating-eye depth calibration is correctly
ineligible. A native scalar observation still has its existing precedence.
No motion inversion, clamping, raw-field fallback, threshold, debug-mask or
shader adjustment is added. VDXR retains its old interval, proved by the
negative runtime capture fixture. Ghost and ordinary Luke capture are checked.

V619 reduced-colour workaround is retained following the owner's report that
the coloured edge is gone. There is no additional resampling change, swapchain,
wait, pacing change, logging mode or configuration control. Provider ABI and
pinned binary remain unchanged. No live installation or publication.

11 focused tests pass, including actual NGX/SL snapshot capture for Horizon
SteamVR and VDXR, the captured native-field geometric receipt, Ghost positive/
negative runtime capture, unmodified generic Luke capture, and actual beta6
1x1/2x2/4x4/interleaved/live-quality GPU checks. Owner next: matching V620 tray
and Horizon, same scene, AFW debug to check the static world no longer goes
green, then debug OFF for the intermittent flicker and reduced qualities.
Headset acceptance and the intermittent symptom remain pending.
