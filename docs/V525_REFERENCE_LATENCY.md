# V525: Reference latency priority on native D3D12 SteamVR

The owner defers Valhalla/MUTAR and supplies a Ghostwire Tokyo UEVR comparison.
Counters agree in UEVR. Before Neural rendering, New and Djules present about
90 FPS with a reported 55 ms cycle; Reference oscillates between 40 and 75 ms
and presents about 60 FPS. The owner explicitly selects latency over FPS for
Reference, with 44 ms as the requested cycle target. This target is not a
measured result or a hard timer/counter clamp. New and Djules retain their
throughput policies; Virtual Desktop retains its route.

Frozen V524 pid37112 confirms native D3D12. Stable Reference windows around
32-53 s and 78-115 s have about 64 accepted submissions/s and 61 presented
compositor frames/s, versus about 90/90 on New and Djules. Reference's source
release globally waits for the preceding session synthesis: median about
11.1-11.2 ms, with app End median about 12.4-12.6 ms. The complete-window
compositor accounting agrees here. Later Neural/load phases are separate;
no first-part comparison is attributed to them. Some later diagnostic absolute
timestamps contain discontinuities; their whole-span raw rate is invalid.

V525 removes the global preceding-pair CPU wait only for active queued native
D3D12 SteamVR Reference OFXR using a separate runtime binding queue and ordinary
direct storage. The output retains immutable history leases and an exact GPU
completion ticket. Capture-slot, destination and work-slot reuse still enforce
their own ownership/fences, and the consumer waits for its exact S/B completion
before opening XR. The producer may render and capture its next frame while
that retained output completes. The existing one-pair/two-output admission
limit is unchanged; no extra buffer or presenter thread is introduced.

Cold Reference startup can create the same isolated runtime binding queue
already used by New, so switching from New is not a prerequisite for the
overlap. Queue ordering transfers remain intact. If queue creation fails,
the existing shared-queue path and wait remain. Shader/flow encoding, fixed
midpoint, source poses, output cadence and frame acceptance accounting retain
their contracts. AFW, special Outlaws startup, Hybrid, FSR, native DLSS-G,
D3D11/Vulkan interop, Lower VRAM storage and Virtual Desktop are excluded from
this Reference overlap change. No MUTAR-specific presentation experiment.

Validation uses the actual layer and GPU with three separate queues: block the
preceding Reference pair after producer End returns; render and Release the
next left/right input while that pair remains blocked; then complete it and
verify eight real/seven synthetic stereo outputs plus poisoned auxiliary
ownership and clean D3D12 debug output. V524 fails because the next Release
waits globally. V525 passes. Route policy checks explicitly exclude Virtual
Desktop, New/Djules, AFW, FSR and staged storage. Focused Release regressions
and packaged loader evidence accompany the candidate.

32 applicable checks pass. The unchanged interactive menu-input fixture cannot
acquire Windows foreground, including an isolated retry; both raw failures
are retained and explicitly marked environment-blocked. Actual D3D11/D3D12
menu GPU checks pass. No input-hook change or foreground-assertion bypass.

Owner validation: fresh V525 in Ghostwire, same scene/settings, Neural and AFW
off, F9 before Reference. Check Cycle length/Cycle estimated and Output FPS.
The first priority is shortening the cycle toward 44 ms while preserving
correct presentation; a higher FPS count alone is not acceptance. These cycle
fields are not measured motion-to-photon latency. Headset benefit is pending,
the candidate stays local, and the HUD investigation remains paused.
