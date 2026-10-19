# V599: retire AFW provider allocations after a reset

Ghost of Tsushima's second benchmark with Neural/DLSS5 enabled drops to 22 FPS
at Djules 2X/AFW 2x2, recovers, then slows persistently in the 4x4 round.
The retained trace shows marker-copy GPU waits rising from approximately 3 ms
to 40-64 ms, ending in a 250 ms timeout with no device removal. The first
collapse and precise black-screen instant are outside the detailed trace.

A separate actual-GPU reproducer confirms an allocation leak in the existing
AFW reset strategy: six isolated beta6 modules, each initialized at 2048x2048
and unloaded after GPU completion, retain about 1.315 GiB. Unloading a module
does not release its abandoned D3D12 objects. The pinned SDK has no shutdown
export. Texture-only retirement leaves roughly 3.2 MiB per reset; the final
implementation covers the provider's other D3D12 factory objects as well.

## Correction

Keep the existing fresh-provider reset on quality, producer-origin, geometry
and reduced-history discontinuities. This preserves the accepted workaround
for beta6's stale reduced-resolution lookup state. Reusing warm provider state
was rejected locally because the independent pixel reconstruction tests fail.
No such reuse, command-list facade or state-repair experiment is included.

Give each isolated provider a private ID3D12Device facade. Ordinary device
operations forward to the real device. Factory results receive a weak COM
lifetime marker through SetPrivateDataInterface; the marker does not retain
the GPU object. A normal provider Release clears its live marker, so teardown
does not release an already destroyed object. After GPU completion and private
module unload, retire one surviving factory reference per observed object in
reverse order. Additional foreign references remain valid. Never drain an
unknown reference count or call an undocumented provider destructor.

The first provider also uses an isolated byte-identical module, so retirement
cannot target a game/mod's shared provider instance. Prepare our completion
fence before provider initialization. Live resets normally find the previous
submission already complete through the existing allocator fence. Teardown
of a cold/failed context can signal a receipt and wait up to 250 ms; if GPU
completion is unproven, leave those objects alive rather than free pending work.
No additional wait or memory sample is added to steady frame production.

## Validation

The actual provider test captures 79 surviving D3D12 objects per 2048x2048
context. All six resets return to the exact measured memory baseline, instead
of accumulating 1.315 GiB. The committed regression also checks destruction
before retirement, an additional foreign AddRef, and idempotent retirement.
The memory allowance is 8 MiB for driver bookkeeping; the observed increase
in the controlled comparison is zero.

Full actual-shader/provider checks retain the original pixel thresholds and
D3D12 validation: live 1x1/2x2/4x4, gaps, producer resets, geometry changes,
interleaved history and Indiana camera axes. First-module isolation increases
the expected private module count; rendering expectations are unchanged.
The final suite result is in [V599_VALIDATION.json](V599_VALIDATION.json).

The director, V598 released-image ownership, V597 phase, AFW math, beta6 binary,
VDXR, counters, INI and benchmark layout are unchanged. The confirmed leak is
consistent with the owner's pressure/stall sequence; correction of the exact
black-screen incident still requires an owner retry. Candidate stays local.
