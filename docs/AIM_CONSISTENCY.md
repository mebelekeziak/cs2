# Aim consistency and quickstop

Implemented October 22, 2026 for the installed Windows AMD64 client, patch
1.41.9.1 / client-server 2000934 / Steam build 26087496. This change concerns
the existing client command pipeline. It adds no hooks or server integration.

## Corrected behavior

Previously an automatic request retained its preparation-time accuracy cone,
punch and readiness after native history construction. Structural weapon
validation checked identity, mode, ammo and recoil index, but changes to native
inaccuracy, spread, punch decay or prediction time could escape it. Publication
now refreshes the selected shot, including its native current origin, recomputes
angles and the complete configured sample count, and checks readiness and the
final damage path. A second native model read after tracing rejects a changed
cone/time/readiness. Final source, active, settings and focus checks precede the
history write. Failed owned requests use the existing attack restoration and
native CRC finalizer. Selection still shares one model/pattern across candidates;
publication samples only the selected candidate again, without reranking all
enemies or using a coarse hitchance prefilter.

The native current origin replaces the scene/view-offset reconstruction as the
starting point for acquisition, FOV, geometry probability, tracing and metadata.
It includes the engine's native view-offset interpolation. Target validation
retains a separate scene-eye snapshot to detect local scene changes; native
origin identity/position is revalidated independently. At publication a changed
native origin can be freshly solved against an otherwise unchanged selected
capsule. Changed scene/target/pose/health/weapon ownership cancels the old request.
See [native getter evidence](../tools/CS2_AIM_SHOOT_ORIGIN_EVIDENCE.md).

Command pitch/yaw now subtract the current native scaled aim punch. Command
roll stays zero. The sampler and shot metadata use command plus punch, including
punch roll in the spread basis. Yaw wraps through 180 degrees. A compensated
pitch outside the writable range rejects the edit; clamping would aim somewhere
different. Unavailable, invalid or changed shooting models leave physical manual
fire and its original history available. Authored geometry unavailable can still
use compensated named-landmark aim when the native shooting model is valid.
Manual thresholds remain advisory; automatic damage/hitchance gates remain exact.

Quickstop now uses verified live ground acceleration, stop speed, ground/surface/
pawn friction, effective movement speed and engine tick quantum. Native ground
acceleration uses a minimum acceleration speed even for small analog input, so
the old speed/30 taper could reverse velocity and oscillate near rest. Full
opposing input is used outside a conservative one-command impulse bound. Inside
the final window, counter-wish speed is capped at 0.25 units/s so the native
directional add-speed clamp bounds any reversal. Already settled speed uses
neutral axes. This tolerance is deliberate tuning, not a native accuracy rule.
Missing physics releases previous braking through the normal physical-input
handoff. Cooldown movement, exact command restoration, native arena growth and
same-command rebuild ownership are preserved. See
[ground-rule evidence](../tools/CS2_MOVEMENT_EVIDENCE.md).

## Diagnostics and limits

`AIM_HITCHANCE` appends the native-origin flag, copied eye, compensated command
angles and effective bullet basis. Failure reasons distinguish refreshed
below-hitchance/cooldown/model changes from a published shot. `QUICKSTOP_BRAKE`
reports observed speed, friction estimate, maximum impulse, settling branch and
live inputs at most once every two seconds. Callback diagnostics use the existing
bounded queue; callbacks do not perform file I/O.

The current native origin and current predicted punch have no explicit shot
timestamp. Native firing can query historical/subtick origins and punch through
different functions. Their full retained-command timing contract is not consumed
here; arbitrary older-origin selection and history time rewriting would not
establish correct alignment. Enemy extrapolation, backtracking, multipoints,
exact future stopped accuracy and future-position firing remain absent. The
quickstop bound does not simulate collisions, exact native movement subdivisions
or every stance/weapon modifier. It does not authorize a shot: fresh native
accuracy and full hitchance continue to do that.

Hitchance remains selected-capsule probability; direct/autowall damage remains
the central ray, rather than per-spread-ray damage through cover. None of these
changes establishes the cause of previously reported missed-shot streaks, exact
server timing or observed hit registration. No client/server was launched and no
installed file was changed for this task.

## Verification

Public and diagnostic x64 Release builds succeed without project warnings and
each pass 57/57 CTest suites. Both DLLs retain zero static TLS and delay-import
directories; PE/hash results are in `logs/aim-consistency-pe.json`. The integrated aim pipeline
passes 355 checks, including changed inaccuracy/spread/readiness/origin after
native history, changed recoil with successful recomputation, changed model/time/
origin during final tracing, late native-call settings/focus invalidation,
manual and automatic compensation, full sample counts, unrepresentable pitch,
and unchanged physical manual fire on failed validation. Target fixtures also
exercise a native eye differing from the scene eye and acquisition in a narrow
FOV that would reject the scene-derived ray.

The native origin fixture passes 61 checks; 25 altered source contracts reject.
Quickstop regressions reproduce the previous taper's oscillation and verify
bounded settling across movement caps, intervals, friction and acceleration
reductions, plus exact transaction restoration for tiny analog axes. All five
new movement source constants reject deliberate mutations.

Installed-file verification passes 28/28 components, including the new origin
contract, in `logs/compatibility/20261022T150721354315Z/`. The initial unchanged
baseline passed 27/27 in `logs/compatibility/20261022T145808023821Z/`.

Reproduce using the existing Release build commands and:

```powershell
ctest --test-dir build -C Release --output-on-failure
python tools/check_compatibility.py
```

Load using the existing **Load Overlay.cmd** workflow. Restart CS2 normally
before loading a rebuilt DLL when an older copy remains resident. Cooperative
stop retains code until process exit: stopped and resident is not physically
unloaded. Development and testing remain within offline/private environments.
