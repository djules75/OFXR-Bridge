# V661: KCD2 non-pooled transport initialization diagnostic

Base V660, including short benchmarks and AFW-only Best Pick/progress.
This is diagnostic, not a claimed game fix. Two owner F9 windows in PID33780
show successful AFW bootstrap, then E_INVALIDARG in the D11/D12 source transport
initializer for both eye chains (3current/2synthetic outputs). Native budget
refusal is not the reported initialization failure. No AFW origin decode runs.

V661 emits the specific rejected validator only when F9 logging is enabled,
using synthesis_initialize with a=0x661D0000|part. Parts1arguments,2first texture,
3description/view,4current destination,5synthetic destination,6packed extent.
Parts100..105 carry expected device/queue, counts, view/state, backend/mode,
queue owner/type and argument rejection bits. Parts110..113 carry rejected
resource dimensions/format/flags/sample/layout and owner. Bit256 in part105
means strict queue-device ownership mismatch. No validation is bypassed.
Additional COM descriptor observations occur only in the failing F9 branch,
not in normal frames. No GPU readback, fence wait, copy or timing changes.
Source normalization after removing diagnostic lambda/return wrappers exactly
matches V660. Version661/IPC66101; official beta6provider45601 unchanged.

Next: close oldtray/game, matchingV661, budget fixOFF before startingKCD2,
F9ON before opening/loading the level, leave failure for15seconds and close.
One run only; no F8 or benchmark needed. Do not call AFW fixed before evidence.
