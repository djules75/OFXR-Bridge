# V595: diagnostics observe the director's clock cache

The owner asks whether switching diagnostic logging off can disable necessary
SteamVR behavior, rather than attributing the remaining drops to logging cost.
Audit finds one residual observer side effect. The physical period/vsync cache
is already refreshed independently of F9, but the diagnostic branch made an
additional native vsync query and wrote that same cache again. Consequently
logging could change when the phase was sampled, even though it did not turn
the director or its fixes on and off.

For a private-director session, diagnostic output now reads the existing cached
anchor without another native query. Required physical polling remains outside
the diagnostic gate. Non-private diagnostic observation is unchanged. The cached
diagnostic record's query-cost is zero: reading the cache issues no clock call.

The V594 private runtime implementation is unchanged. Clock conversion, source
and copy completion times, native-service budgeting, slot selection and both
native renewal policies remain active with F9 off. New D11/D12 GPU fixtures
disable optional diagnostic tracing and still require native renewal, all six exact
FIFO images, current native cycles and balanced Begin/End ownership.

## Remaining normal-2X delivery issue

V594 owner Valhalla PID9876 reaches the full maximum FPS but retains short
losses. The read-only external observer captured the full session, including
the complete automatic benchmark with F9 off. The retained detailed internal
trace covers only 84.935-128.775 seconds in manual Fidelity00, before that suite.
3402 unique content submissions in this fine interval are associated with
external SteamVR receipt IDs; 54 are never shown. For these 54, copy completion
precedes handoff by a median 14.007ms, CPU handoff is on its selected edge
(median lateness 2.050us), and raw native prediction is at least 19.335ms ahead.
These observations do not justify another blind latency or phase adjustment.
They also do not directly measure the downstream native GPU/compositor stage.

Future private-copy overlap is not sufficient to explain the losses: 43 of 54
unshown frames have no such overlap at their handoff. No new copy queue, GPU
wait or producer delay is introduced. Residual drops also exist with logging
off, so this cache-observer correction is not presented as their solution.

33 focused checks pass. Two earlier full runs failed the existing D12 fallback
cadence-precision assertion, at 12.421953ms and 11.501114ms against its unchanged
P+250us tolerance. The sequential same-host control then passes the complete
exact-V594 harness (D12 precision 11.121604ms, D11 11.115551ms), and the current
isolated cadence check passes (D12 11.122340ms,D11 11.113617ms). The full current
suite subsequently passes 33/33. Both failed outputs are retained; no runtime
pacing change or test-threshold relaxation was made. The cause of the earlier
timing variation is not established. Targeted clock checks also pass all 8 cases,
including D11/D12 logging-off delivery. This is not owner game acceptance.

Keep normal 2X with Lower VRAM off as the current priority; Lower VRAM and 3X
failures are explicitly deferred. No AFW/vector/interpolation, meter-formula,
VDXR pacing, private image/credit or XR swapchain budget change is involved.
No new INI setting or always-on diagnostic is added. Keep the candidate local
until owner acceptance. F9 can now record a normal 2X run without adding another
native phase sample to the director cache. Ordinary logging overhead remains;
this correction does not promise identical FPS or repair the F9-off drops.
