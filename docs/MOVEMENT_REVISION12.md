# Movement revision 12 — October 23, 2026

Targets the existing Windows AMD64 client overlay, installed patch 1.41.9.2,
client/server 2000935, Steam build 26144732. No native addresses, hooks, game
files, server settings, loader behavior or shutdown policy changed.

## Findings from the reported run

Reviewed `logs/overlay-33704-134372367408887983.log`, which identifies revision 11
and an actual D3D11 Present. The static compatibility baseline passes all 29
components in `logs/compatibility/20261023T140008355306Z/`.

The source and log identify separate defects:

1. Turning selected a wish more than 90 degrees away from velocity. Its negative
   velocity projection intentionally removed momentum. The speed-80 branch
   switched to a different target-relative wish, producing abrupt low-speed
   A/D changes. The half-budget angle also left acceleration unused when aligned.
2. A jumpbug command ended with Jump down but called `ResetJump`. The following
   airborne command consequently emitted no release transition. Its final
   button mask alone did not clear historical input before integration.
3. Dense analog events repeatedly split movement around landing. They exposed
   the inherited fresh jump to native processing every 1/32 tick and allowed
   ground movement before a subsequent ground-aware command could jump.

At log lines 279–285, tick 563 has fresh jump input at fractions 0, .03125,
.0625, etc. Each call advances the actual press timestamp; the usable press is
cleared at .03125. At .15625 the pawn is grounded without takeoff. The next
slice reduces horizontal speed from about 226 to 183. Tick 563 ultimately has
32 distinct fresh-press fractions. Those are client observations, not 32 user
presses or independent server jumps.

Another example is sequence 9465: prior speed 260.382, observed landing speed
215.158, followed by takeoff at 198.034. A compact extraction is preserved in
`logs/movement-r12-baseline.json`: 62 planned jumpbugs, 99 observed landing
transitions, and 21 native ticks with at least 16 distinct fresh-press fractions.
These counts include prediction behavior and do not establish jumpbug success
rates or prove that every speed loss has the same cause.

## Implementation

The independent steering solver models the verified native accelerator:
`impulse = min(available, max(0, cap - dot(velocity, wish)))`. Its gain-maximizing
nonnegative projection is `max(0, cap - available)`, bounded by current speed.
When necessary to turn at low speed, it widens the wish toward perpendicular,
never behind velocity. This removes intentional braking and the hard speed
thresholds. A reversal follows an arc rather than shedding speed to reverse.

Up to 32 wishes remain available on ascent without jump input. Descending,
apex, and jump-input commands use one wish computed for the whole interval.
This trades some modeled acceleration for fewer artificial integration
boundaries around uncertain contact timing. For synthetic speed 250, wish
speed 250, cap 30, acceleration 12, friction 1 and interval 1/64, one aligned
command predicts 251.794 with one event or 255.429 with 32. These numbers are
not observed gameplay results. Physical events remain present and can still
split movement; this does not guarantee friction-free hops.

Jumpbug now emits Jump up at time zero before its existing duck/landing
transitions. A successfully published timed press remains owned in `JumpState`.
The following command explicitly releases it even when ordinary bunnyhop is
disabled or its rule read fails. It cannot immediately schedule another
jumpbug on that cleanup command. Rebuilds retain the selected release/press,
landing fraction and slice count. Ground handoff uses resolved physical WASD.

The observer now distinguishes `outcome=takeoff`,
`press_recorded_no_takeoff`, `buffer_changed_no_takeoff`, and
`held_no_takeoff`, with explicit vertical impulse and request/buffer changes.
Takeoff requires ground clear plus a positive vertical impulse. Even that is
an immediate client observation, not proof of damage avoidance or server outcome.

The existing hull probe remains a prediction from the current collision bounds.
It does not model ducking origin/hull changes or certify the true jumpbug
window. Replacing its extended trace with a direct-contact trace was considered
but not adopted without establishing native ground categorization. This pass
fixes the demonstrated input defect; it does not claim reliable jumpbugs.

## References and verification

The existing installed-binary evidence in
[`CS2_MOVEMENT_EVIDENCE.md`](../tools/CS2_MOVEMENT_EVIDENCE.md) is authoritative
for this build. External sources were used for comparison, not for offsets:

- [Valve's Source SDK air movement](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp)
  provides the capped-projection acceleration model; it is not CS2 source.
- [Velocity airstrafe](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/movement/impl/airstrafe.cpp)
  is the previous implementation's algorithmic reference.
- [Independent CS2 bunnyhop research](https://github.com/zer0k-z/cs2-movement-issue-bhop/blob/main/README.md)
  explains input splits, press buffering and the shared unduck/jump timing
  requirement. Its posture dimensions were not imported as verified constants.

No upstream implementation text was copied; existing license notices remain.
Both public and diagnostic x64 Release builds succeed without project warnings;
each passes all 58 CTest suites. Regressions cover independent impulse
integration, no speed loss in the modeled air step, sustained low-speed A/D,
changing physics, 1–32 event counts, physical intent refresh, timed-jump cleanup
with or without bunnyhop, prediction rewinds and chronological event ownership.
Both DLLs retain zero static TLS/delay imports. Logs use `logs/movement-r12-*`.

No client/server was launched or injected in this pass. The pre-fix log is real
client evidence; post-fix performance and gameplay remain unobserved. Use the
normal restart and **Load Overlay.cmd** workflow, beginning with Jumpbug off to
separate steering/hopping from the still-experimental landing maneuver.
