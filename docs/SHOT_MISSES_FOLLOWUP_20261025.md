# Renewed miss streaks — October 25, 2026

The initial user observations of clean behavior were followed by recurring
`impact outside saved spread cone` streaks. The user reports that these are
more frequent on non-local sessions. Their usual ping is about 10 ms, but
the exact server/session latency was not measured. The evidence does not
establish high network latency as the cause.

The separate cosmetic changes are checkpointed in **cef25af**. Source review
found no direct cosmetic-to-gameplay mutation: visual punch uses different
native callbacks from gameplay and indexed shot punch; edits apply to temporary
camera/viewmodel outputs. The recurrence is not attributed to cosmetics.

## Completed capture

The log was readable while the user played. A complete-line snapshot was
preserved during play, then a final snapshot after the user exited:

`logs/research/misses_followup_20261025/20261025T092855Z/completed/overlay-37228-134373926904598470.log`

Final size: **11,218,076 bytes**; SHA-256:
`bb53fc1731cb64f9704de291943de367ff9a506b0bb8a6d7f231be1be3509b74`.
The reproducible parser, per-shot results and report are beside that snapshot.
The original growing-session snapshot remains intact in its parent directory.

The final capture contains 100 native records: 55 hits, 41 misses and four
unconfirmed, including unrelated knife, grenade and manual tests. The relevant
normal Scout group with native sampling selected contains **61 records: 42
hits, 18 misses and one unconfirmed**. It has eight outside-cone and six
crossed-capsule diagnoses. Remove spread was off for all eight outside cases.

For 60 settled Scout records, mean saved geometric hitchance is **93.415%**;
the sum of saved geometric miss probabilities is **3.951**, compared with
18 no-damage results. Thirty-five records report 100% sampled hitchance and
include ten misses. These saved models are not authoritative damage
probabilities, and some are associated with the wrong candidate command.
The data does not establish ordinary random spread as the complete cause.

## A different association defect

The earlier repair remains effective: every outside-cone record matches its
selected publication's copied aim and origin within logging precision. The
same-publication snapshot is not being overwritten again.

However, the tracker selected the newest preceding weapon-matching metadata
by receipt time. Several different command publications can precede one
native fire receipt. Keeping each publication immutable does not identify
which one the event belongs to.

Six of the eight outside-cone cases fit an earlier publication much better:

| Shot | Selected publication / sequence | Selected age | Angular residual | Earlier publication / sequence | Earlier age | Angular residual |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| 23 | 18 / 12879 | 16 ms | 0.399188° | 17 / 12876 | 78 ms | 0.122062° |
| 69 | 68 / 12201 | 15 ms | 0.725438° | 67 / 12198 | 62 ms | 0.046458° |
| 79 | 85 / 17417 | 16 ms | 0.291553° | 84 / 17415 | 62 ms | 0.117955° |
| 80 | 87 / 17543 | 15 ms | 0.424451° | 86 / 17540 | 62 ms | 0.142717° |
| 82 | 90 / 17730 | 16 ms | 0.351551° | 89 / 17727 | 62 ms | 0.190733° |
| 98 | 114 / 5430 | 16 ms | 1.258158° | 113 / 5428 | 63 ms | 0.078648° |

The earlier and selected origins differ by less than **0.00006 units**.
Their relative angular fits do not come from a moving-origin adjustment.
Hit controls also frequently fit an earlier publication better, so this is
not a pattern found only by selecting convenient misses.

For shot 80, both publications still report clip 7 and recoil index 0; the
first later logged consumption sample is clip 6/recoil 1. An ammo-consumption
witness therefore cannot uniquely distinguish these two candidates. Choosing
an older publication by a fixed 62 ms delay or by best impact fit would invent
a causal association and is not used.

The other two cases have multiple impacts. Shot 32 has maximum impact angle
0.4251°, furthest-impact angle 0.397383° and saved cone 0.4020°. Shot 60 has
maximum 1.4458°, furthest 0.243396° and cone 0.3535°. Earlier short/penetration
impact directions are outside while the furthest direction is inside. The
old log retained only the furthest position and the maximum angle, preventing
independent reconstruction of all segments.

## Diagnostic repair

Multiple plausible real publications now produce ambiguous metadata instead
of a confident newest-command match. Copied candidate identities, sequences,
receipt times and withdrawal status remain available. A uniquely associated
publication is reserved against reuse by a later fire; the broad timeout
acknowledgement flag is not mistaken for a unique binding. Legacy unidentified
preview metadata retains its prior behavior when no real publication exists.

All uniquely associated raw impacts are retained independently of whether
metadata is known, with explicit bounded storage and overflow reporting.
Mixed inside/outside directions receive a separate straddling-cone diagnostic.
Actual unique hurt/no-hurt evidence still determines HIT/MISS. A more cautious
diagnostic does not turn a missed bullet into a hit or establish improved
shooting accuracy.

Each publication additionally logs `AIM_PUBLICATION_MODEL`, including its
copied command angles, current punch, weapon time, accuracy, ammo/recoil,
capsule and movement/path state. The model has the same publication identity
as the original `AIM_PUBLICATION` and no longer depends on a throttled nearby
decision report.

## Native client construction observer

An optional diagnostic observer records the actual native shot-builder call
at `client + 0x80E530` from its verified ordinary firing caller, return
`0x804EDF`. It observes native execution once and does not invoke a builder
or getter to manufacture a shot model. Verified command ownership comes from
the native weapon owner/controller chain and the controller's active command
producer/clearer, rather than a guessed association with the latest callback.

`AIM_NATIVE_BUILT_SHOT` records the native command sequence and copied indexed
history, the native selected shot/render time, historical/current origin,
effective pre-spread angles and shot punch. Comparing that sequence and model
with the copied publication is the next evidence needed to distinguish a
client command/model mismatch from event association and server pose timing.

This is **client construction evidence**, not proof of a generated bullet or
server acceptance. The native caller has a later gate that can still reject
the shot. Prediction can rebuild the same command. The observer does not bind
server events to a command, change aim or firing state, or gate existing aim
on observer readiness. It queues bounded copied records and keeps code and
trampolines resident after cooperative stop. Exact native evidence and verifier
instructions are in `tools/CS2_AIM_FIRE_OBSERVER_EVIDENCE.md`.

The initial observer did not capture final native spread/inaccuracy, which are
sampled after the builder returns. Revision 8 adds separate observations of
those actual native getter returns; the original short trial still lacks
them. See [the selected-time punch repair](REMOVE_SPREAD_REVISION8.md).

## Verification and limits

The initial installed-file compatibility pass is **35/35** in
`logs/compatibility/misses-followup-20261025/`. The new observer adds an
independent component; **36/36** pass in
`logs/compatibility/misses-followup-20261025-final/`. Its eight deliberate
temporary-header mutations reject. The observer fixture passes **53** checks,
covering forwarding, native output preservation, ownership changes,
recursion, disabled logging, bounded overflow and cooperative shutdown.

No client/server was launched, attached, injected or controlled during this
pass. Analysis read workspace snapshots and installed files. Build/launch
commands remain unchanged. A fresh normal restart before **Load Overlay.cmd**
is required to use the new diagnostics when old code is resident.
The remaining actual no-damage behavior has not been verified fixed.

The user requested testing immediately after the full builds passed **71/71
CTest suites each**. Those tested DLLs are held unchanged for the trial;
their hashes and PE checks are in
`logs/misses-followup-tested-before-quarantine-payload.json`. They include the
main ambiguity/raw-impact repairs and native observer. An additional source
repair for evicted metadata evidence passed **227 tracker** and **140 adapter**
focused checks but was not included in those held DLLs. The subsequent short
trial is now captured; revision 8 includes that repair along with the new
selected-time punch query. See [revision 8](REMOVE_SPREAD_REVISION8.md) for its
build identity and remaining observation limits.
