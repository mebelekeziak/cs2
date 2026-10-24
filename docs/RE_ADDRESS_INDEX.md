# CS2 address and evidence index

Current Windows AMD64 **client** baseline, recertified October 23, 2026:
**1.41.9.2 / client-server 2000935 / Source revision 11162119 / Steam 26144732**.
Version date: October 22, 2026, 14:13:16. All **33/33** installed-file checks pass,
including jumpbug, indexed spread, native cosmetics, knife animations and the installed skin catalog.
These are exact-build constants. Runtime rendering/gameplay remains unobserved.
See the [update record](CS2_UPDATE_26144732.md) and [update guide](CS2_UPDATE_GUIDE.md).
The [previous index](RE_ADDRESS_INDEX_26087496.md) is explicitly historical.

Headers are the complete executable source of truth. Tables below select the
principal dependencies for navigation; use the linked header for full members,
arrays, flags and fingerprints. The frozen verifier independently decodes each
installed contract and rejects unfamiliar hashes. Detailed current evidence is
at the top of each linked evidence file; earlier sections remain historical.

## Address conventions and installation

RVA means module-relative virtual address, never a raw file offset or a process
absolute address. A member/slot is relative to its object/table. Keys, sizes and
masks are not addresses. Pointer slots contain pointers; embedded objects are
already at module base + RVA. All native callbacks use Microsoft x64 ABI.

Game root: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
Client/server/host reside in `csgo/bin/win64`; other modules in `bin/win64`.
Executable: `bin/win64/cs2.exe`. Inventory establishes installed AMD64 files;
it does not establish the graphics API/runtime loaded by a fresh process.

| Module | PE timestamp / image size | SHA-256 |
| --- | --- | --- |
| `bin/win64/cs2.exe` | `0x6acfcbec` / `0x432000` | `8614097aaa2761b0a7157c0045861713e769fb1e8235f50fc68f411294dd60cf` |
| `csgo/bin/win64/client.dll` | `0x6ada7340` / `0x29a0000` | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` |
| `csgo/bin/win64/server.dll` | `0x6ada7343` / `0x257f000` | `2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c` |
| `bin/win64/engine2.dll` | `0x6ada3418` / `0xa15000` | `e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29` |
| `bin/win64/scenesystem.dll` | `0x6ada337d` / `0x978000` | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` |
| `bin/win64/worldrenderer.dll` | `0x6ada331e` / `0x26b000` | `e0b1ed04a652f8268fd9d6b75b5a97910ab57197e2cdb8a0a4966703a5ec8322` |
| `bin/win64/materialsystem2.dll` | `0x6ada32d5` / `0x186000` | `2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69` |
| `bin/win64/rendersystemdx11.dll` | `0x6ada32d8` / `0x4b4000` | `fbd28df3458e1f5d0a21ff03c2c25e63a372063098dc4f455b07b80b3473eb8f` |
| `bin/win64/animationsystem.dll` | `0x6ada331e` / `0xbeb000` | `8aa57d0c5b90cb49ad5fd9f141004710180a601b08159e50e93c4470142ea132` |
| `bin/win64/tier0.dll` | `0x6ada3248` / `0x401000` | `f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d` |
| `csgo/bin/win64/host.dll` | `0x6ada3394` / `0x160000` | `f7db2685acbba12c27b3237aa0efa3f17a1698b0055adb0b950f0ee24519783c` |
| `bin/win64/resourcesystem.dll` | `0x6ada32b3` / `0x96000` | `29d503822278305b197c5d4c299b77961f6c5b9c0ab989dae81723c8546fa75f` |

## ESP and shared entities

Header: [cs2_offsets.h](../include/cs2_offsets.h); verifier: [inspect_cs2.py](../tools/inspect_cs2.py); [evidence](../tools/CS2_ENTITY_EVIDENCE.md).

client.dll; named static schema, entity-system load/store, embedded table initializer and full-handle lookup.

| Symbol | Current value |
| --- | --- |
| `dwEntityList` | `0x271C8B8` |
| `dwLocalPlayerController` | `0x253EEF8` |
| `dwLocalPlayerPawn` | `0x25676D8` |
| `dwViewMatrix` | `0x256CFE0` |
| `TableOffset` | `0x10` |
| `IdentityStride` | `0x70` |
| `m_ArmorValue` | `0x1ED0` |

## Movement and input

Header: [movement_offsets.h](../include/movement_offsets.h); verifier: [inspect_movement.py](../tools/inspect_movement.py); [evidence](../tools/CS2_MOVEMENT_EVIDENCE.md).

client.dll; CCSGOInput RTTI/constructor, native jump/ingest path, named cvars, decoded command and movement layouts.

| Symbol | Current value |
| --- | --- |
| `Input` | `0x257D780` |
| `InputVtable` | `0x1C9FA58` |
| `Finalize` | `0xCEE5F0` |
| `ModernJump` | `0x8C4320` |
| `EffectiveMaxSpeed` | `0x8CED20` |
| `Ingest` | `0xA781C0` |
| `AllocateSubtick` | `0x4F82A0` |
| `AddAllocatedSubtick` | `0x1235270` |

## Input history

Header: [silent_aim_offsets.h](../include/silent_aim_offsets.h); verifier: [inspect_silent_aim.py](../tools/inspect_silent_aim.py); [evidence](../tools/CS2_SILENT_AIM_EVIDENCE.md).

client.dll; CCSGOInput history slot and native protobuf producer/consumer, presence and storage ownership.

| Symbol | Current value |
| --- | --- |
| `HistoryBuilder` | `0xD03790` |
| `HistorySlot` | `0x19` |
| `HistorySize` | `0x30` |
| `Attack1Index` | `0x4C` |

## Jumpbug landing hull

Header: [movement_jumpbug_offsets.h](../include/movement_jumpbug_offsets.h);
verifier: [inspect_jumpbug.py](../tools/inspect_jumpbug.py);
[evidence](../tools/CS2_JUMPBUG_EVIDENCE.md).

client.dll; native movement filter and bbox callers, ground-result consumers,
actual-gravity schema/integration operand, named cvars and crouch-input decoding.

| Symbol | Current value |
| --- | --- |
| `InitializeFilter` | `0x8BE590` |
| `TraceHull` | `0x8E15D0` |
| `InitializeTrace` | `0x177B560` |
| `TraceCache` (services member) | `0x7B0` |
| `TraceNormal` / `TraceFraction` / `TraceSolid` | `0x90` / `0xAC` / `0xBB` |
| `ActualGravityScale` / `CollisionMask` (pawn members) | `0x564` / `0xD50` |
| `Gravity` / `StandableNormal` (cvar wrappers) | `0x23BF1C0` / `0x25344A0` |

## Anti aim and replay

Header: [anti_aim_offsets.h](../include/anti_aim_offsets.h); verifier: [inspect_anti_aim.py](../tools/inspect_anti_aim.py); [evidence](../tools/CS2_ANTI_AIM_EVIDENCE.md).

client.dll; input replay slot, command lookup, GameRules producer and native camera/base-angle ownership.

| Symbol | Current value |
| --- | --- |
| `Replay` | `0xCEB8E0` |
| `ReplaySlot` | `0x8` |
| `CommandLookup` | `0x9452D0` |
| `GameRules` | `0x2567188` |

## Aim entity eye source

Header: [silent_aim_target_offsets.h](../include/silent_aim_target_offsets.h); verifier: [inspect_silent_aim_targets.py](../tools/inspect_silent_aim_targets.py); [evidence](../tools/CS2_SILENT_AIM_TARGET_EVIDENCE.md).

client.dll; named base-model ViewOffset schema, shared full serialized entity ownership.

| Symbol | Current value |
| --- | --- |
| `ViewOffset` | `0xF60` |

## Named bone points

Header: [silent_aim_hitbox_offsets.h](../include/silent_aim_hitbox_offsets.h); verifier: [inspect_silent_aim_hitboxes.py](../tools/inspect_silent_aim_hitboxes.py); [evidence](../tools/CS2_SILENT_AIM_HITBOX_EVIDENCE.md).

client.dll; skeleton RTTI/query slot, named bone lookup and current model/count validation.

| Symbol | Current value |
| --- | --- |
| `LookupBone` | `0x953770` |
| `SkeletonVtable` | `0x1C43C10` |
| `PlayerSkeletonVtable` | `0x1ACFE58` |
| `SkeletonQuery` | `0x1C9E50` |
| `BoneCount` | `0x178` |
| `BoneStride` | `0x20` |

## Authored capsule geometry

Header: [aim_geometry_offsets.h](../include/aim_geometry_offsets.h); verifier: [inspect_aim_geometry.py](../tools/inspect_aim_geometry.py); [evidence](../tools/CS2_AIM_GEOMETRY_EVIDENCE.md).

client.dll and animationsystem.dll; named CHitBox resource schema and native model mesh/set/bone transforms.

| Symbol | Current value |
| --- | --- |
| `HitboxSet` | `0x3FC` |
| `MeshGroup` | `0x348` |
| `SetStride` | `0x48` |
| `BoxStride` | `0x70` |
| `Radius` | `0x30` |
| `Shape` | `0x3C` |

## Weapon accuracy, recoil and readiness

Header: [aim_weapon_offsets.h](../include/aim_weapon_offsets.h); verifier: [inspect_aim_weapon.py](../tools/inspect_aim_weapon.py); [evidence](../tools/CS2_AIM_WEAPON_EVIDENCE.md).

client.dll; named weapon/pawn schema, supported RTTI tables, spread/inaccuracy/punch callers and native distribution/fallback.

| Symbol | Current value |
| --- | --- |
| `Spread` | `0x80E2C0` |
| `Inaccuracy` | `0x80CFD0` |
| `PrimaryCooldown` | `0x94EC90` |
| `CurrentTime` | `0x75E050` |
| `AimPunch` | `0x8534E0` |
| `Defusing` | `0x1EA2` |
| `ShotsFired` | `0x1EB8` |
| `WaitForNoAttack` | `0x1EC4` |

## Current shooting origin

Header: [aim_shoot_origin_offsets.h](../include/aim_shoot_origin_offsets.h); verifier: [inspect_aim_shoot_origin.py](../tools/inspect_aim_shoot_origin.py); [evidence](../tools/CS2_AIM_SHOOT_ORIGIN_EVIDENCE.md).

client.dll; weapon-services/pawn RTTI slots, current getter and complete native view-offset interpolation chain.

| Symbol | Current value |
| --- | --- |
| `Getter` | `0xA80B30` |
| `GetterSlot` | `0x100` |
| `ServicesVtable` | `0x1C16C08` |
| `PawnGetter` | `0x93D490` |
| `PawnGetterSlot` | `0x568` |
| `ViewOffsetGetter` | `0x160750` |
| `BaseEye` | `0x15FA90` |

## Indexed primary spread compensation

Header: [aim_spread_offsets.h](../include/aim_spread_offsets.h); verifier:
[inspect_aim_spread.py](../tools/inspect_aim_spread.py); [evidence](../tools/CS2_AIM_SPREAD_EVIDENCE.md).

client.dll and tier0.dll; exact indexed shot time, shot-time punch, SHA-1 seed
and isolated null-mutex random stream. Candidate angles change the seed.
Held-fire histories without a native attack index are outside this contract.

| Symbol | Module | Current value |
| --- | --- | --- |
| `Seed` | client | `0xD24D70` |
| `TickConvert` | client | `0x75C350` |
| `CurrentTick` | client | `0xAAEA70` |
| `ShotPunch` | client | `0x853410` |
| `RandomPattern` | client | `0x25C0C78` |
| `HistoryTick` / `HistoryFraction` | history entry | `0x68` / `0x6C` |
| `HistoryTimePresent` | history entry | `0x1800` |
| `RandomCtor` | tier0 | `0x15E7A0` |
| `RandomFloat` | tier0 | `0x15E880` |
| `RandomGenerate` | tier0 | `0x15E1B0` |

## Native autofire request

Header: [aim_autofire_offsets.h](../include/aim_autofire_offsets.h); verifier: [inspect_autofire.py](../tools/inspect_autofire.py); [evidence](../tools/CS2_AUTOFIRE_EVIDENCE.md).

client.dll; native button/event producer, recorder and history consumers; preserve attack ownership and manual fire.

| Symbol | Current value |
| --- | --- |
| `RecordHistory` | `0xD06C20` |
| `InputEvents` | `0x288` |
| `InputEventStride` | `0x20` |
| `InputPrimaryIndex` | `0xBCC` |

## Direct-path occlusion

Header: [aim_occlusion_offsets.h](../include/aim_occlusion_offsets.h); verifier: [inspect_aim_occlusion.py](../tools/inspect_aim_occlusion.py); [evidence](../tools/CS2_AIM_OCCLUSION_EVIDENCE.md).

client.dll; complete native filter/trace bodies, shared third-person trace path and fresh target identity.

| Symbol | Current value |
| --- | --- |
| `InitializeFilter` | `0x352CC0` |
| `TraceEntity` | `0x8` |
| `TraceHitFlag` | `0xBB` |

## Direct bullet damage

Header: [aim_damage_offsets.h](../include/aim_damage_offsets.h); verifier: [inspect_aim_damage.py](../tools/inspect_aim_damage.py); [evidence](../tools/CS2_AIM_DAMAGE_EVIDENCE.md).

client.dll and animationsystem.dll; weapon damage schema, helmet/armor accessors, hitgroup and named damage cvars.

| Symbol | Current value |
| --- | --- |
| `ArmorValue` | `0x1ED0` |
| `ItemServicesVtable` | `0x1C12D60` |
| `DamageScaleCtBody` | `0x24BEAD8` |
| `DamageScaleCtHead` | `0x24BEAE8` |
| `UnknownHitboxPointer` | `0x2735D60` |

## Native penetration

Header: [aim_autowall_offsets.h](../include/aim_autowall_offsets.h); verifier: [inspect_autowall.py](../tools/inspect_autowall.py); [evidence](../tools/CS2_AUTOWALL_EVIDENCE.md).

client.dll; multi-hit trace, segment ownership, penetration step, weapon inputs and destruction.

| Symbol | Current value |
| --- | --- |
| `Initialize` | `0x87FC30` |
| `Destroy` | `0x8809A0` |
| `Trace` | `0x883F10` |
| `Step` | `0x8A2650` |
| `Extract` | `0x886660` |
| `DataSize` | `0x1D20` |
| `PenetrationCount` | `0x4` |

## Third person camera

Header: [third_person_offsets.h](../include/third_person_offsets.h); verifier: [inspect_third_person.py](../tools/inspect_third_person.py); [evidence](../tools/CS2_THIRD_PERSON_EVIDENCE.md).

client.dll; mode RTTI/view owner, original query callers, native trace/filter initialization and camera mask.

| Symbol | Current value |
| --- | --- |
| `OverrideView` | `0xB9A090` |
| `IsThirdPerson` | `0xB635E0` |
| `ModeSharedVtable` | `0x1C6A2A8` |
| `ModeNormalVtable` | `0x1CA1B50` |
| `TraceManager` | `0x2229020` |
| `TraceShape` | `0xA1AE30` |
| `InitializeTrace` | `0x177B560` |

## Scoped zoom and mouse sensitivity

Header: [zoom_offsets.h](../include/zoom_offsets.h); verifier: [inspect_zoom.py](../tools/inspect_zoom.py); [evidence](../tools/CS2_ZOOM_EVIDENCE.md).

client.dll; named Scoped schema, normal view wrapper and mouse dispatcher input/output argument flow.

| Symbol | Current value |
| --- | --- |
| `OverrideView` | `0xD04490` |
| `BaselineFov` | `0x940A70` |
| `Scoped` | `0x1EA0` |
| `MouseConvert` | `0xBA7D10` |
| `MouseConvertAlternate` | `0xBA7B40` |
| `MouseInput` | `0x257D780` |
| `MouseScale` | `0xB80FC0` |

## Scope overlay replacement

Header: [scope_overlay_offsets.h](../include/scope_overlay_offsets.h); verifier: [inspect_scope_overlay.py](../tools/inspect_scope_overlay.py); [evidence](../tools/CS2_SCOPE_OVERLAY_EVIDENCE.md).

client.dll; visual parameter producer, render versus FOV callers, complete scope/iron-sight graph consumers. Preserve health boost and unrelated callers.

| Symbol | Current value |
| --- | --- |
| `Producer` | `0x8E2410` |
| `RenderReturn` | `0x11DC541` |
| `OtherReturn` | `0xCF130F` |
| `Consumer` | `0x11EEAA0` |
| `ScopeGate` | `0x11EECC8` |
| `IronSightGate` | `0x830CA0` |
| `IronSightController` | `0x1BD0` |

## Model resources and frame owner

Header: [model_swap_offsets.h](../include/model_swap_offsets.h); verifier: [inspect_model_swap.py](../tools/inspect_model_swap.py); [evidence](../tools/CS2_MODEL_SWAP_EVIDENCE.md).

client.dll, engine2.dll, host.dll, resourcesystem.dll and tier0.dll; stage-8 barrier, named resource loading and engine allocator ownership.

| Symbol | Current value |
| --- | --- |
| `FrameStage` | `0xB6E540` |
| `FrameVtable` | `0x1C65030` |
| `FrameSlot` | `0x24` |
| `SetModel` | `0x9651F0` |
| `InitializeResourceName` | `0xC4380` |
| `ResourceSystemPointer` | `0x1445E0` |
| `BlockingLoad` | `0x16B20` |
| `PurgeResourceName` | `0x53D60` |

## Grenade area snapshots

Header: [grenade_area_offsets.h](../include/grenade_area_offsets.h); verifier: [inspect_grenade_areas.py](../tools/inspect_grenade_areas.py); [evidence](../tools/CS2_GRENADE_AREAS_EVIDENCE.md).

client.dll; named inferno/smoke RTTI/schema, simulation buffer pairing, native occupancy/density read-only sampling.

| Symbol | Current value |
| --- | --- |
| `InfernoVtable` | `0x1CAE650` |
| `SmokeVtable` | `0x1BD4120` |
| `SmokeSimulation` | `0x13B8` |
| `SmokeBuffer` | `0x100` |
| `SmokeOccupancy` | `0x1008` |
| `SmokeDensity` | `0x3008` |

## Smoke and flash rendering

Header: [grenade_effect_offsets.h](../include/grenade_effect_offsets.h); verifier: [inspect_grenade_effects.py](../tools/inspect_grenade_effects.py); [evidence](../tools/CS2_GRENADE_EFFECTS_EVIDENCE.md).

client.dll; smoke descriptor callback and flash parameter producers/consumers. Suppression remains transient on native owner.

| Symbol | Current value |
| --- | --- |
| `SmokeDescriptor` | `0x1CAA228` |
| `SmokeDraw` | `0xD21A60` |
| `SmokeOpacity` | `0xCA1FE0` |
| `FlashSetFloat4` | `0x16AE00` |
| `FlashOverlayReturn` | `0x11DBE4B` |
| `FlashScreenshotReturn` | `0x11DBE7B` |

## Player chams packets

Header: [player_chams_offsets.h](../include/player_chams_offsets.h); verifier: [verify_player_chams.py](../tools/verify_player_chams.py); [evidence](../tools/CS2_PLAYER_CHAMS_EVIDENCE.md).

client.dll and scenesystem.dll; full serialized pawn ownership, skeleton interface and animatable native generation/packet consumers.

The [PVS visibility evidence](../tools/CS2_PLAYER_CHAMS_VISIBILITY_EVIDENCE.md)
also freezes the scene worker's exact caller, boolean predicate and RSI object
provenance. The leaf register adapter admits only validated selected players
with Through walls enabled; it changes no persistent scene flags or PVS data.

| Symbol | Current value |
| --- | --- |
| `Generate` | `0x7D7C0` |
| `DescriptorVtable` | `0x5E5C90` |
| `PvsTest` / `PvsReturn` | `0x36840` / `0xF0941` |
| `PvsSourceLoad` / `PvsCaller` | `0xF07A9` / `0xF0922` |
| `ObjectDescriptor` / `ObjectPvsData` | `0x18` / `0xA0` |
| `OwnerHandle` | `0xC0` |
| `SkeletonInterface` | `0x110` |
| `PacketSize` | `0x70` |
| `PacketMaterial` | `0x20` |
| `PacketColor` | `0x50` |

## Player chams materials

Header: [player_chams_material_offsets.h](../include/player_chams_material_offsets.h); verifier: [verify_player_chams_materials.py](../tools/verify_player_chams_materials.py); [evidence](../tools/CS2_PLAYER_CHAMS_EVIDENCE.md).

client.dll, materialsystem2.dll and tier0.dll; stage owner shared with model feature, KV3/material creation, eight exact shader/texture assets.

| Symbol | Current value |
| --- | --- |
| `ArenaConstructor` | `0x19B6AC0` |
| `ArenaGetRoot` | `0x19C4B40` |
| `ArenaDestructor` | `0x19B7110` |
| `CreateMaterial` | `0x3ADE0` |
| `MaterialVtable` | `0x129AE0` |
| `MaterialMode` | `0xBA30` |

## Sky

Header: [world_offsets.h](../include/world_offsets.h); verifier: [verify_sky_draw.py](../tools/verify_sky_draw.py); [evidence](../tools/CS2_SKY_DRAW_EVIDENCE.md).

scenesystem.dll plus client.dll; CSkyBoxObjectDesc RTTI, float4 copier/callsite/key and client lighting-kind producer. Preserve all seven render arguments.

| Symbol | Current value |
| --- | --- |
| `SkyDescriptorVtable` | `0x5F6508` |
| `SkyRender` | `0x154FB0` |
| `SetFloat4` | `0x4DA70` |
| `SkyTintReturn` | `0x1551A9` |
| `LightingProducer` | `0x28267E` |

## Flat world material

Header: [world_material_offsets.h](../include/world_material_offsets.h); verifier: [verify_world_materials.py](../tools/verify_world_materials.py); [evidence](../tools/CS2_MATERIAL_EVIDENCE.md).

scenesystem.dll, worldrenderer.dll and materialsystem2.dll; aggregate descriptor slot, native reflectivity material and per-callback streams. Packet Color is not aggregate surface tint.

| Symbol | Current value |
| --- | --- |
| `AggregateVtable` | `0x5E4518` |
| `AggregateCallback` | `0x41F30` |
| `FlatBinding` | `0x9217D8` |
| `Type1Stream` | `0x921390` |
| `Type2Stream` | `0x9213A8` |

## Local light radiance

Header: [world_lighting_offsets.h](../include/world_lighting_offsets.h); verifier: [verify_world_lighting.py](../tools/verify_world_lighting.py); [evidence](../tools/CS2_LIGHTING_EVIDENCE.md).

scenesystem.dll; CLightBinnerGPU pack dispatch/upload and real 0x1E0 GPU records, radiance +0xA0. Old cubemap-volume hook remains removed.

| Symbol | Current value |
| --- | --- |
| `PackLocalLight` | `0x1B14B0` |
| `PackThunk` | `0x1B14A0` |
| `PackReturn` | `0x1B2537` |
| `BinnerVtable` | `0x5FE678` |
| `PackSlot` | `0x70` |

## Light suppression

Header: [world_lighting_switch_offsets.h](../include/world_lighting_switch_offsets.h); verifier: [verify_lighting_switches.py](../tools/verify_lighting_switches.py); [evidence](../tools/CS2_ENV_LIGHT_EVIDENCE.md).

scenesystem.dll; exact lb_enable_lights registration, clean native disabled gather paths and per-view frame clear.

| Symbol | Current value |
| --- | --- |
| `GatherPointSpot` | `0x19F300` |
| `GatherArea` | `0x19FE30` |
| `PointGate` | `0x19F30D` |
| `AreaGate` | `0x19FE49` |
| `FrameClear` | `0x1A898B` |

## Sun, ambient and directional lightmaps

Header: [world_environment_lighting_offsets.h](../include/world_environment_lighting_offsets.h); verifier: [verify_environment_lighting.py](../tools/verify_environment_lighting.py); [evidence](../tools/CS2_BAKED_LIGHT_EVIDENCE.md).

scenesystem.dll; per-view source builder/copies/uploads, complete buffer consumers and named directionality producer. Modify fresh source, preserve W/return/ownership.

| Symbol | Current value |
| --- | --- |
| `CopyBuffer` | `0x569120` |
| `CopyReturn` | `0x1B0CD1` |
| `BufferProducer` | `0x1B0CB5` |
| `BufferUpload` | `0x1B0F77` |
| `SunRgb` | `0x79A0` |
| `AmbientRgb` | `0x120` |
| `DirectionalLightmap` | `0x40` |

## Console controls

Header: [console_control_offsets.h](../include/console_control_offsets.h); verifier: [inspect_console_control.py](../tools/inspect_console_control.py); [evidence](../tools/CS2_CONSOLE_CONTROL_EVIDENCE.md).

engine2.dll; named listener RTTI, clear registration and recursive-locked command queue/import.

| Symbol | Current value |
| --- | --- |
| `Listener` | `0x20AC20` |
| `Clear` | `0x1B8270` |
| `Queue` | `0x73BB0` |
| `Buffer` | `0x979820` |
| `ArgCount` | `0x438` |
| `ArgValues` | `0x440` |

## Shot-result events

Header: [shot_events_offsets.h](../include/shot_events_offsets.h); verifier: [inspect_shot_events.py](../tools/inspect_shot_events.py); [evidence](../tools/CS2_SHOT_EVENTS_EVIDENCE.md).

client.dll plus static server.dll and shipped VPK event schemas; manager/event RTTI slots, external listener ownership, generated full pawn handles. No native shot ID/tick.

| Symbol | Current value |
| --- | --- |
| `Manager` | `0x254DC00` |
| `ManagerVtable` | `0x1C337A8` |
| `EventVtable` | `0x1C334F0` |
| `AddListener` | `0x9CAB60` |
| `FindListener` | `0x9E2A20` |
| `FireClient` | `0x9E3790` |
| `Name` | `0x9E8C80` |
| `Empty` | `0x9F25A0` |
| `Boolean` | `0x9E4C90` |
| `Integer` | `0x9E7F50` |
| `Float` | `0x9E7B70` |
| `String` | `0x9EA380` |
| `Handle` | `0x9E5450` |

## Cosmetic items and skins

Header: [skin_offsets.h](../include/skin_offsets.h); verifier:
[inspect_skins.py](../tools/inspect_skins.py); [native evidence](../tools/CS2_SKINS_EVIDENCE.md).
Metadata: [skin_catalog.h](../include/skin_catalog.h), generated by
[generate_skin_catalog.py](../tools/generate_skin_catalog.py) from installed
VPK item/localization resources. The compatibility bundle verifies both contracts
independently. These checks do not establish observed appearance.

| Symbol | Current value |
| --- | --- |
| Embedded entity item | `0x12E0` |
| Item definition index | item + `0x1BA` |
| Fallback paint / seed / wear / StatTrak | entity + `0x18A8 / 0x18AC / 0x18B0 / 0x18B4` |
| Pawn glove item / reapply | pawn + `0x1770 / 0x176D` |
| Native attribute set / remove | client + `0x11A3080 / 0x11A12C0` |
| Composite reset / weapon regenerate | client + `0x14E9D00 / 0x803780` |
| Native regeneration command | client + `0x82AA40` |

Cosmetic updates share the verified stage-8 client-frame owner, use engine-owned
attribute allocation and material regeneration, and retain callbacks for
cooperative restoration after stop. The current client does not expose the old
separate `C_BaseViewModel` schema recipe; no historical viewmodel offsets are
introduced. See [implementation and observation limits](SKIN_CUSTOMIZATION.md).

## Knife animation subclasses

Header: [knife_animation_offsets.h](../include/knife_animation_offsets.h);
verifier: [inspect_knife_animations.py](../tools/inspect_knife_animations.py);
[evidence](../tools/CS2_KNIFE_ANIMATIONS_EVIDENCE.md).

| Symbol | Current value |
| --- | --- |
| Entity subclass ID / native VData | entity + `0x380 / 0x388` |
| Native item-to-subclass getter | client + `0x7CC8B0` |
| Named subclass-change callback | client + `0xB07310` |
| Weapon subclass-change lifecycle | client + `0x820E40` |
| VData animation class / skeleton | VData + `0x868 / 0x528` |
| HUD model handle | weapon + `0x1990` |

The native item getter resolves the exact installed subclass. The changer
validates its scope, class, ID and canonical name before publishing the ID.
The named callback refreshes VData and the native weapon/HUD lifecycle; it does
not require guessed hashes or sequence tables. Model comparison inside that
lifecycle requires installing the selected model before calling the callback.

## Console output and final-image overlay

[aim_console.cpp](../src/aim_console.cpp) retains named tier0 exports Msg `0x7EE60` and ConColorMsg `0x7F710`, exact varargs prologues and current tier0 identity. [inspect_aim_console.py](../tools/inspect_aim_console.py) verifies the export, format, argument and native client callers; [evidence](../tools/CS2_AIM_CONSOLE_EVIDENCE.md).

[overlay.cpp](../src/overlay.cpp) discovers Present/ResizeBuffers through a temporary D3D11 swap chain and SDK COM slots 8/13, then validates real device/swap-chain callbacks. No frozen DXGI function RVA is introduced. [world_postprocess.cpp](../src/world_postprocess.cpp) modifies the final image and also affects HUD. Fresh loaded graphics/runtime and visual behavior remain unobserved.

Keep initialization/teardown outside DllMain and native mutations on verified engine callbacks. Invalidate stale state and preserve cooperative owner-thread restoration/barriers. Stopped code and trampolines stay resident until normal process exit; stopped and resident is not physically unloaded. No loader session/environment gates were added.
