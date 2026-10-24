# Installed client skin contracts — October 24, 2026

Target: the installed **CS2 Windows x64 client**, patch **1.41.9.2**, client
**2000935**, Steam build **26144732**. The overlay's existing D3D11 and native
stage-8 owner remain the integration points. No game/server process was launched
for this investigation, and installed files were only read.

Client path:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256: `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.

Run `python tools/inspect_skins.py` to independently verify the frozen header,
nine named schema registrations, fourteen native code windows, native strings,
caller relationships and the installed client identity. `--client` and `--header`
support alternate installed-file locations and negative contract tests. The tool
uses Python's standard library and never updates source constants automatically.

## Item layout and ownership

Named class registration templates use the established local schema layout:
class name at `+0`, size at `+0x18`, field count at `+0x1C`, field array at `+0x28`;
each 32-byte field record contains the field name at `+0` and offset at `+0x10`.
The verifier records exact registrations and field arrays rather than inferring
offsets from names alone.

| Contract | Installed value and evidence |
| --- | --- |
| Weapon embedded item | `C_EconEntity::m_AttributeManager +0x1290` plus `C_AttributeContainer::m_Item +0x50`, hence `+0x12E0`; native getter `0x1132EE0` returns the same address. |
| Definition/quality | `C_EconItemView +0x1BA` uint16 definition; `+0x1BC` int quality. |
| Item identity | Item ID `+0x1C8`, high/low `+0x1D0/+0x1D4`, account `+0x1D8`; initialized byte `+0x1E8`. |
| Custom name | Named inline buffer `+0x2F8`; following name buffer starts `+0x399`, establishing 161-byte capacity. |
| Fallbacks | Paint/seed/wear/StatTrak at entity `+0x18A8/+0x18AC/+0x18B0/+0x18B4`. Native initialization `0x1141720` consumes these values. |
| Attribute list | Item `+0x208`, native vector count/data `+0x210/+0x218`; entry stride `0x48` agrees with `CEconItemAttribute` schema size and setter/remover iteration. |
| Attribute entry | Definition index `+0x30`, raw current value `+0x34`, initial value `+0x38`, set-bonus byte `+0x40`. |
| Weapon ownership | Pawn services pointer `+0x12F0`; `CPlayer_WeaponServices::m_hMyWeapons +0x48`. Native loop `0x915153..0x91526E` uses count `+0x48`, data `+0x50`, four-byte handles and complete serialized handle resolution. `C_BaseEntity::m_hOwnerEntity` is `+0x520`. |

Entity lifetime and serialized identity resolution reuse the existing verified
entity contracts. A matching address alone is insufficient when a slot is reused.

## Engine-owned attribute mutation

`client+0x11A3080` has the observed x64 ABI:

```cpp
void SetAttribute(void* itemView, const char* attributeName, float rawValue);
```

RCX is the item, RDX the name, and XMM2 carries the value. The wrapper calls
`0x11A2CB0` on `item+0x208` and then invalidates the item's native cached data
through `0x119EBB0`. The list setter resolves the attribute name in the native
item schema, searches non-set-bonus entries, updates the existing current value,
or creates an attribute through native allocation/vector operations. Code does
not construct an engine vector with the project's allocator.

An existing match changes only current value `+0x34`
(`0x11A2ED5..0x11A2EEC`). A newly constructed entry initializes current and initial
values to the input (`0x11A2E5F..0x11A2E7E`). Restoration therefore preserves the
original initial bits as well as current bits when removal and recreation were
needed. A matching set-bonus entry must not be treated as an ordinary owned
attribute: the setter explicitly skips it at `0x11A2D74`.

`client+0x11A12C0` is `void RemoveAttribute(void* itemView, uint16_t index)`.
It searches the same stride/index, removes through the engine's own vector path,
notifies the native manager if present, and invalidates cached data. Missing
original attributes can thus be restored as absent rather than fabricated zero
attributes.

The installed `scripts/items/items_game.txt` attribute definitions establish:

| ID | Native attribute name | Input encoding |
| --- | --- | --- |
| 6 | `set item texture prefab` | Numeric float; stored-as-integer policy `float_floor_to_integer`. |
| 7 | `set item texture seed` | Numeric float; same policy. |
| 8 | `set item texture wear` | Float; stored-as-integer `0`. |
| 80 | `kill eater` | Raw uint32 bits through float-typed ABI; stored-as-integer `1`. |
| 81 | `kill eater score type` | Raw uint32 bits; stored-as-integer `1`. |

The native fallback producer independently proves the encoding distinction:
paint `0x11417DD` and seed `0x11417FE` execute MOVD followed by CVTDQ2PS before
calling the setter. StatTrak at `0x1141845` executes MOVD and calls the setter
without numeric conversion. Type zero is XORPS XMM2 at `0x1141858`. Converting
the StatTrak count numerically to float would store the wrong integer bits.

The separate catalog generator verifies the installed item text and localization;
this verifier checks the resulting IDs and native named producer contracts.

## Native weapon material regeneration

The installed command name `regenerate_weapon_skins` at `0x1BD78A0` is registered
by `0x9C0E0`, which passes callback `0x82AA40`. This gives a named native operation
for tracing the required lifecycle instead of adopting a third-party signature.

For native weapon instances, the command's first loop executes exactly:

```cpp
CompositeReset(entity + 0x610, true); // client+0x14E9D00
WeaponRegenerate(entity, false);     // client+0x803780
```

Call sites are `0x82AABB` and `0x82AAC5`. Reset releases native composite handles
and pending requests using native reference/manager ownership. Regeneration
reads the current item via the native econ interface and calls composite
generation `0x8004B0`; that path resolves the item's paint definition, native
paint compmat resource, wear and seed, and creates native composite requests.
The generator refuses to duplicate current/pending requests at entity
`+0xAB0/+0xAC8`, explaining why reset precedes generation. Native global/model
availability can make generation return without creating a new material.

The earlier investigated virtual `+0x368`/function `0x829E00` updates visibility
and children; it is **not** used as a claimed skin regeneration API.

Skin mutation and calls belong on the existing stage-8 engine owner. Ordinary
UI/config threads publish settings only. Runtime shutdown requests restoration
on this owner, retains code/callbacks, and never physically unloads a live DLL.

## Knives, gloves and rendering limits

Knives retain the exact native general model setter/resource preload contract
already verified in `CS2_MODEL_SWAP_EVIDENCE.md`, with `C_Knife` RTTI at
`0x1C03720`. Knife model paths come from the installed item catalog. That proves
the callable model operation and resource provenance, not observed animation or
first-person rendering after a definition change.

This build contains no discovered named `C_BaseViewModel`/`C_CSGOViewModel` class,
RTTI or old `m_hWeapon` schema. Older game-version recipes for a separately
addressed viewmodel were therefore not imported. The named econ
`m_hViewmodelAttachment +0x18D8` is consumed as a cosmetic attachment; it has not
been established as a substitute first-person knife entity.

Gloves use the pawn's native embedded `C_EconItemView +0x1770`, reapply byte
`+0x176D`, and changed byte `+0x1D20`. Native leaf `0xC8FE40` sets reapply true.
The native owner at `0xC662F7/0xC66509` tests reapply, tears down an existing
attachment when needed, and rebuilds from the embedded item. It checks the
initialized byte (`0xC663F2`), gets the definition's model through `0x11924E0`
and then resolves/creates the native glove model/material attachment. No
project-owned entity or guessed wearable constructor is introduced.

The installed paint parser `0x113D7F7` reads `use_legacy_model` and stores the
boolean at paint-definition `+0xAE`. Weapon helper `0x8265A0` reads that paint
flag through the native item paint getter, subject to the installed
`weapon_skin_force_legacy` policy. Caller `0x91314C` publishes this result to
the `weapon_is_legacy_model` animation-graph variable. The frozen native material
generator builds legacy compmats and current item composites. **This investigation
does not establish that calling material regeneration alone switches every
required legacy render mesh/bodygroup.** No guessed mesh-group mask is applied.

Offline/private observation is still needed for actual paint appearance,
first-person versus third-person knives, glove attachment appearance, all legacy
meshes, animation sequences, network refreshes and setting changes across
respawns/team switches. Static checks, unit fixtures and successful builds must
not be reported as that observation.
