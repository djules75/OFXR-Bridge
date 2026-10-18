# V589: one extra millisecond before native entry

The owner asks to open the OpenXR cycle1ms earlier. This comparison is based
on sealedV588 and changes only the constant margin in PrivateRuntimeBudget:
learned native service cost + learned positive variation +1.1ms, versus +0.1ms.

The physical EndFrame deadline, headset refresh period,100us final metadata
window,2usguard, held phase and source refill stay unchanged. This does not
shorten the refresh period, add a producer wait or allocate images/swapchains.
The adaptive forecast remains capped at one physical period. The current
consumer also finishes the prior End before beginning its next cycle; where
that bound is reached, the full extra1ms cannot be obtained. It is a margin,
not a new concurrent/pipelined OpenXR frame loop.

Earlier native entry can age its prediction by the requested extra1ms. Existing
freshness tests explicitly allow that increment (2ms fast Wait,4ms delayed Wait)
while physical cadence, throughput, FIFO pixels and no-drift checks are unchanged.
33focused checks pass. No MSFS/headset improvement has yet been measured.
No diagnostics, GUI,AFW,benchmark,INI or VDXR behavior changes accompany this.

Restart game and tray and compareV588/V589 in the same scene/settings/refresh.
A short0.3.0 run withF9 is sufficient initially. Native-entry forecast is already
reported by the existingF9 stage5726. No new capture or full benchmark is needed.
