# Scout spread clock ambiguity — revision 2

The October 23 user trial reported strong bursts of accuracy mixed with many
misses. The latest session is `logs/overlay-39232-134372462471446344.log`.
This change addresses a repeatable timing mismatch in that evidence, rather
than increasing the mathematical search budget or assuming a fixed tick shift.

## Observed evidence

The log contains 34 matched, corrected SSG 08 native-fire records: 13 hits and
21 misses, all automatic. Fifteen large miss deviations exceed two degrees;
the solver's logged residual is usually near zero. The mismatch is between
prediction and observed impacts, rather than simple failure to solve the
captured single-seed model. Deagle records include many overlapping events and
are not used to infer an accuracy change.

Independent replay reproduces all 34 saved client seeds and their predicted
rays within 0.000009 degrees. All fifteen large deviations fit the next tick's
seed much better: median angular mismatch falls from 8.64 degrees to 0.090
degrees. Other shots fit the original tick, so an unconditional `tick + 1`
would damage working cases. The sampled events do not prove an authoritative
server seed or a universal pair of possible ticks.

Movement records distinguish command time from simulation time by one tick.
Static native inspection also shows command simulation temporarily replacing
the clock with controller tick base, then restoring the prior clock. The old
shot logs omitted raw history/conversion/current-time inputs, so they cannot
reliably choose a clock domain for every shot. Research is under
`logs/research/26144732/spread_improvement/`; the native replay and clock
disassembly are in its `native/` directory.

## Implementation

Only the SSG 08 changes its compensation policy. It samples each candidate
angle using both the original integer seed tick and its successor, with the
same copied accuracy, recoil and debug settings. The analytic inverse uses
the spherical midpoint of the two local rays; the bounded search minimizes
their worst angular error. Every candidate is resampled at its actual rounded
command angles for both seeds. Both streams share the existing limit of 8192
native random samples. No history timestamp, native clock, engine RNG or
installed game file is modified, and no future-punch evaluator is invoked.

Scout autofire requires both rays to intersect the saved target capsule.
This gate applies even at minimum hitchance zero. When only one fits, the
owned automatic attack is cancelled with existing input restoration and CRC
regeneration; the player may observe slower firing. Manual Scout fire remains
available and uses the best worst-case correction. Other weapons retain their
previous single-tick policy, and the on/off toggle remains unchanged.

`AIM_SPREAD` revision 2 diagnostics retain command sequence, history index,
scene, raw history time, converted time, current time, controller tick, chosen
clamped tick, both seeds/rays and separate residuals. Rejected automatic
corrections emit `corrected=0`. Shot metadata and human output identify two
conditional rays, rather than statistical 100% hitchance. Ordinary spread
miss classifications remain disabled for compensated predictions.

## Limits and verification

The two variants share captured punch and accuracy; future prediction may
change those values. Actual firing origin, target pose, edge-cover traversal
and hitgroup can also differ. Both rays intersecting a copied capsule is a
client-model condition, not a promise of native damage or server agreement.
The paired-tick policy may miss a better candidate within the bounded search.

The offline regression fixture preserves all 34 captured shot states, including
their original command, seed, predicted/observed ray, accuracy and capsule.
The test independently reimplements the verified SHA-1/RNG sequence, checks the
old predictions, explains the large next-tick deviations and reconstructs both
new search outputs. It compares geometry under the two hypotheses; it does not
count synthetic results as observed gameplay hits.

On those copied states, the single-tick policy's median worst error across the
two clock hypotheses is 7.86 degrees; the dual search reduces it to 0.92 degrees
and improves that metric on all 34 states. Both hypothetical rays intersect
the saved capsule in seven cases, compared with three for the single-tick
policy. These are simultaneous conditional intersections, not seven observed
hits. Automatic firing can try again on subsequent ready commands when the
two-ray gate fails.

Public and diagnostic x64 Release builds each pass **62/62 CTest suites**,
including 5068 inverse checks, 463 search checks, 105 native-adapter checks,
275 captured-replay checks and 372 integrated pipeline checks. The integrated
cases explicitly reject a Scout's primary-hit/alternate-miss result even with
minimum hitchance zero, and verify both-ray publication and metadata. Both
DLLs remain AMD64 with zero static TLS and delay imports. No project compiler
warnings were emitted. Logs are `logs/remove-spread-r2-build-{public,diagnostic}.log`,
`logs/remove-spread-r2-tests-{public,diagnostic}.log` and
`logs/remove-spread-r2-payload-contract.json`.

All **30/30 installed-file compatibility checks** pass in
`logs/compatibility/20261023T164055707377Z/`; no native offsets or installed
game files were changed by revision 2.

Use the existing restart and **Load Overlay.cmd** workflow, then **Aim → Remove
spread**. Stopped and resident is not physically unloaded. No new gameplay
session was launched by this change; revision 2 behavior needs a fresh offline
user trial.
