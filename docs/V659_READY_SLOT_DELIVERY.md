# V659: ready packets use their first eligible physical slot

Owner requests a controlled trial after Crimson V658/0.3Director/AFW2x2 trace:
sameprocess8760, clean2credit82fps/76.971ms vs3credit78fps/88.342ms. Larger FIFO
also increased a monotone historical service floor from11.111ms to13.966–15.529ms.
Physical scheduling required accepted_at+thatfloor even when copies and native
Begin were complete.93of96two-slot intervals in the clean3credit window had
that floor as the sole observed previous-slot exclusion;54of60under2 similarly
excluded an otherwise eligible previous slot. Trace-aligned claim entry is
conservatively bounded by its post-call log timestamp plus5us. This is evidence
for a scheduling trial, not a feedback simulation or guarantee of sustained90fps.
V615 already had Director; its short90fps peak does not prove future throughput.

ExactV658 source copied into isolated ready-slot-delivery-v659. Remove the
physical scheduler's retained work floor and its observe_work capacity coupling.
Eligibility is max(accepted, prepared, nativeBeginReady+existing1.1ms,
now+existing100us preparation guard). Select the same absolute physical grid,
same1ms early handoff, slot uniqueness, last actual handoff guard, runtime clock
renewal/current-prediction validation and actual End completion. Late work still
misses an expired slot honestly; it never causes eviction, overwrite, duplicate
delivery, compressed catch-up or borrowing an unfinished source. FIFO admission
is still2/3, all3image slots retained, live changes drainaccepted first.

Physical delay telemetry is zero for the removed policy floor, not zero measured
latency. Actual receipt-derived Cycle minimum, generation and delivery timings
remain byteidentical V658, with no accounting subtraction. Trace19 keeps its
accepted timestamp with floor0; trace21 keeps actual completed work with added
floor0. D11 release readiness, source/overlay fences, copy completion, image
leases and GPU publication are unchanged. Framework bypass skips Director and
remains unchanged, as do Legacy and directAFWOnly. AFW math/markers/capture/Neural/
DLSS-G/FSR/optical engines unchanged. Physical policy is common to every Director
backend on the private bridge, including forced non-Legacy VDXR. Ordinary
AutoVDXR remains Legacy and is not affected.

When physical feedback is not yet available, retain the existing late-source
fallback and native service forecasting. Its refill bound is fixed to the
existing pair(1P), rather than growing with admission3. Thus Increase buffer
changes only admission on both physical and fallback paths. No new checkbox,
INI field, timing sampler, GPUquery or per-frame logging requirement.

Suite41/65/51 and private AQ:DU83-cell adapter unchanged.12actual captured slot
witnesses cover2,3aftertoggle andlater3, asserting the previously excluded slot
is chosen, no early handoff or repeated slot. GPU controls retain pending-source,
overlay/rewrite, exact stereo pixels/FIFO, stop/failure,3X and live2->3->2 checks.
Controlled35ms source stall is followed by fresh same-capacity packets and a
reduction3->2: no historical service floor may survive. Both fallback budgets
tested with finite irregular input. No publication or live installation.
