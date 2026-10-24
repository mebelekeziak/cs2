# Firing accuracy at takeoff — revision 4

Investigated October 24, 2026 using the user's two revision 3 logs and the
installed Windows AMD64 client 2000935 / Steam build 26144732. This is client
work. No client/server was launched or contacted and no installed files or
server settings were changed.

## What the new logs establish

The two logs contain 75 native Scout fires matched to correction metadata:
47 confirmed hits, 26 misses and two lifecycle interruptions. These are all
automatic shots. The adapter remains available in every diagnostic. Accuracy
recovers after bad stretches: the second log has consecutive observed minute
groups of 8 hits / 1 miss, later 4 / 6, then 7 / 1. This is not a persistent
feature shutdown.

Independent SHA-1/RNG replay reproduces all 150 recorded client seeds and rays;
the largest reconstructed ray discrepancy is below 0.000008 degrees. This
checks the published model, not the eventual server seed or firing state.

One specific failure is demonstrated by PID 3024 shot 27, sequence 12663.
At the history edit the movement record is grounded, vertical speed is zero,
and the copied weapon inaccuracy is 0.15515. The command subsequently executes
a native jump, clears ground and receives vertical impulse 298.868. The
recorded impact fits the raw-tick spread bearing with inaccuracy about 0.3614;
the first post-shot native accuracy sample is 0.3667979. The impact fit is
retrospective, but the independently logged jump execution and accuracy change
establish that the pre-takeoff snapshot cannot describe the later shot.

Other records also show accuracy changes around takeoff, including a jump in
the previous command. The direct same-command evidence covers only four of
the 75 shots, three misses and one hit. Revision 4 addresses this concrete
transition; it does not explain or fix every bad stretch.

The remaining misses have mixed evidence: six impacts cross the saved capsule
without attributed damage, two stop short, and eighteen miss the saved capsule.
Ten differ by over one degree from both predicted rays. Timing outliers and
changes to origin, accuracy or target pose remain unresolved. The event stream
does not supply authoritative shot tick, seed or lag-compensated target pose.
The largest discrepancy also has competing publications around a prediction
rollback: an earlier command fits the recorded impact much better than the
most recent command selected by the logger, but does not reproduce it exactly.
It cannot establish which command fired. A different large-angle record has
an impact only about 76 units away, making copied-origin differences material.
Neither case justifies blindly expanding the seed tick range.

Full parsers, source line references and replay results are under
`logs/research/26144732/spread_revision4/{log_audit,native}/`.

## Implemented behavior

With Remove spread enabled, automatic publication checks the outgoing command
for jump intent while the pawn is still grounded. It includes native subtick
presses followed by release, so a clear final jump button does not hide a
takeoff. The feature defers its own automatic pulse on that transition and
regenerates the movement checksum through the existing cancellation path.
Subsequent commands use freshly captured weapon accuracy; there is no retained
wait counter or transition cache.

This may postpone a shot during takeoff. It preserves physical manual fire,
the outgoing movement/subticks, and behavior with Remove spread disabled.
Missing or invalid jump-intent data rejects automatic compensation rather than
publishing an unverifiable transition model. Existing airborne shooting still
uses the ordinary indexed correction path.

`AIM_SPREAD_TRANSITION` reports the reason, command sequence, flags, outgoing
buttons and subtick counts when publication is deferred. Revision 4
`AIM_SPREAD` records also include the copied accuracy, spread, pattern mode,
movement flags and velocity, with more precise command/ray angles. Movement
diagnostics are optional copies, not a new adapter eligibility requirement.
These values describe the callback, not an observed final firing state.

The independent on/off toggle remains **Aim → Remove spread**, default off,
saved as `aim.remove_spread`. The clock hypotheses and 8192-call search budget
are unchanged. Independent server-generated seeds remain unsupported when
active; their runtime use in these sessions has not been established.

## Research choices

UnknownCheats was searched as requested, but its forum could not be opened.
Accessible original [Vesta source](https://github.com/Read1dno/vesta/blob/467c33bbc8fe052833054bf8a1cd80a57590e9ca/src/simulation/seed_window.hpp#L60-L112)
explicitly considers timing and recoil uncertainty for external input. Its
external click timing is not a verified contract for this internal callback.
No third-party implementation was copied. Pinned source links and dates are
saved in `logs/research/26144732/spread_revision4/external/provenance.md`.

A separate experiment minimized spread error across the entire native
inaccuracy interval [0,1], seeking seeds insensitive to accuracy changes. It
was rejected: only 5/75 saved states contained all four endpoint rays, and
nominal capsule intersections fell from 75/75 to 17/75. Increasing uncertainty
that far would mostly suppress useful shots. Results and the reproducible
scratch solver are under `logs/research/26144732/spread_revision4/search/`.

## Verification and limits

The integrated checks cover held and subtick jump transitions, a jump added
after preparation, owned cancellation/checksum behavior, preservation of native
movement, manual and disabled-toggle behavior, and fresh airborne continuation.
Adapter checks cover copied movement diagnostics and missing diagnostics without
changing spread eligibility. Public and diagnostic x64 Release builds both
succeed without project compiler warnings and pass **63/63 CTest suites**.
The integrated pipeline has **400 checks**, and the native adapter has **119**.
Both DLLs retain AMD64 architecture, zero static TLS and zero delay imports.

All **30/30 installed-file checks** pass in
`logs/compatibility/20261024T083835649204Z/`. Build/test output is saved as
`logs/remove-spread-r4-{build,tests}-{public,diagnostic}.log`; payload inspection
is `logs/remove-spread-r4-payload-contract.json`. No native address, signature,
loader, build or launch workflow was added or changed in this revision.

After testing revision 4, the user reports better, decent but still imperfect
behavior. This is qualitative user feedback, not a measured post-revision hit
rate or confirmation that the remaining discrepancies are resolved.
Restart CS2 normally before
**Load Overlay.cmd** if an older DLL is resident. Stopped and resident is not
physically unloaded. Testing remains offline/private.
