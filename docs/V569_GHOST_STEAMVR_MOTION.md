# V569: Ghost SteamVR native motion interval

Parent V56867980d3d. Preserve its AFW reset recovery, V567 bootstrap,
V566 meter and18trial benchmark suite. Owner recalls Ghost working onQuest3.
This patch is gated to GhostOfTsushima.exe, detected Luke, and SteamVR identity
read from successful xrGetInstanceProperties. VDXR/unknown runtime retain the
previous interval. No headset-name guess, config switch or global vector fix.

## Captured failure

V568PID22560 F8 at21:49:04: two complete AFW evaluations, opposite source eyes,
distinct retained native feature streams with verified A/B/A history. Motion
1568square, final XR3136x2873. Input declaration says span1(N-1 opposite-eye).
Actual raw native field fits the exact same-eye N-2 depth/camera transform at
median0.0971/0.1083guide pixels; opposite-eye N-1 misses by5.8815/5.9110pixels.
Raw camera clip matrix still advertises N-1. Thus matrix/receipt order alone
cannot identify this field's actual interval. Do not scale or invert the flow.

## Offline replay of the actual owner images

Replay both source histories, exact cameras and captured plugin inputs through
real unchanged beta6. The second output matches the captured image atRGBMAE
0.00306442 with60.6868percent green. Retain the same colors, cameras, provider,
Normal-motion gains/signs; use the existing span2 contract's own native motion
crop and native depth crop, without false opposite-eye depth calibration. Green
falls to0.350499percent. First output is bootstrap-sensitive and is not claimed
pixel-identical; second output supplies the matched control. This is offline
capture/provider evidence, not a liveV569 headset acceptance.

## Change

The native capture declares span2 for the measured GhostSteamVR path. Existing
explicit scoped span declarations retain priority. This selects the already
implemented depth/camera subtraction and same-eye reconstruction. It also avoids
learning a false stereo depth remap from a field that contains no opposite-eye
stereo disparity. No mandatory depth/subtraction method is removed. Raw pixels,
gains/signs, cameras, eye order, source capture timing, provider binaries, NR,
shaders, OFXR pacing, counters and swapchains are unchanged. Other titles and
GhostVDXR retain their existing contract; no inference about Quest3input pixels
without a corresponding captured comparison. SteamVR identity defaults false
until successfully observed; capture policy does not query the runtime.

## Validation and trial

12focusedchecks pass: actual NGX capture GhostSteamVRspan2 and GhostVDXRspan1,
generic/Crimson capture, native depth/motion GPU scenarios, realprovider
interleaved history and2x2/4x4, inherited2x2epochrecovery, AFW terminalownership,
meter and benchmark. Shared internal setter adds no export or ABI change.
Fresh tray/gameV569: verify GhostAFW movement/debug; no newF8 before verdict.
Owner Crimson/DOOM V568 latest complete benchmark rows exported separately:
CrimsonS18:BF18 run6407004177784, DOOMS24:BF24 run6426665946068.18complete each,
21FPS samples/trial,3ssettle excluded, Q/Rpreserved, LukeAB/Preferblank. Local
adapter and captures stay outside DLL/Git. No online sheet changes or push.
