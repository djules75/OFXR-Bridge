# V575: remove accumulated timer wakeup overshoot

Local candidate based on sealed V574
`41a710d1c0233b914db4d300edf35e80482346c2`. The owner reports that V574 improves
all tested Ghostwire routes to approximately86FPS without Neural, but asks why
they cannot reach90 despite a game reference near65FPS. Actual V575 headset
validation remains pending.

## Evidence

V574 PID35068 contains21,547 private acceptances and21,547 successful real End
returns, no transport failure. Settled light-route output medians are roughly
84.5–85.9FPS; production is roughly86FPS. Reference/Fidelity/Djules/New common
handoff intervals have medians11.60–11.63ms, versus the physical11.111ms period.
Their final copies are ready at least200us before the deadline for approximately
98% of settled outputs. In that early-copy subset, the residual wait still
overshoots the deadline by about0.49–0.51ms median,0.90–0.92ms p90. That repeated
positive wakeup error explains the common ceiling; it is not all synthesis cost.

The brief Disabled window after3s settling has only8 compact rate samples:
production median71.896, compositor-output median68.215. It is insufficient to
declare a stable65 production baseline or attribute the entire historical71-to65
difference to GPU copying. Do not relabel the sheet's OFXR-Off column as a
fresh unloaded-DLL measurement. No spreadsheet values are changed.

## Change

The consumer retains the high-resolution timer for the long residual wait but
requests wakeup before the deadline. In the final window it checks QPC with
YieldProcessor until the deadline. The active precision window is capped at
min(1.25ms,physicalPeriod/8), on the consumer thread only, with cancellation
checked each iteration. This trades a small bounded amount of consumer CPU
time for reduced systematic scheduling error. No global timer-resolution,
thread-priority, affinity or Windows setting is changed.

An additional2us call-edge guard covers timestamp quantization and native
function-call overhead. The target/raw OpenXR prediction is unchanged. No
handoff may occur before a full physical period. Real late copy/runtime work
still shifts the phase by its exact overrun; no catch-up burst is introduced.
The end-target audit is logged BEFORE capturing the handoff edge so logger
work is not placed between that edge and the actual native End call.

This applies only to the existing common SteamVR private consumer, D3D11/D3D12
and existing translated paths. V574 preparation overlap, V573 dedicated GPU
delivery queue, FIFO3/backpressure, private image ownership, A/B, AFW math and
provider, VDXR, and the compositor meter remain unchanged.

## Validation

With the same actual GPU fixture and ready output queue, exact V574 code gives
11.628823ms mean spacing,85.993FPS. Final V575 gives11.113419ms D3D12 and
11.113470ms D3D11,89.981FPS for both. These are fake-runtime fixture numbers,
not measured game/headset gains. The full35 focused checks pass; see
V575_VALIDATION.json for the exact list and duration.

The first precision attempt exposed sub-microsecond spacing failures that the
old timer jitter had masked: logging was between the timestamp used to advance
the schedule and the real native call. Moving that audit before the timestamp
and adding the2us guard fixes the actual edge. The strict minimum-spacing
assertions were retained, and the final full35 were rerun successfully.

First trial: restart tray/game withV575, same Ghostwire location/resolution,
Neural-Off,2X,F9. About15s Disabled,15s Reference,15s Fidelity checks the brief
baseline plus both delivery cases. Compare real+generated against compositor
output. No full benchmark or new dump is needed. No automatic installation,
activation or online publication accompanies this package.
