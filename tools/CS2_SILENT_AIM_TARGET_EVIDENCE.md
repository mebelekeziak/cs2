# Basic aim target acquisition evidence

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
