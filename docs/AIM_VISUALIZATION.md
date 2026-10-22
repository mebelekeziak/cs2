# Ragebot point visualization

The markers represent the damage-path scan, rather than all evaluated geometry
or only commands that will fire. They use authored hitbox centers in the selected
groups/FOV and require range, a valid direct/autowall path and at least 1 predicted
HP. The configured minimum damage, hitchance, trigger state and firing cooldown
do not decide marker visibility. A 20 HP path is visible with minimum damage 40;
automatic shots and quickstop still require the configured minimum. Changing
minimum damage also keeps existing, unexpired path markers visible.
An empty clip or unsupported/stale weapon data leaves no markers.

The command owner reads one weapon sample and enumerates candidates. Each
available capsule center is checked on a copied decision before shot ranking
and spread estimation. The copy never requests an attack or quickstop. Real
automatic/manual shots retain their existing gates and fresh path rechecks.
Preview-only commands, including manual mode before any trigger input and
automatic cooldown, do not run hitchance estimates. Rendering consumes copied
positions/serialized handles with a short expiry and configurable RGBA.

The FOV setting is currently applied twice: target enumeration first checks the
selected bone pivot, then candidate evaluation checks the authored hitbox center.
Those positions can differ. A pivot outside FOV prevents that center from being
scanned even if the center itself would pass. This is a geometry prefilter, not a
line-of-sight requirement, and was not changed by the minimum-damage correction.

Verified October 22, 2026: public and diagnostic x64 Release builds succeed and
each pass 56/56 CTest suites. The integrated pipeline passes 329 checks, including
below-minimum direct/penetrated markers without attack or movement ownership;
the visualization suite passes 24 checks, including capture after changing
minimum damage. The software D3D11 menu preview was inspected. No client/server
was launched; live behavior remains unobserved.

## Local reference investigation

Inspected October 20, 2026, without modifying or building the user's reference:
`C:\Users\dawid\Desktop\Axion-CS2-RAGE-CHEAT`.

No working aim-point visualizer was found in that snapshot. In
`cstrike/features/rage/rage.cpp`, `get_points` is marked removed and returns an
empty vector. `ScanPoint` independently calculates hitbox damage and keeps damage
candidates; its best-point selection applies minimum damage. Later, `Run`
selects a target, invokes `Hitchance`, handles stopping/scoping, and finally gates
attack on accuracy and `cheat->canShot`. `Ready` checks weapon data/ammo, without
requiring the firing timer to be ready. This provides evidence for separating
damage scanning from the later firing decision; it does not establish the exact
visualization behavior of Neverlose or Fatality.

The reference is MIT licensed, copyright 2024 T1GXR. No reference implementation,
offsets, signatures or assets were copied into this project. Implementation
uses this repository's already verified target/weapon/path/damage adapters.
