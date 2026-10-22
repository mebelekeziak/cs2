# Native client autowall

## Current static recertification, October 21, 2026

Installed Windows AMD64 client: patch `1.41.9.1`, client/server `2000934`,
Steam build `26087496`, Source revision `11150591`, version date October 20,
2026, 11:41:22. Installation/executable remain the paths below. Client SHA-256
is `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`.

A separate scratch contract copy selected the new hash before the frozen
contract changed. All fourteen complete native windows, runtime header arrays
and addresses, weapon penetration schema and independently decoded operands
passed unchanged. Fresh Capstone 5.0.9 disassembly and an independent byte/hash
audit remain in `logs/research/26087496/aim_ballistics/`. Constructor, destructor,
trace, segment-step and extractor ABI/storage contracts are unchanged; no RVA,
field, budget or buffer ownership rule needed adjustment. The frozen JSON
client hash is updated, with unknown binary hashes still rejected.

`python tools/inspect_autowall.py` passes. Temporary mutations to `Trace`,
`Penetration`, `DirectionVector` and `InitializeBytes` each fail for their actual
header mismatch. The scratch output and sixteen-case shared negative report
are retained in the research directory. Runtime behavior was unchanged. No
client/server process was launched or opened, and no installed file changed.
Live penetration/damage behavior remains unobserved on this build. The original
investigation below describes the preceding build and is historical.

Investigated October 20, 2026 for the Windows AMD64 client, using the installation
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
Executable: `bin/win64/cs2.exe`. Installed patch 1.41.9.1, client/server 2000933,
Steam build 25966348. Client SHA-256:
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The project uses the previously established native x64 command owner and D3D11
overlay; this feature does not introduce a runtime or graphics interface.
All 26 existing compatibility components passed before implementation in
`logs/compatibility/20261020T150720024542Z/`. Installed files were read only.
No client/server was launched, opened or contacted.

## Discovery and ABI

The existing `FX_FireBullets` anchor at `0xD23B43..0xD23B6C` loads base damage,
range, penetration and range modifier from live weapon data. Static named
`CCSWeaponBaseVData` registration `0x221AAE8`, fields `0x2219F80`, independently
proves `m_flPenetration = 0x82C`. These are current build contracts, not signatures
from a different game. Local Capstone disassembly used the ignored dependency
directory `logs/research/python-deps`; ordinary verification is standard-library
Python. The research scripts and decoded native functions stay under
`logs/research/`.

The native shot at `0x887EFF` constructs its stack bullet data through
`0x87EE00`, then at `0x887F56..0x887FBA` builds the established shot filter and
calls the bullet trace producer. The producer ABI is:

```
void Trace(data*, const float3* start, const float3* displacement,
           filter*, int penetrationBudget, bool append)
```

It is `0x8830E0..0x8831F2`. The native caller passes budget 4 and append true.
This is a displacement, not an absolute endpoint. The exact shared shot filter
uses mask `0x1C300B`, layer 3, flags `0x0F`, extra byte `+0x39` bits 2 and
exclusion mask `0x4000000000`. `0x352920` constructs the filter and ownership
rules. The producer calls `0x89CA80` and native manager `0xA19E90`, then
`0x886090` constructs ordered free-space/solid segments. `0x89CA80` can extend
the original ray to find an exit. The final displacement at data `+0x1D04`,
also used by the native damage caller at `0x885723..0x885745`, supplies the
distance domain for every segment fraction. Target damage beyond weapon range
is rejected even if exit acquisition extends the trace farther.

## Owned native storage

The complete constructor `0x87EE00..0x87EE98` sets up inline-capacity native
containers and initial flags. Runtime reserves a zeroed, 16-byte-aligned
`0x1D20`-byte stack buffer. Constructor writes end at `+0x1D14`; buffer rounding
provides alignment and tail padding.

| Member | Value | Independent native use |
| --- | --- | --- |
| Hit count / pointer / capacity | `0`, `8`, `0x10` | Constructor, trace manager and destructor |
| Hit stride | `0x38` | Manager sort `0xA19F97`; extractor caller `0x88823A` |
| Segment count / pointer / capacity | `0x1C20`, `0x1C28`, `0x1C30` | Constructor; loop `0x885752..0x8857CE` |
| Segment stride | `0x18` | Native damage loop and segment append |
| Segment fractions | floats `0`, `4` | Free falloff `0x8A1857..0x8A18A5` |
| First / last hit index | words `0x10`, `0x12` | Solid step/extraction; first masks `0x7FFF` |
| Solid flag | word/byte `0x14`, bit 1 | Segment producer and damage branch `0x8A1841` |

The destructor `0x87FB70..0x87FCA6` releases grown segment and hit containers
using their native allocator/ownership flags and inline-address comparisons.
The original caller destroys the same data at `0x889448`. Runtime always calls
this destructor after a successful construction, including blocked paths and
trace/iteration faults. Cleanup faults reject the result. No native allocation
is freed through the overlay CRT; no hitbox, surface, manager, container or trace
pointer is retained in a returned result. The filter has no dynamic resources.

## Native damage step

`0x885710..0x885827` supplies the established ABI for the segment step:

```
bool Step(data*, state*, segment*, int shooterTeam, impactsOutput*)
state = {float damage, penetration, rangeModifier, distance;
         int remainingPenetrations; bool stopped; padding[3];}
```

Step is `0x8A1820..0x8A20DF`. Native `0xA7FD40` returns the service pawn's team
byte `+0x3E7`; runtime supplies the validated copied local team. The optional
impact-output pointer is null, as permitted by the function's checked branches.
This skips diagnostic impact-record allocation. No firing/effect/attack routine
is called. Free segments multiply residual damage by `powf(rangeModifier,
segmentTravel/500)`. Solid segments query native surface penetration/material
properties, entry/exit geometry, thickness and special material rules, update
the damage and penetration budget, and return true when stopped. Runtime rejects
native stop, damage below 1, increasing/nonfinite damage, malformed containers,
invalid fractions/indices and impossible budget changes. The only policy limits
added to container traversal are 4096 hits and 8192 segments; these are bounded
prediction limits, not claims about engine maximum capacities.

For each free segment, damage is advanced to its end before examining the hit
at its last index. The native extractor `0x885830..0x885B23` has ABI
`void(data*, traceResult*, float fraction, hit*)`; native callers prove its four
arguments at `0x88821F..0x888245`. It resolves full serialized entity identity,
the authored hitbox, native hit flag and hit position. Only the selected pawn,
with a known group 1–8 and no native blocking flag, yields damage. A foreign
authored-body hit blocks prediction; collateral shots are not modeled. Unknown
hitbox pointers remain unknown. Current hitbox group/sentinel evidence remains
in [damage evidence](CS2_AIM_DAMAGE_EVIDENCE.md).

Residual damage is copied before hitgroup/armor scaling. `AimDamageRead` checks
that base damage, range modifier and penetration power still match the inputs
used by the native step, then applies the existing rules, immunity, healthshot,
hitgroup, helmet and armor logic. It does not apply range falloff twice. The
retained damage snapshot checks live penetration power alongside other inputs.

## Integration and limits

`SilentAimSettings::autowall` uses atomic bit 61, after the eight hitgroup bits.
It defaults off and persists as `aim.autowall`; missing older fields use off.
The menu places it alongside Autofire. Existing direct traces remain the fast
path. A valid blocked direct trace can enter the native autowall branch; an
unavailable direct trace cannot be bypassed. Minimum damage, candidate ranking,
quickstop and delayed publication use resulting health damage. Publication runs
the entire path again; setting/source/pose/weapon changes still cancel through
the existing owned-attack and checksum machinery. Telemetry reports toggle and
penetration count. No new hook, DLL lifecycle, loader lock work, physical unload,
entity write or installed-file change is introduced.

This is a central-ray client estimate. Hitchance does not run material tracing
for every spread sample. Surface movement, pose differences, prediction timing,
server/plugin damage rules and spread can change actual damage or registration.
Only native hurt events establish observed damage. Stopped and resident code
is not physically unloaded; restart CS2 normally before loading a rebuilt DLL
over a previous resident version.

## Verification

`python tools/inspect_autowall.py` pins the installed hash, named weapon schema,
14 native windows, decoded layout operands and every runtime header value,
byte array and window address. The contract JSON is included in compatibility
source hashes. `--header PATH` permits negative contract tests without changing
the runtime header. The combined checker now contains 27 independent components.

The fake-native adapter suite checks exact argument ABI, filter exclusions,
alignment, multiple cover segments, native residual damage, extended-ray
distance, range limits, blocked/foreign/unknown hits, malformed native output,
SEH faults, stale local/manager/geometry sources and native destruction.
Damage tests cover residual-damage armor/head scaling and changed penetration
inputs. Integrated tests cover toggle-off behavior, request/publication retrace,
minimum-damage rejection, direct-path fast behavior and cancellation/CRC repair.
Config, coherent settings and real mouse-driven menu tests cover persistence
and the toggle. Build, CTest, preview and contract artifacts are recorded under
`logs/autowall-*`. These are synthetic/static checks; live wall penetration and
predicted-versus-actual damage have not been observed.

Final public and diagnostic x64 Release builds succeed without project warnings
and each pass 55/55 CTest suites. Adapter: 358 checks; damage: 230 checks;
integrated pipeline: 235 checks. All 50 header constant, instruction-byte and
runtime-window-address mutations reject (`logs/autowall-negative.json`). All
27 compatibility components pass in
`logs/compatibility/20261020T151944619063Z/`. Normal and narrow menu previews
were inspected (`logs/autowall-menu*.bmp`). Both payloads are AMD64 PE32+ with
zero static TLS and delay-import directories (`logs/autowall-payloads.json`).
