# Basic aim target acquisition evidence

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

The named `C_BaseModelEntity` registration moves `0x21F3F48 -> 0x21F61D8`,
field array `0x21F4600 -> 0x21F6860`; 47 fields and `m_vecViewOffset=0xF60`
remain unchanged. Native eye-origin/offset reader moves `0x8CD2CF -> 0x8CE0FF`,
scale-enabled transform call `0x8CD367 -> 0x8CE197`, target
`0x16A3D0 -> 0x16A3B0`; absolute-origin getter moves `0x219A80 -> 0x219A60`.
Current full serialized-handle comparison is at `0xB1A28D`; independent native
reference constructions are at `0xB1A2B4` and `0xC961AB`. Their serial adjustment
and field widths remain unchanged. `inspect_silent_aim_targets.py` checks the
new schema records, all eight machine windows and the decoded live offset.

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

Installed Windows AMD64 client: patch `1.41.9.1`, client/server `2000934`,
Source revision `11150591`, Steam build `26087496`. The installed path below
is unchanged. New client SHA-256:
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`.

The separate scratch investigation
`logs/research/26087496/aim_targets/recheck.py` ran the complete schema, eight
instruction-window, decoded view-offset/transform and source-header checks
against the new image before changing build pins. All pass unchanged:
`m_vecViewOffset` remains `0xF60`, the named class template/array and native
eye/handle consumers retain their previous RVAs and bytes. Contiguous float3,
scene rotation/scale, origin sum and full serialized handle contracts therefore
retain the evidence below. No new eye getter or engine call is introduced.

Final frozen command `python tools/inspect_silent_aim_targets.py` passes;
full output is `logs/research/26087496/aim_targets/inspect_silent_aim_targets.json`.

`negative.py` in that research directory rejects a temporary header with
`ViewOffset=0xF64`. The frozen verifier continues to import the strict client
hash pin from `inspect_cs2.py`. This recertification is installed-file evidence
only; no CS2 process, server, live target or shot was observed. Earlier build
identities and live observations below are historical.

## Historical investigation — client 2000932

This concerns the installed Windows AMD64 **client**, patch **1.41.9.1**,
client/server **2000932**, Steam build **25947352**, inspected October 15, 2026.
Installed client path:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256: `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
PE timestamp `0x6AD01314`, SizeOfImage `0x299D000`. Investigation reads installed
PE bytes only, without launching or modifying the game. This is static evidence,
not a live shot or aim observation.

## Existing entity and target geometry

[Entity evidence](CS2_ENTITY_EVIDENCE.md), [cs2_offsets.h](../include/cs2_offsets.h)
and [inspect_cs2.py](inspect_cs2.py) own the existing entity table, full serialized
handle/backreference, controller/pawn, health, life, team, scene dormancy, scene
position/rotation/scale and collision-bounds fields. Those checks must continue
to pass independently.

Acquire fresh values on the command thread when needed, with separate reader
storage: `PlayerReaderCapture` owns Present-thread mutable state and cannot be
called concurrently. Validate controller and pawn identities and reciprocal
handles; reject local players, non-player teams, teammates, dead or dormant
pawns, invalid geometry and sources that change during capture. Do not keep
entity pointers across commands or lifecycle changes.

The initial investigation considered a 75%-height collision-hull point. The
implemented hitbox selector supersedes that proposal with dynamic named animated
bone landmarks; see [named-bone evidence](CS2_SILENT_AIM_HITBOX_EVIDENCE.md).
Missing names/poses skip the player, with no collision-hull fallback. Copied
scene rotation/scale/origin remain inputs to the local eye calculation below.

## Local eye origin

The installed `C_BaseModelEntity` static schema registration is RVA `0x21F3F48`:
class size `0x1098`, 47 field records, array RVA `0x21F4600`, stride `0x20`.
Named record `m_vecViewOffset` at RVA `0x21F4B40` gives member **`0xF60`**.
The new header exposes that additional field only.

Native consumers establish its live representation and use:

- `0x8CD2CF` calls origin getter `0x219A80`; `0x8CD2DB` forms
  `R8 = pawn + 0xF60`.
- Getter `0x219A80` reads pawn scene pointer `+0x330`, refreshes its transform
  when required and returns scene absolute origin `+0xC8`.
- The consumer optionally obtains an interpolated view-offset value before
  calling transform `0x16A3D0` at `0x8CD367`. `RCX` is the pawn, `RDX` a local
  output vector, `R8` the input vector, and `R9B=1` enables scale.
- Transform instructions `0x16A429`, `0x16A42D`, `0x16A432` read contiguous live
  float components at input `+0`, `+4`, `+8`. Other branches apply current scene
  orientation; this is a direction/vector transform without origin translation.
- `0x8CD36C` onward adds the transformed offset to the absolute scene origin.
  Independent output routine `0xC7BCA0` repeats the same sequence at
  `0xC7BCD2` through `0xC7BDA2`.

Thus the copy-only eye calculation is **scene origin + scene-rotated/scaled live
view-offset float3**. Existing Source Euler `AngleMatrix` convention in the
reader supplies the copied scene transform. This uses the latest live wrapper
value; it does not execute native interpolation, camera smoothing or a native
eye getter. A plain fixed standing-height addition would ignore crouching and
is not justified. The similarly named `CNetworkViewOffsetVector` serialization
schema's component records are not physical live float offsets for this wrapper:
do not add those component offsets to `0xF60`.

Calling a suspected native getter would add unresolved rejection predicates and
engine lifecycle assumptions. No such ABI is required by this minimal reader.

## Visibility and exclusions

There is no verified local line-of-sight trace API in this project. A spotted
state is not a replacement for such a test. Acquisition has no visibility,
penetration, damage, probability or spread calculations. Selected bone landmarks
do not trace hitbox volumes. An enemy may be behind a wall; selecting a valid
landmark does not guarantee a hit.

## Reproduction

```powershell
python tools/inspect_cs2.py
python tools/inspect_silent_aim_targets.py
python tools/inspect_silent_aim_targets.py --header include/silent_aim_target_offsets.h
```

The additional verifier pins the full installed client hash, named schema
registration and field association, native instructions and decoded field/call
operands. `--client` selects an installed file to inspect; `--header` selects a
source header to check, never an output destination. It does not discover or
silently update replacement offsets for another build.

The real header and installed client passed this verifier on October 15, 2026.
Seven independent in-memory negative mutations were rejected: the header's
view offset, its named schema field offset and one byte at each of the five
native anchors. These checks require no game process and do not establish live
aim behavior.

## Failure investigation: controller identity semantics

Read-only inspection after the user reported no effect found a distinction that
must remain observable rather than assumed to explain the failure. Function
`0xB19400` first resolves pawn `m_hController` (`+0x14BC`) through the entity
table. At `0xB1944D` it compares the complete handle against identity `+0x10`;
a mismatch rejects that lookup. On assigning a different controller,
`0xB19474..0xB19496` constructs the reference as
`((rawHandle >> 15) - (identityFlags & 1)) << 15 | (rawHandle & 0x7FFF)`,
with invalid-handle handling. Independent assignment at
`0xC94B3B..0xC94B61` repeats this construction. The verifier freezes all three
sites, including the flags member, bit mask and serial shifts.

Thus a raw identity handle and a newly constructed reference can differ when
identity flags bit zero is set. This does not establish that any live controller
in the user's test had that flag, or that accepting the adjusted serial would
be correct: the native resolver still requires exact table equality. The aim
reader retains its existing full identity checks. Optional target diagnostics
now record the failing stage, raw controller handle, identity flags, reverse
controller handle, selection counts and bone/model/pose values. These separate
an identity or pose rejection from an actual angular-FOV rejection.

No current game process or corresponding live target-state capture was
available during this investigation. The existing log's `AIM_BUILD_VERIFIED`
only confirms preparation; absence of `AIM_HISTORY` alone cannot identify the
failing stage or prove that the callback ran.
