# Basic silent aim: installed-client evidence

## Static recertification — October 21, 2026

Target: Windows AMD64 client at
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
Installed metadata: patch **1.41.9.1**, client/server **2000934**, Steam build
**26087496**, Source revision **11150591**, version date October 20, 2026,
11:41:22. Client SHA-256:
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
PE timestamp **0x6AD7B41C**, SizeOfImage **0x299D000**, machine **0x8664**.
The prior client SHA-256 was
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
timestamp `0x6AD01314`. The patch changed that identity; the contracts below
were checked independently before updating the frozen hash/timestamp.

All **25** frozen history/shot instruction windows match at their original RVAs.
The constructor, input RTTI and slot-25 target, before/after-finalizer ordering,
producer/consumer field operands, native primary-attack enum and protobuf
field descriptors were checked again. **No source address, member, presence
mask, runtime fingerprint or argument/ownership contract changed**. Input stays
`0x257B640`, InputVtable `0x1C9DB20`, HistoryBuilder `0xD02130`; arena-owned
history and synchronous callback scope retain their existing contracts.

Verification: `python tools/inspect_silent_aim.py`. Rejected header changes:
`Input`, `command::HistorySize`, `HistoryBuilderBytes`.

Research is retained under `logs/research/26087496/movement_input/`:
`contract_recheck.json` records every original exact instruction window, native
body hash and decoded result before freezing; `silent_aim.json` records the updated
verifier run. The frozen verifier continues to reject every unfamiliar hash.
Three temporary header copies for this component independently changed a global,
a member/layout field and a runtime entry fingerprint; all were rejected.
`negative_headers.json` contains the nine results across these three components.

No CS2 process was launched/opened, no server was contacted and no installed
file was changed. This is static contract evidence; current gameplay, movement
and camera behavior remain unobserved. Build/CTest results are recorded in the
main update handoff. Earlier dated evidence below describes its original build;
its unchanged contract details remain applicable where rechecked here.


Static investigation on October 15, 2026. Installed client is AMD64,
`game/csgo/bin/win64/client.dll`, patch 1.41.9.1, client/server 2000932,
PE timestamp `0x6AD01314`, image size `0x299D000`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
All seven existing compatibility checks passed before investigating this path.
No client or server was launched or modified during this investigation.

## Callback and timing

The current `CCSGOInput` embedded object and vtable are `0x257B640` and
`0x1C9DB20`, established by the constructor and RTTI evidence in the movement
verifier. Vtable slot 25 is `0xD02130`. Producer `0xB66A36..0xB66A43` passes
RCX=input, EDX=split-screen slot and R8=command, then calls slot `+0xC8`.
The native caller ignores its result. The function has no meaningful common
return-value contract; its useful effects are writes to the command.
Use `void(input*, int, command*)`, call the original first and preserve all
three arguments. Returning from the original precedes pawn slot `+0xAA8`
dispatch at `0xB66A5A` and later command processing.

The existing movement finalizer is called at `0xB66973`, **before** this
history builder. Modifying input history in that pre-finalizer hook is too
early: the native builder populates/rebuilds it afterward. The finalizer's
movement CRC contains base angles and buttons; a history-only edit does not
require rerunning it. Keep base angles, the input camera, subtick camera deltas,
render state and attack buttons unchanged.

## Layout and presence

`CSGOUserCmdPB` is embedded at command `+0x10`. Its descriptor includes
`input_history` (repeated field 2), `attack1_start_history_index` (field 6,
default -1), and `attack2_start_history_index` (field 7, default -1).
Descriptor text is at `0x1BCC6B8..0x1BCC9A0`.

| Storage | Offset | Independent native evidence |
| --- | --- | --- |
| Command presence | `+0x20` | Builder `0xD02826`; embedded protobuf presence `+0x10` |
| History repeated container | `+0x28` | Append `0xCDE158` |
| History count | `+0x30` | Append `0xCDE135`, `0xCDE147`; shot bounds `0x80D865` |
| History capacity | `+0x34` | Repeated start `+0x28`, append capacity `+0xC` at `0x123302B` |
| History representation pointer | `+0x38` | Append `0xCDE11A`, consumer `0x80D955` |
| Representation allocated count / pointers | `+0` / `+8` | Append compare `0xCDE139`, indexed read `0xCDE13E` |
| Attack1 index / presence bit | `+0x4C` / `0x20` | Builder `0xD0282A..0xD02836`; consumer `0x80D85C` |
| Attack2 index / presence bit | `+0x50` / `0x40` | Builder `0xD02839..0xD02844`; consumer `0x80D856` |
| History presence / angle pointer | `+0x10` / `+0x18` | Producer `0xCFA725..0xCFA74D` |
| History angle presence bit | `1` | Producer `0xCFA725` |
| QAngle presence / pitch / yaw / roll | `+0x10` / `+0x18` / `+0x1C` / `+0x20` | Producer `0xCFA751..0xCFA77D`; converter `0x803810` |
| QAngle XYZ presence bits | `1`, `2`, `4` | Producer `0xCFA756`, `0xCFA762`, `0xCFA76D` |

The native builder assigns each existing attack index if it is not -1, then
checks it against the produced count (`0xD02848..0xD0291C`). Invalid indices
are reset to -1 and their presence bits cleared. Preserve that native choice;
do not manufacture a shot index, allocate history entries or synthesize attack.
Validate count, capacity, allocated count, each entry and angle storage
before mutating existing history angles. Existing protobuf fields
remain arena-owned; retain no engine pointers beyond the callback.

## Native shot consumer

Weapon-side routine `0x80D700` obtains a current command pointer via pawn
services at `0x80D83B`, then selects primary index `command+0x4C` at
`0x80D85C` or secondary index `+0x50` at `0x80D856`. It bounds the index
against history size at `0x80D865`. At `0x80D955..0x80D960`, it reads
`command+0x38`, then `representation+8+index*8`. At `0x80DA93` it reads
the selected history entry's angle pointer `+0x18`, then calls converter
`0x803810` at `0x80DA9E`. That converter loads pitch/yaw/roll from
`+0x18/+0x1C/+0x20`. This independently connects the produced field to
the native attack history consumer instead of relying on protobuf names.

Changing these angle messages leaves base command/camera values untouched.
Static evidence establishes field ownership and a native consumer;
it does **not** establish observed hit registration, lack of visible camera
movement in all view modes, or behavior of every weapon/server rule.
Recoil, spread, visibility, penetration, prediction and damage are separate
systems and are not established by this evidence.

## Held-fire fallback and retained history

The attack index is the start of an attack transition, not a guarantee that
every held-fire command has an index. `0x80D878` handles the absence of a valid
index by looking at retained history, rather than simply selecting the last
current entry. At `0x80D89E`, it loads the pawn's weapon services; the retained
32-entry circular buffer begins at services `+0x380`, uses `0x78`-byte embedded
`CSGOInputHistoryEntryPB` entries, and has start/count at `+0x1280/+0x1284`.
The backward search `0x80D8D0..0x80D937` compares player tick/fraction fields
`+0x68/+0x6C` against the firing time. Where the firing time falls between
entries, the path at `0x80DD2C` converts the selected retained entry's angles
and the next entry's angles (`0x80DD4F`) for native interpolation. When the
retained sample is sufficient by itself, `0x80DF25` reads its angle pointer.

There is a further boundary fallback at `0x80E033`: if the current command
exists and has at least one history entry, `0x80E04B..0x80E05A` loads the
representation and its **first** entry at `representation+8`, then converts
that entry's angles at `0x80E065..0x80E070`. Before that conditional override,
the ordinary weapon/pawn angle path is the fallback. Therefore a valid-index
only edit cannot cover held automatic-fire continuation.

Weapon-services method `0x911310` (RTTI `CCSPlayer_WeaponServices`, vtable
`0x1C14C78`, slot 13) feeds that retained buffer from command history. It
loads the current command through its pawn/controller ownership at
`0x911339..0x911399`, iterates command entries at `0x911940`, checks player
timestamps, and assigns accepted entries into the ring at `0x911A18` or
`0x911A68` via `0x7B4DB0`. The assignment clears the destination and invokes
the generated message merge; `0x7AEFA0..0x7AEFAE` copies the angle submessage.
These are native retained copies, not pointers the feature should write.

The minimal history-only policy is consequently to set the angles of **all
existing history entries** when native primary attack is held, or a valid native
primary attack index exists (an intra-command tap can finish with down=0).
This includes the initial fire command and supplies native index selection and history samples
that native processing retains for continuation, while preserving index,
buttons, camera/base angles and timestamps. Retained entries from earlier
unassisted commands and native interpolation can affect boundary samples;
no live full-auto or every-weapon behavior is established by static evidence.

Native down state is the first uint64 at `movement_offsets::command::Buttons`
(`command+0x60`): the native finalizer loads it at `0xCED0A7`, and production
copies it into protobuf button-state1 at `0xB66DF9..0xB66E09`. The engine's
named `IN_ATTACK` enum registration is at `0x1B28D10`, with its name pointer
at `+0` resolving to `0x1B28F78` and uint64 value at `+8` equal to **1**.
The silent aim verifier checks that name/value and the shared movement header's
button offset. Neither a held primary button nor a start index alone proves
that the weapon fired; weapon readiness and server rules remain native.

`python tools/inspect_silent_aim.py` verifies the full binary identity, frozen
producer/consumer/caller windows, input RTTI/object construction via the existing
movement verifier, source constants and hook-entry bytes. It reads installed
files only. `--header` selects a header to validate and never rewrites offsets.
