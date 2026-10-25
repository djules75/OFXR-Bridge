# V658 buffer benchmark matrix

Owner sheet checked read-only2026-10-24: AQ:DU,83measurement cells after two
manual game-only references inAO:AP. Snapshot SHA256
429e7cccadaf37172472be701bd22c25cb506ef1b3e635c9f2a4fae29769bf54.
No GoogleSheet edit, Git mutation, live installation or render/pacing change.

AdvancedMode=0 non-Legacy execution compares Framework and Director, each with
Increase bufferOff2 andOn3. For nonLuke, run Normal thenFast for each budget.
Five optical routes include Djules3X:41trials including one Off reference.
Full adds Hybrid/FSR/DLSS-G:65trials. One selected DLSS-G startup preset, unchanged.
Luke ends at0.3.0, no3X, vectors orFast. Four generators, two pacing choices,
two buffers, three AFW qualities plus one AFW-only reference per quality=51.
OFXR-Off/AFW-only references select the default2 once, not a buffer comparison.
Direct AFWOnly startup still bypasses this bridge and cannot start the matrix.

Execution groups Luke by quality to avoid unnecessary AFW resets; the portable
metric-row CSV transposes into route/pacing/buffer/quality triplets. All new
labels suffix the larger budget with Buffer; two remains Normal/Fast or the
quality label without Buffer. Each trial explicitly sets its budget through
the ordinary per-game Increase buffer setting. Before the10second timer starts,
require actual private runtime buffer_capacity2/3, applied revision, pacing and
AFW quality. A request alone does not label an un-applied limit as measured.

AdvancedMode=1 diagnostic plan retains its older combined-pacing comparison
and the manually selected buffer; it is not mapped to the owner's AQ:DU layout.
Legacy Framework plans/23-column owner export remain unchanged, with no buffer
sweep. No route/default is removed pending owner comparison. All prior settings,
including saved buffer, return on completion/Stop unless auto-pick selects a
measured winner. Existing auto-pick rules operate on each distinct configuration.

The main CSV remains portable and workbook-independent. The private owner
adapter stays in.local/tools, outside the DLL, source export and runtime ZIP.
It validates all83current headings, preserves historical63-column snapshots and
G:AC Legacy exports. Ordinary rows map65measurements plus18intentional blanks;
Luke maps51measurements plus32unsupported n.a. No extra buffer AFW-only columns.
Ordinary wide groups have Normal,Fast,Buffer,BufferFast,blank,blank; Luke reuses
the six as1x1,2x2,4x4 at2, then1x1,2x2,4x4 at3. Some fourth-cell headings only
show their Luke meaning; the reviewed adapter recognizes that exact alias.
Older CSVs lack an explicit buffer label and are rejected forAQ:DU rather than
silently relabelling a possible manually selected3 as2. They retain historical
snapshot export support. Missing trials stay n.a., unavailable metrics N/A.
One cell stays FPSmedian(min-max)LATmedian(min-max)VRAMMiB, rounded as before.

Logging remains optional.10seconds/trial, first3seconds removed, no trailing
cut; memory snapshot after9.5seconds before next switch. Compact-log parser now
retains index64 and groups pacing/buffer separately, avoiding merges between
the new65trials. No buffer change in shared delivery code or AFW math.
