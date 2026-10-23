# Landed smoke and inferno area geometry

## Static recertification, October 21, 2026

Current installed Windows AMD64 client: patch `1.41.9.1`, client/server
`2000934`, Source revision `11150591`, Steam build `26087496`; version date
October 20, 2026, 11:41:22. The client path below is unchanged. SHA-256 is now
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
PE timestamp changed `0x6AD01314` -> `0x6AD7B41C`; SizeOfImage remains
`0x299D000`. `include/grenade_area_offsets.h::ClientTimestamp` and the frozen
verifier hash were updated after checking the new image.

All 17 existing instruction windows match byte-for-byte, including smoke
initialization, current occupancy/other-buffer density selection, Morton/world
coordinate conversion, inferno live positions/burning selection and bounds.
The exact named schema records, object sizes, field counts and RTTI-derived
vtables also match. Every consumed RVA, member, width, stride and constant is
unchanged; no relocation or new layout was assumed. The copied-data capture
ownership and lifecycle described below remain applicable.

Research ran the full frozen verifier body with the new independently read
identity and a workspace-only timestamp-adjusted header before editing source
pins. Results are in
`logs/research/26087496/render_grenade/initial-contract-checks.json`.
`python tools/inspect_grenade_areas.py` then passed normally. Four incorrect
header copies (timestamp, smoke vtable, buffer selector, fire-position member)
were rejected, as was an otherwise readable client copy with one byte changed.
See `verification-and-negative.json` in the same research directory. Build and
CTest results belong to the integrating update report. No client/server was
launched or opened; live aura appearance remains unobserved for this build.

## Historical October 20 baseline

Verified October 20, 2026 against the installed AMD64 client:

`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`

SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
PE timestamp `0x6AD01314`, image size `0x299D000`.
This investigation reads installed files only. It does not launch a process or
establish live visual behavior. Existing project entity-system contracts apply;
see `CS2_ENTITY_EVIDENCE.md` and `include/cs2_offsets.h`.

Run `python tools/inspect_grenade_areas.py` to check the exact file identity,
schema records, RTTI-derived primary vtables, native instruction windows and
`include/grenade_area_offsets.h`. An alternate `--header` supports negative
contract testing. No external Python packages are required.

## Class and source identity

Primary vtables derived independently from each exact MSVC RTTI class name:

| Class | Primary vtable RVA | Schema registration / field array | Count / object size |
|---|---|---|---|
| `C_Inferno` | `0x1CAC6F0` | `0x223FE48` / `0x223FB30` | 24 / `0x86A0` |
| `C_SmokeGrenadeProjectile` | `0x1BD2190` | `0x2218958` / `0x22185E0` | 11 / `0x1500` |

The native smoke constructor writes the smoke primary vtable at `0x7C3E79..87`.
Schema records use field-name pointer `+0`, field byte offset `+0x10`, and
`0x20` stride. Require full serialized identity, entity identity back-reference,
current entity-system ownership, and the matching primary vtable before reading
feature-specific data. Designer labels alone are insufficient class evidence.
Never keep entity pointers in the render publication.

## Inferno

| Field | Offset | Schema type |
|---|---|---|
| `m_firePositions` | `0x1108` | `VectorWS[64]` |
| `m_bFireIsBurning` | `0x1708` | `bool[64]` |
| `m_fireCount` | `0x1A48` | `int32` |
| `m_bInPostEffectTime` | `0x1A54` | `bool` |
| `m_maxFireHalfWidth` | `0x866C` | `float32` |

Native visual patch acquisition at `0xD51951..0xD51A88` bounds the iteration
by `m_fireCount`, reads positions with a 12-byte stride, and reads burning flags
with a one-byte stride. The native visual patch capacity check is 64 at
`0xD51A98`. Positions are world coordinates; do not add entity origin.
`0xD43953` branches on post-effect state before cleaning up native particles.
Skip post-effect instances and unlit cells. An inferno with no active fire
positions contributes no outline; an airborne molotov has no active inferno
footprint.

The constructor sets horizontal half-width to 30 at `0xD410F8`. Native visual
bounds at `0xD4F55F..0xD4F5C8` expand each drawable patch by that half-width in
X/Y. This is a visual extent, **not proof of a gameplay damage radius**. The
union of disks around live fire positions, using the current native visual
half-width, is a footprint approximation. It preserves spread, gaps and
disconnected patches better than one circle or a convex hull, but does not
reproduce each animated flame's exact silhouette. Omit internal disk arcs
covered by neighboring patches; retain only the outer union border. Never
invent a fixed overall inferno radius.

## Smoke simulation and native occupancy

| Source | Offset | Meaning |
|---|---|---|
| entity | `0x1360` | `m_nSmokeEffectTickBegin`, int32 |
| entity | `0x1364` | `m_bDidSmokeEffect`, bool |
| entity | `0x1378` | `m_vSmokeDetonationPos`, world Vector |
| entity | `0x13AA` | `m_bSmokeEffectSpawned`, bool |
| entity | `0x13B8` | embedded native simulation |
| simulation | `0x70` | native voxel-owner pointer |
| simulation | `0x100` | current ping-pong buffer index, 0 or 1 |
| simulation | `0xE8` | world detonation origin |
| voxel owner | `0x1008 + current * 0x1000` | 4096-byte occupancy bitset |
| voxel owner | `0x3008 + (1-current) * 0x80000` | native render density records |

Native start `0x7D8060..0x7D80D1` requires nonzero tick-begin **or** did-effect,
does not recreate an already spawned effect, copies detonation origin into
entity `+0x14A0` (simulation `+0xE8`), and passes simulation `entity+0x13B8` to
initialization. Requiring this start state plus `m_bSmokeEffectSpawned` and
nonempty native density excludes an in-flight projectile. No guessed timer is
needed. At the next acquisition, empty native density, native entity removal
or identity replacement removes the corresponding outline. Acquisition caching
and publication expiry are described below; removal is not instantaneous.

`0x7C5F30..0x7C5F75` walks exactly 32768 occupancy bits in 512 qwords and selects
the current bitset by `simulation+0x100`. Native render publication repeats
this selection at `0x7E0200..0x7E0242`; it reads density from the **other** buffer
at `0x7E02DC..0x7E030B`. The record stride is 16 bytes; density is the first
float. Do not read density from the occupancy buffer's same-numbered density
page. Native normalization at `0x7D90A0` clamps density/50 to [0,1]. Finite
positive density, restricted to occupied cells, supports the area boundary.

The grid has 32 cells per axis, with Morton ordering:

```
x bits = morton bits 0,3,6,9,12
y bits = morton bits 1,4,7,10,13
z bits = morton bits 2,5,8,11,14
```

The actual native Morton decoder is frozen at `0x7E0242..0x7E02DC`.
The coordinate converter `0x7E0EFA..0x7E0FA0` operates on the decoded byte XYZ.
Its cell-size constant at `0x1ADA2EC` is 20. The guarded constant initializers
at `0x7E0FCC..0x7E0FE0` and `0x7E1014..0x7E1028` establish grid center 16 and
cell center 10 respectively. Therefore:

```
world cell center = detonation origin + (decoded XYZ - 16) * 20 + 10
world cell lower corner = detonation origin + (decoded XYZ - 16) * 20
```

The implementation builds an XY occupancy map from live cells at every Z and
projects exposed column edges onto the smoke's detonation plane (the copied
origin's Z). It preserves concavities and holes in that projected footprint;
shared occupied-column edges are internal and are omitted. It does not follow
the terrain or retain separate vertical smoke layers. This is a projection of
the native voxel footprint, not an exact shader opacity contour; animated
volumetric scattering may extend beyond or soften its grid edge. It adapts to
native voxel growth and removal instead of drawing a fixed radius.

## Ownership and publication

Acquisition runs on the existing shared native frame owner after its stage-8
callback. A 30 ms cache limits sampling when the entity-system source remains
unchanged. Bounded copies reject invalid counts, nonfinite coordinates, invalid
buffer indices, null/changed voxel owners and stale entity identities. Nonfinite
or nonpositive densities are excluded. The buffer and voxel owner are rechecked
after copying; the full serialized entity identity, scene node and primary
vtable are checked before publication. A changed entity-system source discards
the acquisition.

Publications contain copied edges and retain only the entity-system pointer and
capture timestamp as source metadata. They do not contain entity pointers or a
local-player dependency, since the sampled areas are world state. Rendering
requires the same client module and entity-system source and rejects snapshots
older than 250 ms. Entity destruction or replacement on the same entity system
is reflected at the next acquisition, normally after the 30 ms sampling cache;
if the owner stops running, the 250 ms publication expiry bounds stale display.
There is no per-entity validation on the render thread.

The owner clears publications when both aura controls are off or the source is
unavailable. Draw-time settings hide each disabled area kind immediately;
cooperative stop disarms acquisition and clears the publication under its lock.
Rendering clips projected edges and never calls engine methods. Callback code
remains resident until process exit as documented by the shared lifecycle.

Acquisition/stop serialization is separate from the publication lock, so native
scanning and geometry generation do not hold the render publication lock.
Only swapping/clearing copied geometry and taking the render snapshot require
that lock. No engine methods run under either lock.

## October 23, 2026 recertification: Steam build 26144732

Installed Windows AMD64 client patch **1.41.9.2**, client/server **2000935**.
Read-only investigation used the documented Steam game installation; no game
process was opened or launched, and no installed files were changed.

| Module | PE timestamp / image size | SHA-256 |
| --- | --- | --- |
| client.dll | `0x6ADA7340` / `0x29A0000` | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` |
| scenesystem.dll | `0x6ADA337D` / `0x978000` | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` |
| materialsystem2.dll | `0x6ADA32D5` / `0x186000` | `2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69` |
| tier0.dll | `0x6ADA3248` / `0x401000` | `f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d` |

Research scripts, candidate searches, decoded current instruction windows and
positive/negative JSON reports are retained under
`logs/research/26144732/render_grenade/`. Optional discovery used Capstone
**5.0.9**; the frozen verifiers remain standard-library Python and reject
unrecognized complete module hashes before checking contracts. Discovery
masked only instruction-decoded RIP-relative displacements and relative branch
immediates to locate candidates; the final frozen checks contain exact current
bytes/hashes and unmasked independently decoded operands.

`C_Inferno` RTTI vtable moved `0x1CAC6F0 -> 0x1CAE650`; named schema
registration/field array moved `0x223FE48/0x223FB30 -> 0x2241E58/0x2241B40`.
The class remains size `0x86A0`, 24 fields, including positions `+0x1108`,
burning bytes `+0x1708`, count `+0x1A48`, post-effect byte `+0x1A54`, and
visual half-width `+0x866C`. The new native fire world-coordinate consumer
`0xD53451`, burning selector `0xD534FA`, visual-width consumer `0xD5105F`,
default-width stores `0xD42BF8`, post-effect branch `0xD45453` and bounded
capacity check `0xD53598` preserve those meanings and the 64-cell capacity.

`C_SmokeGrenadeProjectile` RTTI vtable moved `0x1BD2190 -> 0x1BD4120`;
registration/field array moved `0x2218958/0x22185E0 -> 0x221A798/0x221A620`.
It remains size `0x1500`, 11 fields, with unchanged smoke tick/effect/detonation/
spawned fields. The start/origin producer `0x7D8EF0` proves the embedded
simulation `+0x13B8`. Native iteration `0x7C7190` and render occupancy
`0x7E1030` retain owner `+0x70`, current buffer `+0x100`, occupancy `+0x1008`
and 32768 voxels. The density consumer `0x7E110C` still computes `1-buffer`,
uses its `0x80000` byte buffer and `+0x3008` base. Morton decode is now
`0x7E1072`; world-coordinate conversion `0x7E1D2A` decodes cell-size constant
RIP target `0x1ADC2FC` as float 20. Grid/cell-center initialization
`0x7E1DFC/0x7E1E44` contains float 16/10. Origin remains simulation `+0xE8`.
These independent windows moved by different deltas; no common shift was used.

`python tools/inspect_grenade_areas.py` passes. All **28** numeric constants,
including timestamps, vtables, members, buffer strides and float sizes, reject
individual temporary header mutations (`negative-results.json`). No runtime
behavior changed beyond build-specific identities/vtables. Shared frame-owner
stage/lifecycle updates are owned by the model/frame verifier. Actual grenade
footprint appearance remains unobserved; builds/CTest are reported separately.
