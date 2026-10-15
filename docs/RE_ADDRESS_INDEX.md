# CS2 address and evidence index

This is a navigation map for the Windows AMD64 **client**, recertified October
15, 2026: patch **1.41.9.1**, client/server **2000932**, Source revision
**11131657**, Steam build **25947352**, version date October 14, 2026, 16:57:53.
These are the installed build's verified constants, not portable signatures.
The October 15 evidence is static: it does not establish fresh ESP, movement or
world appearance observations. Historical sections in the linked evidence
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
| `client.dll` | `0x6AD01314` / `0x299D000` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` |
| `scenesystem.dll` | `0x6ACFCD4F` / `0x978000` | `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5` |
| `worldrenderer.dll` | `0x6ACFCCF2` / `0x26B000` | `f9eeff50acb10a89ab78c98d2726d3e5e6b5adf7a8b11be2a41cb5ebded11dac` |
| `materialsystem2.dll` | `0x6ACFCC60` / `0x186000` | `b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6` |

The verifiers pin complete hashes and independently checked instruction/schema
evidence. A hash failure requests renewed investigation; replacing a hash or
applying one relocation delta to all symbols does not establish compatibility.
Source-header `--header` options select a header to check, never an output to
generate. Scope identities per subsystem: sky's client identity alone does not
certify ESP or movement.

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
animated landmarks, not authored hitbox-volume centers. No visibility trace,
recoil/spread correction, penetration or damage system is provided.

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
