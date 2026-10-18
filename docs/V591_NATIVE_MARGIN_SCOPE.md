# V591: avoid charging native headroom after late GPU preparation

The V590 Valhalla benchmark regresses against V585 in several combinations:
Reference Lower VRAM median63.976 ->52FPS; Djules2 normal74.188 ->67.499;
New normal75.405 ->70.8. Djules3 normal remains90. Lower-VRAM Djules improves,
so this is not a uniform regression or an isolated proof of compositor loss.

An earlier detailed V590 Reference run exposes a concrete added cost:611
native-margin adjustments total689.621ms. Only4 have a late native Begin;
684.396ms comes from waiting for later GPU preparation/release. These events
belong to that detailed run, not the latest benchmark's useful sample window.
The full benchmark ran with detailed logging off after startup. Produced and
presented rates in that window cannot be reconstructed from its CSV alone.
An additional fresh V585 manual comparison is preserved locally too.

V590 used release completion as the start of its1.1ms native margin. With
paired/irregular input this imposes another pause whenever a GPU copy is late,
even though native Wait/Begin finished several milliseconds earlier. V591
uses native Begin completion instead. Source/GPU lateness keeps its existing
exact deadline/refill handling, without another post-copy native margin.
Genuinely late native entry still gets its exact missing held headroom.

The one-ahead native Wait worker, independent preparer, source refill, image
ownership, queue priority and physical minimum period remain unchanged. No
new image, swapchain, buffer credit or producer sleep. VDXR and AFW/vector/
interpolation math are unchanged. This common SteamVR correction is not keyed
to a game name. Stage5727 b now identifies native Begin completion, not release
completion; its c remains the exact added margin. No extra diagnostic work.

The new actual-GPU/mocked-XR check forces native Begin before an8ms image wait
on four finite inputs, in both D3D11 and D3D12. It requires zero native-margin
charges, exact FIFO pixels and the unchanged minimum physical interval. A
separate8ms native Wait still requires positive held headroom. Exact V590
production code fails the new late-preparation check; V591 passes. All33
focused checks pass. These results prove the corrected charge, not recovered
headset FPS or elimination of every pre-existing compositor loss.

Compare Valhalla with V591 in the same scene/settings and benchmark sequence.
Use F9 for a short manual Reference/New comparison if investigating produced
versus presented rates; another image dump is unnecessary. Local candidate,
no live installation, publication or change to the online V585 release.
