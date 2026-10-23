# V647: V641 restart, Outlaws source marker outside the FPS overlay

Owner corrected the prior tests: AER1/2 was selected unintentionally. All builds
AFTER V641 are excluded as a combined baseline, including V626bis diagnostic.
This candidate starts from exact V641 packaged source, independently extracted
in work/isolated/outlaws-v641-restart. V641 Luke AFW math, native guides, depth,
camera subtraction, continuity/admission, layer, pacing, provider and capture
recorder remain byte-identical. No V645 jitter or V646 retained-input changes.

New correct-AER owner run: SteamVR, V641 PID38800. Retained flight has2098 failed
AFW preparation attempts and zero synthesis_pair events. F8 has40 records:16 XR
colors,8 native motion,8 depth and8 color diagnostic attempts; zero AFW evaluations.
The compact origin log's826 successful reads plateau long before the F8, then
missing markers continue to accumulate. A successful marker read alone does
not prove an AFW evaluation. Waiting is truthful in the retained interval.

Pixel evidence: F8 image0 top-left shows the Luke FPS83 overlay atx0..53,y0..37
covering the barcode prefix. Enlarged offline decode of all16XR captures over
700x180 returns no CRC-valid token; this is not merely a too-small read rectangle.
The original token strip is visible at the edge, but its identity prefix is
occluded. Exact identity is still required; no guessed/partial-token admission.

The sole production rendering change reuses the individually reviewed marker
protection from V642: Outlaws.exe only, inset256x32 for the same256x8 strip,
matching reader ROI with16px final-translation margins. Other process layouts
remain unchanged. At the captured2800x2576 native /2800x2566 XR geometry, the
strip starts x258,y44, outside the observed FPS rectangle. Same token, CRC,
producer boundary, guide identity and resource count. No extra GPU pass/copy,
fence rendezvous or swapchain; compact reader ROI is slightly larger.

This is a candidate to restore actual AFW activity in Legacy on SteamVR first.
No claim about vector quality or remaining flashes until that path produces.
Quest/VDXR testing deferred by owner. This is not a current build with an old
compatibility flag. Version647/IPC64701 are identity changes only.
No Git mutation/push, live installation, game/INI/registry/driver changes.
