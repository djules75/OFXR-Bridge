# V633: Horizon native motion on VDXR

Base: V632. The owner reports that Horizon worked on V483/Quest before the
SteamVR work, and now captures a green AFW debug scene on Legacy VDXR.

The V620 correction was deliberately gated on SteamVR. V632 therefore still
labels Horizon VDXR native motion N-1, although this new capture establishes
N-2 same-eye motion. The 25-point native RG16F/D24 receipt fits have median
errors N-1 1.8805/3.0254 px versus N-2 .01135/.01372 px for the two eyes.
The first captured output is 73.26% green in the sampled central region.
The F8 is partially incomplete: the second AFW input/output readbacks include
E_OUTOFMEMORY. Its exact native depth is independently available as an earlier
successful ngx_native_depth snapshot with the same stream and serial. Only
that exact identity is substituted for geometric analysis. No exact full
provider replay or improved headset green percentage is claimed.

Select Horizon N-2 independently of runtime, as already done for Tsushima in
V631. Preserve explicit scoped motion-span precedence and the required depth
and camera subtraction. Provider, shaders, gains, threshold, crop, resize,
allocations, pacing, and V632 estimated-counter code are unchanged.

Historical scope: V483 and V632 are not identical in common guide/history code.
In particular V561 introduced interleaved native histories into the shared
capture/AFW route; later title corrections were runtime-scoped. Therefore
SteamVR-only pacing changes did not imply that shared VDXR guide conversion
was untouched. The new F8 proves today's wrong temporal contract. It does not
by itself establish the first bad revision versus the owner's V483 result.

Nine focused checks pass, including both Horizon runtimes, both Tsushima
runtimes, generic/Crimson/KCD controls, a four-frame original-native geometric
oracle (SteamVR plus VDXR), and bundled provider full/half/quarter quality.
Initial run was 8/9: the existing Horizon VDXR test explicitly required the old
SteamVR-only restriction. Its expectation is updated to the newly captured
runtime-independent N-2 contract, and all nine final checks pass. No additional
runtime change followed that initial run. Hardware/headset validation pending.
No live install, active INI/registry change, or online publication.
