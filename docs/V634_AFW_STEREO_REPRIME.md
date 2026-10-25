# V634: retained-color AFW stereo re-prime

## Evidence and limits

The owner's V633 Horizon Quest/VDXR Legacy captures from PID12136 at
2026-10-22 23:58:43 and 23:58:50 use Combined, full resolution, debug enabled,
and Clear before warping disabled. Native motion is now correctly N-2.
The saved first outputs have only 0.138% and 0.00125% central debug green.
They do not capture the reported all-green flash. Second evaluations were
missing because readbacks exhausted the existing 1280 MiB F8 budget.

The second capture and flight log do establish a broken temporal contract:
publication 1839 is absent from AFW production; 1840 primes eye 0 in Alternate;
1841 immediately resumes Combined with eye 1 still retaining publication 1837,
rather than required publication 1839. Checking only the immediately preceding
opposite eye accepts this stale same-eye history.

## Change

For Previous/Combined with Clear disabled, require both contiguous opposite-eye
history and the exact same-eye N-2 source. After a gap, prime both eyes using
Alternate before resuming the requested temporal mode. The reduced-resolution
provider reset still responds only to an actual opposite-eye discontinuity;
the second prime must preserve the first eye's newly retained source.

Clear enabled uses a different pinned-provider protocol and retains the V633
opposite-only policy. This release does not claim to repair Clear-enabled
output. Mandatory depth/camera subtraction, N-2 motion selection, shaders,
gains, thresholds, provider binary, pacing, counters and swapchains are unchanged.
The continuity guard applies to all Luke routes with retained-color AFW.

F8 now omits subsequent upstream ngx_* images after AFW capture has begun.
Initial NGX images, exact consumed afw_ngx_*_before_crop guides, prior colors,
AFW inputs/outputs and terminal XR eyes remain. The owner's 44-shot sequence
falls from 1614.846 MiB to 32 admitted images / 1093.166 MiB, retaining both AFW
evaluations within the unchanged cap. Non-AFW F8 behavior is unchanged.
This diagnostic change executes only during an active F8 capture.

## Verification

Eleven focused checks pass in 5.69 seconds, including Horizon/Ghost SteamVR and
VDXR contracts, Crimson/KCD controls, depth GPU checks, bundled quality tests,
and an actual-provider continuity test. The captured gap sequence asserts
Alternate, Alternate, Combined for publications 1840, 1841, 1842.

Isolated real-provider child processes reproduce a 26-pixel wrong-eye error
without the guard and verify the retained-color recovery with it. Independent
Clear-enabled runs compare the exact V633 policy with V634 and produce matching
pixel receipts. The pinned provider has an inherited empty output at its first
Combined transition in that control; matching it is compatibility, not a repair.
Early failed tests and the invalid same-process provider reinitialization trial
are retained in local evidence. No complete replay of the missing owner output
or headset confirmation is claimed. The exact first bad Horizon revision remains
unknown. V634 is a local runtime-pending candidate; no live install or push.
