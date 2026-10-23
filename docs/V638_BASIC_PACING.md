# OFXR-BRIDGE 0.3.0 (v638)

Local laboratory candidate on the uncommitted V637 release-controls work.
No Git commit, tag, merge, push, publication or active installation.

## Basic pacing and benchmark scope

With `[ofxr] AdvancedMode=0`, Pacing offers Framework and Director only.
Framework+director remains available when AdvancedMode=1. Saved combined-mode
preferences normalize to Director in basic mode, so a hidden mode cannot remain
active. Factory private-transport pacing is Director. Original VDXR transport
continues to use Framework, independently of this UI preference.

Private-transport suites freeze AdvancedMode when started. Basic suites contain
21 optical tests, 33 complete tests, or 27 Luke Ross tests. Advanced suites retain
31, 49 and 39 respectively. Luke tests retain AFW 1x1 / 2x2 / 4x4, with no Fast
or 3X. Original VDXR suites remain 11 optical / 17 complete / 15 Luke tests.
Timing remains ten seconds per trial, excluding the first three seconds only.

CSV export uses the exact executed plan; omitted combined-mode trials are not
relabelled as Director. The private owner-sheet adapter preserves the existing
column layout and fills omitted Framework+director measurements with n.a.
That personal adapter remains outside the shipping package and source snapshot.

No frame generation, delivery pacing, capture, motion, depth or provider math
changes. V637 profiles, factory reset, tray controls and mandatory benchmark
CSV output remain. IPC/discovery is version-isolated as 63801.

Automated validation uses isolated profiles and a mocked downstream XR runtime,
including actual D3D12 resources; headset acceptance remains pending.
See V638_VALIDATION.json for the actual checks and retained initial failures.
