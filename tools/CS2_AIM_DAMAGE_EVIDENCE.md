# Direct bullet damage contracts

## Current static recertification, October 21, 2026

Installed Windows AMD64 client: patch `1.41.9.1`, client/server `2000934`,
Steam build `26087496`, Source revision `11150591`, version date October 20,
2026, 11:41:22. Installation/executable remain the paths below. Client SHA-256
is `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`. Animation identity is unchanged.

Before changing the frozen client hash, a separate scratch invocation reran the
complete existing schema, RTTI, native windows, rule registration/consumer,
import, numeric-constant and runtime-header checks against the new image. All
twenty native windows remain byte-identical at their existing RVAs. Fresh
Capstone 5.0.9 disassembly and an independent window-hash audit are retained in
`logs/research/26087496/aim_ballistics/`. No consumed field, RVA, fingerprint,
rule width, ownership or argument/return contract changed. Only the verifier's
client hash pin changes; the header records this recertification.

`python tools/inspect_aim_damage.py` passes. Temporary header mutations to
`Damage`, `DamageScaleCtBody`, `TraceHitbox` and `WeaponDamageInputsBytes` each
fail for the corresponding source mismatch. Reports, scratch code and the
sixteen-case shared negative report are in the research directory above.
Unknown binary hashes remain rejected. No client/server process was launched
or opened, and no installed file changed. Actual damage and live behavior on
this build remain unobserved; the earlier investigation below is historical.

Investigated October 20, 2026, against the installed Windows AMD64 client at
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
The executable is `bin/win64/cs2.exe`; client SHA-256 is
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The previously inspected runtime is native x64 with D3D11 rendering. This
feature operates on the existing command owner and introduces no graphics API.
The existing build identity is patch 1.41.9.1, client/server 2000933, Steam
build 25966348. Combined static compatibility passed all 22 existing components
before this investigation in `logs/compatibility/20261020T125420081801Z/`.

Research read installed files only. No client/server was launched or opened,
and no installed file was modified. Local Capstone under the ignored
`logs/research/python-deps` directory decoded instructions; runtime and the
ordinary verifier do not depend on it. The installed server DLL was inspected
only for schema comparison; no server address or server integration is used.

Run `python tools/inspect_aim_damage.py`. This standard-library verifier checks
the client and animationsystem SHA-256 values, four client schema registrations,
the independent authored hitbox schema, item-services RTTI, five named console
rule wrappers and consumers, twenty complete native instruction/data windows,
numeric constants, and every runtime header constant/window byte/address.
It fails on changed evidence instead of substituting new addresses.

## Weapon, protection and rules

| Named owner/field | Current offset |
| --- | --- |
| `CCSWeaponBaseVData::m_nDamage` | `0x820` |
| `CCSWeaponBaseVData::m_flHeadshotMultiplier` | `0x824` |
| `CCSWeaponBaseVData::m_flArmorRatio` | `0x828` |
| `CCSWeaponBaseVData::m_flRangeModifier` | `0x834` |
| `C_CSPlayerPawn::m_ArmorValue` | `0x1EC8` |
| `C_BasePlayerPawn::m_pItemServices` | `0x12F8` |
| `CCSPlayer_ItemServices::m_bHasHelmet` | `0x49` |
| `C_CSPlayerPawn::m_bGunGameImmunity` | `0x3508` |
| `C_CSPlayerPawn::m_flHealthShotBoostExpirationTime` | `0x15A8` |

The item-services native RTTI table is `0x1C10DD0`; its existing component
owning-pawn contract is `+0x38`. There is no heavy-armor field in the current
two-field client item-services schema. This implementation does not import
legacy heavy-armor constants from another game/build.

`FX_FireBullets` loads damage, range, penetration and range modifier from
current live weapon data at `0xD23B43..0xD23B6C`. The existing weapon adapter
validates the serialized weapon identity, live data pointer and supported
single-projectile firearm table; damage does not use a hardcoded item-ID table.

| Rule | Wrapper RVA |
| --- | --- |
| `mp_damage_headshot_only` | `0x24BCA68` |
| `mp_damage_scale_ct_body` | `0x24BCA78` |
| `mp_damage_scale_ct_head` | `0x24BCA88` |
| `mp_damage_scale_t_body` | `0x24BCA98` |
| `mp_damage_scale_t_head` | `0x24BCAA8` |

Each wrapper holds its current storage pointer at `+8`, and the value is at
storage `+0x58`. The first rule is a byte boolean; the other four are floats.
Native registrations at `0x85AF0..0x85E1A` independently name them. The damage
scaler at `0x89D9F1..0x89DA39` selects the CT body/head pair for victim team 3,
and the T pair otherwise. Supported prediction requires ordinary T/CT victims.
The native headshot-only consumer is `0x89DC24..0x89DCC6`; non-head damage is
zero in that mode. Immunity is read at `0x89DA41` and bypasses ordinary damage.

Healthshot expiration is queried by `0x8CCCF0..0x8CCD4B` in the same prediction
time domain as the existing weapon current-time helper. The damage path at
`0x89FE64..0x89FE88` may apply
`healthshot_healthboost_damage_multiplier` while a boost is active. Active
boosts are outside this ordinary-bullet model; prediction rejects them instead
of silently assuming their multiplier is one.

## First impact and hitgroup

The existing shot-mask trace supplies a fresh first collision along the segment
from local eye to the authored capsule center. Native helper
`0xAADF40..0xAADF80` reads result `+0x10` for its authored hitbox pointer.
`0x1779560..0x177956F` compares that pointer against the current sentinel stored
at client `+0x2733DC0`. A nonnull, nonsentinel hitbox supplies integer group
`+0x38`. This is independently named `CHitBox::m_nGroupId` by animationsystem
schema registration `0x81D848`, fields `0x81D6A0` (13 fields, size `0x70`).
Animationsystem SHA-256 is
`fb2b4cd71468e880c3b3845e8e7a18c9d550dd3c4504e4a257a2110fb91c1235`.

The runtime copies the group while the trace is current. It does not carry a
native hitbox pointer into shot logs or another command. The damage hitgroup
comes from the actual first collision on the selected pawn, so a ray hitting
that pawn's arm before its selected chest uses arm damage. Unknown/native
fallback collision-body groups, no actual selected-pawn collision, other first
entities, and covered paths cannot establish a valid damage estimate. A clear
full segment alone does not prove a hitgroup.

Distance to this first impact is the fresh trace fraction times the copied
eye-to-capsule-center segment length. Existing native trace validity and target
identity/geometry checks remain necessary. This is a direct-path model;
penetration, subsequent bodies, surfaces and pellet aggregation are outside it.

## Arithmetic

The ordinary unoccluded segment branch at `0x8A187D..0x8A18A5` computes:

```
damage = float(baseDamage) * powf(rangeModifier, distance / 500.0f)
```

It uses traveled segment distance; 500 is the float at `0x1B14FE4`. Its exponent
operation binds to the named `tier0.dll!V_powf` import at `0x1AAD530`. The native
head/body switch at `0x89DB48..0x89DC14`, with jump table `0x89DEB0`, applies:

| Group | Multiplier |
| --- | --- |
| 1: head | current weapon headshot multiplier × victim team's head scale |
| 2: chest | victim team's body scale |
| 3: stomach | 1.25 × victim team's body scale |
| 4/5: arms | victim team's body scale |
| 6/7: legs | 0.75 × victim team's body scale |
| 8: native additional group | victim team's body scale |

The double constants 1.25 and 0.75 reside at `0x1C0E8D8` and `0x1C0E8D0`.
Native helper `0x8D8CF0..0x8D8D64` requires positive armor and a helmet for
group 1. Groups 2/3/4/5/8 are armored without a helmet; legs 6/7 are unarmored.
The current native switch independently proves group 8's body multiplier and
armor coverage; this evidence does not infer a body-part name for that group.
Generic group 0 is also armored in the native predicate but is outside the
supported damage input groups 1 through 8.

The ordinary firearm armor getter at `0x884160..0x8842E5` starts with 0.5 and
multiplies by current `m_flArmorRatio` at `0x8842B6`. The ordinary bullet armor
branch at `0x8A0052..0x8A00BD` computes:

```
healthDamage = damage * (weaponArmorRatio * 0.5f)
armorCost = (damage - healthDamage) * 0.5f
if (armorCost > armor):
    healthDamage = damage - float(armor) / 0.5f
```

The minimum-damage policy floors the positive final float to a whole health
point. This is a conservative policy for comparing the estimate to the user's
slider; it does not claim the native server rounds every eventual event in the
same way. There is no epsilon that rounds a value up across a threshold and no
automatic cap to the target's remaining HP.

## Practical limits

Prediction estimates one current direct bullet at the first traced impact with
current protection and replicated rule values. Hitchance remains a probability
estimate and does not guarantee that a spread sample strikes the same group.
The server can use a different tick, pose, protection state or rules; private
server/plugin damage modifiers are not discoverable from this client snapshot.
Only the separately received native `player_hurt` event reports actual health
damage. An unmatched or ambiguously attributed event cannot honestly be claimed
by a particular prediction. Stopped code stays resident until normal process
exit; this work adds no teardown or physical-unload mechanism.

## Static verifier checks

Installed verification passed and was saved to
`logs/research/aim-damage-verification.json`. All 61 deliberately incorrect
header constants, runtime bytes and runtime window addresses were rejected;
details are in `logs/research/aim-damage-negative.json`. These are static/native
contract checks, not runtime observation of actual damage or hit registration.

## Implementation and verification

`src/aim_damage.cpp` reads copied weapon, armor, helmet, immunity, healthshot
expiry and rule inputs on the existing native command owner. It checks full
target/volume validity and local source, live weapon data and item-service
ownership. The retained snapshot is invalidated by changed damage inputs;
native tracing and history publication recheck the existing weapon/target
contracts. There are no new hooks, entity writes, installed-file edits or
initialization/teardown calls under the loader lock.

`SilentAimSettings::minDamage` defaults to 1 HP, clamps to 1–100 and occupies
atomic snapshot bits 46–52. `aim.min_damage` persists in presets; missing older
fields restore 1. The Aim slider is enabled with aim and autofire. Its minimum
must pass before a target can own a shot or quickstop. A rejected nearest
candidate leaves selection open to another enemy. The fresh pre-publication
trace re-evaluates damage; stale or insufficient estimates cancel through the
existing owned-attack restoration and native checksum path. Manual input is
not gated by the slider and may retain an unknown prediction.

Shot metadata copies the floating prediction, minimum and first native
hitgroup. Human results and `AIM_SHOT_DETAIL` compare it only with confirmed
damage to matching full victim identities. Missing damage remains unknown;
other victims, incomplete totals and ambiguous attribution stay explicit.

Public and diagnostic x64 Release builds each pass all 49/49 CTest suites.
New/expanded suites include 221 damage, 182 aim pipeline, 260 occlusion, 140
shot-log and 108 logging-runtime checks. These cover native-byte/identity
mutations, finite math and boundary behavior, armor exhaustion, per-team rules,
headshot-only mode, healthshot exclusions, stale sources, minimum equality,
candidate fallback, quickstop rejection and owned-shot cancellation. Settings,
config and mouse-driven menu tests also pass. Normal and narrow standalone
software-D3D11 previews were inspected at `logs/damage-menu.bmp` and
`logs/damage-menu-narrow.bmp`.

Final build/test output is retained in `logs/damage-build-{public,diagnostic}.log`
and `logs/damage-tests-{public,diagnostic}.log`; payload hashes and AMD64 PE32+
checks are in `logs/damage-payload-check.json`. Both DLLs retain zero static TLS
and delay imports. All 23 combined static compatibility components pass.
No client or server was launched. This establishes implementation and synthetic
behavior, not observed in-game damage accuracy, deceleration or hit registration.
