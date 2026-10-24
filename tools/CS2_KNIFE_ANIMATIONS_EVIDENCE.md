# Native knife animation identity — October 24, 2026

The user reports that cosmetic appearance works, while knife-specific animations
continue using the original knife. The existing implementation changes the econ
item definition and general render model. Native inspection establishes a third
identity that also controls animations: the weapon entity's subclass/VData.

Target remains the installed Windows AMD64 CS2 client **1.41.9.2**, client
**2000935**, Steam build **26144732**, D3D11 overlay. The parent investigation
reran the complete existing compatibility bundle: **32/32 pass**. No client or
server was launched for this investigation, and no installed files were changed.

Installed `client.dll`:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256: `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.

Run `python tools/inspect_knife_animations.py` to independently verify the exact
native contract. It supports `--client`, `--header`, and optional `--game` for
the installed VPK location. It reads installed PE/VPK files, verifies twelve
native code windows and named schema/RTTI/consumer relationships, and rejects
changed source constants/fingerprints. No disassembler, game process or asset
decompiler is required to run the verifier.

## Native animation producer and cause

Named `CCSWeaponBaseVData` schema contains:

| Field | Offset |
| --- | --- |
| `m_szAnimSkeleton` | `0x528` |
| `m_szName` | `0x720` |
| `m_szAnimClass` | `0x868` |

The native weapon graph updater at `0x91310A` reads the weapon's VData pointer
from entity `+0x388`, then uses `VData+0x868` as `weapon_type`, falling back to
`VData+0x720` when no animation-class override is present. The named
`CCS2WeaponGraphController::m_weaponType` schema member is `+0x120`. Earlier econ
item and model changes do not update this entity VData pointer or its subclass
ID. This supplies a concrete native explanation for the reported behavior;
no live post-fix animation result has yet been observed.

The installed `scripts/weapons.vdata_c` declares distinct subclasses matching
catalog internal names, including `weapon_bayonet`, `weapon_knife_butterfly`,
`weapon_knife_karambit`, and `weapon_knife_widowmaker`. The latter uses the Talon
animation skeleton. Each declares its own `m_szName` and `m_szAnimSkeleton`;
these knife entries do not override `m_szAnimClass`, so the native name fallback
selects the knife-specific graph identity. Resource data, not an invented
sequence-number translation table, supplies that selection.

## Selected item to native subclass

The installed UI weapon initializer at `0x8269D0` already demonstrates the
required relationship. It resolves an econ item and calls:

1. `0x7CC8B0(itemView)` to obtain the native weapon subclass/VData.
2. `0x210880(entity, subclassName)` to change native subclass identity.
3. `0x9651F0(entity, modelPath)` to select the item's model.

The item subclass getter has this x64 ABI:

```cpp
void* ItemSubclass(void* itemView); // client + 0x7CC8B0
```

It calls native static-definition lookup `0x1192390`, which uses the econ item's
uint16 definition index at `+0x1BA`. It reads that definition's native subclass
name and invokes `0x378D80` with weapon scope **1**. No project hash, offset
scanner, external SDK or temporary fake econ item is needed.

The returned native VData object has the observed base layout:

| Member | Offset / value |
| --- | --- |
| Primary vtable | `client + 0x1BDB390`, `CCSWeaponBaseVData` RTTI |
| Scope | int32 at `+0x8`, required weapon scope `1` |
| Subclass ID | uint32 at `+0xC` |
| Subclass name | native `const char*` at `+0x10` |

These offsets are independently consumed by native lookup/setter/UI callers:
`0x378E5D` checks the scope, `0x2108F0` reads the ID, and `0x826A4A` reads the
name. The native high-level setter resolves the same VData through the scoped
ID map `0x378FB0`; the native refresh below writes that returned object to the
entity's `+0x388`. Runtime validates the getter's result, nonzero ID, complete
name and expected native VData type before attempting a subclass transition.

## Named subclass notification and VData refresh

`C_BaseEntity::m_nSubclassID` is a named uint32 schema member at `+0x380`.
The generated schema callback registration loads the string
`OnSubclassIDChanged` at `0x42D9BF` and the callback address `0xB07310` at
`0x42D9CD`. This is a named native update contract, not a guessed vtable method.

```cpp
void OnSubclassIDChanged(void* entity); // client + 0xB07310
```

The complete callback calls `0x208EA0(entity)`, which resolves the native
subclass using the entity's scope and `+0x380`, writes the engine-owned VData
pointer to `+0x388`, then dispatches virtual `+0x640` with change type **1**
when VData exists. `C_Knife` RTTI at `0x1C03720` has scope getter `+0x638` =
`0x7CEB40` returning scope **1**, and change callback `+0x640` = `0x820E40`.

The project writes only its owned uint32 subclass ID and calls the named native
notification. It must not install a project-owned VData pointer or rewrite
the engine's graph-controller fields. The target ID comes from a successfully
resolved native object; arbitrary/missing IDs are not sent through the resolver,
whose failure path can remove malformed native entities.

The high-level native `SetSubclass` method at `0x210880` additionally checks
native entity-authority/creation conditions through `0x15E2160`. It is suitable
for the cited UI producer, but is not assumed to mutate an ordinary network
weapon. The network schema notification is the observed native consumer for a
changed replicated subclass member and performs the required resolution directly.

## HUD model invalidation and ordering

Native `0x820E40` resolves the weapon's current HUD model through its complete
serialized handle at entity `+0x1990`. At `0x820F1F..0x820F3F` it compares that
HUD model's model binding against the weapon's current binding. When they differ,
the named diagnostic says `Re-creating weapon hudmodel due to vdata subclass
model change.` and native `0xA1C6C0` removes the obsolete HUD entity. The normal
native lifecycle owns its recreation. The callback then forwards to native base
VData/model handling `0x9581E0`.

For the local cosmetic transition, make the requested weapon model current
before invoking the subclass callback. Otherwise a callback that compares two
old bindings can leave the original HUD model alive. The verified integration
order is:

1. On the existing stage-8 owner, validate the local pawn, complete serialized
   weapon identity, ownership and current settings.
2. Save the original item state, model, native subclass ID and VData identity.
3. Apply the selected item definition/attributes; obtain and validate its native
   subclass/VData through `ItemSubclass`.
4. Assign the requested model using the existing verified resource/setter path.
5. Write the selected native subclass ID, call `OnSubclassIDChanged`, then
   revalidate entity lifetime, resulting ID and VData against the selected native
   object. Material regeneration keeps its existing native ownership path.
6. On change/disable/stop, restore the owned original model and subclass ID with
   the same native notification, preserving concurrent native replacements.

The original VData pointer is diagnostic/ownership evidence, not memory owned by
the project. Native lifecycle changes or unexpected subtype changes invalidate
stale snapshots. Restoring the ID resolves current native VData again. No native
HUD deletion function is called directly, no graph object is manually destroyed,
and no callback is physically unloaded while it can execute.

## Asset provenance and verification limits

The shipped compiled weapon resource SHA-256 is
`879900e6af45204508b3c20a9d781b0e960684111b848b7ae5c26b438fc6ff19`.
The verifier reads this resource directly from the installed VPK and checks that
identity. Diagnostic decoded metadata is in workspace logs
`logs/knife_weapons_data.txt` and `logs/knife_animation_assets.json`.

The parent investigation decoded the installed resource using official
[ValveResourceFormat 20.1](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.1).
Downloaded release archive SHA-256:
`67aaf8a67a85ca60fd09ff343e62103a4cdf3a2d177fc656231d7d734c5b3aa3`.
This was a read-only diagnostic tool; it is not a runtime dependency. Installed
Valve animation/model assets are not redistributed by this change.

The contracts establish the source of knife-specific animation identity and a
native refresh/invalidation path. Build success and synthetic fixtures can
verify lifecycle/rollback logic, but actual draw/inspect/attack animation timing,
HUD recreation and every knife animation remain subject to offline/private
observation after loading the new DLL normally.
