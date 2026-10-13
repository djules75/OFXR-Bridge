# V519: selected KCD branch merged with pre-HUD V513

V519 integrates the owner-selected latest KCD V603 delta and its V601 terminal
diagnostics onto the clean exported V513 line. It preserves DOOM native camera
row correction, sparse marker copyback and batched independent eye marker reads.
No V514-V518 HUD implementation is included. Original source tags and ZIPs
remain unchanged; see the shared KCD selection and milestone mapping.

Runtime remains pending for the merged DLL. Original KCD V603 still has the
owner-reported distant-tree wobble; this merge does not claim to fix it.
Mandatory depth-aware native subtraction/same-eye reconstruction/AFW Normal,
guide/color ownership, route separation and external provider ABI are retained.

Broad V513 reproduction passed 178/179 automated checks. The default CPU mock
director test timed out twice: its fixture clears the session registry before
invoking the actual EndFrame dispatcher, which requires a registered session.
A local fixture-only investigation is excluded from the exported branch.
The real GPU director tests pass. Integration checks explicitly exclude only
that existing invalid mock setup; no production renderer behavior is altered
to accommodate it. This limitation remains documented for later test repair.
