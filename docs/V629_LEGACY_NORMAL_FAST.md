# V629: complete Legacy Normal/Fast benchmark pairs

Base V628: 1ad2e1efcd993965147486a239c43b4ed5258176. Local candidate.

The refreshed owner sheet G:AC has Normal/Fast pairs for Fidelity, 0.2.1,
Djules2X and0.3.0, with the third slot reserved for Luke AFW4x4. V627/V628
incorrectly ran only Fast for those four optical engines. Legacy now runs
Normal and Fast for every supported route: 11 optical or17 complete trials.
Djules3X still has its two Normal/Fast slots; Hybrid/FSR/DLSS-G add two each.
One Disabled reference is retained. Labels consistently say Normal/Fast.

Legacy Luke remains15 trials: AFW alone once at each1x1/2x2/4x4 quality, and
four OFXR routes at those three qualities. No3X, Fast or vector-generator
trials. Modern VDXR and SteamVR retain31/49/39 trials and all three pacing
choices. Trial duration, leading3s exclusion, VRAM snapshot, preset selection
and restoration/cancellation behavior are unchanged.

The personal unbundled adapter now validates the actual Normal/Fast Legacy
headers and maps23 cells G:AC. Non-Luke uses each pair and leaves the third
quality slot empty; Luke uses all three quality slots and marks unsupported
routes n.a. The historical Lower VRAM alias maps only to Fast, never Normal.
Old Fast-only runs cannot supply unmeasured Normal values. Exact source/header
and family guards remain. No spreadsheet cells are written automatically.

The actual controller completes17 normal and15 Luke trials in isolated tests;
its CSV files pass CLI export against the fresh sheet, each source route
mapped exactly once. Synthetic unique-value tests verify23-cell order, blanks,
unsupported slots, old aliases and header/duplicate guards. Existing modern
adapter regressions pass. Display is tested for the11-trial Legacy button
and31-trial opt-in button. Two focused CTest suites pass; packaged loader is
checked separately. These are schema/controller tests, not headset timings.

No renderer, pacing, allocation or capture behavior changes. The owner's
observation is that director increases latency without a benefit on VDXR;
Legacy remains enabled by default, and the optional comparison is retained.
CMake0.629.0; IPC62701/provider45601 unchanged. No live installation, owner INI
mutation, push or release. New Legacy benchmark needs an owner game run.
