# V572: fresh runtime cycle after distributor pacing

Local candidate based on sealed V571
`7132fb6fd2160ce09f3d24e8cbc3e3c24f4c9c36`. Actual SteamVR improvement is
pending an owner trial. This does not establish a SteamVR defect.

## Evidence and correction

The owner's V571 run contains 8,450 accepted private packets, 8,450 real
End calls, and 8,450 successful returns, with no terminal transport error.
Nevertheless the settled Fidelity windows show approximately 72 real plus
generated outputs per second versus 44–45 compositor presentations. The CPU
handoff floor was respected; acceptance and End success are not scanout proof.
No exact packet-to-compositor-record identity is claimed by this analysis.

V571 selected the real runtime cycle before image preparation and before the
distributor's refresh wait. Its manually forwarded displayTime could not make
that earlier runtime cycle fresh again. V572 moves the distributor wait ahead
of real Wait and removes all manual displayTime forwarding from this consumer.

The content sequence is now:

1. Keep the FIFO head and wait for its exact private-input GPU fence.
2. Wait for the common distributor's minimum physical-refresh spacing.
3. Call real `xrWaitFrame`, then real `xrBeginFrame`.
4. Acquire/wait real images, copy, observe completion, release real images.
5. Call real `xrEndFrame` with exactly that Wait's `predictedDisplayTime`.
6. Retire the packet's private read leases and FIFO credit.

There is no deliberate pacing sleep after real Wait. The actual copy and its
ownership synchronization remain after Begin. This follows the frame-loop
recommendation in the [Khronos rendering chapter](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc):
Begin precedes graphics work, while Wait supplies synchronization and prediction.
An initial empty Wait/Begin/End cycle seeds the virtual clock immediately;
its Wait is no longer retained while a level loads. Stop closes any open cycle
using its runtime prediction, without inventing a replacement timestamp.

Private images, the bounded three-packet FIFO, producer detachment, backpressure
and the existing exact held-phase policy remain. This applies to all generators
using the common SteamVR endpoint, native D3D11/D3D12 and existing translated
Vulkan. VDXR dispatch and AFW motion/depth/camera/provider data remain unchanged.
It is not a rewrite of the upstream synthesis execution lanes.

## Diagnostics and validation limits

Existing transition codes remain: 5711 acceptance, 5712 successful real End,
5713 submitted/raw XR displayTime, 5719 transport failure. New codes use the
same packet sequence: 5714 private GPU ready, 5717 real Wait entry after pacing,
5716 real Wait return, 5715 real-image copy complete. Their b field is QPC ns.
They separate queue, pacing, runtime Wait and copy costs on the next real run.
The existing compositor meter remains the output measurement.

The GPU fixtures now require exact raw runtime displayTime and Begin before
real image preparation. The variable-horizon fake runtime itself now produces
monotonic predictions, as required by OpenXR. A saturated FIFO case checks that
the intentional pacing wait does not occupy its small-copy runtime window.
The runtime is mocked; this proves ordering/ownership, not real headset recovery.

An initial development experiment moved image preparation before Wait and used
a deliberately slow image-availability fixture. That experiment was superseded
after checking the Begin-before-graphics recommendation. Its local logs are
preserved, but it is not the final implementation or proof of the live fault.

Restart tray/game with V572. First compare Fidelity and Djules in the same
Ghostwire location, same Neural setting and 2X, with F9 enabled. About 15 seconds
each is sufficient for the first visibility/counter check; no full benchmark
is needed yet. Other routes share this exact final consumer. No automatic live
installation, activation or publication accompanies this package.
