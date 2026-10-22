# V628: preserve a stereo transaction across worker GPU backpressure

Base: V627, 2775a1ae22e94c02514a40ebec649023e53197f0. Local candidate.

Owner Valhalla V627 Legacy VDXR PID2584 completes Disabled, Fidelity Fast and
0.2.1 Fast, then fails on Djules 2X Fast. At130493.476ms the left prime succeeds;
the right prime immediately returns HRESULT0x800700AA (ERROR_BUSY). The existing
worker then completes the first eye, labels the stereo transaction failed and
latches XR_ERROR_RUNTIME_FAILURE. Release/Wait/End fail thereafter. The earlier
startup XR_ERROR_LAYER_INVALID is isolated and followed by normal rendering;
it is not the transition that terminates the benchmark.

The worker's bounded busy retry existed only for non-Djules Luke D3D11. Apply it
to every asynchronous synthesis worker: on ERROR_BUSY/WAS_STILL_DRAWING, release
the call lock, wait for that synthesizer's existing submission, reacquire the
lock and retry the same capture/destinations/camera once. The session GPU lock
is also free throughout the wait. No idle wait on a device or runtime queue,
new pacing delay, clock change, output drop, capture replacement or retry of
device removal is added. The original inline Luke Fidelity condition remains.
Timeouts and actual failures still propagate; no unfinished stereo output is
published. The successful path does not add a wait.

A real D3D12 queue gate recreates the second-eye refusal after a prior scratch
copy. All three new checks fail on V627; fixed Normal/Fast recover the same
stereo pixels after source poisoning, with GPU/call locks free. The timeout
case remains bounded; device-removal HRESULT is returned after one attempt.
GPU debug errors and resource leaks are checked. Existing VDXR private paths,
SteamVR transport, Luke worker and benchmark/UI checks protect unchanged scope.

The owner's second V627 Valhalla session PID31896 completes all31 opt-in VDXR
tests with21 FPS/cycle samples per step. It is preserved separately and exported
to the Quest block AD:CN. V628 does not alter that matrix or the private endpoint.
Legacy remains a restart-only tray preference. CMake0.628.0; IPC62701 and native
provider ABI45601 unchanged. No VRAM retention fix, installation or publication.
Hardware verification of the interrupted Legacy transition remains pending.
