# Timed strafe and jumpbug — October 23, 2026

This change targets the Windows AMD64 CS2 client and existing D3D11 overlay.
It replaces revision 10's single-wish Axion steering with an independently
implemented adaptation of the algorithms inspected in these references:

- [Velocity airstrafe.cpp](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/movement/impl/airstrafe.cpp),
  blob `d39767b2e52c82fd088c7678fbdbb02fad167723`.
- [Velocity jumpbug.cpp](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/movement/impl/jumpbug.cpp),
  blob `e71fbe84a055632b4e0f1af6a13ce12f43108f1e`.

The inspected upstream repository reports no license. No upstream implementation
text, SDK structures or addresses are imported. Existing third-party notices
remain because this change does not establish that their other uses disappeared.

Strafe planning recalculates a wish direction over 32 equal slices of one
command. Its ideal angle depends on speed, the air cap and the available slice
acceleration. Simulated velocity advances with the full verified acceleration
budget for each slice; the sum never receives 32 whole ticks of acceleration.
Physical WASD, including W alone and diagonals, supplies the target heading.
Existing newest-key resolution and release/ground handoffs remain in use.
The camera is unchanged.

Jumpbug is an independent, disabled-by-default setting saved as
`movement.jumpbug`. With Space held and a descending airborne pawn, the native
player hull probe predicts contact in the next command. A qualifying landing
schedules duck down at zero, followed by duck up, jump up and jump down at the
selected landing fraction. This takes precedence over the ordinary bunnyhop
edge for that command. It also works with ordinary bunnyhop disabled. Physical
crouch input, including timed crouch transitions present when the command is
first planned, makes jumpbug yield for that command. A retained sequence keeps
its already selected transition set on prediction rebuilds.

The two-unit landing probe extension finds a nearby contact plane. Its raw
fraction is converted to time along the unextended command displacement using
the full plane normal. Contacts reachable only through the extension, or planes
the actual trajectory is not approaching, produce no jumpbug events. Accepted
times are not shifted by the reference's 0.001/0.99 clamp.

The command owner stages the complete transition set before applying it. Exact
event ownership permits restoration after native command reconstruction and
prevents unbounded appending on repeated callbacks. Prediction clock/ground
corrections retain the chosen landing fraction; changing WASD may refresh only
the steering from the frozen command-start state. Source/settings changes
invalidate the replay. The native trace adapter's installed-build contracts and
limitations are recorded in the [native evidence](../tools/CS2_JUMPBUG_EVIDENCE.md).

These are client predictions. Automated checks do not establish successful
jumpbugs, fall-damage avoidance, greater speed, or improved movement feel in a
live session. No client or server launch is part of this implementation pass.

Final verification: both public and diagnostic x64 Release builds pass 58/58
CTest suites without project warnings. The combined installed-file bundle passes
29/29 checks (`logs/compatibility/20261023T135339666379Z/summary.json`); all 17
jumpbug header mutations reject. Both payloads retain zero TLS/delay imports.
The standalone Movement menu preview was rendered and inspected. Build/test
logs use the `logs/movement-velocity-` prefix; the menu preview is
`logs/movement-jumpbug-menu.bmp`.
