# V625: retain Outlaws warm outputs across Off and control changes

Base V624: e564f4390ebb56c3ef3533b67478f861a487b260. Local candidate.

The owner V624 Outlaws PID5020 run initializes three original sources successfully
before the first frame. Its retained detailed tail (about207.4-250.5s) contains
2,745 ready stereo projection mappings, but2,746 empty-mappings generation
bypasses and no AFW admission record. The complete sparse configuration stream
starts with OFXR/AFW Off, then enables AFW and OFXR independently. This is not an
observed swapchain budget refusal. Exact native count is fixture evidence only.

The shipping-layer GPU reproduction follows that control sequence after the
Outlaws pre-frame initialization seal. V624's general output cleanup destroys
the warm resources while both switches are Off. It marks the eye pending, but
Outlaws correctly refuses cold reinitialization after its first Wait/Begin.
Each valid later projection therefore remains at the arming bypass before AFW
or ordinary OFXR can run. The new test fails on V624 at its first Off revision.

V625 excludes the already-warmed, sealed Outlaws output set from that ordinary
control-driven retirement. Off still disables generation and final director
pacing. It pauses use of the reserved resources instead of destroying the only
set that this SINGLETHREADED producer can initialize. App/session destruction
and actual allocation failure retain their existing teardown behavior. Late
eligible/replaced sources still cannot cold-initialize during play.

The fixture now verifies seven Off/AFW/OFXR configurations, identical warm owners,
no pending stereo generation, no new native allocation, then exact left/right
GPU pixel FIFO delivery through all output rings and full teardown. Existing
six-native-handle publication pooling is retained. Other games, VDXR, AFW-only,
AFW motion/depth math, latency definitions and all pacing are unchanged. These
reserved outputs remain counted in Details even with both generators Off.

V624 UI, Swapchain budget fix, stable ofxr-bridge data root and benchmark-folder
button remain. IPC62401/provider45601 unchanged. Restart tray and Outlaws with
matching V625 files. No new full benchmark or F8 is initially needed; confirm
that AFW alone and then OFXR+AFW engage in the same level. Headset acceptance
remains pending; the GPU reproduction does not establish in-game FPS or image
quality. No activation, active INI change, registry mutation or push.
