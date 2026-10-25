# Autofire latency, console attempts and movement — revision 7

October 24, 2026. Addresses the user's long firing delay, console bursts and
request to keep autostrafe/Jumpbug active while firing. This follows revision
6's repair of the permanent aim stop.

## What the capture establishes

The immutable snapshot of `overlay-32204-134373219494373350.log` contains 287
corrected publications and 236 immediate native-primary rebuild retirements.
None of those 236 has a matched native-fire record; 44 of the 51 non-retired
publications do. One continuous burst publishes 123 accepted corrections over
1.984 seconds before the final request receives a native fire. All sampled aim
diagnostics remain active, so this is distinct from revision 5's global stop.

The pasted 35 `AIM_SHOT cmd #15481` through `#15515` lines are unconfirmed
command attempts flushed at a source lifecycle transition, rather than 35
observed bullets. Thirty-four had already lost their native primary fields.
The final enemy dies at the same transition; the evidence is consistent with
normal round end and does not establish a spurious source reset.

Separately, 2,360 of 2,647 detailed spread searches are rejected by the previous
two-clock model. Even the successful corrections can subsequently disappear
before observed firing, so search rejection and request lifecycle both matter.
The audit, immutable snapshot hash, parser and cadence results are preserved in
`logs/research/26144732/spread_revision7/log_audit/`.

## Changes

An automatic publication stores its clip and recoil index. Every valid weapon
sample, including cooldown and other command sequences, latches consumption
when clip decreases or recoil index increases. Once latched, prediction rollback
cannot reopen that publication. Ordinary recoil decay does not count as firing.

When native input replaces an automatic primary pulse without observed
consumption, Prepare may immediately select again from the current base view
and call the verified native recorder. There is no added retry timer. A reset
first observed after History withdraws the old attempt and waits for Prepare;
old corrected history cannot become fresh standalone/manual input. Physical
takeover, unknown history and genuine restoration/checksum failures retain
their existing ownership handling.

A retry marker remains until a replacement native request is actually acquired.
If a partial rebuild leaves the old primary index live, acquisition fails,
the target disappears or the weapon enters cooldown, History still cannot
reinterpret that old correction. Consumption observations include publication
refresh and final model validation, not only Prepare.

Withdrawal uses exact serialized local controller/pawn, sequence and weapon
keys. It suppresses obsolete command-attempt settlements while retaining copied
context for late genuine native fire/hurt events. A fresh publication can reuse
the sequence. Default console output hides remaining command-only attempts;
the existing Verbose option exposes them. Actual shot/hit output remains.

Production spread solving selects one copied clock/recoil model. Indexed time
ahead of the native clamped tick selects the retained alternative. A lead of
one tick uses next-tick time and the shared punch; a greater lead uses raw
indexed time and copied current punch. Otherwise it uses the clamped model.
The chosen ray must intersect the selected capsule before automatic publication.
The original clamp still participates in freshness validation. Diagnostics
report selected clock, selected punch and original clamp explicitly.

This removes the requirement that two incompatible speculative rays both hit.
It is a responsiveness tradeoff: the chosen client clock can disagree with the
actual firing state or server. This does not establish perfect compensation or
an authoritative seed. The 4096 automatic/8192 manual bounds and native-bin
cache remain.

The quantized movement transaction now permits held, indexed and subtick
primary/secondary attacks. It preserves attack fields and Jump/Duck events.
Use and invalid movement states retain their passthrough rules; firing retains
ordinary anti-aim pattern passthrough. Jumpbug itself had no attack gate; the
shared steering gate was the firing-related restriction. Existing airborne/jump
priority over Quickstop remains.

## Offline measurements and verification

At the same 4096 budget, 70 copied comparisons improve from 22 to 64 saved
capsule solutions, median callbacks 4096 to 626, and median native SHA/RNG
samples 2683 to 273. Median solver time changes from 2.198 ms to 0.353 ms;
p95 from 3.717 ms to 2.479 ms across 11 repetitions. These measurements use
synthetic native callbacks and copied geometry, not live FPS or hit rate.
Thirty-four older Scout cases lack raw timestamps and explicitly assume the
next clock; the timestamped 36-case subset improves from 15 to 33 solutions.
The six pistol captures improve from one to three. Results and independent
budget/ray validation are in
`build/aim_spread_benchmark_revision7_{4096,8192}.txt`.

Pipeline regressions cover immediate retry in both callback phases, recoil
decay, exact withdrawal keys, rebuilding from the current base view, and
consumption observed during another command's Prepare/History/final validation
surviving rollback, and failed replacement acquisition retaining its guard. Tracker/adapter
tests cover 512 withdrawals through timeout, capacity, reset and stop, wrong
keys, fresh retries and late native events. Native spread tests check selected
seed/time/punch routing while retaining freshness and generic two-model tests.

Both public and diagnostic x64 Release builds succeed without project compiler
warnings and pass **67/67 CTest suites each**. Focused totals include 474 aim
pipeline, 467 native spread, 738 search, 44 pistol replay, 198 shot tracker,
128 logging adapter and 67 console-setting checks. Movement regressions first
exposed 20 failures before the fix; the corrected suites, including all 435
Jumpbug checks, pass. All **33/33 installed-file compatibility checks** pass in
`logs/compatibility/20261024T135615263957Z/`.

Build/test outputs are `logs/remove-spread-r7-{build,tests}-{public,diagnostic}.log`.
Both DLLs remain AMD64 with zero static TLS and delay imports; exact hashes
are in `logs/remove-spread-r7-payload-contract.json`. `git diff --check` passes.
No client/server was launched or injected during this pass.
No native address, installed file, game setting, loader or launch workflow
changes. Restart CS2 normally, then use **Load Overlay.cmd**. Stopped code
remains resident until normal process exit. Live private/offline firing delay,
miss rate, FPS and movement coexistence still require observation.
