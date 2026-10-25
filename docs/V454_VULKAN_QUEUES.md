# V454: stop blocking independent Vulkan queues

Parent: sealed V452, 3accd73e6bc43c1f4864dc5a067a333072192aa2.
V453 native GPU profiling is deliberately absent from this candidate.

The owner measured 95 FPS after disarming/restarting No Man's Sky, versus
37.4 application FPS in the V452 loaded/FG-disabled log. Active retained
OFXR phases were approximately 61-75 physical submissions per second.
The disabled window contains no synthesis/history/presenter records, and
its XR wait duration is 0.006 ms. This proves activation-associated loss;
it does not identify which component accounts for the entire loss.

The Vulkan implicit layer held one device-wide mutex across every driver
QueueSubmit, QueueSubmit2/KHR, QueueBindSparse, QueuePresentKHR and QueueWaitIdle.
A blocking operation on one queue prevented submissions on every other queue
of that device. That can add stalls to game/driver calls outside the XR timings.

V454 keeps a stable mutex per actual VkQueue. Registry access is brief and ends
before waiting for the queue or entering the driver. The device table remains
owned for the full call, including its queue mutex lifetime. Device destruction
still removes the device table; queues from different devices cannot share a gate.
Failure to allocate a gate returns VK_ERROR_OUT_OF_HOST_MEMORY rather than
allowing an unsynchronized driver call. Normal driver results are unchanged.

The Vulkan requirement is external synchronization of the exact queue:
[vkQueueSubmit](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html),
[vkQueueWaitIdle](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueWaitIdle.html).
Cross-queue GPU dependencies are still enforced by existing semaphores/fences.
No same-queue protection is removed and no native guide/camera policy changes.

The test includes the actual queue-layer implementation and holds the fake driver
inside WaitIdle on queue A. All six entry points on queue B must still progress;
all six entry points on queue A must remain blocked until the wait is released.
The same test fails on unmodified V452 with "independent queue blocked behind
another queue's WaitIdle", then passes in V454. No layer registration or game
launch is involved. This is a reproduced synchronization defect, not proof of
NMS's exact in-game queue usage or the recovered frame rate.

The Vulkan color bridge/resource flags, compositor, renderer and cadence remain
V452. GPU game validation is pending. No return to 95 FPS is claimed yet.
