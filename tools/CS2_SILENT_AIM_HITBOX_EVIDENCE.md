# Named animated aim landmarks

**October 20 extension:** multi-selection now includes torso and bilateral limb
capsules. Stomach uses the verified `spine_0` stomach region, replacing the
historical `spine_2` landmark below. See
[multi-hitgroup evidence](CS2_AIM_MULTIHITGROUP_EVIDENCE.md) for the current table,
asset provenance and verification. The original single-selector investigation
is preserved below as historical evidence.

Installed Windows AMD64 client **1.41.9.1**, client/server **2000932**, Steam
build **25947352**, inspected October 15, 2026. Client path:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
PE timestamp `0x6AD01314`, SizeOfImage `0x299D000`. Static verification reads
installed files only. A subsequent read-only live observation is described
below, followed by the user's corrected offline aiming observation.

## Selection labels and scope

The selector uses named animated bone landmarks resolved against each pawn's
current model. It does not assume fixed numerical bone indices.

| Selection | Native bone name | Geometric meaning |
| --- | --- | --- |
| Head | `head_0` | Head-bone world origin |
| Chest | `spine_3` | Upper-spine bone world origin |
| Stomach | `spine_2` | Lower-spine bone world origin |
| Pelvis | `pelvis` | Pelvis-bone world origin |

The chest/stomach labels identify upper/lower torso landmarks. These points
are **not authored hitbox-volume centers** and are not guaranteed to lie at
the center of a model's damage hitbox. The native strings reside at RVAs
`0x1BA2648`, `0x1BA2638`, `0x1BA2630`, `0x1BA2614`, respectively. Native
bone-name initializers and gameplay consumers reference these same names.
Missing names or a missing pose cache must skip that candidate; do not silently
substitute the former collision hull point.

## Skeleton and model ownership

Exact named static schema registrations:

| Class | Registration | Field array | Count | Size | Required field |
| --- | --- | --- | --- | --- | --- |
| `CSkeletonInstance` | `0x21FF428` | `0x21FF300` | 7 | `0x490` | `m_modelState = 0x140` |
| `CModelState` | `0x21FE008` | `0x21FDE40` | 14 | `0x2B0` | `m_hModel = 0xA0` |

MSVC RTTI `.?AVCSkeletonInstance@@` independently identifies primary vtable
`0x1C41CE0`. Its byte slot `+0x68` points at `0x1C9E70`, whose exact bytes
`48 8B C1 C3` implement `return this`. The base `CGameSceneNode` vtable at
`0x1C2E408` instead uses `0x1C9E50`, which returns null at that slot. Therefore
requiring a verified skeleton vtable and identity query before invoking a
pawn helper establishes that its scene node has the supported layout.

The first implementation accepted only that standalone primary vtable. A
read-only observation in the user's practice client PID `37968` subsequently
found that both local and enemy pawn scene nodes use **`0x1ACDE48`**. Both
query slots still pointed at `0x1C9E70`, with non-null model/pose pointers and
valid model bone counts. This proves that the old exact-vtable predicate
rejected those live player skeletons before bone lookup, for every selector.

The live table's RTTI identifies
`.?AVNetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance@@`, with
complete-object locator `0x1EBC1E0`, type descriptor `0x22C4E20`, and hierarchy
`0x1EBC208`. Base descriptor `0x1EBC270` names `CSkeletonInstance` and has
PMD `(0, -1, 0)`, establishing the skeleton base at offset zero. The following
base is `CGameSceneNode`, also at offset zero. The verifier independently
resolves this primary vtable and requires its query slot to match the verified
skeleton identity query.

Named schema registration `CBodyComponentSkeletonInstance` at `0x21C0918`
has size `0x510`, one field in array `0x21C08D8`, and
`m_skeletonInstance = 0x80`. Constructor `0x92FC30` allocates `0x510` bytes,
passes `body+0x80` to the base skeleton constructor `0xA99FC0` at `0x92FC8F`,
then loads `0x1ACDE48` at `0x92FC99` and stores it at `body+0x80` at
`0x92FCA0`. Thus the observed wrapper preserves the verified skeleton offsets
and identity query. Both explicit tables are supported; unverified derived
vtables remain rejected.

The model-state strong-handle pointer is at skeleton `+0x140+0xA0 = +0x1E0`;
dereferencing that pointer gives the current model pointer. This agrees with
the native bone-name lookup and bone-count helpers, independently of schema.

## Native name lookup ABI

Function RVA **`0x952940`** has the Microsoft x64 contract:

```cpp
int __fastcall LookupBone(void* pawn, const char* name);
```

`RCX` is the pawn and `RDX` a null-terminated bone name. It reads scene node
`pawn+0x330`, invokes its `+0x68` skeleton query, then tail-calls `0xABB430`.
That helper hashes the name with seed `0x31415926`, reads/dereferences current
model handle `skeleton+0x1E0`, and searches the model bone-name table via
`0x1770CF0`. A missing model/name returns `-1`. It does not refresh animation.

Native caller `0xC7C507` passes the string `pelvis`, tests EAX for a negative
index and passes a valid result as R8D to `0x93F490`. The first three floats of
the returned bone transform become the caller's world position.

Use this verified helper only on the existing engine command thread after
validating the supported pawn and skeleton. Do not cache the resulting index
across changes of pawn, scene, strong handle or model.

## Cached world pose layout

Native helper `0xAAF920` reads/dereferences model handle `+0x1E0`; it tail-calls
`0x1775860`, whose `MOV EAX,[RCX+0x178]` returns model bone count. A missing
handle/model yields zero.

Native world-transform helper `0xAB11B0` uses the following data:

- Skeleton world-pose array pointer **`+0x1C0`**.
- Bone transform stride **32 bytes**, demonstrated by `SHL RAX,5` at `0xAB1223`.
- First 16 bytes: world position XYZ and scale; second 16 bytes: orientation.
- Native position consumer `0xC7C527` copies the first 12 bytes as a vector.

The native helper first requests animation work through `0xAA0F80`; when no
pose array is available, it returns the scene world transform. This minimal
implementation does **not** call that refresh path or adopt its origin fallback.
It copies the already available cached animated world transform and skips
unavailable poses. Consequently positions can lag a pending animation refresh.

## Read validation contract

Acquire current scene/model/pose values in the command reader's own storage.
Require either verified skeleton vtable, its `+0x68` method pointer and
query fingerprint, nonzero model-handle and model pointers, a positive bounded
bone count, a dynamically resolved index in that count and a nonzero pose array.
Copy world XYZ from the transform's first 12 bytes with fault-tolerant reads;
require finite world coordinates.
After acquisition, re-read pawn scene pointer, skeleton vtable, handle pointer,
model pointer and pose pointer for equality, alongside existing full serialized
entity identity checks. Skip changes or failures instead of reusing old state.

This route adds neither visibility nor penetration tests, probability/damage
logic, spread compensation or skeleton rebuilding.

## Reproduction

```powershell
python tools/inspect_cs2.py
python tools/inspect_silent_aim_targets.py
python tools/inspect_silent_aim_hitboxes.py
python tools/inspect_silent_aim_hitboxes.py --header include/silent_aim_hitbox_offsets.h
```

The verifier requires the exact full client hash, named schema registrations,
skeleton RTTI/vtable association, names, frozen native instructions, decoded
members/stride and source constants/fingerprints. `--client` and `--header`
select inputs to verify; nothing is rewritten or silently adapted.

The real installed client and source header passed on October 15, 2026.
Twenty-five independent in-memory mutations were rejected: ten numeric header
constants, four bone-name literals, nine native byte anchors and both schema
registration sizes. No mutated input was written to installed game files.

After adding the observed player wrapper, the installed verifier passed again.
Negative checks additionally reject changes to its header RVA, query pointer,
RTTI base adjustment, constructor call/store, and body-component schema offset.
The retained base table and newly supported player table are both checked.

## Corrected offline observation

Fix `fa7845c` accepts the verified player wrapper as well as the standalone
skeleton. Synthetic regressions using the observed wrapper fail for Head and
Chest before that change and pass afterward; an unrelated class with the same
query method remains rejected.

Following normal process restart/reload, the user's bot-practice test produced
`logs/overlay-36880-134365469299477046.log`. It records Chest bone index 5 on the
current model, successful target acquisition and 106 applied history edits by
16:10:25 Warsaw time, with zero bone/write rejections at that snapshot. The user
then confirmed that shots redirected toward the bot. This supports the corrected
basic offline aiming path. It does not establish authored hitbox-center accuracy,
every weapon, guaranteed damage, or a separate observed Head-selection result.
