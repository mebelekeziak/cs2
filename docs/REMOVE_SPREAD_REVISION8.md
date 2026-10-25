# Selected-time punch correction — revision 8

October 25, 2026. The user reports that ordinary aim behaves decently in a
short trial while Remove spread misses heavily. This follows the first trial
with the native shot-construction observer. The statistical hitchance sampler
is independent of this correction and is unchanged.

## What the native capture establishes

Snapshot:
`logs/research/no_spread_native_20261025/20261025T100413Z/overlay-36812-134373951938869620.log`

It contains 5,885,630 complete-line bytes, SHA-256
`f768f9bc8635b6ce7d6e41461542f23cebb8bbe8b7272a8ec260dbbe97eab1d9`.
The report, parser, complete joins, independent native replay and copied
geometry fixtures are preserved beside it. The running test DLL was the
documented pre-quarantine diagnostic build; it did not include the later
metadata-loss repair or this punch correction.

There are 82 native builder callbacks representing **37 distinct commands**,
not 82 bullets: 22 ordinary and 15 corrected. Each command has one observed
time/effective-angle/punch signature. The corrected interval has 15 native
fire events: **six hits and nine misses**. Ten earlier ordinary Scout events
all hit, but the conditions differ: corrected shots are mostly airborne with
high inaccuracy. This short sample does not establish a general hit rate.

All **15/15** corrected native integer shot ticks match the solver's selected
raw clock; their fractions agree within logging precision. A blanket rollback
to the older clamped clock is not supported by this capture.

Event 36, command **20776**, demonstrates a different mismatch. The publication
uses selected punch approximately **(0.064690515, -0.001253350, 0)**; the actual
native builder uses **(1.350812793, -0.027182225, 0.000001516)** at the same
selected time. Its sole observed native callback precedes the fire-event
receipt, rather than being an additional post-fire signature in this capture.
The effective directions differ by **1.28638°**.

The spread seed depends on half-degree bins of those effective angles. With
copied accuracy held constant, independent replay changes the resulting ray
by **30.8673°**. The observed impact differs from the saved model by about
29.597°; reconstructing from native effective angles, time and origin reduces
that discrepancy to about 0.3755°. This supports a specific punch-query fault,
not a spread arithmetic or selected-integer-clock fault.

## Implemented repair

The selected alternative previously copied the current-prediction getter's
`weapon.aimPunch` for a raw clock, and reused clamped punch for the next-tick
alternative. The native builder instead calls verified `ShotPunch` with its
selected scene-domain time pair. These queries are not interchangeable.

Both selected alternatives now query that same verified shot-time punch API
at their exact selected time. No scene/global conversion is applied to this
punch query. The existing clock-selection rule, bounded spread search, native
private RNG and solver remain in place.

Before publication, freshness validation re-queries the selected punch as
well as the original clamped time/punch. A changed value rejects the copied
model. Output is poisoned before each native call; missing/foreign returns,
partial writes, nonfinite results and structured exceptions are unavailable
models, rather than an implicit fallback to current recoil.

Standalone manual compensation also uses the selected-time punch consistently
for its intended unspread direction and solver input. It retains the existing
ownership of manual fire. Ordinary aim with Remove spread off is unchanged.

The native getter runs on the existing command owner. Read-only inspection
confirms that future queries extend separate native memoization vectors keyed
by base time/angle/velocity; they do not advance the recoil base state. Earlier
queries read their cached prefix, with a maximum of 129 integration samples.
This corrects which native query is made;
future prediction state can still change after a valid publication. It does
not promise an authoritative future recoil state.

## Remaining differences and final accuracy observation

Corrected native origins differ from the copied current origin by
**0.558–17.234 units**, median **3.368**. A fixed velocity rewind is unsuitable:
one displacement points forward by approximately 36.77 ms. Holding each saved
predicted ray fixed and moving it to the native origin turns two of 15 saved
capsule intersections into misses. Origin mismatch alone does not explain all
of this capture's corrected misses.

The native historical-origin wrapper and reverse scene/global conversion are
now decoded, but querying it early can lack a history bracket that exists
later during native firing. An unsupported extrapolation or blind rewind is
not added in this revision.

The first observer did not capture final native spread/inaccuracy, which the
ordinary caller samples after building its shot. The observer now also records
those **actual invoked getter returns** at their exact native firing callsites
as `AIM_NATIVE_ACCURACY`, including float bits, native command and serialized
ownership. It makes no extra getter calls and leaves native arguments,
outputs and return values intact. Own estimator calls and unrelated native
callers do not become firing records. The getter arguments do not establish
an attack selector, so the record explicitly leaves it unknown.

These are client construction and input observations, not server acceptance,
authoritative seed or server damage-pose evidence. A fresh trial is required
to compare final accuracy with each publication and to measure any improvement
from the punch-query repair.

## Validation and use

The focused native adapter passes **489 assertions**. A separate scratch build
using the prior adapter fails **20** of the same assertions. Coverage includes
the captured punch difference, selected tick/fraction routing, freshness
changes and native query failures. The clock policy and sample budgets are
unchanged. Both x64 Release variants build without project compiler warnings
and pass **71/71 CTest suites each**. The integrated pipeline passes **489**
checks, the expanded native observer **121**, tracker **227**, and logging
adapter **140**. All **36/36** installed-file components pass in
`logs/compatibility/remove-spread-r8/`; all **18** deliberately altered
observer headers reject.

Build/test outputs are `logs/remove-spread-r8-{build,tests}-{public,diagnostic}.log`.
Both DLLs remain AMD64 with zero static TLS and delay imports; exact hashes
are in `logs/remove-spread-r8-payload-contract.json`. `git diff --check` passes.

Use the existing normal restart and **Load Overlay.cmd** workflow, with Shot
logging enabled for the next offline/private comparison. No loader session
gates, installed game files or private server settings change. No client/server
was launched or attached during this pass. Stopped and resident code is not
physically unloaded. Remaining miss rate and live performance are unmeasured.
