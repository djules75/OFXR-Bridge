# V622: AFW-only direct delivery experiment

Base: V621 (`91471645af2b3fc7b32ec46fbde629be30bfe021`). Runtime validation pending.

The tray now has **AFW only (Luke Ross, restart game)**. It defaults off.
For a detected Luke Ross session, enabling it before launching the game:

- Forces the effective OFXR switch off for that session, irrespective of saved
  engine, vector, multiplier, Fast mode, pacing and subsequent OFXR requests.
  The saved OFXR preference is retained for the next ordinary session.
- Skips the SteamVR private runtime endpoint altogether: no transport FIFO,
  preparer/waiter/consumer workers, private final binding queue, or final
  transport copy. AFW terminal images go to the real OpenXR dispatch inline.
- Prevents OFXR presenter promotion. Native AFW scratch and required D3D11 /
  Vulkan interop remain; they are not optional OFXR temporal generation.
- Skips D3D11 post-AFW temporal history/synthesizer allocation and defers native
  D3D12 optical-flow resources. Does not replace AFW's accepted depth/camera
  subtraction, motion contracts, per-title capture, quality or resize behavior.
- Preserves AFW controls and the independent Neural Rendering / DLSS5 path.
  Disabling AFW in this mode leaves the original real-frame delivery path.
- Reports AFW-only in F10, withholding unavailable OFXR controls. A normal OFXR
  benchmark matrix is refused while this transport is active; it would falsely
  label repeated AFW-only samples as different OFXR engines.

Non-Luke sessions ignore the flag. On VDXR a selected Luke session remains on
its existing native inline route with OFXR disabled; there is no SteamVR private
endpoint there to remove. With the flag off all V621 routes are unchanged.

This is a startup transport experiment. Changing native Wait/Begin/End ownership
and swapchain handles inside an existing session is not attempted. Restart the
**game** after changing AFW only. No restart is needed for Benchmark mode.

## Timing comparison

The existing three meters remain: OFXR time, Cycle length, Cycle estimated.
Their definitions and sampling-demand gate are unchanged. AFW-only attaches a
CPU receipt to the actual native EndFrame and its own converted display target;
this is after any Luke virtual-source prediction has been replaced. It adds no
refresh period, no GPU query/readback, no frame queue and no synchronization.
The native AFW GPU readiness and runtime Wait/Begin/End still have to complete.
The accepted Luke source-identity filter/prediction clock remains necessary to
avoid delivering an old AER image as a new production. This experiment does not
claim zero latency or measure physical panel scanout.

## Benchmark mode in the tray

**Benchmark mode** reads/writes the existing `[benchmark] enabled=0/1` in the
single canonical `ofxr-bridge.ini`. It enables the existing Display controls and
on-demand suite sampling. Turning it off stops a running suite, restores its
settings and retires the benchmark sampler lease immediately. It neither enables
F9 logging nor disables the normal visible latency meter. Defaults remain off.
The AFW-only preference is stored in `[tray] afw_only=0/1`; the generated runtime
shim receives `[ofxr] afw_only`. There is no new user INI or IPC layout change.

## Owner comparison

Use matching V622 tray/game, same title/scene/refresh and AFW quality. Record the
three meters and actual FPS first with AFW only off and OFXR manually off; then
close the game, enable AFW only and repeat. Keep DLSS5 in the same state in both
runs. Do not use the OFXR route matrix while AFW only is active. Headset image,
latency and performance acceptance remain pending; automated GPU results are not
claims of a measured headset gain.
