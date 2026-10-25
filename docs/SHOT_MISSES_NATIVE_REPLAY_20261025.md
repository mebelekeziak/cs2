# Native replay, ambiguous commands and the ordinary takeoff guard

October 25, 2026. The user reports recurring apparently unmissable shots and
the diagnostic `several possible aim commands` after testing revision 8.
The completed log contains the native final-accuracy observer introduced in
that revision. This pass reads the log and installed files; it does not run,
attach to, inject into or interact with a client/server session.

## Preserved capture and method

Complete-line snapshot:
`logs/research/multiple_commands_20261025/r8-26460/overlay-26460-134373972778562546.log`

35,097,002 bytes; SHA-256
`ae00cc7fa30bc7dfc7b890e8c8f37676227f17c3df306968be1f6bd7cd1bd62a`.
The adjacent `analyze.py`, `results.json`, `replay.py`, `replay_results.json`
and `replay_stdout.txt` retain parsing, exact lines, copied inputs and results.
Replay uses the independent SHA1/ran1 reference in
`logs/research/corrected_misses_20261025/replay.py`, with the observed native
integer shot tick, effective angles, final spread/inaccuracy and shooting origin.
It does not search for a seed, time shift or command that fits an impact.
The first observed matching native input is used, rather than the best fit.

There are 422 copied publications/models, 486 accepted native construction
observations and 972 actual native accuracy returns. Construction callbacks
can repeat during prediction; these counts are not bullet counts. The 302
native fire records include knives, grenades, interrupted records and several
different sessions/settings. They are not a controlled comparison of samplers
or revisions.

## What the command label establishes

There are 16 no-damage records labelled `several possible aim commands`: 14
Scout and two Deagle. Each has two saved publications and one **captured**
native command in the preceding receipt window. Fifteen correspond to the
older candidate; one corresponds to the newer candidate. Always selecting the
newest or the oldest is therefore unjustified.

All 14 Scout cases have independently reconstructed native rays agreeing with
the furthest impact direction within 0.000006 degrees. Eleven actual segments
from native origin to impact intersect the corresponding saved client capsule.
Two miss that capsule by about 0.53 units; another stops before reaching it.
Several of these saved models report 100% geometric hitchance.

The label describes uncertain event-to-command association. It does not mean
that multiple bullets caused a miss, or establish why damage was absent.
Native construction is client prediction evidence, and this observer can
reject/drop records. Absence of another captured command is not proof of
server acceptance of the remaining command. Runtime metadata selection stays
conservative; neither impact fitting nor a preferred receipt delay resolves it.

The displayed line now says `MISS unknown: aim context ambiguous`, followed
by the candidate count and `miss cause unavailable`. Actual native damage,
outcomes, candidate identities and event-association rules are unchanged.

## Native inputs reproduce the impacts

There are 170 comparable Scout records with a single captured publication
identity, native inputs and raw impacts: 124 ordinary and 46 corrected. Every
reconstructed native direction agrees within **0.009 degrees**. Ordinary
median error is about 0.0000025 degrees; corrected median is about 0.0000036.
The maximum corrected error is below 0.00009 degrees. Repeated inputs with
small changes and logged float precision bound this comparison.

This supports the existing distribution/seed calculation for these captured
shots. It does not establish future input stability, server target geometry,
or a guarantee for every unobserved shot.

Two ordinary outside-cone misses show substantial accuracy changes:

| Fire record / command | Published inaccuracy | Native final inaccuracy | Published HC | Re-estimate using native inputs |
| --- | ---: | ---: | ---: | ---: |
| 91 / 4857 | 0.00382876 | 0.06945238 | 100% | 16.50% |
| 250 / 24129 | 0.02029658 | 0.06227221 | 70.14% | 27.11% |

The re-estimate uses the same saved client capsule, actual native origin and
509 independent native-stream reference samples; it is geometric probability,
not damage probability. Native effective angles match both saved directions.
Using final accuracy reduces ray discrepancies of about 1.32 and 1.92 degrees
to roughly 0.000004 and 0.000001 degrees respectively.

The other three ordinary outside-cone cases fit the native cone when measured
from native origin. Their saved origins differ by about 1.10, 2.43 and 1.14
units. Saved-origin diagnostics cannot establish an RNG failure.

## Implemented gameplay repair and remaining work

`WriteDecision` previously checked grounded jump intent only inside the
Remove spread branch. Ordinary automatic fire could publish its grounded
accuracy even when the same command queued a native takeoff afterward.
The guard now precedes both branches, including both ordinary samplers.
Held jump, a subtick press/release and jump added after preparation defer only
the owned automatic pulse. A subsequent command uses freshly observed
accuracy; an airborne command must satisfy ordinary hitchance normally.
There is no fixed waiting interval. Manual input keeps its existing ownership.

The two final-accuracy increases above are consistent with sampling before
native movement. This capture does not contain movement at the getter return,
so it does **not** prove that a queued jump caused either specific increase.
Observer revision 3 now copies optional flags and velocity at the actual
getter return using the already verified movement layout. Unavailable movement
leaves accuracy evidence intact and never changes gameplay or getter results.

This guard does not solve all reported misses. Of the 33 ordinary no-damage
Scout records reconstructed here, 23 actual impact segments cross their saved
client capsules. The corresponding count for 19 reconstructed corrected
no-damage records is two. Some remaining rays stop before the capsule, while
others miss it after accuracy/origin/punch changes. These are distinct cases.
Saved client pose, native render-history time, penetration/damage path and the
server's damage pose still need separation. Replacing the RNG sampler or
renaming a diagnostic cannot fix those differences. No new live improvement
is claimed by this pass.

## Validation and use

Both x64 Release builds pass **71/71 CTest suites** without project compiler
warnings. The integrated pipeline passes **533 checks**, including ordinary,
native-experiment and corrected takeoff cancellation, exact input restoration,
fresh airborne hitchance and subsequent resumption. The observer passes
**124**, tracker **229**, and logging adapter **140** checks.

All **36/36** installed-file compatibility components pass in
`logs/compatibility/multiple-commands-20261025/`. This uses the existing
Windows AMD64 client 1.41.9.2 / 2000935 / Steam build 26144732 and its verified
movement/native firing contracts. No new native offsets or APIs are introduced.
Build/test outputs are `logs/multiple-commands-{build,tests}-{public,diagnostic}.log`.
Both DLLs have zero static TLS and delay imports; exact hashes are retained in
`logs/multiple-commands-payload-contract.json`.

Build/launch workflow is unchanged: restart CS2 normally and use
**Load Overlay.cmd**, with Shot logging enabled, for the next offline/private
comparison. Existing stopped code remains resident until process exit.
