# Scoped viewmodel and cosmetic recoil follow-up — October 25, 2026

The user reports that the first implementation's unscoped XYZ/FOV controls
work, but scoped weapon size and both cosmetic recoil toggles do not.
The original memory fixtures validated output edits without reproducing
the enclosing native view builder's later writes or all camera contributions.

Final user observations: revision 2 fixes scoped weapon size; revision 3
fixes camera recoil removal. The user subsequently requested removing the
weapon recoil removal option. That option and all exclusive HUD hooks,
settings and config output have been removed; legacy config keys are ignored.

## Scoped projection correction

Normal mode slot 15 (`0xB9D7B0`) invokes slot 16 at `0xB9DA41`.
The original Zoom hook runs in slot 16; its viewmodel FOV write was overwritten
by subsequent builder stores at `0xB9DBE2/0xB9DBF2` and
`0xB9DD41/0xB9DD51`, followed by aspect conversion at `0xB9DE72`.

Viewmodel revision 2 hooks the completed slot-15 builder and changes only
its viewmodel FOV output after native returns. World zoom and sensitivity
remain in the existing Zoom module. The callback validates the current local
source and scoped weapon around native execution, uses the native/custom
unzoomed base, and respects the completed aspect field and skip flag.
Nested builders forward native; stop drains the builder and parameter owners.

The integrated fixture now invokes the normal Zoom callback inside the
builder, performs the later native projection overwrite, and checks the final
result. It also covers custom/native bases, both scope levels, all six
supported scoped classes, aspect ratios, disabled settings, source changes,
nested builders and a blocked builder during cooperative stop.

The user tested revision 2 and **confirms scope size is fixed**.
Session `logs/overlay-18276-134373922380155709.log` confirms the completed
builder callback runs: at `08:59:12.413Z`, 522 eligible scoped views received
522 successful edits, with zero source/base/projection rejections. One copied
sample changed native projection 26.460 to 105.394 from selected base 89.100.
The user's observation establishes the visible result; counters alone do not.

## Recoil observation and missing contributions

The same trial still shows visible recoil to the user. Its logs establish
that the initial suppression runs: at `08:59:12.413Z`, camera edits numbered
4,506 and weapon edits 1,897, including 1,063 nonzero temporary punch vectors.
There were no runtime service-table, owner, returned-storage or output failures.
Eight initial missing-pawn rejections occurred before an available local pawn.
This rules out preparation or source rejection as the reason for that trial's
remaining motion.

The initial hook clears only the visual aim-punch getter output:
0.45 times that vector in camera evaluation, and 0.325 in the HUD transform.
Native camera evaluation separately adds a decayed camera-service viewpunch
vector before the 0.45 contribution. HUD evaluation also inherits camera
angles before adding its separate 0.325 contribution. The normal local HUD
uses cached renderer angles; another branch evaluates the camera directly.
Thus clearing 0.325 alone cannot remove the inherited camera kick.

The native weapon firing clip also contains authored arms/weapon motion and
bolt events. Removing that whole clip would remove other requested/native
animation behavior; no separate animation recoil weight has been verified.
Native bob, sway, roll and fire/reload animation require separate treatment
from the two additive camera punch contributions.

Exact contracts and evolving runtime verification are in
[camera/viewmodel recoil evidence](../tools/CS2_COSMETIC_RECOIL_EVIDENCE.md)
and [viewmodel evidence](../tools/CS2_VIEWMODEL_EVIDENCE.md).
This follow-up does not claim complete visible recoil removal from the
original getter-only hook. No gameplay recoil, shot-angle, prediction, spread,
command or installed-game data is changed by these cosmetic output edits.

## Final camera-only repair

The camera hook now establishes a validated native camera-evaluation scope,
preserves the original evaluation, and suppresses two temporary visual inputs:
the decayed camera-service viewpunch addition and the 0.45 aim-punch contribution.
The caller-specific angle-add replacement uses a zero temporary operand;
camera service state and its decay still run normally. Other angle additions,
camera roll, shake, input and native weapon animations remain native.
Damage/landing impulses stored in the same viewpunch vector are also hidden.

The expected internal aim-punch callback shares the outer camera barrier;
nested unrelated evaluations forward native, and cooperative stop drains the
complete camera callback before retaining stopped code until normal exit.
Tests model both native contributions together, source rejection, unrelated
callers, nested evaluation and a blocked camera callback during stop.

The user confirms **camera is fixed** after testing revision 3. They still see
weapon kick and explicitly request retiring that option. The final build keeps
camera recoil removal, scoped viewmodel-size correction and XYZ/FOV controls;
it makes no claim of removing the weapon's firing animation or visual kick.

Final public and diagnostic x64 Release builds each pass all **70/70 CTest
suites**, including **201 camera recoil checks**, **105 viewmodel checks** and
**2,015 integrated scoped-view checks**. All **35/35** installed-file verifiers
pass in `logs/compatibility/20261025T091815604629Z/`. UI/config tests verify that
the weapon control is absent and its retired config key is ignored/dropped.
