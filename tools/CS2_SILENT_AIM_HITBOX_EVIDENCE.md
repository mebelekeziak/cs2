# Named animated aim landmarks

## October 23, 2026 recertification - client 2000935

Installed Windows AMD64 client `1.41.9.2` / `2000935`, Source revision
`11162119`, Steam build `26144732`, version date October 22, 2026 14:13:16.
Installation remains `C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game`;
executable `bin/win64/cs2.exe`, module `csgo/bin/win64/client.dll`.
Client SHA-256 `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`.

Research is preserved in `logs/research/26144732/aim_sources/`: `schema.json`
records independently located named schema templates, `relocations.json` and
`address_mapping.json` record candidate/call-target rediscovery, and
`disassembly.txt` contains decoded current instruction windows (Capstone 5.0.7).
Preserved prior-file comparison uses only windows matching the former frozen
window hashes; the old negative-test sample is not accepted as a valid complete
baseline image. New named schema/RTTI/caller checks and full module pins remain
mandatory. No uniform relocation delta is assumed.

Named bone lookup moves `0x952940 -> 0x953770`; its complete 38-byte entry
continues to resolve the pawn scene's skeleton through byte slot `0x68`, then
tail-call the native string/model lookup `0xABC270`. The skeleton RTTI table is
`0x1C43C10`, player networked skeleton table `0x1ACFE58`, identity query
`0x1C9E50`. Independent native constructor at `0x930AB5` and RTTI hierarchy
retain the zero-offset skeleton base and embedded skeleton `+0x80`.

Named schema keeps model state `+0x140`, model handle `+0xA0`; native model
consumer reads combined `+0x1E0`. Bone-array member `+0x1C0`, resource count
`+0x178`, cached transform stride `0x20` and position `+0` remain unchanged.
World bone transform moves `0xAB11B0 -> 0xAB1FF0`; native pelvis lookup at
`0xC7D817` calls the newly verified lookup and consumes the returned float3.
All client named strings are rediscovered at their new addresses; leg-upper
names remain established through the exact unchanged authored SAS model hash.
The verifier checks ten machine windows, three schema templates, constructor,
RTTI hierarchy, vtable slots, named strings and compiled header fingerprint.

`final_checks.py` records five passing frozen verifier commands in `frozen.json`
and their full outputs in per-verifier JSON files. Twenty deliberately wrong
headers are rejected in `negative.json`, covering source functions, vtables,
new pawn members, resource layout, weapon table records and entry fingerprints.
All five verifiers also reject the preserved old negative-test client sample;
see `hash_rejections.json`. All module/asset hash guards remain exact. This is installed-file verification:
no client/server was launched, no native getter was executed, and no live aim,
accuracy, bone pose, capsule or shot-registration observation is claimed. Existing
command-thread ownership, cache invalidation and cooperative residency rules
remain in force. Earlier build-specific addresses below are historical.

The `silent_aim_checks` fixture now selects the verified current player
skeleton table instead of its historical captured RVA. The behavioral chest/head
acquisition and foreign-table rejection cases remain. Target rebuilds and
`ctest -R "^silent_aim_checks$"` pass in both Release build variants.

## Historical evidence

## October 21, 2026 recertification — client 2000934

Installed AMD64 client `1.41.9.1` / `2000934`, Source revision `11150591`,
Steam build `26087496`; SHA-256
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`, at the same installed path
below. The separate pre-pin investigation
`logs/research/26087496/aim_targets/recheck.py` passes all ten native instruction
windows, three named schema registrations, RTTI/vtable/zero-offset inheritance,
constructor ownership and decoded model/bone layout checks without alteration.
All addresses, slot values, members, bone names and entry fingerprints remain
unchanged. The SAS authored model remains SHA-256
`7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22`.

Final frozen command `python tools/inspect_silent_aim_hitboxes.py` passes;
full output is `logs/research/26087496/aim_targets/inspect_silent_aim_hitboxes.json`.

The matching temporary-header negative checks reject wrong `LookupBone`,
`PlayerSkeletonVtable`, `BoneCount` and `LookupBoneBytes`; see `negative.json`
in that research directory. The frozen verifier retains its strict client pin
through `inspect_cs2.py`. No native lifecycle or invocation behavior changed.
No client/server was launched and no live landmarks or aim were observed for
this build. Earlier build identities and live observations below are historical.

## Historical implementation and observations

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
