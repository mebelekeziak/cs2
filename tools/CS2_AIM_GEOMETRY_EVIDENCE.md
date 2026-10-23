# Authored capsule geometry and sampled hitchance

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

The installed `bin/win64/animationsystem.dll` now has SHA-256
`8aa57d0c5b90cb49ad5fd9f141004710180a601b08159e50e93c4470142ea132`.
Its named `CHitBox` registration/array are `0x81D6B8 / 0x81D510` (13 fields,
size `0x70`), and `CHitBoxSet` registration/array `0x81D7B8 / 0x81D730`
(4 fields, size `0x30`). All consumed geometry fields retain their prior values.
The new hash is frozen only after independently checking these named layouts.

Client mesh/set lookup moves `0x1773030 -> 0x1775290`, enabled-mask consumer
`0x17737F0 -> 0x1775A50`, current skeleton set getter `0xAADFA0 -> 0xAAEDE0`,
transform-space check `0xAB0D2F -> 0xAB1B6F`, capsule type/endpoint transform
`0xCA10D8 -> 0xCA29D8`, radius consumer `0xCA134F -> 0xCA2C4F`.
The nine complete frozen windows and independent client/animation schema
operands prove the unchanged set stride `0x48`, box stride `0x70`, type `2`,
endpoints `+0x18/+0x24`, radius `+0x30`, translation flag `+0x3D` and alternate
transform flag `+0x4A`. The authored asset
`agents/models/ctm_sas/ctm_sas.vmdl_c` remains SHA-256
`7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22`.
Cached pose and unit-scale constraints remain unchanged.

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

## Historical evidence

## October 21, 2026 recertification — client 2000934

Installed Windows AMD64 client `1.41.9.1` / `2000934`, Source revision
`11150591`, Steam build `26087496`; SHA-256
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`, at the same installed path
below. Scratch `logs/research/26087496/aim_targets/recheck.py` re-runs all nine
native code windows, client skeleton/model schema, animation CHitBox/CHitBoxSet
schema, decoded operand and source-header checks before refreshing build pins.
Every consumed RVA, field, stride, transform flag and capsule shape remains
unchanged. `animationsystem.dll` and the SAS authored asset retain their exact
hashes recorded below. Cached pose ownership, unit-scale requirement and capsule
endpoint/radius interpretation therefore retain the existing evidence.

Final frozen command `python tools/inspect_aim_geometry.py` passes; full output
is `logs/research/26087496/aim_targets/inspect_aim_geometry.json`.

Research `negative.py` rejects temporary headers with wrong `HitboxSet`,
`BoxStride` and `Radius`. The frozen verifier keeps strict client, animation and
asset hashes, with the shared client pin supplied by `inspect_cs2.py`. This is
static file verification only: no native resource method, client/server run,
live capsule or shot was observed. Older build details below are historical.

## Historical implementation evidence

The October 20 [multi-hitgroup extension](CS2_AIM_MULTIHITGROUP_EVIDENCE.md)
reuses these resource/pose contracts for verified torso and bilateral limb
names. The historical `spine_2` lower-torso landmark below is chest group 2;
the current Stomach selection uses authored stomach `spine_0` instead.

Inspected October 16, 2026, Windows AMD64 client 1.41.9.1. This is installed-file
evidence and synthetic verification; no new live client run is claimed.

The installed client is
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`,
SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The matching `game\bin\win64\animationsystem.dll` SHA-256 is
`fb2b4cd71468e880c3b3845e8e7a18c9d550dd3c4504e4a257a2110fb91c1235`.
The layouts below are checked by `python tools/inspect_aim_geometry.py`.

## Current resource ownership

The existing target reader resolves the pawn's current model and dynamically
looks up the selected bone name. The geometry reader then follows that same
model's current enabled meshes and the skeleton's selected hitbox-set index.
It reads resource records and an already available pose; it calls no allocation,
animation-refresh, collision, resource-load or model methods.

Native model helper `client+0x1773030` has three inputs: model, hitbox-set index,
and mesh index. Its complete frozen body establishes these relationships:

| Resource/member | Offset or stride | Independent evidence |
| --- | --- | --- |
| Model mesh count / array | `+0x70` / `+0x78` | Signed bounds checks and indexed mesh read in `0x1773030` |
| Model mesh-group masks | `+0x90` | Native enabled-mesh test `0x17737F0..0x17737FB` |
| Skeleton mesh-group mask | `+0x348` | Named `CSkeletonInstance::m_modelState=0x140` plus `CModelState::m_MeshGroupMask=0x208`; native gather reads `skeleton+0x348` |
| Skeleton hitbox-set index | `+0x3FC`, byte | Named `m_nHitboxSet`; native wrapper `0xAADFA0` |
| Mesh set count / storage flags / array | `+0x174` / `+0x164` / `+0x168` | Complete model helper and native gather |
| Mesh set-map entry stride / value | `0x48` / `+0x18` | Helper's `9*index*8+3*8` address calculation |
| Set name / box count / box array | `+0` / `+0x10` / `+0x18` | Named `CHitBoxSet` and native gather operands |
| Hitbox stride | `0x70` | Named `CHitBox` size and gather increment `0x1773D8C` |
| Bone-name pointer | `+0x10` | Named `CHitBox::m_sBoneName` |
| Endpoints / radius | `+0x18` / `+0x24` / `+0x30` | Named bounds/radius schema and native capsule consumer |
| Shape / translation-only flag | `+0x3C` / `+0x3D` | Named shape/translation schema and native transform branches |
| Alternate-transform flag | `+0x4A` | Native `0xAB0D38` branches to an alternate scene/local-transform path |

Exact named schema registrations in `animationsystem.dll`:

| Class | Registration | Field array | Field count | Size |
| --- | --- | --- | --- | --- |
| `CHitBox` | `0x81D848` | `0x81D6A0` | 13 | `0x70` |
| `CHitBoxSet` | `0x81D948` | `0x81D8C0` | 4 | `0x30` |

The reader requires the current set name `cstrike`, the selected bone name and
native shape **2**. The native consumer at `0xCA10D8` selects shape 2, reads
both authored bounds as endpoints, rotates/scales/translates them by the bone
transform, and reads the shape radius at `0xCA134F`. These bounds are capsule
endpoint centers rather than the opposite corners of an AABB. They need not
be numerically ordered on each axis.

The supported path is a bone-space capsule with translation-only and alternate
transform flags clear. Native `0xAB0E15` obtains the selected bone's cached
32-byte world transform in that case. Other spaces/shapes are unavailable.
The native capsule consumer transforms endpoints using pose scale but reads
the radius directly. This implementation therefore requires pose scale exactly
1; it does not invent radius behavior for scaled player models. Quaternion
values must be finite and already normalized to within 0.001 squared norm.

Every volume contains copied geometry, pose and resource identity. Before use
and commit it rechecks full pawn/controller identity, model/pose ownership,
local eye and selected point, mesh count/array/masks, enabled group, current set,
set storage, box array/count, selected record bytes and selected-name strings.
In-place pose/record changes invalidate the copy, including changes retaining
the same allocation pointer. Missing geometry preserves ordinary manual
landmark selection while making hitchance and automatic fire unavailable.

## Installed authored example

The real player resources are under `agents/models/`; similarly named
`characters/models/` resources are small dummy models in this installation.
The installed asset `agents/models/ctm_sas/ctm_sas.vmdl_c`, extracted read-only
from `game/csgo/pak01_dir.vpk`, has SHA-256
`7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22`.
Its embedded `MDAT` blocks contain `m_hitboxsets` named `cstrike`.

| Selected bone | Endpoint A | Endpoint B | Radius | Shape |
| --- | --- | --- | --- | --- |
| `head_0` | `(-1, 1.8, 0)` | `(3.5, 0.2, 0)` | 4.3 | 2 |
| `spine_3` | `(2.5, -0.6, -6)` | `(2.5, -0.6, 6)` | 5 | 2 |
| `spine_2` | `(4.8, 0.15, -4.1)` | `(4.8, 0.15, 4.1)` | 6.2 | 2 |
| `pelvis` | `(-2.7, 1.1, -3.2)` | `(-2.7, 1.1, 3.2)` | 6 | 2 |

These example values are documentation only. Runtime code reads the selected
current model's authored values, without a static radius or model-name table.
Reproduce the original metadata dump with the already retained official
[ValveResourceFormat 20.0 CLI](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.0):

```powershell
python tools/material_vpk_probe.py 'C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk' --extract agents/models/ctm_sas/ctm_sas.vmdl_c --out logs/research/ctm_sas_agent.vmdl_c
.\logs\research\vrf20\Source2Viewer-CLI.exe -i logs/research/ctm_sas_agent.vmdl_c --all > logs/research/ctm_sas_agent-all.txt
```

The tool's existing MIT LICENSE remains beside its executable. No VRF code,
game asset or research-only dependency was added to the runtime or build.

## Probability model and scope

`AimEstimateHitchance` accounts for every requested sample against the copied
selected capsule and current finite weapon range. It uses four deterministic
Halton dimensions (bases 2, 3, 5 and 7) to integrate two independent uniform
radii and two independent uniform angles. There is no `sqrt(U)` radial
conversion: that would model a uniform-area disk, disagreeing with the native
shot distribution. Native Negev early-recoil radius shaping and maximum-radius
and fixed-angle overrides are modeled; the weapon reader documents and checks
the supported weapon/model contracts separately.

Each raw shot direction is `forward + right*x + up*y`. The native consumer
at `0x887F04..0x887F20` multiplies that raw vector by weapon range without
normalizing it. The estimator preserves that native endpoint: its geometric
solver normalizes the direction and uses `range * length(raw direction)` as
the world segment length. Capsule intersection minimizes squared distance between
that segment and the capsule's endpoint segment, including their interior
stationary point and all four constrained edges. It handles endcaps, tangency,
parallel axes, degenerate sphere capsules, origins inside a shape and weapon
range ending before the shape. Recoil can carry the shot pitch beyond view
pitch limits, so the estimator accepts finite direction angles through 360
degrees.

The displayed percentage is a finite-sample estimate of intersecting **the
selected capsule** in the currently copied pose. Its numerical increment is
`100/sample count` percentage points. It is not a binomial confidence interval
or a guarantee, and it does not reproduce actual native seed choices. Other
player hitboxes, occlusion, penetration, damage, motion between sampling and
impact, pending pose refresh and server lag compensation are outside this
geometric estimate. An obstruction can prevent damage even when geometric
hitchance is high.

## Verification

`aim_hitchance_checks` covers analytic sphere silhouettes with uniform radial
sampling, independent fixed-radius angular convolution, Negev's do-while
boundaries and override ordering. A dense independent 512-by-512 radius /
relative-angle CDF integration checks the four-dimensional sampler. Synthetic
resource fixtures exercise authored endpoint transforms, selected-name and
current-set lookup, unsupported spaces/scales and stale state. Existing
`silent_aim_checks` also remains passing with its original target fixtures.

The new suite passes **143 checks**; no observed runtime accuracy or server
damage rate is claimed. Static header/native negative checks are recorded in
`logs/research/aim-geometry-negative.json`.
