# V529: logged measurements and paste rows

The owner reports Early improves UEVR and Fidelity works; Shared is slower and
does not cure instability. HAGS stays enabled for native DLSS-G. These are owner
observations, not a new renderer algorithm or full candidate acceptance.

F9 now enables the existing CPU interface-cycle measurements even with Settings
and F10 closed. At250ms cadence, the same OutputFPS and CycleLength/Estimated
telemetry used by the menu is recorded with validity/source and applied revision.
No GPU timing commands or waits are added. Existing clocks, generation, capture,
ownership and pacing remain unchanged. F9off returns to ordinary UI-demand
sampling. Recorded cycles are the menu's metrics, not motion-to-photon latency.
OutputFPS is the OFXR menu's measurement, not UEVR's Unreal `stat fps` counter.

Configuration receipts identify every selection and independent A/B, PreferFPS,
3X, native guides, requested/effective Hybrid, Neural and actual binding queue.
The existing asynchronous writer also saves `ofxr-benchmark-*.benchmark.log`:
only compact benchmark/session records, retained when the verbose flight log
wraps. F9off/on makes another uniquely named part. Failures of the optional
sidecar leave ordinary flight logging running. Dropped-record warnings remain
visible; no render-thread filesystem writes.

`scripts/export-benchmark-row.py` reads these compact logs and emits TSV plus
raw interval/setting provenance JSON. Meganex output has35cells for S:BA:
33measurement slots, AZ separator, BA build. Quest output has8cells for G:N.
The existing owner-sheet column order is preserved, no workbook is modified.
Unmeasured/short trials stay n.d.; invalid cycles stay n.d., not zero. Separate
rows distinguish Neural, Shared/Separate queue and Luke AFW context. Last
complete repeat of a cell wins; its exact settings, sample source, file and
interval stay in JSON. Requested Hybrid without effective guides and no confirmed
generation are marked in the detailed report.

Ranges are min/max of valid menu samples after3s settle and1s tail; require at
least10 useful seconds, round to integers for the owner's existing cell format.
The full precise numbers remain in JSON. Parentheses default to CycleEstimated;
`--metric length` selects CycleLength. No substitution between the two metrics.
All files in one exported row must have the same build. Directory input selects
only the latest process and its parts, not every prior game in that directory.
The assistant must identify the game/process before selecting explicit files.

Keep scene/resolution/Hz, HAGS and UEVR SyncMode fixed for one comparison row.
With F9on, hold each selection/checkbox combination20-25s. No fixed order,
capture hotkey or menu left open is required. Close the game and report the
game/runtime/SyncMode; the assistant returns a tab-separated line and starting
cell. For Djules, test PreferFPSoff for the ordinary00 cell and on for its
dedicated column. Include2X/3X and A/B if those columns are wanted.

Example command (local paths supplied by the operator):
`python scripts/export-benchmark-row.py LOG_DIRECTORY --game GAME --platform meganex --sync-mode early --hags on --metric estimated --out OUTPUT_DIRECTORY`

Engineering checks cover hidden-menu metrics/units/source, compact preservation
through flight rotation, all33Meganex columns and A/B/PreferFPS/3X mapping, cycle
selection, missing/short intervals and unchanged renderer/control compatibility.
The first implementation used the verbose basename for the sidecar; the existing
live logger test caught two flight-looking files. The sidecar now has its own
prefix; no existing fixture was weakened. Owner log/export validation pending.
Final optimized Release:70/70 applicable CTest checks pass72.69s. The Python
exporter fixture separately verifies all33measurement slots,35totalMeganex
cells, metric selection and missing/short guards. Source layout verified against
a fresh owner-sheet read-only export. No workbook edits.
