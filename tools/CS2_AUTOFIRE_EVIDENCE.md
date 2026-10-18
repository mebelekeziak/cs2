# Native primary-request integration

Static investigation, October 16, 2026. Target: the installed AMD64 CS2 client
at `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
Patch 1.41.9.1, PE timestamp `0x6AD01314`, image size `0x299D000`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The thirteen existing compatibility components passed before investigation.
This is native client command integration; it uses no server plugin framework.
No game/server process was launched or installed file changed for this work.

## October 18: wide-FOV target eligibility

The reported crosshair dependence was investigated against
`logs/overlay-19716-134368069092234122.log` and the current client source. The
run contains 180-degree settings, Head selection, a 70% minimum and ready SSG08
snapshots rejected for insufficient chance or a blocked direct path. Some
snapshots show 100% chance with cover blocking, others show low chance with
large native inaccuracy. Automatic requests and publications also occur.
Local actor failures were misleadingly reported as "no enemy in aim FOV".
These records do not identify which on-screen enemy the user intended.

The source selected the nearest angular enemy before testing geometry, chance
and occlusion, then stopped if that enemy failed. It never considered another
enemy in the configured FOV. Changing the view could therefore change which
enemy supplied the only firing decision, even at 180 degrees.

Selection now has an optional command-owner eligibility filter. Autofire uses
it to reject unavailable geometry, insufficient chance and blocked/unavailable
direct paths while continuing the same scan. Angular ranking only prunes against
accepted candidates. Native weapon accuracy/readiness is sampled once per
decision and shared across candidates. The nearest rejected candidate supplies
diagnostics if none qualifies. Manual aiming uses the unfiltered scanner.
The final chosen candidate is revalidated and freshly traced before requesting
an attack; history publication retains its separate trace and cancellation.
No offsets, native ABI, recorder, loader or installed game files changed.

The integrated regression initially failed seven assertions with the old
decision path. Cases cover cover/chance/geometry rejection of the enemy near
the crosshair with a valid enemy directly behind, in both entity orders; narrow
FOV rejection; nearest eligible priority; one weapon sample; late cover with
owned-attack cancellation; and manual input while autofire is enabled. The real
entity scanner is tested separately with authored synthetic identities and poses,
including 180-degree fallback, all-rejected state and pruning expensive checks.
These are standalone checks; live behavior of this change remains unobserved.

`AIM_HITCHANCE` now includes angular separation, intended outgoing pitch/yaw
and the eligibility rejection count. Invalid local actors report "local actor
unavailable" rather than claiming an enemy FOV rejection. Thresholds and cover
checks continue to apply at 180 degrees; this change does not establish that
every reported instance of crosshair dependence had the same cause.

Validation: both x64 Release builds succeed without compiler warnings and each
passes all 44 CTest suites (113 integrated pipeline assertions). All 21 installed
contracts pass in `logs/compatibility/20261018T144006926027Z/`. Both payloads
retain AMD64 with zero static TLS and delay-import directories. Build/test output
is saved in `logs/autofire-wide-fov-{public,diagnostic}-{build,tests}.log`.
No CS2 client/server was started or loaded for this correction.

## October 18: pre-request base-view rejection

The supplied offline run `logs/overlay-4016-134366430011225970.log` ends with
`decisions=1079`, `requested=0`, `published=0`, and `fire_reason=not_requested`.
Its valid 100% estimates, including minimum 0% with `ready=1`, are logged as
`action="manual primary aimed"`. Those estimates came from the existing
post-history manual-fire path. They do not establish that the pre-finalizer
autofire decision path ran. The nearby anti-aim ingestion diagnostics repeatedly
show ordinary base-angle presence **3** and zero roll.

`SilentAimReadView` incorrectly applied shot history's all-three-fields rule to
the earlier base command. Its exact `(presence & 7) == 7` check rejects that
ordinary native representation before candidate/hitchance evaluation and before
the native attack recorder is requested. The installed binary hash remains
unchanged. This is a source contract mismatch, with no offset relocation needed.
The log contains no attempted automatic request from which to infer a second
native recorder failure. Native request fields, attack mask, ABI and slot should
remain unchanged unless new request-stage diagnostics identify another failure.

### Sparse scalar fields and current native consumers

Native producer `0xB85D70` is called at `0xB66450`, before command finalization
at `0xB66973`. It obtains the base through command `+0x40` and its angle-message pointer at base
`+0x40`. For a nonzero view it sets base presence `+0x10` bit **4** with
instruction `0xB85DF6` (`83 4B 10 04`); the independent bit operand is
`0xB85DF9`. The feature's `BaseAnglesPresent` source constant follows this operand.

Each angle scalar has its own native presence/zero-payload contract:

| Scalar | Payload / bit | Zero branch | Nonzero branch |
| --- | --- | --- | --- |
| Pitch | `+0x18` / 1 | `0xB85E2E` writes raw zero; `0xB85E31` clears bit 1 | `0xB85E3A` sets bit; `0xB85E41` stores float |
| Yaw | `+0x1C` / 2 | `0xB85E52` clears bit 2; `0xB85E55` writes raw zero | `0xB85E5D` sets bit; `0xB85E63` stores float |
| Roll | `+0x20` / 4 | `0xB85E74` clears bit 4; `0xB85E77` writes raw zero | `0xB85E7F` sets bit; `0xB85E85` stores float |

Presence lives at angle `+0x10`. Each zero comparison includes negative zero
and canonicalizes omitted payloads to positive zero. NaN takes the unordered,
present branch, so finite-value checks remain necessary. All scalar masks 0–7
are legal native message representations; an omitted component must retain its
raw zero payload. A zero roll normally means mask 3 or less. Absent nonzero
payload, nonfinite values or unsupported roll remain reasons to reject the view.

The native reset reads all three payloads directly at
`0xB6CA39/0xB6CA3E/0xB6CA43`; the movement CRC finalizer does the same at
`0xCECFE1/0xCECFE6/0xCECFEB`. Neither reader checks scalar presence. Therefore
silently replacing an absent **nonzero** raw payload with a default would diverge
from native command processing. Borrow the current raw float3 and validate its
presence/payload consistency. This is distinct from the later shot-history
producer `0xCFA751..0xCFA77D`, which explicitly sets all three scalar bits.

### Whole-zero view and angle-message lifetime

Producer `0xB85D92..0xB85DAD` compares the complete view against native float3
zero through `0x2056F0`. For a whole-zero view, branch
`0xB85E8C..0xB85ED4` calls base helper `0x4FD870`. That helper loads the
existing angle pointer at base `+0x40`, calls angle clear `0x644910` when it
exists, and clears **base** presence bit 4. It retains the existing angle
pointer. Angle clear zeroes all three raw payloads when scalar bits were present,
then clears scalar presence. Thus a cleared base presence bit does not imply
that the existing angle allocation is stale or should be freed.

On a fresh base whose angle pointer is null, that whole-zero branch does not
allocate an angle message. Both native readers have the same independent
null-pointer fallback: CRC LEA `0xCECFCE` and reset LEA `0xB6CA26` select
the default angle object at `0x22082E8` when base `+0x40` is null. Its scalar
presence is zero and its native raw XYZ payload is exactly `(0,0,0)`. Therefore
base-angles presence absent plus a null pointer is a valid native zero view.
The implementation accepts that restricted default representation; a present
base-angles bit with a null pointer is inconsistent and must report a named
rejection instead of inventing a message.

For a nonzero view with a missing angle pointer, `0xB85E14` calls allocator
`0x6723C0`, using the current base message's arena when present or native heap
allocation otherwise, and stores the returned pointer at `0xB85E19`. Constructor
`0x676DB0` initializes scalar presence/cache and all three raw fields to zero.
Existing non-null storage is reused; whole-zero clearing retains it. The feature
borrows these current callback-owned values only. It must not allocate messages,
change presence/payloads while reading a view, cache the allocation across source
changes, or substitute a history-message pointer for the base view.

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

`python tools/inspect_autofire.py` freezes 27 native request and base-view
producer/consumer/lifecycle windows, the complete installed file identity,
source constant inventory, independently decoded event layout, sparse scalar
offsets/masks, shared native default object, zero payload and native entry bytes. It requires only
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
