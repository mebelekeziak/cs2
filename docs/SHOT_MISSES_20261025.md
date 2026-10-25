# Shot misses and native sampling experiment — October 25, 2026

Two independent changes are implemented: normal indexed-shot publication
retention, and an optional native random-stream hitchance sampler. Neither
claims to solve every saved-capsule miss. The statistical experiment remains
off by default; the existing Halton estimator and indexed Remove spread solver
are preserved.

## What the captures establish

The latest six completed overlay logs contain **951 native shot records**:
438 hits, 245 misses and 268 unconfirmed. Among these are **48** `impact crossed
saved capsule without damage` diagnoses (25 corrected, 23 normal) and **eight**
`impact outside saved spread cone` diagnoses (all normal Scout). The newest
three captures contain no outside-cone cases. These are retrospective samples
with different movement, weapons and settings; their hit rates are not a
controlled comparison of solvers.

Reproducible six-log parsing, source hashes, examples and exact source-line
references are under `logs/research/misses_20261025/`. Start with
`LOG_FINDINGS.md`, `log_analysis.py`, `publication_comparison.py` and
`log_analysis.json`.

Four outside-cone examples have a changed saved aim relative to a nearby
earlier publication. In `overlay-9276-134373356558121116.log`, shot 44, the
earlier published yaw is **110.110°** and the saved metadata yaw is
**109.0704°**. The reconstructed impact is **0.17937°** from the former,
inside its **0.1851°** cone, and **1.17149°** from the latter. Earlier
publication logs are throttled and omit the sequence, so these captures alone
cannot distinguish a repeated command from selection of a newer command.
The source does permit repeated normal publication and same-sequence metadata
replacement, which the new regression reproduces.

There is separate evidence beyond spread. Corrected Scout shot 138 in
`overlay-13964-134373241842872618.log`, sequence 2332, has one impact crossing
the saved head capsule and no damage. Its reconstructed impact direction is
within approximately **0.000050°** of the predicted direction, with zero
recorded local velocity. This example cannot be explained by a bad spread
offset alone. Intersection of two saved client geometries does not establish
intersection with the server's damage geometry.

## Implemented publication repair

Normal aim now uses the same completed indexed-publication retention and
native-rebuild replay machinery as Remove spread. Repeated callbacks keep the
first published view and metadata rather than sampling post-fire recoil,
accuracy and a newer target pose as a fresh shot. Exact native rebuilds restore
that view. Native primary replacement, unconsumed retries, consumed-state
latching, physical input takeover, settings/source changes and genuine new
commands retain their existing ownership rules.

A completed publication is not a fresh target acquisition. If native input
retires an unconsumed request, a replacement still needs current eligibility
and path checks. The tests cover blocked cover at replacement, preservation
through ammo/recoil changes, subsequent commands, and the existing automatic
retry and cancellation cases.

Each stored publication receives a monotonic copied `publicationId`.
The shot tracker ignores duplicate captures of that same identity. A genuinely
new publication can replace context even when it reuses the same sequence and
weapon, such as after a native timestamp or settings change. Existing preview
metadata without a publication identity can still refresh. Fresh publications
reset their attempt timeout; the existing native-event acknowledgement and
explicit-withdrawal behavior remains.

A separate scratch build disabling only the normal-publication repair
reproduces seven regression failures. The integrated pipeline passes all
488 checks with the repair. The scratch copy/build and output are under
`logs/research/misses_20261025/normal_regression/` and
`logs/misses-20261025-before-tests.log`; production sources were not reverted
for this comparison.

`AIM_PUBLICATION revision=8` records each captured publication without the
continuous-decision throttle, including sequence, publication ID, receipt
time, native history index/time, copied origin and effective aim. It also
records the selected experiment and paired percentages. `AIM_SHOT_DETAIL`
includes `publication_id`, allowing a settled record to be linked to the
exact captured context. These IDs are local diagnostic identities, not native
or server shot IDs. Native fire association still uses receipt windows and
cannot prove which command the server consumed.

Impact-angle diagnostics now use a stable cross/dot `atan2` calculation.
The old `acos` calculation treated the copied float aim direction as exactly
unit length and reported about **0.00965°** for one perfectly parallel pair
from the captured Scout angles. The new regression checks the parallel case.
This corrects small diagnostic residuals; it does not explain the much larger
outside-cone deviations above.

## Independent corrected-shot replay

The newest three captures provide 118 corrected records with detailed
publications and observed impacts. An independent SHA1/ran1 reconstruction
reproduces **118/118 saved seeds** and their saved predicted rays within
**0.00000693°**. This checks arithmetic against the saved client inputs;
it does not recover authoritative firing inputs.

For 43 unambiguous corrected Scout misses, the production clock/punch pair is
the best recorded alternative in **42/43**. Reverting to the clamped pair is
worse in **39/43**. There is no evidence for a blanket clock rollback.

The strongest moving-shot lead is current versus historical shooting origin.
The adapter samples the native current-origin getter without a timestamp.
Native firing has a separate controller-adjusted historical-origin path,
documented in `tools/CS2_AIM_SHOOT_ORIGIN_EVIDENCE.md`. A diagnostic backward
shift of `velocity/64` reduces median reconstructed miss residual from
**0.682568° to 0.330636°**, but improves only **26/43** shots. A fixed rewind
would worsen the other 17 and is not shipped.

The exact historical firing timestamp, equivalence of the callback's command
to the native internal command, actual firing origin and target/server rewind
pose are not established by these logs. A future native-origin integration
needs those contracts verified before using that data. Current capsules are
copied from current client bone poses; source validity does not prove matching
server pose timing. Detailed reconstruction and per-shot alternatives are in
`logs/research/corrected_misses_20261025/REPORT.md`, `replay.py` and `replay.json`.

## Native RNG experiment

Enable **Aim → Native RNG experiment** with **Remove spread off**.
The preference saves as `aim.native_hitchance`; missing legacy fields restore
the disabled default. Remove spread disables the control while preserving its
preference and continues to use its existing indexed solver.

The experiment calls the actual verified private native RNG constructor and
float methods with independent seeds 1 through the configured sample count.
The copied primary single-projectile distribution and capsule evaluator use
the same weapon model and geometry as baseline. It prepares offsets once per
decision for reuse across candidates. When enabled, the native estimate drives
ranking and hitchance gating, while `AIM_HITCHANCE_COMPARE` records both
estimates and threshold disagreements. Unavailable native input invalidates
the experiment rather than silently substituting baseline for Autofire.
Manual fire remains under its existing ownership rules.

The full helper in the linked implementation reseeds shared engine RNG and
initializes global caches. This implementation uses its **private native RNG
components**, with the verified copied distribution formula; it does **not**
call the complete client spread function. Exact contract, provenance, test
commands and limitations are in [the experiment notes](NATIVE_HITCHANCE_EXPERIMENT.md).

Sequential native samples are not intrinsically more accurate: in an analytic
50.0025% case at 1,024 samples, Halton estimates **50.0977%**, while this native
seed ensemble estimates **52.9297%**. Both are finite estimates. The experiment
is a useful comparison, not an upgrade proven by using native code.

For a useful offline/private comparison, first try the publication repair with
the default sampler. Then compare the native option with the same weapon,
hitgroups, sample count, thresholds and movement, keeping Remove spread off.
Investigate Remove spread separately: that mode bypasses statistical
hitchance, so its hit/miss result cannot validate this sampler experiment.

## Verification and remaining observation

The installed Windows AMD64 client remains **1.41.9.2 / 2000935 / Steam
26144732**. All **33/33** static compatibility components pass in
`logs/compatibility/misses-20261025-baseline/`. The prior capture records actual
D3D11 rendering; no new client/runtime observation is claimed.

The native sampler passes **18,546** synthetic assertions and **37,098**
assertions in an isolated executable loading only verified installed
`tier0.dll`, including bit-exact offset parity with its independent reference.
No client or server is launched by these tests. Both integrated x64 Release
variants build without project compiler warnings and pass **68/68 CTest
suites each**, including **488** aim-pipeline and **202** shot-tracker checks.
Both DLLs retain zero static TLS and delay-import directories; exact hashes
are in `logs/misses-20261025-payload-contract.json`. Build and suite logs are
`logs/misses-20261025-{build,tests}-{public,diagnostic}.log`.
`git diff --check` passes.

Build and launch commands are unchanged. Restart CS2 normally before using
**Load Overlay.cmd** when an older DLL is resident. Stopped and resident code
is not physically unloaded. No client/server was launched or injected during
this pass, and no installed game files or server settings were changed.
The user subsequently reports substantially better behavior and essentially
no random misses so far, then confirms clean behavior both with and without
Native RNG experiment. They also briefly tried Remove spread on/off. The
native sampler is not required for the reported improvement, which supports
the shared publication/replay repair as the stronger explanation. No exact
configuration matrix, shot counts or rates were supplied, so this does not
establish a sampler advantage or resolution of every capsule-crossing case.
Remove spread continues to bypass statistical sampling. The agent has not
independently inspected a post-fix capture.
