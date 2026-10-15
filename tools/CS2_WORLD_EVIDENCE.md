# Installed-client world/sky evidence

Sky path recertified October 15, 2026, without launching or opening a client
process. Historical native-update research below remains from October 14.

The local client installation is
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
`csgo/steam.inf` reports patch 1.41.9.1, client/server 2000932, source revision
11131657, version date October 14, 2026 (Steam build 25947352). `client.dll` is
AMD64 PE32+, timestamp `0x6AD01314`, image size `0x299D000`, SHA-256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
`scenesystem.dll` is AMD64 PE32+, timestamp `0x6ACFCD4F`, image size `0x978000`,
SHA-256 `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`.
The bounded sky recertification uses separate client identity constants; shared
ESP/movement headers were not changed or implicitly validated.
The existing overlay verifies actual D3D11 objects in its callbacks; no current
client run or visible world-modification observation was performed here.

## Implemented rendering paths

The final sky implementation changes a local float4 supplied to the native
sky shader parameter copier during each sky draw. Surface controls change
newly generated aggregate world draw packets. Both operations stay on their
native rendering callbacks and retain no engine pointer across callbacks.

| Option | Verified mechanism | Limit |
| --- | --- | --- |
| Sky color/brightness | Copied sky draw shader parameter | Existing sky texture remains |
| Sky probe/reflections tint | Distinct native lighting-sky draw kind | Only views the engine refreshes; baked lighting remains |
| Surface tint (unsupported, removed from UI) | Aggregate DrawArray/DrawPrimitive does not consume packet RGB | Requires a separate authored-material constant implementation |
| Flat world textures | Engine-owned reflectivity material and verified native mode | Built-in flat appearance; no arbitrary texture loading |
| Local-light color/brightness | Generated point/spot/orthographic radiance and area/barn GPU records | Corresponding native light contributions only |
| Disable local lights | Native point/spot and area gather early returns | Baked map lighting remains |
| Sun color/brightness; directional lightmaps | Fresh frame-stack view constants before native copy | No baked lightmap recomputation |
| Exposure/contrast/saturation/gamma | D3D11 final-image pass | Also changes the game HUD |

Detailed frozen machine evidence is in
[CS2_SKY_DRAW_EVIDENCE.md](CS2_SKY_DRAW_EVIDENCE.md) and
[CS2_MATERIAL_EVIDENCE.md](CS2_MATERIAL_EVIDENCE.md).
Lighting evidence is in [CS2_LIGHTING_EVIDENCE.md](CS2_LIGHTING_EVIDENCE.md),
[CS2_ENV_LIGHT_EVIDENCE.md](CS2_ENV_LIGHT_EVIDENCE.md) and
[CS2_BAKED_LIGHT_EVIDENCE.md](CS2_BAKED_LIGHT_EVIDENCE.md).
The unsupported tint path is explained in
[CS2_SURFACE_TINT_EVIDENCE.md](CS2_SURFACE_TINT_EVIDENCE.md).
`python tools/inspect_world.py` invokes all five final standard-library verifiers,
which compare runtime constants against independently decoded local operands.
There is no source-header generation or automatic offset substitution.

## Native sky construction and update research

The RVAs in this historical section and the two following research sections
describe client 1.41.9.0 with SHA-256
`44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008`.
They are not runtime offsets for the newly installed client. Current sky
runtime evidence and the relocated lighting-kind producer are documented in
[CS2_SKY_DRAW_EVIDENCE.md](CS2_SKY_DRAW_EVIDENCE.md).

The prior local schema dump under `logs/research/cs2-dumper/generated` associates
the `C_EnvSky` class with the following fields:

| Field | Offset | Type |
| --- | --- | --- |
| `m_hSkyMaterial` | `0x1098` | strong material handle |
| `m_hSkyMaterialLightingOnly` | `0x10A0` | strong material handle |
| `m_vTintColor` | `0x10AC` | RGBA Color bytes |
| `m_vTintColorLightingOnly` | `0x10B0` | RGBA Color bytes |
| `m_flBrightnessScale` | `0x10B4` | float |
| `m_bEnabled` | `0x10CC` | bool |

These source fields are not written by the feature. Historical local research provides
the additional runtime/output evidence:

* Constructor `0x26FCB0` initializes the matching source fields, sky output
  pointers `0x10E8`/`0x10F0`, and primary vtable `0x1AD76B8`.
* Vtable slot 13 points to `0x271300`, which registers `C_EnvSky*` and callback
  `0x282020` through `0x1CF870`. The callback token is stored at `+0x10E0`.
  Helper `0x161650` is simply `MOV RAX, RCX; RET`; its registration receiver is
  the entity itself.
* Update `0x282020` reads the visible source color at `0x282066`, converts byte
  RGB to linear float3 through `0x1744230`, and applies positive native
  brightness at `0x282076`. Native zero/negative brightness skips multiplication,
  but still computes and writes a fresh source-color baseline.
* The visible output pointer is `entity+0x10E8`, loaded through `RSI` at
  `0x2820B2`. Baseline float3 is copied to scene `+0xE8`/`+0xF0` at
  `0x2821A3`–`0x2821B3`. The lighting-only branch does the same using source color
  `+0x10B0`, output pointer `+0x10F0`, and stores `0x2823C2`–`0x2823D2`.
* Disabled native sky only updates scene visibility flags after the baseline
  write. An absent material deletes/clears that output pointer instead of leaving
  a stale output. The callback uses RCX as its only entity receiver and returns
  zero in EAX at `0x28253E`.

The renderer receives linear source tint including positive native brightness.
The final draw parameter hook multiplies that freshly copied baseline, supports
additional zero brightness, and preserves the float4 alpha. It does not modify
the source entity or the scene output fields above.

## Why the native callback alone is insufficient

Registration flows through `0x1CF870` to `0x3C0F20` and creates queue-category-3
entries. Registration supplies base flag 1 and a null scene pointer; the helper
can add flag 2 from native component state. Dispatcher `0x3A0FD0` calls the
receiver and function at `0x3A1156`–`0x3A1160`. Requeue requires flags 3 and
either the scene-update helper or callback returning 1. The scene helper returns
0 for the null registered scene, and the sky callback returns 0. Queue
processing at `0x3CDCB6` snapshots/clears the
next generation of double-buffered queues. A sky update can therefore follow
native changes and initialization, but is not sufficient to apply menu changes
on an already loaded, otherwise unchanged map. No scheduler flags or token are
modified to manufacture recurrence.

## Recurring owner stage research, excluded from implementation

`0x3CDB30` has embedded source path `game/shared/rendergamesystem.cpp` and method
name `OnClientPreOutputParallelWithServer` (`0x1B05930`/`0x1B05A00`). Its ABI takes
RCX=render-game-system and RDX=event pointer; the caller thunk `0xA4EAA0` loads the
system and calls it. The stage constructs a stack-owned worker at `0x3CE123`,
with vtable `0x1B0E890` whose worker method leads through `0x3DE9B0` to
`0x3A0FD0`. The native job invocation uses this stack-owned context at
`0x3CE14C`; the stage completes that work before returning to its caller.

This stage was investigated as a possible place to force recurring sky updates.
Its main-stage context and parallel-worker scene context did not establish an
additional direct updater invocation as tightly as the actual draw path. The
final implementation does not hook this stage, enumerate sky entities each
frame, manipulate scheduler recurrence, or write source scene tint.

`python tools/inspect_world.py --legacy-update` retains this read-only research
as independent historical machine checks. It does not compare the abandoned
offsets with the final runtime header and intentionally rejects the updated
client. Running that historical mode requires the original binary under the
selected `--game` root; current default verification uses the final verifiers.

## Selected recurring sky draw

Installed `scenesystem.dll` RTTI identifies `CSkyBoxObjectDesc`, whose vtable
`0x5F64D8` render slot `+0x08` points to `0x154FB0`. The seven-argument native
draw preserves descriptor, graphics context, packet array, packet count,
unused fifth argument, view and statistics. It reads the last packet's source
scene from packet `+0x18` at `0x70` stride. Native visible and lighting scene
kind IDs are distinguished by scene `+0xC4`.

The renderer constructs float4 tint on its own stack from scene `+0xE8` and
calls native parameter writer `0x4DA70` with key `0x99E6002A`, returning at
`0x1551A9`. The writer copies all 16 input bytes into collection-owned storage.
The final implementation scopes a thread-local designation around the original
sky render and replaces this exact keyed parameter with a local modified copy.
It restores nested scope state and forwards every original native argument.
Other shader parameter writes remain on the native path. No pointer to that
local copy is retained by the native copier.

## Shutdown and verification

Stop disarms additional modifications and drains the short local-copy/packet
mutation sections. Original/native render callbacks remain available and
continue forwarding. Subsequent draws produce native parameters and primitives;
there is no long-lived source state or pending scene restoration. Hook code and
trampolines remain resident until normal process exit because no full physical
unload barrier is claimed.

`python tools/inspect_world.py`, `python tools/verify_sky_draw.py`, and
`python tools/verify_world_materials.py` are read-only frozen verification tools.
Synthetic tests run actual hook policy with controlled originals, while screen
grading is checked using standalone software D3D11/WARP rendering. They do not
load the game and do not demonstrate runtime visual output. No client process
was launched, injected or used for live visual validation in this task.

Material replacement and other native world-render features require separate
verified material/descriptor interfaces; sky scene tint does not replace skybox
texture resources. Screen grading is a separate D3D11 pass that also affects the
game HUD, and is not a world-material texture replacement.
