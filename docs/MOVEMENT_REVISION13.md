# Jumpbug recovery and landing investigation — October 23, 2026

Revision 12 is preserved in checkpoint commit `c885645`. The user reports that
air steering works well with `sv_airaccelerate 32`, and that disabling jumpbug
substantially improves hopping. Revision 13 leaves the steering solver and
ordinary bunnyhop policy unchanged. The tentative predicted-contact bunnyhop
experiment was set aside in `logs/movement-r13-deferred-bunnyhop.patch` before
this change; it is not in either rebuilt payload.

## Observed failure

`logs/overlay-33312-134372389997829026.log` identifies revision 12 and records
82 jumpbug attempts. Each has native jump-handler observations in the attempt's
tick; none of those ticks records a takeoff. This counts client predictions,
not authoritative server outcomes or proof about every possible jumpbug.

One concrete sequence:

| Simulation tick | Observation |
| --- | --- |
| 1645 | Jumpbug predicts contact at .552358; the native press at .546875 is still airborne and produces no takeoff. |
| 1646 | The next command publishes the required jump release. Native landing is subsequently stamped at .468750. |
| 1647 | First grounded command observes speed 330.492; the planner still waits. |
| 1648 | Ordinary fallback finally jumps, with horizontal speed 156.210 already present before the jump handler. |

The early press and the recovery delay are separate issues. Repeated press
timestamps are not successful jumps. Sample 2111 similarly records an airborne
press, release on grounded tick 2112, and a retry only at 2114, with speed
353.377 at the first ground observation and 170.581 at subsequent takeoff.

The later `overlay-27888-134372393324387841.log` contains no jumpbug attempt
records and still includes landing speed losses. Runs differ in duration,
trajectory, speed and physical input, so their aggregate loss medians are not
a controlled estimate of jumpbug's effect. The user comparison and the missed
attempt traces justify isolating jumpbug; they do not establish that all
remaining landing loss comes from it. The extraction is saved in
`logs/movement-r13-jumpbug-comparison.json`.

## Implemented correction

A timed press is issued strictly inside its request tick and explicitly
released in a following command. Recovery already adds a full interval to the
cooldown to cover the latest possible press within the request tick. Requiring
elapsed time to be *strictly greater* than that conservative bound added
another grounded tick when the penalty was an integral number of intervals.

Timed recovery now accepts equality with that bound after at least one full
released simulation interval and observed engine Jump-up. Thus the captured
1645/1646/1647 path can retry at 1647. The bound is discarded on clock rewind;
ordinary requests retain the previous strict comparison. This corrects the
planner wait, not the contact predictor or native speed changes.

## Contact timing remains unresolved

The predictor traces a copied scene-node origin and current collision hull.
The emitted command then crouches and uncrouches. Installed native code shows
airborne crouch/uncrouch adjusting move-data origin by opposite half-hull-height
differences and re-running ground categorization before jump handling. A
standing-hull trace alone does not prove the resulting contact time. Event
quantization, source prediction state and intervening movement also matter.
No guessed posture dimensions or timing offsets were added.

Diagnostics now record the exact probe origin, bounds and velocity, plus native
move-data origin around jump processing. Airborne duck-edge calls are included
even without Jump down, exposing the previously omitted first crouch step.
These remain copied observations through the existing bounded log queue and
existing read-only jump hook; no movement state is written by the observer.

## Verification and use

Both public and diagnostic x64 Release builds pass all 58 CTest suites. New
regressions cover the captured extra wait, release acknowledgement, duplicate
callbacks, longer penalties and clock rewind. The trace fixture checks copied
diagnostic coordinates. Native origin operands and the inspected crouch bodies
are frozen in the jumpbug compatibility contract.

All 29 installed-file compatibility components pass in
`logs/compatibility/20261023T144226075315Z/`. An altered native-origin offset
is rejected. Both rebuilt DLLs retain zero static TLS/delay-import directories;
identities are recorded in `logs/movement-r13-artifacts.json`. No project build
warnings were reported.

Build/test outputs use `logs/movement-r13-*`. No client or server was launched
or injected by the agent. The user-supplied runs above precede revision 13;
its effect on live speed and jumpbug reliability is not yet observed.

Keep Jumpbug off for the working revision-12-style hopping behavior. To test
revision 13, restart CS2 normally before the existing **Load Overlay.cmd**
workflow, then compare the same private/offline route and physics with Jumpbug
off and on. The log identifies `revision=13` and
`jumpbug=bounded_release_recovery`. Stopped code stays resident until normal
process exit. Fall-damage avoidance remains unverified.
