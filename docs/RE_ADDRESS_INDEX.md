# CS2 address and evidence index

This is a navigation map for the Windows AMD64 **client**, recertified October
21, 2026: patch **1.41.9.1**, client/server **2000934**, Source revision
**11150591**, Steam build **26087496**, version date October 20, 2026, 11:41:22.
These are the installed build's verified constants, not portable signatures.
The October 21 evidence is static: it does not establish fresh ESP, movement or
world appearance observations. See the [update record](CS2_UPDATE_26087496.md).
All consumed runtime addresses below remain unchanged. Historical sections in the linked evidence
documents describe 1.41.9.0 and must not supply current runtime addresses.

For the update procedure, see [CS2 update guide](CS2_UPDATE_GUIDE.md). Use the
source headers as the complete inventory; this index groups the important
dependencies and explains where to resume investigation.

## Address conventions and installed modules

An **RVA** is relative to the named module's loaded base, not a raw PE file
offset or absolute process address. A **member** is relative to its owning
object/buffer. A **key**, mask, stride or enum value is not an address.
A **pointer slot** contains a pointer; an **embedded object** already is the
object at `module base + RVA`. Preserve this distinction before dereferencing.
All listed callback contracts use the Microsoft x64 ABI.

Installed game root:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
Client is under `csgo/bin/win64`; the other modules are under `bin/win64`.

| Module | PE timestamp / SizeOfImage | Full SHA-256 |
| --- | --- | --- |
| `client.dll` | `0x6AD7B41C` / `0x299D000` | `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49` |
| `scenesystem.dll` | `0x6ACFCD4F` / `0x978000` | `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5` |
| `worldrenderer.dll` | `0x6ACFCCF2` / `0x26B000` | `f9eeff50acb10a89ab78c98d2726d3e5e6b5adf7a8b11be2a41cb5ebded11dac` |
| `materialsystem2.dll` | `0x6ACFCC60` / `0x186000` | `b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6` |

The verifiers pin complete hashes and independently checked instruction/schema
evidence. A hash failure requests renewed investigation; replacing a hash or
applying one relocation delta to all symbols does not establish compatibility.
Source-header `--header` options select a header to check, never an output to
generate. Scope identities per subsystem: sky's client identity alone does not
certify ESP or movement.

## Landed grenade footprints and removal: client.dll

Added October 20, 2026 against the installed client identity listed above.
`include/grenade_area_offsets.h` owns the smoke/inferno class vtables and
geometry fields; `include/grenade_effect_offsets.h` owns native smoke rendering,
inside opacity and flash shader-parameter contracts. Run
`python tools/inspect_grenade_areas.py` and
`python tools/inspect_grenade_effects.py`, or the combined compatibility command.

| Contract | Rediscovery/evidence |
| --- | --- |
| `C_Inferno`, `C_SmokeGrenadeProjectile` primary vtables and fields | MSVC RTTI plus exact static schema registrations; [area evidence](../tools/CS2_GRENADE_AREAS_EVIDENCE.md) |
| Smoke simulation embedded in the projectile, voxel owner and ping-pong buffers | Native initialization/render iteration and coordinate conversion; occupancy uses current buffer, density uses the other buffer |
| Active fire world positions and visual half-width | Native burning-cell selection, world-coordinate copies and visual bounds; half-width is not a proven damage radius |
| Smoke volume draw and inside opacity | Native descriptor RTTI/dispatch and exact opacity callsite; [removal evidence](../tools/CS2_GRENADE_EFFECTS_EVIDENCE.md) |
| Flash white/afterimage alpha | Native `FlashbangOverlay` producer, exact parameter keys and writer return sites; transient float4 inputs, no persistent pawn writes |

The existing shared stage-8 owner captures copied world edges. Smoke is an XY
voxel-column projection; fire uses a union of live visual patch disks. Rendering
does not call engine methods. No live grenade appearance observation is claimed.

## Current native shooting origin and aim consistency: client.dll

Added October 22, 2026. `include/aim_shoot_origin_offsets.h` freezes the
`CCSPlayer_WeaponServices` table `0x1C14C78`, current-origin slot `0x100`
(index 32), and getter `0xA7FD00`. Microsoft x64 ABI is
`AimVector*(services*, AimVector*)`. Its pawn slot `0x568` resolves to
`0x93C660`; native base eye `0x15FAB0` and view-offset getter `0x160770`
include native interpolation. Named schema and independent history-producer
and fallback callers establish ownership and dispatch. Read only on the command
owner; native queries may refresh engine-owned scene/interpolation caches.

The aim pipeline uses one copied current native origin throughout a scan and
refreshes the selected origin/weapon cone after history construction. Current
punch is subtracted from outgoing primary-history pitch/yaw; the full sampler
models the actual command-plus-punch basis. Final path/model/source rechecks
cancel stale owned attacks through the native CRC restoration path. Historical
shot-time origin and punch are separate native contracts and remain unconsumed.
See [native-origin evidence](../tools/CS2_AIM_SHOOT_ORIGIN_EVIDENCE.md),
[consistency notes](AIM_CONSISTENCY.md), and run
`python tools/inspect_aim_shoot_origin.py` or the combined compatibility command.

## Ragebot autowall: client.dll

Added October 20, 2026.

`include/aim_autowall_offsets.h` and `tools/aim_autowall_contract.json` freeze the
installed client's bullet-data constructor `0x87EE00`, destructor `0x87FB70`,
trace producer `0x8830E0`, segment damage/penetration step `0x8A1820` and impact
extractor `0x885830`. Named weapon penetration is `CCSWeaponBaseVData +0x82C`.
The `0x1D20`-byte aligned buffer owns native hit and segment containers, including
any heap growth; its destructor runs before returning from each prediction.
No native trace pointers survive. The final native displacement at `+0x1D04`
owns the fraction-to-distance conversion because the producer can extend a ray
to locate exit surfaces. Native penetration state begins with a budget of four.
See [autowall evidence](../tools/CS2_AUTOWALL_EVIDENCE.md) and run
`python tools/inspect_autowall.py`. The optional autowall branch extends the
direct-only behavior described in historical occlusion evidence below.

## Entity acquisition and ESP: client.dll

Header: [cs2_offsets.h](../include/cs2_offsets.h), namespace `cs2_offsets`.
Verifier: [inspect_cs2.py](../tools/inspect_cs2.py).
Details: [entity evidence](../tools/CS2_ENTITY_EVIDENCE.md).

| C++ symbol (within namespace) | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `globals::dwEntityList`, `globals::dwGameEntitySystem` | `0x271A908`, pointer-slot RVA | Independently agreeing RIP-relative store `0x15BCD49` and load `0xA17AFB`; setter enters initializer `0x15B8CF0` |
| `globals::dwLocalPlayerController` | `0x253CB78`, pointer-slot RVA | Controller load `0x933E80` |
| `globals::dwLocalPlayerPawn` | `0x2565718`, pointer-slot RVA | Prediction-address getter `0xBF7240` resolves `0x2565620`; pawn compare/store at `0xBD0625` adds `0xF8` |
| `globals::dwViewMatrix` | `0x256B020`, inline matrix-data RVA | Matrix getter LEA `0xC14173`, selected-matrix stride 64 bytes; no extra pointer dereference |
| `entity_layout::TableOffset`, `IdentityStride`, `EntityPointerOffset`, `HandleOffset` | `0x10`, `0x70`, `0`, `0x10`, members/stride | System initializer forms embedded table at `0x15B8D6F`; lookup `0x39B8E0` loads table global `0x236FA70` and decodes identity slot/full-handle comparison |
| `entity_layout::IndexMask`, `BucketShift`, `BucketMask`, `BucketCount` | `0x7FFF`, `9`, `0x1FF`, `64`, constants | Handle-lookup operands `0x39B903` onward; 64 buckets × 512 identities |
| `globals::dwGameEntitySystem_highestEntityIndex` | `0x2120`, diagnostic member | Increment at `0x9F7D2D`; historical live evidence disproves using it to bound enumeration |
| `CEntityInstance::m_pEntity`; `CEntityIdentity::m_designerName`, `m_flags` | `0x10`; `0x20`, `0x30`, members | Exact schema class/field association; full identity/backreference checks in reader |
| `CCSPlayerController::m_hPlayerPawn`; `C_BasePlayerPawn::m_hController`, `m_hDefaultController` | `0x92C`; `0x14BC`, `0x14C0`, members | Named schema registrations and reciprocal full serialized handles |
| `C_BaseEntity::m_pGameSceneNode`, `m_pCollision`; `C_CSPlayerPawn::m_ArmorValue` | `0x330`, `0x340`; `0x1EC8`, members | Exact class/field records; armor changed from historical `0x1ECC` |
| `CGameSceneNode::m_nodeToWorld`, `m_vecAbsOrigin`, `m_angAbsRotation`, `m_flAbsScale`, `m_bDormant`; `CCollisionProperty::m_vecMins`, `m_vecMaxs` | `0x10`, `0xC8`, `0xD4`, `0xE0`, `0x103`; `0x40`, `0x4C`, members | Exact named schema fields; geometry policy and scale/rotation evidence in entity document |

Remaining health/team/name fields are enumerated in the header and verifier's
`FIELDS` mapping. Static schema registration templates use class name `+0`,
class size `+0x18`, field count `+0x1C`, field-array pointer `+0x28`; records
have stride `0x20`, name pointer `+0`, member offset `+0x10`. These are distinct
from runtime schema bindings with an additional leading base pointer.

Do not treat the table as a pointer at system `+0x10`: bucket pointers are
embedded there. Do not reject pawn label `c_cs_player_for_precache` based on its
wording; it was observed on active offline pawns too. Names alone do not prove
an active pawn. [player_reader.cpp](../src/player_reader.cpp) validates controller
ownership, handles, life state, dormancy and copied geometry, refreshing values
and projection on the selected Present. Do not retain engine entity pointers
across lifecycle changes.

## Movement: client.dll

Header: [movement_offsets.h](../include/movement_offsets.h), namespace
`movement_offsets`. Verifier: [inspect_movement.py](../tools/inspect_movement.py).
Details: [movement evidence](../tools/CS2_MOVEMENT_EVIDENCE.md).

| C++ symbol | Current value / kind | Rediscovery anchor / ABI |
| --- | --- | --- |
| `Input`, `InputVtable` | `0x257B640`, embedded-object RVA; `0x1C9DB20`, vtable RVA | `CCSGOInput` RTTI, constructor LEA `0xEE574` and vtable store `0xEE59A`; producer slot 5 = `0xB65DE0` |
| `Finalize` | `0xCECF90`, function RVA | Input vtable slot 6 and caller `0xB66968`; `void(input*, command*)`, return ignored |
| `ModernJump` | `0x8C34F0`, function RVA | Caller LEA `0x8DF74B` / call `0x8DF752`; `void(state*, moveData*)`, state = services `+0x6C8` |
| `EffectiveMaxSpeed` | `0x8CDEF0`, function RVA | Pawn argument and XMM0 result at `0x8A2131..0x8A214A` |
| `Ingest` | `0xA77390`, function RVA | Forwarding thunk `0x8E9510`; `void(services*, command*)`, native button/effective-axis copies |
| `AllocateSubtick`, `AddAllocatedSubtick` | `0x4F74A0`, `0x1233010`, function RVAs | Producer calls `0xB6661C` / `0xB66627`; allocator receives arena, append receives repeated container and step |
| `TickIntervalConstant`, `StaminaScaleConstant`, `SurfaceManager` | `0x1AC6064`, `0x1AC6098`, `0x254E100`, data RVAs | MULSS `0xA55F99`, DIVSS `0x8C46DA`, surface-row helper MOV RCX `0x9E3564`; consult evidence for indirection/row semantics |
| `command::Sequence`, `Presence`, `Base`, `Buttons` | `0x8`, `0x20`, `0x40`, `0x60`, command members | Native sequence assignment `0xB66113`; protobuf presence producer and finalizer loads |
| `base::SubtickSize`, `SubtickCapacity`, `SubtickRep` | `0x20`, `0x24`, `0x28`, base members | Producer `0xB665F7..0xB6662F`, append capacity comparison; container starts at base `+0x18` |
| `base::Angles`, `Tick`, `Forward`, `Left`; `ForwardPresent`, `LeftPresent` | `0x40`, `0x54`, `0x58`, `0x5C`; `0x40`, `0x80`, members/masks | Constructor `0x4F8260`, producer stores and presence ORs `0xB662A1` / `0xB66338` |
| `services::ModernJumpState`, `ModernActual`, `ModernUsable`, `ModernLanded` | `0x6C8`, `0x6D8`, `0x6E0`, `0x6E8`, members | Receiver LEA and state loads `0x8C3591`, `0x8C3619`, `0x8C362A` |
| `services::CommandForward`, `CommandLeft`, `Buttons` | `0x1A0`, `0x1A4`, `0x58`, members | Ingestion copies `0xA7762A..0xA77658`; historical effective axes differ from schema forward/left at `0x1C0`/`0x1C4` |

Other pawn/controller/service and move-data members are in the header. Nine
consumed fields are checked against their named installed schema classes;
non-schema layouts are checked through decoded producer/consumer operands.
Function byte arrays in the header are entry checks, not standalone proof of
ABI, ownership, or cross-version uniqueness.

### Movement cvar wrappers

All values below are **wrapper-start RVAs in client.dll**, independently matched
to exact registration names. `CvarValuePointer = 0x8` is the wrapper's pointer
member; `CvarValue = 0x58` is relative to the pointed-to storage. Getter
`0x160B10` confirms both steps. Supplying a pointer-slot RVA to the wrapper
accessor introduces an extra `+8` and reads the next object.

| Source symbol | Registration name | Wrapper RVA |
| --- | --- | --- |
| `AirCap` | `sv_air_max_wishspeed` | `0x25325B0` |
| `AirAccelerate` | `sv_airaccelerate` | `0x2564E90` |
| `AutoBunnyhop` | `sv_autobunnyhopping` | `0x2532758` |
| `JumpPenalty` | `sv_jump_spam_penalty_time` | `0x25325A0` |
| `JumpPrecision` | `sv_jump_precision_enable` | `0x2532640` |
| `LegacyJump` | `sv_legacy_jump` | `0x2532670` |
| `BhopWindow` | `sv_bhop_time_window` | `0x25324B0` |

Native `command+8` is the sequence; legacy protobuf base `+0x50` is not a valid
deduplication source. Prediction can rebuild one command and overwrite native
buttons from physical input. Preserve selected jump edges across same-command
prediction, invalidate replay on source/rollback changes, and refresh steering
intent. Subtick messages are arena-owned protobuf objects with presence masks,
capacity and pointer-order constraints; preserve unrelated actions/camera data
and their relative order. See [movement_command.h](../include/movement_command.h),
[movement_replay.h](../include/movement_replay.h) and the evidence revisions
before changing these contracts. Ingestion observation is not proof of final
collision response or server receipt.

## Silent aim: client.dll

The three additional verifiers are part of `check_compatibility.py`:
[shot history](../tools/inspect_silent_aim.py),
[eye offset](../tools/inspect_silent_aim_targets.py), and
[named animated points](../tools/inspect_silent_aim_hitboxes.py).
Detailed evidence: [history](../tools/CS2_SILENT_AIM_EVIDENCE.md),
[eye/entity](../tools/CS2_SILENT_AIM_TARGET_EVIDENCE.md),
[bones](../tools/CS2_SILENT_AIM_HITBOX_EVIDENCE.md).

| Symbol / owning header | Value / kind | Rediscovery anchor |
| --- | --- | --- |
| `silent_aim_offsets::HistoryBuilder`, `HistorySlot` | `0xD02130`, function RVA; `25`, vtable slot | Input producer dispatch `0xB66A36..0xB66A43`; `(input*, int slot, command*)`, call original first |
| `command::HistorySize`, `HistoryCapacity`, `HistoryRep`, `Attack1Index` | `0x30`, `0x34`, `0x38`, `0x4C`, command members | Native append and independent weapon consumer `0x80D85C`, `0x80D955` |
| `history::Angles`; `angle::Pitch`, `Yaw`, `Roll` | `0x18`; `0x18`, `0x1C`, `0x20`, members | Producer `0xCFA725..0xCFA77D`, native attack converter `0x803810` |
| `silent_aim_target_offsets::ViewOffset` | `0xF60`, pawn member | Exact `C_BaseModelEntity::m_vecViewOffset` schema; contiguous live float3 in native eye transform |
| `silent_aim_hitbox_offsets::LookupBone` | `0x952940`, function RVA | `(pawn*, const char*) -> int`; native named-pelvis caller `0xC7C507`, no animation refresh |
| `SkeletonVtable`, `SkeletonQuerySlot`, `SkeletonQuery` | `0x1C41CE0`, vtable RVA; `0x68`, byte slot; `0x1C9E70`, function RVA | `CSkeletonInstance` RTTI; supported query returns scene itself |
| `PlayerSkeletonVtable` | `0x1ACDE48`, vtable RVA | Live player wrapper `NetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance`; zero-offset skeleton RTTI base, schema `m_skeletonInstance=0x80`, constructor `0x92FC85..0x92FCA7` |
| `ModelState`, `ModelHandle`, `BoneArray`, `BoneCount`, `BoneStride` | `0x140`, `0xA0`, `0x1C0`, `0x178`, `0x20`, members/stride | Named skeleton/model-state schema; model lookup/count helpers and world-transform consumer |

History construction occurs after the existing movement finalizer. Primary taps
use the native attack index; held attack also needs all existing history angle
messages because the weapon retains/interpolates previous entries and can fall
back to the first current entry. Never synthesize attack indices, rewrite the
retained ring or alter base/camera angles. Missing/invalid history is a no-op.
Bone names resolve per model and callback; no cached numerical indices or forced
animation refresh. Missing selected bones/pose data skip the player. These are
animated landmarks, not authored hitbox-volume centers. Manual firing does not
require a visibility trace. This original stage did not provide recoil/spread
correction or penetration; later autowall and October 22 current-punch
compensation are documented in their sections above.

### Hitchance and autofire extension

The combined compatibility bundle also checks
[weapon contracts](../tools/inspect_aim_weapon.py),
[authored capsules](../tools/inspect_aim_geometry.py),
[native attack requests](../tools/inspect_autofire.py),
[direct damage](../tools/inspect_aim_damage.py),
[direct-path occlusion](../tools/inspect_aim_occlusion.py) and
[console output](../tools/inspect_aim_console.py).
The manual firing path above remains available; automatic requests additionally
require a current supported weapon, authored volume, sufficient sampled estimate
and native readiness, plus minimum predicted damage and a fresh direct-path trace before request and history
publication. See the corresponding
[weapon](../tools/CS2_AIM_WEAPON_EVIDENCE.md),
[geometry](../tools/CS2_AIM_GEOMETRY_EVIDENCE.md),
[autofire](../tools/CS2_AUTOFIRE_EVIDENCE.md),
[occlusion](../tools/CS2_AIM_OCCLUSION_EVIDENCE.md) and
[console](../tools/CS2_AIM_CONSOLE_EVIDENCE.md) evidence for exact field and ABI
rediscovery. Geometry changes from landmarks to authored centers when available.

| Symbol / owning header | Value / kind | Rediscovery anchor |
| --- | --- | --- |
| `aim_autofire_offsets::RecordHistory` | `0xD055C0`, client function RVA | Native history builder's initial recorder; primary event producer/consumer and native attack-index ownership |
| `aim_geometry_offsets` | Model mesh / selected-set / `CHitBox` layouts | Model helper `0x1773030`; named schema in `animationsystem.dll`; enabled-mesh consumer and capsule transform consumers |
| `aim_weapon_offsets` | Native spread/inaccuracy/readiness/punch and explicit firearm vtables | Named weapon/service schema, firearm RTTI, native shot spread law and punch consumers |
| `aim_occlusion_offsets::InitializeFilter` | `0x352920`, client function RVA | Complete filter constructor called by BulletServices at `0x887F71`; shared synchronous ray uses `third_person_offsets::TraceShape` |
| `aim_occlusion_offsets::ShotMask`, `ShotExclude` | `0x1C300B`, `0x4000000000`, collision masks | Native bullet filter producer `0x887F60` onward: layer 3, flags `0x0F`, additional byte `+0x39` bits `0x02` |
| `aim_occlusion_offsets::TraceEntity`, `TraceHitFlag` | `0x08`, `0xBB`, result members | Native entity comparison and DidHit consumer; shared fraction is `+0xAC`; zero shape bounds select a ray |
| `aim_damage_offsets::Damage`, `HeadshotMultiplier`, `ArmorRatio`, `RangeModifier` | `0x820`, `0x824`, `0x828`, `0x834`, weapon-data members | `CCSWeaponBaseVData` schema; native firing inputs `0xD23B43`, head/body switch `0x89DB48`, ordinary armor `0x8A0052`, falloff `0x8A187D` |
| `aim_damage_offsets::ItemServices`, `HasHelmet`, `ArmorValue`, `GunGameImmunity`, `HealthShotBoostExpiration` | `0x12F8`, `0x49`, `0x1EC8`, `0x3508`, `0x15A8`, pawn/service members | Exact named schema, item-service RTTI/owner, native armor coverage `0x8D8CF0`; active boost modifiers stay unsupported |
| `aim_damage_offsets::DamageScaleCtBody/CtHead/TBody/THead`, `HeadshotOnly` | `0x24BCA78/88/98/A8`, `0x24BCA68`, wrappers | Named `mp_damage_*` registrations `0x85AF0..0x85E1A`; native team scale consumer `0x89D9F1`; wrapper `+8` points to storage, value `+0x58` |
| `aim_damage_offsets::TraceHitbox`, `HitboxGroup`, `UnknownHitboxPointer` | `0x10`, `0x38`, `0x2733DC0`, trace/member/pointer slot | Native trace group helper `0xAADF40`, sentinel predicate `0x1779560`; independent animationsystem `CHitBox::m_nGroupId` schema |
| Native `tier0!Msg` | `0x7EE60`, named export | Variadic prologue and client `%s` format callers; runtime export/module verification in `aim_console.cpp` |
| Native `tier0!ConColorMsg` | `0x7F710`, export `?ConColorMsg@@YAXAEBVColor@@PEBDZZ` | RCX points to four RGBA bytes; RDX format, R8 first vararg. Client caller `0x1C6149` constructs a four-byte stack color. Optional color validation in `aim_console.cpp`; plain `Msg` fallback |

Console cleanup adds `engine2.dll` contracts in
[console_control_offsets.h](../include/console_control_offsets.h), verified by
`python tools/inspect_console_control.py` and the combined compatibility bundle:

| Boundary | RVA / kind | Independent evidence |
| --- | --- | --- |
| Console listener | `0x2094B0`, function | `CEngineConsoleLoggingListener` RTTI, primary vtable `0x586708` slot 0; formatted message in R8, console route `0x209536..0x20954C` |
| Native clear callback | `0x1B7D70`, member command | Registration `0x1B518E`, name producer `0x1B51C2` references `clear`; text-console and Panorama clear calls both complete before project replay |
| Locked command enqueue | `0x73720`, function | Native caller `0x75960/0x7596A` supplies buffer `0x8DBF20`; recursive lock and named tier0 `CCommandBuffer::AddText` import call at `0x73850` |

See [console-control evidence](../tools/CS2_CONSOLE_CONTROL_EVIDENCE.md).

Autofire prepares before anti aim and the original movement finalizer. It scopes
a native input press and lets the native recorder and subsequent history builder
choose serialized indices. Cancellation restores owned primary fields and reruns
the original checksum finalizer. It does not rewind native allocation/timing
caches, retain a physical attack latch, or perform physical DLL unload.
Newly obstructed or unavailable paths cancel owned automatic attacks through
that same checksum path. The direct occlusion check conservatively blocks all
cover. Optional **Autowall** extends it with the native penetration path indexed
above; manual input keeps its existing behavior.

## Anti aim: client.dll

Header: [anti_aim_offsets.h](../include/anti_aim_offsets.h).
Verifier: [inspect_anti_aim.py](../tools/inspect_anti_aim.py).
Details: [command/view evidence](../tools/CS2_ANTI_AIM_EVIDENCE.md).

| Symbol / owning header | Value / kind | Rediscovery anchor |
| --- | --- | --- |
| `anti_aim_offsets::Replay`, `ReplaySlot` | `0xCEA280`, function RVA; `8`, vtable slot | `CCSGOInput` RTTI/vtable; wrapper calls native reset `0xB6C980`, ABI `void(input*, int slot)` |
| `InputAngles` | `0x688`, input member, inline float3 | Native reset stores at `0xB6CA5C/0xB6CA64`; producer uses input-slot state `+0x228/+0x460` |
| `InputPreviousButtons`, `InputCurrentButtons` | `0x250`, `0x258`, input members, uint64 masks | Native replay stores command down to previous at `0xB6CA09`; producer combines previous/current at `0xB66924`/`0xB6690A` |
| `CommandLookup` | `0x9444A0`, function RVA | Native replay calls it at `0xB6C9BC`; ABI `command*(controller*, int sequence)`, sequence-checked 150-command ring with `0x98` stride |
| `GameRules`, `FreezePeriod` | `0x25651D0`, pointer RVA; `0x40`, bool member | Native RIP-relative rules load at `0x817FA0`; `C_CSGameRules` schema registration `0x2212A78`, field array `0x2211E20`, named `m_bFreezePeriod` |
| `Attack`, `Attack2`, `Use` | `1`, `0x800`, `0x20`, uint64 button masks | Named `IN_ATTACK`, `IN_ATTACK2`, `IN_USE` native enum records |
| `base::Up`, `UpPresent` | `0x60`, base member; `0x100`, presence mask | Native producer `0xB663D0`, ingestion `0xA77658` |

Command changes run after movement planning and before the original movement
finalizer. Physical input and shot history remain independent. The reset wrapper
retains the current physical input view across native command replay, including
retained commands after stopping edits. A time-zero analog event compensates
historical movement at basis handoffs; native allocator/append ownership and
stable original event order are retained. The verifier freezes the historical
SetupMove, zero-time decoder/integrator and AirMove flattened-basis consumers.
Do not replace WALK with a full-3D movement model or infer other move modes.
Native clone `0xB5E310` copies protobuf fields and the native sequence; command
publication history must use copied values and source/sequence, independently
of exact-pointer mutation restoration. The verifier freezes that clone body
and relevant dispatch sites. Revision 3 uses a +89-degree ground-facing pitch;
`cl_pitchdown`/`cl_pitchup` constructors reference float 89 at `0x1C66258` and
native pitch clamp `0xB63B70` consumes their live values. These are ordinary
defaults, rather than immutable bounds. The user confirmed local view for
revision 2; revision 3 movement and server results remain unobserved.

Revision 5 reads sequence N-1 through the controller lookup for each rebuild of
N; the most recently ingested prediction command can be N itself. Movement
planning and serialized time-zero handoffs use the same predecessor. Replay
also advances previous direction bits from current physical input, retaining
native action bits and changed/tap resets. Lookup/ring bodies and the input
producer/reset fields are frozen by the verifier. New replay/rebuild regressions
pass; live movement confirmation remains pending.

## Third-person camera: client.dll

Header: [third_person_offsets.h](../include/third_person_offsets.h). Verifier:
[inspect_third_person.py](../tools/inspect_third_person.py). Details:
[native camera evidence](../tools/CS2_THIRD_PERSON_EVIDENCE.md).

| Symbol | Current value / kind | Rediscovery anchor / contract |
| --- | --- | --- |
| `OverrideView`, `IsThirdPerson` | `0xB99250`, `0xB627A0`, function RVAs | `ClientModeShared` slot 16; three direct predicate callers; `void(mode*, view*)`, `int(input*, slot)` |
| `ModeSharedVtable`, `ModeNormalVtable`, `ModeSlot` | `0x1C68368`, `0x1C9FC20`, vtables; `0x28`, member | Named mode RTTI, slot-32 getter `0xB96570` |
| `ViewOrigin`, `ViewAngles`, `IsThirdPersonCameraReturn` | `0x4A0`, `0x4B8`, members; `0xB992A9`, return RVA | Base-view origin math and angle stores; exact native camera query return |
| `TraceShape`, `TraceManager` | `0xA1A000`, function; `0x2227058`, pointer slot | Native camera thinker `0xB62A20` hull-sweep call `0xB636F1`, six arguments |
| `InitializeShape`, `InitializeTrace`, `PopulateFilter` | `0x36DDB0`, `0x1779300`, `0xB722A0`, functions | Verified camera shape/result/filter producers; current-thread stack storage |
| `ShapeSize`, `TraceSize`, `FilterSize`, `TraceFraction` | `0x30`, `0xC0`, `0x48`, sizes; `0xAC`, result member | Native stack layout, filter destructor allocation size, fraction consumer |

Only slot-zero live local sources are overridden. A dynamic TLS scope keeps
one base-view camera query native while the model visibility consumers see
third person. After the base view returns, only its current origin is translated;
angles/FOV and later game-specific effects remain native. Collision copies the
native 9-unit camera hull/filter contract and validates the sweep fraction.
Input flags, shared cvars and command angles are never written. Stop disarms
queries and drains view edits; hooks remain resident and forward until exit.

## Sky: scenesystem.dll plus one client.dll producer

Header: [world_offsets.h](../include/world_offsets.h), namespace `world_offsets`.
Verifier: [verify_sky_draw.py](../tools/verify_sky_draw.py).
Details: [sky draw evidence](../tools/CS2_SKY_DRAW_EVIDENCE.md).

| C++ symbol | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `SkyDescriptorVtable`, `SkyRender` | `0x5F64D8`, vtable RVA; `0x154FB0`, function RVA | RTTI `CSkyBoxObjectDesc`, primary descriptor render slot `+0x08` |
| `SetFloat4`, `SkyTintReturn`, `SkyTintKey` | `0x4DA70`, function RVA; `0x1551A9`, return-site RVA; `0x99E6002A`, key | Sky stack float4 construction/call at `0x1551A4`; copier reads all 16 bytes into collection storage |
| `DrawStride`, `DrawSource`, `SceneKind` | `0x70`, `0x18`, `0xC4`, stride/members | Render's last-packet source lookup and scene constructor |
| `VisibleKind`, `LightingKind` | `0xE5FD3C88`, `0x0881EE9E`, kind IDs | Scene constructor default and client's lighting-scene kind store |
| `LightingProducer` (**client.dll**) | `0x28269E`, instruction RVA | Exact kind store, lighting material handle `+0x10A0`, creation slot `+0x138`, lighting output `+0x10F0` flow |

Render takes seven arguments; preserve all of them. Scope tint interception to
the verified callsite/key and nested thread-local sky designation. The native
writer copies a temporary float4 synchronously. Entity-update scheduling alone
does not recur for unchanged maps. Lighting-sky appearance depends on native
view refresh and does not recompute baked lighting.

## Flat world materials: scenesystem.dll and material consumers

Header: [world_material_offsets.h](../include/world_material_offsets.h), namespace
`world_material_offsets`. Verifier:
[verify_world_materials.py](../tools/verify_world_materials.py), including hashes
and consumer checks for worldrenderer/materialsystem2.
Details: [material evidence](../tools/CS2_MATERIAL_EVIDENCE.md).

| C++ symbol | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `AggregateVtable`, `AggregateCallback` | `0x5E4538`, vtable RVA; `0x41F30`, function RVA | RTTI `CAggregateSceneObjectDesc`, slot `+0x20`; worldrenderer exact lookup string `AggregateSceneObjectDesc` |
| `FlatBinding`, `Type1Stream`, `Type2Stream` | `0x9217D8`, `0x921390`, `0x9213A8`, global-slot RVAs | Initializer name `materials/dev/reflectivity_90.vmat`; aggregate native selection/stream loads |
| `PacketSize`, `Material`, `Mode`, `Color` | `0x70`, `0x20`, `0x28`, `0x50`, stride/members | Native append/constructor stores; actual DrawArray consumes material `+0x20` and separate mode tuple |
| `ViewInArgs`, `ViewFlags`, `ModeInVtable` | `0x10`, `0x48`, `0x30`, members/vtable byte slot | Four-argument primitive-generation callback; material dispatch receives `(material, sceneView+0x30)` |

Call original generation before modifying only its newly appended records;
overflow storage can reallocate. Resolve engine-owned materials and streams
within each callback. Material dispatch must lie inside the verified loaded
materialsystem2 image. Native view flag bits do not classify camera/auxiliary
views. Packet `Color` is retained as layout evidence: aggregate DrawArray and
DrawPrimitive **do not consume it for surface tint**. Arbitrary surface tint is
unsupported; [surface tint evidence](../tools/CS2_SURFACE_TINT_EVIDENCE.md) records
the shader-consumer research and unresolved lifetime/cache questions.

## Local lights and suppression: scenesystem.dll

Headers: [world_lighting_offsets.h](../include/world_lighting_offsets.h) and
[world_lighting_switch_offsets.h](../include/world_lighting_switch_offsets.h).

| Namespace / C++ symbol | Current value / kind | Verifier and rediscovery anchor |
| --- | --- | --- |
| `world_lighting_offsets::PackLocalLight`, `PackThunk`, `PackReturn` | `0x1B14B0`, `0x1B14A0`, function/thunk RVAs; `0x1B2537`, return-site RVA | [verify_world_lighting.py](../tools/verify_world_lighting.py): actual `g_BarnLights` record pack, reached from the ordinary GPU-light upload |
| `world_lighting_offsets::BinnerVtable`, `PackSlot` | `0x5FE688`, vtable RVA; `0x70`, slot byte offset | `CLightBinnerGPU` descriptor and indirect native packing dispatch |
| `world_lighting_offsets::GpuStride`, `GpuRadiance` | `0x1E0`, `0xA0`, stride/member | Native output stride at `0x1B2519` and RGB store at `0x1B17BB`; generic/lightmapped structured-buffer consumers load 480-byte records at byte 160 |
| `world_lighting_switch_offsets::GatherPointSpot`, `GatherArea` | `0x19F300`, `0x19FE30`, function RVAs | [verify_lighting_switches.py](../tools/verify_lighting_switches.py): exact `lb_enable_lights` registration and clean native disabled paths |
| `world_lighting_switch_offsets::PointGate`, `AreaGate`, `FrameClear` | `0x19F30D`, `0x19FE49`, `0x1A898B`, instruction RVAs | Native boolean consumers branch before output/state work; per-view `[binner+0xE3D0]=0` clear |

The local GPU pack receives binner, source record and output GPU record in
RCX/RDX/R8. Preserve its full RAX and all native arguments; modify only fresh
output RGB after the original returns at the exact verified upload caller.
The engine removes the selected sun from this ordinary light list, leaving sun
controls independent. Both suppression gathers
take three pointers and return void; their native disabled paths establish
that skipping is safe before output work. Sun/shadow gates inside other large
functions do not establish safe whole-function early returns.

The old generator tint hook has been replaced to cover the final light records.
The removed `0x1B0FB0` hook was misidentified as area/barn packing: it packs
cubemap reflection volumes (`0x90` stride, multiplier at `+0x70`). Those values
are not diffuse local-light radiance. Do not restore that hook under a local
lighting label or treat its callback counts as proof of a world-light change.

Details: [local radiance evidence](../tools/CS2_LIGHTING_EVIDENCE.md) and
[gather/switch evidence](../tools/CS2_ENV_LIGHT_EVIDENCE.md).

## Sun, ambient fill and directional lightmaps: scenesystem.dll

Header: [world_environment_lighting_offsets.h](../include/world_environment_lighting_offsets.h),
namespace `world_environment_lighting_offsets`.
Verifier: [verify_environment_lighting.py](../tools/verify_environment_lighting.py).
Details: [baked/environment lighting evidence](../tools/CS2_BAKED_LIGHT_EVIDENCE.md).

| C++ symbol | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `CopyBuffer`, `CopyReturn`, `BufferProducer`, `BufferUpload` | `0x569120`, function RVA; `0x1B0CD1`, return-site RVA; `0x1B0CB5`, `0x1B0F77`, instruction RVAs | Native per-view buffer builder `0x1AF7B0`; source-to-variant copy and graphics upload slot `+0xF8` |
| `BufferSize`, `SunRgb`, `DirectionalLightmap` | `0x7DE0`, size; `0x79A0`, `0x40`, buffer members | Copy/upload byte count; source buffer base is `RBP-0x30`, so stack `RBP+0x7970` is buffer RGB `+0x79A0` |
| `AmbientProducer`, `AmbientStore`, `AmbientRgb` | `0x1AF956`, `0x1AF998`, instruction RVAs; `0x120`, buffer member | Context key `0x8310AEF1` and float4 getter `0x4D270`; stack `RBP+0xF0` maps to buffer `+0x120`; original generic/lightmapped DXBC reads CB3 float4 18 as diffuse fill |
| `DirectionalProducer`, `DirectionalStore`, `SunProducer` | `0x1AF917`, `0x1AF939`, `0x1AFA8C`, instruction RVAs | Exact `r_directional_lightmaps` registration; native 0/1 producer/store; selected-sun RGB loads |

Modify the first freshly built stack **source**, so native copies propagate to
all three submitted variants. Verify both buffers belong to the current thread
and preserve the original destination return. Sun RGB `+0x79A0` is distinct
from adjacent `+0x7990`; keep alpha/other fields intact. Directionality zero
means enabled, one disabled. This changes shading constants, not baked textures.
`sc_disable_baked_lighting` and shader metadata `g_flLightMapScalar` do not
establish safe recurring fullbright controls.

The optional ambient control adds `color * intensity` to the fresh native fill
RGB, preserving W. Disabled, zero-intensity or black is a no-op. This is a real
scene-lighting input before albedo/fog; it cannot subtract/recolor baked
irradiance. [inspect_ambient_lighting.py](../tools/inspect_ambient_lighting.py)
reproduces consumers in twelve original installed shader variants. Its heavier
shader research dependencies are documented in the evidence; the ordinary
environment verifier checks the native producer/store and header constants.

## Scoped weapon zoom: client.dll

Header: [zoom_offsets.h](../include/zoom_offsets.h).
Verifier: [inspect_zoom.py](../tools/inspect_zoom.py).
Details: [scoped zoom evidence](../tools/CS2_ZOOM_EVIDENCE.md).

The game-specific `ClientModeCSNormal` view override at `0xD02E30` (primary
vtable slot 16) calls the shared third-person implementation before later
camera effects. Apply scoped FOV after the game-specific override returns.
The controller's native unscoped FOV getter at `0x93FC40` reads desired FOV
and applies the engine default when zero. Recheck named schema fields,
scoped-weapon RTTI ownership, the active weapon's full serialized handle,
view-FOV stores and both native entries before adapting this feature.

Scoped sensitivity uses the same current serialized source and replaces only
the fourth float argument in the normal/alternate mouse conversion callbacks.
The baseline multiplier is 1; the native argument includes the game's scoped
adjustment. Verify the complete caller chain before reusing this assumption.

| Symbol | Current value / kind | Rediscovery anchor / ABI |
| --- | --- | --- |
| `MouseConvert`, `MouseConvertAlternate` | `0xBA6ED0`, `0xBA6D00`, function RVAs | Dispatcher calls `0xBA6934` / `0xBA694C`; normal `void(int,float,float,float,float*,float*,int*,int*)`, alternate `void(int,float,float,float,float,float*,float*)` |
| `MouseInput`, `MouseInputVtable`, `MouseScale` | `0x257B640`, embedded object; `0x1C9DB20`, primary vtable; `0xB80180`, method RVA | CCSGOInput RTTI, slot 24 identity getter returns incoming XMM1 |
| `MouseCaller`, `MouseScopeDispatch` | `0xB655C3`, `0xBA68DA`, code windows | Native caller passes literal 1 through identity getter; dispatcher multiplies positive pawn scope adjustment and forwards XMM3 |
| `MouseScaleUnit`, `FovSensitivity` | `0x1AC6074`, float constant RVA; `0x149C`, pawn member | Caller literal and named `C_BasePlayerPawn::m_flFOVSensitivityAdjust` schema; runtime does not write the member |

## First-person scope overlay: client.dll

Header: [scope_overlay_offsets.h](../include/scope_overlay_offsets.h).
Verifier: [inspect_scope_overlay.py](../tools/inspect_scope_overlay.py).
Details: [scope overlay evidence](../tools/CS2_SCOPE_OVERLAY_EVIDENCE.md).

The native producer `void(pawn*, output*)` at `0x8E15E0` writes temporary visual
parameters. Only its render caller returning to `0x11DA311` is eligible for scope
replacement; the FOV caller returning to `0xCEFCAF` remains native. Output byte
`+4` gates the legacy sniper scope mask, lines and blur at the graph consumer
`0x11EC870` / scope branch `0x11ECA98`. Output float `+0` belongs to the separate
health-boost effect and must be preserved, along with all remaining fields.
The caller also copies byte `+4` to stack `[rbp+0x176]`, or view-state `+0x1B6`.
That flag gates the modern `SniperZoomStencil` and sniper crop setup too.
Re-establish the producer, both callers, copied view-state flag and complete
consumer branch before adapting these constants. Replacement also requires a
fresh crosshair sample for the same validated local pawn and scoped weapon.

For AUG/SG556, `CIronSightController::IsRendering` at `0x82FE70` returns a boolean.
Its geometry and complete resolve/filter graph callers return to `0x8321E2` and
`0x11F9976` respectively; both have native resource-free early exits when false.
Only these two call sites may override a native true result. The controller is
embedded in its owning weapon at schema field `m_IronSightController` (`0x1BD0`),
so the full active local weapon identity can be checked before replacement.
Other predicate consumers remain native. Recheck both complete caller windows,
controller ownership, gate ABI and native early exits when updating.

## Local player models: client, engine, host, resource system and tier0

Header: [model_swap_offsets.h](../include/model_swap_offsets.h).
Verifier: [inspect_model_swap.py](../tools/inspect_model_swap.py).
Details: [native evidence](../tools/CS2_MODEL_SWAP_EVIDENCE.md) and
[pack evidence](../tools/CS2_MODEL_ASSET_EVIDENCE.md).
Added October 18, 2026 against client/server 2000933 / Steam build 25966348.

| Symbol | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `FrameStage`, `FrameVtable`, `FrameSlot`, `ApplyStage` | client `0xB6D700`, `0x1C630F0`, slot 36, stage 8 | CSource2Client RTTI, native previous-job barrier, engine network-update stage producers; apply after original returns |
| `SetModel`, `ModelInterface` | client `0x9643C0` function, `0x2560EB0` pointer slot | `void(entity*, const char*)`; GameModelInfo resolver and native strong-model setter |
| `ModelBinding`, `ResourceName` | skeleton `+0x1E0`, CModel `+8` | Named skeleton/state schema and native getter; dereference binding before CModel |
| `InitializeResourceName`, `ResourceSystemPointer` | host `0xC4380` function, `0x1445E0` pointer slot | Native CResourceName constructor caller, inline capacity/flags, hash and model type |
| `BlockingLoad`, `BlockingLoadSlot`, `ResourceSystemVtable` | resourcesystem `0x16B20`, byte slot `0x148`, `0x65348` | Named blocking JIT manifest path; native manifest owns the returned binding |
| `PurgeResourceName` | tier0 `0x53D60` | Exact CBufferString::Purge export and native caller; cleanup temporary name through engine allocator |

`host.dll` resides in **csgo/bin/win64**. The remaining non-client modules are
in **bin/win64**. Setter lookup alone can return an error placeholder; perform
the verified blocking load and compare the returned canonical model path first.
Restore only on the native owner with fresh full pawn/controller identities.
Stop queues owner-thread restoration and retains resident forwarding code.

## Overlay and final-image pass

[overlay.cpp](../src/overlay.cpp) discovers Present and ResizeBuffers addresses
using a temporary D3D11 swap chain and SDK COM slots 8/13, then verifies real
callback objects/methods before selecting a chain. There are no frozen DXGI
function RVAs to relocate in this index. Rediscover/observe the installed
graphics runtime, actual D3D11 device, swap chain, render thread and device-loss
behavior before assuming this path is available.
[world_postprocess.cpp](../src/world_postprocess.cpp) uses that verified D3D11
device/context and final image; exposure/contrast/saturation/gamma also affect
the game HUD. It does not establish an engine world-material color input.

Across all hooks, initialization/teardown stay outside DllMain. Respect native
callback ownership, invalidate stale sources and use cooperative stop barriers.
Original calls may still execute after disarming. Stopped code/trampolines remain
resident until normal process exit without a verified physical-unload barrier.

## Shot result events: client.dll

Header: [shot_events_offsets.h](../include/shot_events_offsets.h).
Verifier: [inspect_shot_events.py](../tools/inspect_shot_events.py).
Details: [native event evidence](../tools/CS2_SHOT_EVENTS_EVIDENCE.md).
Added October 18, 2026 against the same installed client identity.

| Symbol | Current value / kind | Rediscovery anchor |
| --- | --- | --- |
| `Manager`, `ManagerVtable` | `0x254BC08` pointer slot, `0x1C31878` RVA | CGameEventManager RTTI; native client AddListener callers and client dispatch |
| `AddListener`, `FindListener` | `0x9C9D30`, `0x9E1BF0` RVAs; byte slots `0x18`, `0x20` | Registration returns event index or -1; client interest uses server=false; external listener is retained |
| `FireClient` | `0x9E2960`, byte slot `0x40` | Manager dispatch calls listener byte slot +8, then frees owned event through slot +0x50 |
| `EventVtable` | `0x1C315C0` RVA | CGameEvent RTTI; typed getters, 16-byte Murmur2 field key |
| `Name`, `Empty`, `Boolean`, `Integer`, `Float`, `String`, `Handle` | `0x9E7E50`, `0x9F1770`, `0x9E3E60`, `0x9E7120`, `0x9E6D40`, `0x9E9550`, `0x9E4620` RVAs | Getter slots +8/+0x28/+0x30/+0x38/+0x48/+0x50/+0x60; generated `userid_pawn`/`attacker_pawn` retain full serialized handles |

The verifier also checks shipped server event producers and VPK event schemas.
`weapon_fire`, `bullet_impact` and `player_hurt` carry no shot ID or server tick.
The adapter registers on the native command owner, copies fields synchronously,
and retains an inert listener after cooperative stop. Correlation windows and
capsule comparisons are diagnostics, not proof of a spread or prediction cause.

## Player chams: client ownership and scene packets

Header: [player_chams_offsets.h](../include/player_chams_offsets.h).
Verifier: [verify_player_chams.py](../tools/verify_player_chams.py).
Evidence: [player chams contracts](../tools/CS2_PLAYER_CHAMS_EVIDENCE.md).

The installed October 18 baseline uses the same scene/client hashes listed
above. `CAnimatableSceneObjectDesc` primary vtable `scenesystem+0x5E5C90`,
slot `+0x20`, resolves generation callback `+0x7D7C0`. Its four native arguments
are descriptor, scene object, generation args and draw list. New packet records
have stride `0x70`, source `+0x18`, material `+0x20`, view variant `+0x28` and
packed RGBA `+0x50`. These contracts belong to animated objects; the aggregate
world renderer has different stream/color consumers.

The client creation producer at `+0xAC5799..+0xAC5859` associates the scene
object's `+0x110` interface with skeleton `+0x130`, and writes the pawn's native
serialized owner handle at scene `+0xC0`. Handle construction subtracts
identity `m_flags & 1` from the serial before joining the index. Runtime checks
also require reciprocal controller/pawn ownership, matching skeleton interface,
life/dormancy and current local-team identity. A matching object class alone
does not establish a player.

Player material creation and model swaps share the verified client stage-8
owner through [client_frame.cpp](../src/client_frame.cpp); the native callback
runs first, nested stages forward without dispatch, and each subscriber keeps
its own stop barrier. The material feature does not depend on a model pack or
on enabling model swapping. Material/shader creation contracts are recorded
in the linked chams evidence and material header/verifier.

Through-wall chams select an immutable depth-disabled material for either
style. The installed shader feature `F_DISABLE_Z_BUFFERING=1` evaluates both
DepthEnable and DepthWriteEnable to zero; `F_DISABLE_Z_PREPASS=1` is also set.
Normal variants preserve native depth behavior. The portable verifier pins the
exact eight shader/texture assets, while optional
[inspect_player_chams_depth.py](../tools/inspect_player_chams_depth.py)
reproduces decoded metadata with the documented workspace research tools.
Scene CPU culling and draw-layer order remain native; this does not submit
players the engine omitted. See player evidence for the independently checked
same-layer aggregate/animatable descriptor ordering and its limits.
