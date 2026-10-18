# Native primary-request integration

Static investigation, October 16, 2026. Target: the installed AMD64 CS2 client
at `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
Patch 1.41.9.1, PE timestamp `0x6AD01314`, image size `0x299D000`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The thirteen existing compatibility components passed before investigation.
This is native client command integration; it uses no server plugin framework.
No game/server process was launched or installed file changed for this work.

## Why native command buttons alone are insufficient

The primary start index in `CSGOUserCmdPB` is generated from the input history
producer, independently of native command down/changed flags. Slot 25's history
builder `0xD02130` calls recorder `0xD055C0` at `0xD02171`. Recorder ABI is
`void(input*, int splitScreenSlot)`: it copies EDX to EBX, RCX to RDI, calls the
input-slot getter with the original arguments, and returns no common result.
Only slot zero is used by this feature.

Getter `0xB70640` computes `input + slot*0x928 + 0x258`. Its first 24 bytes are
native input down/changed/third. Recorder `0xD05648` reads its event count at
`+0x2C`, then `0xD05650..0xD05660` addresses first event's button at `+0x38`,
with a `0x20`-byte stride. A primary press has button **1** and pressed byte
at button `+8`. The exact named `IN_ATTACK` enum at `0x1B28D10` independently
confirms the primary mask.

The ordinary input snapshot producer proves the underlying event layout:
`0xB60799` writes native buttons at slot-state `+0x30`; `0xB609E5` reads event
count at `+0x5C`; `0xB609E9` checks the fixed capacity **32**; and
`0xB609FD..0xB60A15` copies two float4 blocks to `(count+3)*0x20`, then increments
the count. Therefore slot zero has native buttons at input `+0x258`, count at
`+0x284`, event start at `+0x288`, event button at `+8`, pressed byte at `+0x10`.
The first event float is its time; this temporary recorder-only event does not
enter the already produced protobuf subtick stream.

## Native history and attack-index creation

Recorder `0xD05627..0xD056B8` scans events starting at input `+0xBF0`, copies
primary/secondary pressed state into `+0xBC8/+0xBC9`, accumulates per-call press
flags, and advances the scan cursor. At `0xD059A0` it requires a fresh press;
at `0xD059AE` it requires that input attack index `+0xBCC` remains **-1**.
`0xD05A34` assigns the native current-record index to that field.

Native history count is input `+0xBD8`, array pointer `+0xBE0`, record stride
`0x60`. Allocation/initialization remains native at `0xD05732..0xD0585D`.
At `0xD0570F..0xD0572C`, the recorder reuses the last record when its native
tick at record `+0x50` matches the current tick. Calling the recorder once
before the normal history builder therefore supplies the native press record;
its later normal invocation reuses that same-tick record.

The normal builder preserves its own count, thinning and index remapping. Its
`0xD021BD..0xD021F9` compaction can remap an input primary index to zero.
`0xD02824..0xD02836` copies the input primary index to command `+0x4C` and sets
presence `0x20`; its usual bounds checks then validate/reset that serialized
index. The existing silent-aim consumer evidence connects this index and the
generated angle messages to the weapon's native shot-history consumer.

## Request lifetime and cancellation

The request runs on the existing engine command owner, after anti aim restores
its prior command mutation and before anti aim emits a new pose/native movement
CRC finalization. All selection, hitchance and readiness decisions happen
before requesting primary. Manual primary down, changed, third, existing native
attack index, input held state and any physical primary event win over autofire.

`SilentAimStartAutofire` preflights command and input writes. It saves the unused
event slot's complete 32 bytes, count, scan cursor and primary-held byte, adds
one primary press, and calls the verified native recorder synchronously. The
temporary event/scan/held bytes are restored immediately. Native history
allocation and its native record/index calculation remain native. If no valid
native index is returned, the input index is restored and command attack stays
unchanged. On success, only command primary down/changed are set and primary
third is cleared; every unrelated action remains intact. This occurs before
native movement CRC generation.

The input primary index remains owned until the original slot-25 builder has
consumed it. `SilentAimFinishAutofireHistory` then restores the original input
index, including when normal native compaction remaps it. It verifies that no
physical primary takeover/event-list change occurred before restoring.
Only the current command keeps the generated primary/index. No global physical
input down state is held, and later command production resumes from physical
input.

After native history creation, stale source/weapon/target, history-read failure
or angle-write failure must cancel the owned primary request. Cancellation
restores only that primary's command bits, native attack index and its presence
bit; unrelated later actions/presence bits stay intact. Because native CRC
generation already occurred, integration must immediately rerun the original
movement finalizer before command publication. Running the detour recursively
is not an acceptable substitute.

Copied ownership records may be retained for an exact same-command rebuild.
`SilentAimRestoreAutofireRebuild` accepts the current callback's input/command
addresses and checks those plus native sequence before dereferencing any
cached address. Replaced addresses/sequences or already rebuilt native primary
fields abandon copied ownership without writes. A physical primary takeover
also abandons ownership and preserves the command, even when its masks match
the former synthetic press. Shutdown must drain the current callback chain;
it must not dereference arbitrary cached command pointers.

The recorder can create native history records and native primary timing/debug
snapshots before publication. Cancellation removes the command's primary
request; it does not rewind native arena allocations or rewrite retained native
history/debug structures. This follows the normal native per-frame lifetime.

## Verification and limits

`python tools/inspect_autofire.py` freezes twelve producer/consumer/caller
windows, the complete installed file identity, source constant inventory,
independently decoded event layout and native entry bytes. It requires only
Python's standard library, accepts `--header` for contract mutation tests, and
never writes source offsets.

`tests/silent_aim_autofire_checks.cpp` uses a synchronous native-recorder
substitute and native-builder substitute to exercise temporary state restoration,
native-index history reading/writing, primary ownership, cancellation, unrelated
input preservation, manual takeover, capacity rejection, native producer failure,
readonly preflight and same-command rebuild/stale-address retirement.
These checks establish the integration contract in standalone memory fixtures;
they do not establish observed shots, server acceptance, hit registration,
automatic/semiautomatic weapon timing, or live prediction/rebuild behavior.
Those remain offline/private runtime validation work.
