# Named animated aim landmarks

Installed Windows AMD64 client **1.41.9.1**, client/server **2000932**, Steam
build **25947352**, inspected October 15, 2026. Client path:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
PE timestamp `0x6AD01314`, SizeOfImage `0x299D000`. All investigation here reads
installed files only. No live pose, aiming or hit observation is claimed.

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
requiring the verified skeleton vtable and identity query before invoking a
pawn helper establishes that its scene node is the supported skeleton class.
Unknown derived-class vtables are skipped instead of assumed compatible.

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
Require the verified skeleton primary vtable, its `+0x68` method pointer and
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
