# Pistol timing, repeated prediction and search cost — revision 5

Investigated October 24, 2026. Target remains the Windows AMD64 CS2 client
2000935, patch 1.41.9.2, Steam build 26144732, and the existing D3D11 overlay.
Installed files and workspace logs were inspected; no game/server was launched,
joined, injected into or reconfigured during this investigation.

## Findings

The user reports Deagle as the worst weapon with Autofire, inconsistent Scout
results during movement, and frame drops with Remove spread enabled.

The previous implementation considered two indexed clock hypotheses only for
Scout. Deagle used the callback's clamped tick alone, although the native
indexed shot-time machinery is shared. Three unambiguous corrected Deagle
misses in `overlay-37892-134373047916445176.log` independently reproduce the
old seeds and predicted rays, then fit the raw indexed tick much better:

| Shot | Clamped / raw tick | Old predicted-to-observed error | Raw-tick replay error |
| --- | --- | --- | --- |
| 1 | 94236 / 94238 | 5.925 degrees | 0.089 degrees |
| 2 | 94380 / 94383 | 11.063 degrees | 0.748 degrees |
| 4 | 95510 / 95512 | 4.278 degrees | 0.011 degrees |

These are reconstructions of the recorded command with copied accuracy and
origin, not the result of running the new code. The six-shot pistol fixture
also retains two corrected Deagle hits and the latest capture's sole
unambiguous corrected Glock miss, with file hashes and source line references.
Overlapping pistol events are excluded from trajectory claims.

The Glock record demonstrates another problem: sequence 1848 is successfully
corrected twice, first with clip 8/recoil 0/inaccuracy 0.146721661, then with
clip 7/recoil 1/inaccuracy 0.203515917. Native history callbacks can revisit an
already-published shot after its accuracy has changed. Recomputing or treating
the previous correction as the new manual desired direction can corrupt the
original shot and replace its log metadata. The observed ray fits the earlier
accuracy substantially better, but does not establish which command version
the server executed.

The latest capture has 878 logged search attempts and 5,300,091 model sample
evaluations. Rejected attempts account for 4,876,244 evaluations (92%). There
are 244 repeated attempts beyond the first across 634 sequences. One second
contains 437,196 evaluations. No per-frame FPS was captured, so this establishes
workload rather than a measured FPS loss.

Native inspection confirms indexed history angles are copied verbatim before
punch addition. Nonzero history fractions do not require angle interpolation.
Optional inaccuracy getter outputs describe current movement/air contributions;
they are not future error bounds. Neither angle interpolation nor invented
movement-accuracy intervals were introduced.

## Changes

All supported ordinary indexed firearms now use the existing two-clock policy:
clamped tick and its successor, or raw converted time with copied current recoil
when raw time leads by more than one integer tick. Both returned rays must
intersect the saved capsule before Autofire can publish. This may defer a
pistol shot that the former single-model check would have allowed.

Successful indexed spread publications retain copied original/applied angles
and an ownership key. Repeated callbacks retain the correction and original
metadata, or restore it only when native history exactly matches its original
views and indexed timestamp. They do not re-solve using later shot penalties.
Unknown or unsupported automatic rebuilds cancel the owned pulse through the
existing checksum path. Manual input remains under native ownership. Source,
weapon, settings and lifecycle changes invalidate reuse.

The Remove spread path no longer prepares or estimates the statistical spread
cone that its ranking and publication do not use. It still validates geometry,
weapon readiness, path, damage and both final corrected rays.

Search enumeration computes eligible yaw intervals for each pitch instead of
scanning the full 720-column grid twice. Default ordering and results remain
identical. A per-solve cache reuses native seed/RNG samples only for identical
canonical float half-degree bins and integer ticks. Angles outside [-180,180]
bypass the cache; distinct endpoints and signed-zero payloads remain distinct.
The cache expires after every solve and never borrows engine RNG storage.

Automatic search stops once both modeled rays fall within a conservative
half-radius interior tolerance, and its maximum attempt budget is 4096 model
samples instead of 8192. Final capsule/range checks are still required. A failed
attempt can retry with fresh state on a later command. Manual compensation
retains the 8192 budget and original precise residual tolerance.

## Performance measurements

The standalone benchmark runs the actual adapter with independent SHA-1 and
136-byte random-stream substitutes; it calls no installed game functions.
For 70 copied two-clock scenarios, 19 repetitions:

| Search budget | Median solve | p95 solve | Both saved-capsule intersections |
| --- | --- | --- | --- |
| 8192 | 2.768 ms | 5.986 ms | 26/70 |
| 4096 | 1.923 ms | 3.080 ms | 22/70 |

This comparison isolates the budget within the new adapter and tolerance
policy. It is not a live FPS benchmark or a measured comparison of complete
old/new gameplay. The smaller budget loses four immediate copied-clock
solutions; Autofire cancels those attempts if neither usable ray is found.
Consequently, this change bounds work at the cost of potentially waiting
longer for a shot. It does not authorize firing rays that miss the capsule.

The interval-enumeration change separately reproduces all 110 captured default
results and 420 additional seam/pole/punch/budget edge scenarios exactly,
including command angles, seeds, residuals and evaluation counts. Pure callback
performance improves modestly; native SHA/RNG work dominates the rejected
two-clock searches. Caching chiefly helps repeated within-bin refinements.

Build the optional benchmark with:

```powershell
cmake --build build --config Release --target aim_spread_search_benchmark --parallel
.\build\Release\aim_spread_search_benchmark.exe local 19 tolerance summary 4096 cache
```

Its counters distinguish model sample attempts from actual seed/RNG calls.
Benchmark output is preserved under
`logs/research/26144732/spread_revision5/performance/`. Log audit scripts,
native decoded evidence and source references are in adjacent `log_audit/`
and `native/` directories.

## Verification and limits

The new pistol replay independently reproduces old predictions before checking
the additional timing model. Search checks cover counted budgets, arbitrary
continuous callbacks, both-ray tolerance and float-rounded final sampling.
Adapter checks cover all supported firearm clock variants, cache expiration,
independent seed/RNG reconstruction, negative bins and the yaw seam. Pipeline
checks cover repeat publication, native rebuilds, post-shot accuracy, metadata
retention, cancellation, owner changes, disabled settings and cooperative stop.

The initial installed-file compatibility bundle passes all 33 components in
`logs/compatibility/20261024T124041948447Z/`. No native offset, signature,
installed game setting, hook, loader or launch workflow was changed.

Both final public and diagnostic x64 Release builds succeed without project
compiler warnings and pass **67/67 CTest suites each**. Focused coverage includes
433 pipeline assertions, 738 search checks, 169 adapter checks and 37 pistol
replay checks. Both DLLs retain AMD64 architecture, zero static TLS and zero
delay imports. The final installed-file bundle also passes **33/33** in
`logs/compatibility/20261024T130324406480Z/`. Build/test logs use
`logs/remove-spread-r5-{build,tests}-{public,diagnostic}.log`; payload identities
are recorded in `logs/remove-spread-r5-payload-contract.json`.

This remains a conditional client model. The independent server-seed branch,
later movement/accuracy changes, authoritative firing origin and lag-compensated
target pose remain outside the evidence. Four latest Scout misses cross the
saved capsule without damage; a client angle change cannot establish their
cause. New gameplay accuracy, firing cadence and FPS require a fresh permitted
private/offline trial. Restart CS2 normally before using **Load Overlay.cmd**
with rebuilt DLLs. Stopped and resident is not physically unloaded.
