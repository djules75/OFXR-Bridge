# V530: DLSS-G model dropdown and A/C benchmark columns

The DLSS Frame Generation section in desktop Settings and F10 now includes
**DLSS-G preset (restart game)**: Game default, **Preset A (slower)** and
**Preset C (faster)**. Desktop Settings provides **Choose game executable**,
remembered across tray restarts. Choose the actual rendering executable, not
its launcher. F10 targets the executable of its own game process.

This narrowly adapts Elliott Tate's release 36 per-game public NVIDIA DRS
implementation, source b8048568c574f3a89847c18a4a0cd6d5aec22c39:
https://github.com/elliotttate/OFXR-Bridge/releases/tag/0.2.13.36-dlssg-V439
The two profile keys are FG override 0x10E41E03 and FG preset 0x10E41DF1.
Writes are explicit user choices, atomic at profile-save level and read back
for verification. Other profile keys/global profile are preserved. Game default
removes only those overrides. NVIDIA profiles can include several executables;
global NVIDIA overrides may still apply. Failed writes display the driver error
and do not pretend to have succeeded. No extra NVAPI SDK/binary is required.

**Restart the game after selecting A/C.** NVIDIA caches its model in the process;
changing the profile and recreating our feature does not switch it live in
Elliott's test. C needs the updated NVIDIA app beta FG model and a compatible
driver. The supplied runtime is unchanged. Faster/slower labels are the owner's
requested comparative labels based on Elliott's measurements, not guaranteed
FPS/latency gains on another GPU or headset. Model, generated resolution,
transport A/B and the tray's SteamVR queue are separate choices.

The process-start profile is captured read-only during layer negotiation.
Benchmark configuration bits17-18 record 1=A, 3=C, 0=unverified/default/inherited.
Changing the driver profile from F10 never relabels the current process.
This is a **configured driver request**, not independent proof of the actual
model loaded by NVIDIA; exporter reports retain that warning. Unknown presets
never fill A/C columns. Current GPU interpolation validity checks remain in place.
No synthesis/guide/depth/pacing/queue math changed and no IPC Settings ABI changed.

## Benchmark collection and export

The owner added the Djules3X B-only column after the runtime ZIP was sealed.
Use the current exporter from Git/the separate release asset or the benchmark
tools companion ZIP, rather than the older script embedded in the immutable
runtime ZIP. Runtime binaries and original V530 tags are unchanged. The new
column is AI; 3X AB moves to AJ. Unsupported/unmeasured B-only3X stays `n.d.`;
no2X result is substituted. Named portable output retains every valid complete
interval, including A/B combinations absent from compact destination layouts.

For A/C comparison, select A before starting, run the desired routes/flags with
F9 on, hold each20-25s in a fixed scene, and close the game. Select C, restart
and repeat under identical settings. The AI must select explicit compact files
from both correctly identified game runs; directory input still selects only
the latest process. Keep Neural, AFW, quality, scale, runtime, resolution, Hz,
UEVR SyncMode and HAGS consistent. Use Early/HAGS-on as the owner's initial
UEVR/native DLSS-G comparison. No image dump is needed.

The refreshed owner sheet has **40cells S:BF**: 38 measurements, BE Log left
blank, BF build. The prior order remains through FSR; **AW:AZ = DLSS-G A**
00/10/01/11 and **BA:BD = DLSS-G C**00/10/01/11. A/B in these columns still
means private synthesis / reduced XR allocation, not model presets.
Cycle estimated is the parentheses metric. Missing/short/unverified tests are
`n.d.`. Native DLSS-G3X has no column and cannot replace2X.

Pimax's existing compact block is now supported with `--platform pimax`, ten
cells **BI:BR** (eight measurements, BQ blank, BR build). Because Pimax and
Quest have one DLSS-G column, different process preset requests produce separate
context rows and must be labelled A/C. Quest remains eight cells G:N. Keep
one fixed transport combination per compact row: those layouts have no A/B
columns. No workbook is changed by the exporter.

Paste `row-N.tsv` at the first measurement cell of any block with the matching
column order; its values contain no cell addresses. `row-N-with-headers.tsv`
includes names and can start anywhere in an empty table. `measurements.tsv`
is a portable named table (game/build/route/model request/A/B/multiplier/Neural/
queue/FPS/cycle/warning), allowing another AI to map results to different
headsets or column orders. Keep the model label when transferring compact rows.
Do not paste a40-cell Meganex row into the10-cell Pimax block; select its layout
or map from the portable table. This preserves all measured combinations even
when the destination has fewer columns.

Focused optimized Release validation:71/71 checks passed in72.24seconds.
Exporter checks verify37measurements/40cells, separate A/C columns, Pimax layout,
unknown-model guards and metric selection. Actual A/C model/headset comparison
remains pending. No user's NVIDIA profile is written by these automated checks.

```powershell
python -B autonomous/scripts/export-benchmark-row.py 'C:/logs/run-A.benchmark.log' 'C:/logs/run-C.benchmark.log' --game 'Game name' --platform meganex --sync-mode early --hags on --metric estimated --out '.local/benchmark-A-C'
```

Read [the collaborator AI handoff](../../docs/ofxr-autonomous/V529_COLLABORATOR_AI.md)
with these V530 changes taking precedence over its older S:BA mapping. Owner
explicitly authorizes private publication of this next build before headset
testing. Pimax/game validation remains pending; HUD experiments stay paused.
