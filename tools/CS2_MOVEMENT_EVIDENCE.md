# Movement evidence for the installed client

## October 16 revision 9: prediction correction fix

The user reports perfect offline hops but intermittent ground stutter with
ordinary settings in a private online session, approximately 10 ms ping.
Existing log `logs/overlay-35844-134366329278398936.log` identifies revision 8,
MSVC x64 client hooks and actual D3D11 rendering on the RTX 3060 Laptop GPU.
Its movement rules are modern jump, automatic mode off, penalty 0.015625 s,
window 0.007812 s and interval 0.015625 s.

Among 165 `MOVE_JUMP` edge records, 18 distinct sequences contain both down
and released outputs. There are 11 adjacent simulation-clock rewinds and no
backward sequence transitions among these edge records. These counts describe
client log observations, not authoritative server hops or network delay.
Concrete trace at 14:06:33 UTC:

| Sequence | Command tick | Simulation tick | Ground | Output |
| --- | --- | --- | --- | --- |
| 4728 | 47779 | 47781 | yes | release |
| 4729 | 47780 | 47780 | yes | fresh press despite engine-down feedback |
| 4730 | 47780 | 47783 | yes | release |
| 4730 rebuilt | 47781 | 47781 | yes | fresh press despite engine-down feedback |

Three paths discarded emitted state: same-sequence command-tick changes,
same-sequence simulation rewinds, and planner rollback resetting to Ready.
Revision 9 freezes native and serialized jump edges for the same sequence and
verified source despite clock/ground corrections. Physical steering can still
refresh from the frozen steering start. A new sequence with a corrected
simulation clock retains the pending/releasing phase, conservatively restarts
timing anchors, discards old branch processing/buffer proof and waits for engine
release acknowledgement. Continuous corrections may postpone retries. Actual
source/owner/lifecycle changes still invalidate state. The existing single-entry
cache does not implement arbitrary historical sequence replay.

Initial static bundle: `logs/compatibility/20261016T141415475169Z`, all ten pass.
The installed executable is AMD64 under the documented game root. `steam.inf`
now reports 1.41.9.1 / client-server 2000933 / source 11133601 / Steam build
25966348; the consumed client DLL remains SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
No ABI, signatures or offsets changed. Both public and diagnostic x64 Release
builds and all 18 CTest suites pass, including 49 planner checks, the captured
release/rewind trace, and 10,000 combined clock/ground correction rebuilds.
`MOVE_IMPLEMENTATION revision=9` and `MOVE_REPLAY clock_corrections` identify
the new behavior. No game launch, injection or server interaction was performed;
post-fix gameplay remains unobserved. Restart normally before loading this DLL.

## October 15, 2026 compatibility update: 1.41.9.1

Current installation: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive`,
`game/csgo/bin/win64/client.dll`; AMD64, PE timestamp `0x6AD01314`, image size
`0x299D000`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
`steam.inf` reports client/server **2000932**, patch **1.41.9.1**, source revision
**11131657**, version date October 14, 2026. The previous movement RVAs belonged
to 1.41.9.0, so the existing build checks correctly rejected this client.

This update renews the client movement addresses and their verification. It
retains the existing jump/steering policy. No client/server process was started,
opened or modified during this investigation; all evidence came from installed
files and earlier local disassembly. Live jumping and steering remain unobserved
on this build.

| Runtime target | Current RVA | Independent local evidence |
| --- | --- | --- |
| Embedded input object | `0x257B640` | Constructor LEA RCX at `0xEE574`, vtable write at `0xEE59A` |
| Input vtable | `0x1C9DB20` | Constructor LEA RAX at `0xEE58C`; RTTI below |
| Finalizer | `0xCECF90` | Input slot 6; RCX=input, RDX=command call at `0xB66968` |
| Modern jump | `0x8C34F0` | Services+`0x6C8` receiver LEA and call at `0x8DF74B`/`0x8DF752` |
| Effective maximum speed | `0x8CDEF0` | Pawn argument and XMM0 result at `0x8A2131..0x8A214A` |
| Command ingestion | `0xA77390` | Forwarding thunk `0x8E9510`; copied historical axes below |
| Arena subtick allocator | `0x4F74A0` | Producer call `0xB6661C`, RCX=arena |
| Repeated subtick append | `0x1233010` | Producer call `0xB66627`, RCX=repeated, RDX=step |
| Surface manager | `0x254E100` | Row helper MOV RCX at `0x9E3564` |
| Tick quantum / stamina scale | `0x1AC6064` / `0x1AC6098` | MULSS at `0xA55F99` / DIVSS at `0x8C46DA` |

`CCSGOInput` RTTI type descriptor is `0x23283D0`, complete-object locator
`0x1F236C8`, vtable `0x1C9DB20`; its producer slot 5 is `0xB65DE0`.
Input remains an embedded object. Callbacks retain the existing x64 contracts.
All six runtime function-entry fingerprints are unchanged, checked both against
the installed bytes and independently frozen verifier fingerprints.

Current cvar wrappers, independently decoded from registration LEA RCX and
matched to their registration names:

| Source constant | Cvar | Wrapper start |
| --- | --- | --- |
| AirCap | `sv_air_max_wishspeed` | `0x25325B0` |
| AirAccelerate | `sv_airaccelerate` | `0x2564E90` |
| AutoBunnyhop | `sv_autobunnyhopping` | `0x2532758` |
| JumpPenalty | `sv_jump_spam_penalty_time` | `0x25325A0` |
| JumpPrecision | `sv_jump_precision_enable` | `0x2532640` |
| LegacyJump | `sv_legacy_jump` | `0x2532670` |
| BhopWindow | `sv_bhop_time_window` | `0x25324B0` |

Getter `0x160B10` still reads wrapper+8 then storage+`0x58`. Both legacy
selectors (`0x8DF72D` and `0x8C0336`) directly load slot `0x2532678`.
The movement source receives wrapper **start** `0x2532670`, and the diagnostic
now formats that actual source constant instead of printing an obsolete literal.

### Current layout evidence

Nine consumed schema fields are read from installed, on-disk registration
templates and compared with the source: pawn flags/absolute velocity/actual move
type/water level, pawn movement services, controller tick base, services maximum
speed, friction and stamina. Class templates are `0x21F4BE8`, `0x21F4588`,
`0x2223A38`, `0x222AD38`, `0x222B2D8` and `0x2221E88`. Field arrays have a
32-byte stride, field-name pointer at +0 and offset uint32 at +`0x10`.
The verifier checks class names, field-array pointers, field counts and the named
fields. Their consumed offsets are unchanged from the historical section below.

Command sequence assignment `0xB66113` still writes command+8. The command's
inline protobuf starts at +`0x10`, with presence another +`0x10` (`0xB66121`,
`0xB6612A`), yielding command+`0x20`. The finalizer reads base at command+`0x40`,
angles at base+`0x40` and native buttons at command+`0x60` (`0xCECFB1`,
`0xCECFCA`, `0xCED0A7`). Base presence at +`0x10` is initialized by constructor
`0x4F8260`. Producer OR masks at `0xB662A1` and `0xB66338` establish forward
presence `0x40` and left presence `0x80`, paired with base+`0x58`/`0x5C` writes.

Producer `0xB665F7..0xB6662F` establishes base subtick representation+`0x28`,
repeated-object start+`0x18`, repeated size+8, and representation allocated count+0
with pointers+8. Append `0x1233010` compares repeated capacity+`0xC`. Therefore
base size/capacity remain +`0x20`/`0x24`. Constructors `0x4F8260` and `0x4F8580`
and the producer's digital/analog/camera payload stores preserve the prior
protobuf layouts. The verifier freezes these code windows and decodes the
corresponding member operands; it does not infer layouts from protobuf names alone.

Modern state LEA `0x8DF74B` gives services+`0x6C8`; actual, usable and landing
members at `0x8C3591`, `0x8C3619` and `0x8C362A` supply relative +`0x10`,
+`0x18`, +`0x20`, yielding services+`0x6D8`, +`0x6E0`, +`0x6E8`.
Ingestion `0xA7762A..0xA77658` continues copying base forward/left axes to
services+`0x1A0`/`0x1A4`. Those historical effective axes intentionally differ
from schema `m_flForwardMove`/`m_flLeftMove` at +`0x1C0`/`0x1C4`.
Surface pointer services+`0x278`, surface row lookup and stamina division retain
the previous semantics; their current operands and code windows are checked.

A separate comparison against retained October 13 local disassembly found
**1,802 instructions unchanged after masking only relative branch/call addresses
and RIP-relative displacement bytes**, across air acceleration, AirMove,
SetupMove, simulation tick runner, historical input handling, command dispatch,
button setter, finalizer and base protobuf constructor/copy. This is supporting
compatibility research, not proof of observed gameplay. Records and scripts are
in `logs/movement-compat-20261015/`.

### Verification

`python tools/inspect_movement.py` passes against this installed client. It uses
only the Python standard library and checks the complete binary hash/PE identity,
44 frozen machine-code windows, input RTTI/vtable/constructor, seven cvar
registrations and getter/selector operands, 11 decoded globals/helper targets,
35 member/presence constants, six source function-entry fingerprints and nine
named installed schema fields. It reads no game process and changes no game files.

Seven separately mutated header copies were rejected: mistaken legacy pointer
slot, old input RVA, old jump target, wrong friction offset, wrong native button
offset, wrong subtick capacity and altered finalizer fingerprint. Results are
`logs/movement-compat-20261015/verification.json` and `negative.json`.
Build and CTest integration results are recorded in the main README. Static
compatibility checks and standalone tests do not establish actual jump success,
server consumption, collision response or offline steering performance.

---

# Historical movement investigation: 1.41.9.0

Inspected October 13, 2026. Client AMD64, PE timestamp `0x6AC7FFFA`, image size
`0x2994000`, SHA256
`44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008`.
Installation/version are documented in README. No installed files were changed.
`python tools/inspect_movement.py` verifies the binary hash, relevant function
bytes, callback caller, vtable entry and engine tick constant without launching
or opening CS2. It also compares source Cvar constants with independently decoded
registration and getter operands. A changed build needs renewed investigation.

## Command lifecycle

RTTI: `CCSGOInput` descriptor `0x23202F0`, complete-object locator `0x1F1D598`,
vtable `0x1C97B28`. Its slot 5 producer is `0xB64E60`. Slot 6 is `0xCEC510` and
the call at `0xB659E8` passes RCX=input, RDX=command; its return is ignored.
The callback is called with `void(input*, command*)` on x64.

`dwCSGOInput` is the **embedded object** at `0x2572460`. Constructor `0xEE574`
passes its address directly to base constructor `0xB5AF00`; `0xEE59A` writes
the final vtable into the same object's first word. It is not a pointer global.

Native command sequence is int32 at command+8, assigned at `0xB65193`. The
protobuf legacy number at base+0x50 is cleared and not assigned in this producer;
it must not be used for deduplication. Client tick at base+0x54 is assigned at
`0xB651D5` and may repeat between successive native command sequences.

Original slot 6 constructs a temporary protobuf containing view angles and
native buttons, serializes it, and writes the resulting bytes to `move_crc`.
Mutations precede the original so it rebuilds those bytes consistently. Slot 25
`0xD01610` follows and manages input/attack history. Normal production later
copies native buttons to the actual network protobuf at `0xB65E79/0xB65E9A/0xB65EB9`.
Attack history is preserved. The revision appends transitions using the verified
game arena/container helpers below.

A subsequent input-related predicate at `0xB65AE0` can clear analog movement
when object `0x21F7FD0`+0x10 has a positive count. Its precise semantics are not
verified; command-hook telemetry alone cannot prove final server consumption.

## Command fields

Installed embedded `usercmd.proto` descriptor at `0x1B26ED0`, following generated
reflection/hasbit tables, constructors and machine code establish these layouts.
The [tracked Valve protobuf definitions](https://github.com/SteamTracking/GameTracking-CS2/blob/master/Protobufs/usercmd.proto)
provide background field names; runtime layouts were taken from the installed binary.

| Field | Offset | Presence |
| --- | --- | --- |
| Native sequence | command+0x8 | native |
| CSGO protobuf presence | command+0x20 | base bit 1 |
| Base protobuf pointer | command+0x40 | |
| Native button state words | command+0x60/0x68/0x70 | native |
| Base presence | base+0x10 | uint32 |
| Subtick arena / size / capacity / representation | base+0x18/0x20/0x24/0x28 | repeated |
| Angles pointer | base+0x40 | 4 |
| Forward / left analog | base+0x58/0x5C | 0x40 / 0x80 |

Repeated representation stores allocated count at +0 and pointers at +8.
Constructor `0x4F7EE0`, copy `0x4F7FA3` and reserve `0x122D2B0` verify this.
Arena ownership and original relative ordering are preserved. Additional
time-zero events are prepended to the active list through the verified engine
append path. Failure rollback restores pointer order and clears generated
payloads before returning them to the inactive pool.

Subtick size is 0x38. Presence+0x10, button uint64+0x18(bit1), pressed bool+0x20(bit2),
when float+0x24(bit4), forward delta+0x28(bit8), left delta+0x2C(bit0x10),
pitch delta+0x30(bit0x20), yaw delta+0x34(bit0x40). Constructor `0x4F8200`
and reflection tables match these. The implementation removes assisted button
and analog deltas while preserving time, camera deltas and unrelated actions.

Native button-state setter `0x4C0D20` and truth table at `0x222C500` establish
fresh press as down=1/change=1/third=0; held down=1/change=0/third=0;
release down=0/change=1/third=0. Registered masks: jump 2 (`0x222C3B0`),
forward 8 (`0x222C0E0`), back 0x10 (`0x222C170`), left 0x200 (`0x222C200`),
right 0x400 (`0x222C290`). Direction bits must match synthesized analog signs:
SetupMove `0xA739D0` only copies forward/left analog when the corresponding
native direction bits exist. Positive-left basis is (-sin yaw, cos yaw).

Analog command magnitude **1 represents maximum run speed**. Registration
`0xCE630` gives `cl_boxmove_speed` wrapper `0x2556790` the description at
`0x1C5A160` describing this convention; built-in generator `0xB79AB0` reads it,
substitutes 1 when zero, and writes it unchanged to base+0x58 at `0xB79C9C`.
Independently, parameter checking at `0x8C3C2B..0x8C3C52` loads effective speed
from move-data+0x120 and multiplies forward/left/up axes by it before AirMove.
The solver's unit-circle analog output therefore requests full wish speed.

## Physics and state

Local schema plus machine code: pawn flags+0x3F4, absolute velocity+0x3F8,
actual MoveType+0x526, water level float+0x528, movement services pointer+0x1330;
services surface friction+0x26C and maxspeed+0x1AC. Enum metadata establishes
WALK=2, LADDER=9, NOCLIP=7, OBSERVER=8. Flags ONGROUND=1, WATERJUMP=4, FROZEN=32.
Water accessor `0x21CE80` maps normalized water>=0.5 to swimming; assistance
preserves dead, frozen, waterjump, swimming and non-WALK modes.

Runtime cvar wrapper+8 points to storage; storage+0x58 contains the float/bool
value. Getter `0x1607A0` directly performs this read. Wrappers: air cap `0x2529140`,
air acceleration `0x255BB40`, jump penalty `0x2529130`, server autobhop `0x25292E8`,
jump precision `0x25291D0`. Registered defaults are provenance, never fallback
settings. No cvar is written. Tick duration helpers `0xA550D0/0xA3F800` use the
float at `0x1AC0064`; read it rather than a render-thread frametime.

Verified helper `float( pawn* )` at `0x8CD030` returns effective movement speed
using current `sv_maxspeed`, services limit and active weapon's virtual speed
getter (slot 372). Caller `0x8A127C` passes pawn in RCX and consumes XMM0.
The helper reads no prediction clock directly. It is called only by the engine
command callback, after build/pawn checks, with an exception guard. This avoids
recreating weapon layouts or assuming one weapon's speed limit. Parameter
checking `0x8C3590` also modifies the substep speed. The implementation copies
the observed surface speed factor (services+0x278 -> index+0x10, manager
`0x2544E00` -> rows+0x28 -> index*32, getter `0x9E26A0`) and stamina at
services+0x694, applying `clamp(1-stamina/scale,0,1)^2` with scale read from
`0x1AC0098`. Exact per-substep walk/duck/firing transitions are not mirrored.

AirMove `0x8E48F0` combines forward/left vectors and caps wish speed by the
effective limit. Accelerator `0x8BEB70` computes
`min(airaccelerate * wishspeed * friction * dt, cap - dot(velocity, wishdir))`
when the second term is positive. Its half-step split is recombined at `0x8E0A80`.
For collision-free final velocity under the supplied current-speed model, the maximum-gain projection is
`max(0, cap - availableAcceleration)`. The two symmetric directions allow
steering toward camera/WASD intent without changing camera angles. Revision 7
uses that model for full-input gain and useful-turn projections. Its policy and
command replay changes are documented below; no additional engine ABI or offset
is assumed.

Modern jump helper `0x8C2630` updates actual press time even when the current
`sv_jump_spam_penalty_time` rejects that press. Revised requests remain pending
across creation callbacks until takeoff or a bounded failed attempt; release
and retry follow actual simulation progress. Legacy `0x8C24C0` similarly
retains old press state until its cooldown expires. Ground command presses do
not prove perfect fractional landing time; that requires live observation.

## October 13 movement revision

Modern state at services+0x6C8 stores actual press, usable press and landing
tick/fraction pairs at +0x10/+0x18/+0x20. Runtime legacy selector wrapper is
`0x2529200`; its storage-pointer slot is `0x2529208`.
`sv_bhop_time_window` wrapper is `0x2529040`. Modern jump compares
the usable press to landing within half that window. A retained usable stamp
does not prove takeoff and can survive an obstructed attempt.

Controller TickBase+0x6B8 is uint32 (getter `0x9433E0`, setter `0x965020`). Runner
`0xA719A0` increments it at `0xA71AB9`, then uses it to establish the simulation
clock before movement execution at `0xA71C0A`. Pending/release timeout uses
relative copied differences, resetting on controller identity or rollback.
It never subtracts modern world-group-converted timestamps from creation ticks.

Producer `0xB65677..0xB656AC` reuses a pooled subtick if size<allocated count;
otherwise allocator `void*(arena*)` at `0x4F7120` constructs size0x38 and generic
`void*(repeated*, step*)` at `0x122D140` appends it with matching arena ownership.
The revision follows that path, preflights headers/pointer pages, and clears
rolled-back payloads. It never substitutes a DLL-owned heap message.

Event decoding `0xA73E33` selects digital button processing when raw button!=0;
otherwise it copies analog deltas. Therefore jump and analog are separate
messages. At when0, `0xA64D8D` skips an empty integration slice but processes
the event. `0xA65096/0xA6509D` add analog deltas to historical effective axes;
the sentinel slice then writes those axes at `0xA64DAB/0xA64DB1` before normalized
scaling and air movement. Delta is desired minus historical axes, not desired
minus current base fields. No synthetic direction steps are added because
those would digitally add another +/-1. The final base/native values are ingested
after integration, establishing the next command's initial state.

Forwarding ingestion callback `void(services*, command*)` at `0xA764D0` is called
through derived slot+0xF0 thunk `0x8E8650` from `0xA7030A`, with RCX/RDX and
ignored return. It copies native states to services+0x58/0x60/0x68 and analogs to
services+0x1A0/0x1A4. `MOVE_CONSUMED` observes those values after original
ingestion; it does not claim to observe the final collision response or server
receipt. Full revision captures live under `logs/jump-revision/` and
`logs/command-revision/`.

## Revision 3: disabled jumps and callback cost

The revision 2 live log `logs/overlay-39840-134363788528589904.log` showed
`presses=0` and grounded `movement physics unavailable` while held Space.
The reader already added wrapper+8, but its LegacyJump argument was the pointer
slot `0x2529208`, causing it to read the next wrapper's registry handle. Cvar
constructor `0x19A6C60` places that handle at wrapper+0 and storage at wrapper+8.
Registration LEA RCX at `0xB2377` resolves to wrapper start `0x2529200`;
LEA RDX at `0xB2370` resolves to the name `sv_legacy_jump`. Independent selectors
MOV RAX at `0x8DE86D` and `0x8BF476` resolve to pointer slot `0x2529208`, then
compare byte[storage+0x58]. The corrected source now agrees with both paths.

The verifier derives wrapper starts and names for LegacyJump, BhopWindow,
AutoBunnyhop and JumpPenalty, then checks the source getter offsets against
`0x1607A0`. Passing `--header` with a copy containing LegacyJump=`0x2529208`
fails even though the installed client hash is unchanged. The memory test also
exercises the production Cvar accessor with a valid wrapper and mistaken slot.
Evidence is retained in `logs/jump-disabled/`.

The user reported airborne FPS drops, but the preceding live log has no
movement callback timing. Revision 3 removes the steering scan/refinement,
replaces per-field ReadProcessMemory with guarded local copies, and queues
movement diagnostics for bounded render-thread draining. Each queued record
retains its origin thread and creation uptime. Forced per-line file durability
flushing was removed; cooperative stop flushes the log. This removes file I/O
from the command callback, while render-thread writes remain. `MOVE_PERF`
measures assistance work including lock acquisition, excluding the original
engine finalizer and rendering, so live cost can be distinguished from solver
microbenchmarks.

MSVC x64 Release comparison on an identical seven-case corpus measured solver
median 11.286969 microseconds before and 0.988894 after. Revised work has a
deterministic bound of 13 candidate evaluations; 12 were observed in that
corpus. The benchmark counter is compiled only into its test executable, not
the DLL. Twenty-one steering checks include independently integrated circular
and release trajectories plus large-impulse/tiny-cap cases. All nine CTest
executables pass for both menu builds. These are standalone checks, not live
jump/FPS validation. Comparison evidence is in `logs/steering-perf/`.

Full local disassembly and read-only investigation scripts are retained under
`logs/input-investigation/` and `logs/physics-investigation/`. These diagnostic
paths are ignored by Git; this summary and the verification tool are tracked.

## WASD revision and live stall trace

The user reported nearly flawless jumping but poor steering and severe airborne
FPS loss with revision 3. Its PID 20344 log recorded air callback means around
11–12 ms. Revision 4 replaced circle/recovery behavior with camera-relative WASD
heading and gain: turn toward the requested heading, then use the two mirrored
maximum-gain impulses to build speed near that heading. No entry-speed ceiling
remains. The gain envelope is 15 degrees and candidate work is bounded by two.
No net movement input returns no steering plan; no autostop is added.
Twenty-five independent acceleration/trajectory checks exercise all eight
directions, low-speed recovery, reversals, camera changes and extreme rules.

Revision 4 live log `logs/overlay-15980-134363808628744714.log` confirmed a
performance cost outside the math. Weighted means: layout preflight 3957 µs,
subticks 425 µs, history 370 µs, append 290 µs, apply 286 µs; engine speed 6.22 µs and
solver 3.05 µs. Air callback mean 5866 µs. Stage counts differ because layout also
runs grounded; these do not establish an exact per-air percentage. All command
preflight paths repeatedly used VirtualQuery. Revision 5 removes those kernel
queries and uses guarded byte-preserving writable probes solely on command
storage owned by the callback. It retains guarded Apply and stale-source checks,
and does not cache page permissions. Tests exercise immutable, inaccessible and
cross-page storage while verifying preserved bytes. Standalone API and machine
code investigation is retained in `logs/air-stall/`.

That run exposed a latent duplicate-command bug as well. Producer
`0xB65970..0xB659CB` rebuilds native buttons from physical input through
`0x4C0D20` before calling the finalizer at `0xB659F3`, even when reusing a
sequence. Skipping the duplicate therefore allows held physical Space to
overwrite a synthetic release. The prior PID 20344 log had 75 grounded press requests and
74 takeoffs with no grounded failed-release transitions; PID 15980 had 20 requests,
11 takeoffs and 9 failed releases with unchanged actual stamps and engineDown=1.
Those copied counts support the diagnosis; they are not independent server
confirmation. Revision 5 repairs a rebuilt duplicate using the cached plan
without advancing jump/steering state. It reuses only exact matching generated
pointer/payload events, recalculates analog deltas from current historical input,
and preserves unrelated button/camera/action data. Lifecycle/settings/rollback
invalidate replay eligibility. Fresh live verification remains necessary.
Public and diagnostic x64 Release builds pass all ten CTest suites. Seventeen
replay checks include 10,000-frame physical-button rebuilds, event retention/loss,
pool reuse for unrelated actions, fresh historical deltas and cache invalidation.

## Revision 6: prediction-stable input and ground handoff

The PID 14292 log contained 164 of 625 native commands with opposite assisted
jump edges during rebuilds; 163 flipped with unchanged simulation tick. For
example, sequence 1443 selected a ground press, predicted a takeoff, and replaced
its own press with a release. Sequence 1444 alternated press/release/press while
the engine stamp rewound. Ground is an output of predicting that command;
it must not invalidate the selected input. Same sequence/base/tick now retains
the cached plan despite ground changes. Source, settings, held-input, command
tick and simulation rollback checks remain. Analog deltas still use fresh
historical axes. New regressions exercise 10,000 ground/air prediction rewinds
without changing the original press or release.

On the first grounded command after assisted air movement, physical base analog
values alone arrive too late for the first integration slice. The handoff now
emits the owned analog transition to physical axes and aligns native direction
bits, preserving jump/attack/camera data. It performs no ground steering or
autostop. The pending planner can release a processed request earlier after a
further grounded simulation tick without a new buffer signal, then retries only
after engine release and strictly elapsed cooldown from observed processing.
Actual/usable stamps are never subtracted from the simulation clock.

Modern helper `void(state*, moveData*)` at `0x8C2630` is called at `0x8DE892`:
`0x8DE874` passes move-data in RDX; `0x8DE88B` passes services+0x6C8 in RCX;
the return is ignored. State+8 points to services; services+0x38 points to pawn.
Move-data integration velocity is XYZ at +0x38/+0x3C/+0x40, raw tick +0xD4,
fraction +0xDC. Jump clears live pawn flag +0x3F4 through the ground-reset path.
The observer verifies local pawn/controller full identities and owner links,
copies these fields before/after original execution, and enqueues diagnostics.
It does not mutate engine state or retain pawn pointers. Disabled observation
still forwards through retained code. The installed-binary verifier checks the
observer prefix, caller and source field offsets independently.

An invalid usable stamp is not a rejection marker: successful helper `0x8E6FB0`
clears usable at `0x8E737C`. Its near-landing path can restore stored horizontal
velocity. Separately, ground friction `0x8CB090` runs from grounded walk after
jump dispatch, while successful same-slice takeoff bypasses it. When the live
`sv_enablebunnyhopping` setting is false (wrapper `0x25292D8`, registration
`0xB1D27`), jump helper `0x8DBD80` limits full velocity to services maxspeed×1.1.
No setting is written by the revision; these paths explain why ingestion alone
cannot diagnose landing speed loss.

The user reported perfect bunnyhop behavior in the subsequent offline run.
PID 32264 snapshot: 0/442 commands with opposite jump outputs, 176 qualifying
held-input landings (139 at one command tick, 37 at two), observed dwell median
16 ms versus earlier 94 ms, median landing-to-takeoff speed change zero. The
observer recorded 432 ground-clear executions across 221 distinct jump ticks;
duplicates represent repeated client prediction, not independent server jumps.
Both x64 Release menu builds pass all ten suites, including 39 jump checks and
19 replay checks. The subsequent steering request is implemented in revision 7.

## Revision 7: aggressive directional gain and fresh replay intent

The previous turning branch held velocity magnitude exactly constant whenever
heading error exceeded 15 degrees. That is a zero-gain policy, including during
continuous circles. Its smaller corrections could reduce analog amplitude to
fit that fixed envelope. The replay branch also ran before physical keys and
view yaw were sampled, retaining stale analog input across changed steering in
the same sequence/base/tick. Jump stability does not require stale steering.

The new policy derives both directions from the verified accelerator above.
Let `S=length(v)`, `A=acceleration*wishSpeed*friction*dt`,
`C=min(wishSpeedCap,wishSpeed)`, `q=dot(v,wishdir)`, and
`a=min(A,max(0,C-q))`. A full-input impulse changes squared speed by
`2*a*q+a*a`. Maximum gain uses `qGain=min(S,max(0,C-A))`.
Near intent, select its mirror toward the desired heading. Large corrections
maximize heading change while retaining half the maximum squared-speed gain:
the full-impulse branch solves `q=(requiredGain-A*A)/(2*A)`, and the
cap-limited branch solves `q=-sqrt(C*C-requiredGain)`. Choose the applicable
branch and clamp to the feasible `[-S,qGain]` interval. If intent lies between
the gain and turn heading changes, sixteen bounded scalar bisection iterations
find its projection. All active outputs retain unit analog magnitude; there is
one final candidate simulation and no angle scan. Weak acceleration still
constrains turn authority; no cvar, speed, camera or velocity is written.

This is an independently derived turn/gain policy. Background primary sources:
[Valve's Source air accelerator](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp)
and a [public CS2 directional autostrafer](https://github.com/cacamelio/weave-v3-csgo-and-cs2/blob/main/cs2/counterstrike2/feature/movement/movement_.cpp).
The latter demonstrates eight-direction intent, newest-opposing-key arbitration
and velocity-relative correction. Its source is not copied, and no proprietary
Neverlose implementation is claimed. Installed CS2 code remains the authority
for physics, layouts and integration.

Physical keys and yaw are now sampled before replay. Changed steering uses the
frozen starting velocity, physics and planner state for that command, updates
only analog/direction input, and retains the selected jump edge exactly. Stable
input reuses its plan without advancing solver state again. Newest observed
opposing key presses win independently per axis; simultaneous first presses
are neutral. Source/focus/settings/Space and command rollback reset priority.
Airborne release returns historical assisted axes to physical input through the
existing owned time-zero analog transition. It does not apply a braking impulse.

The standalone before/after harness under `logs/revision7-comparison/` compiles
the actual revision 6 solver from HEAD alongside revision 7, then independently
integrates their command analogs. Synthetic physics: wish speed 250, cap 30,
acceleration 12, friction 1, dt 1/64; initial speed 250, 128 commands, no ground
or collision response. Rotating a held diagonal at +/-360 degrees/second gave
392.810 -> 415.649 final speed and 7.277 -> 1.002 degrees mean heading error.
The new solver gained on all 82 correction commands; the old solver gained on
none of its 26 correction commands. Cycling eight WASD headings every eight
commands gave 352.987 -> 379.340 final speed, with maximum end-of-block heading
error 5.665 -> 5.370 degrees. Both signs were mirrored.

At +/-500 degrees/second, the new solver's growing speed eventually exceeds
the available turning rate: final speed 377.991, maximum lag 20.073 degrees.
Preserving gain cannot provide arbitrary turn rate at arbitrary speed. These
results are model comparisons, not client/server observations. Fractional
prediction, collisions, ground rules and exact substep modifiers remain the
limitations described above; the user must verify revision 7 in offline play.

## Revision 8: unrestricted capture and actual timed direction changes

Revision 7 is preserved in commit `4f0a1aa`. The user's subsequent offline test
in `logs/overlay-18352-134363851916287038.log` reports better control but timid
low-speed movement, weak acceleration and slow reversals. The log confirms
cap 30, acceleration 12, dt 1/64, mostly friction 1, and effective speed 240/250.
All 19 active sampled plans request unit analog magnitude and predict positive
gain; several copied flight observations nevertheless lose speed. These snapshots
do not distinguish collisions, fractional modifiers and prediction correction.
All 11 recorded ingestion mismatches occur at/after Space release, so they do not
establish an active-assistance command ingestion failure. Air callback means
remain in the tens of microseconds, rather than earlier millisecond stalls.

The old half-gain policy deliberately excluded the fastest capture impulse.
For speed `S` and available impulse `0<A<S`, the tangent projection `q=-A`
maximizes one-fragment heading change: `asin(A/S)`, with retained speed
`sqrt(S*S-A*A)`. This can sacrifice substantial speed near low `S`. Revision 8
uses that direction for large forward-half-plane corrections, with bounded
projection bisection for reachable intermediate angles. Behind-velocity intent
instead spends the full permitted impulse toward the desired axis, canceling
old momentum rather than following a wide reversal orbit. At zero speed, normal
desired-direction acceleration resumes. Exact opposite requests do not depend
on the unstable sign of wrapped +/-180-degree error.

Static air-acceleration evidence (`logs/physics-investigation/air-accelerate.txt`)
shows the half-step split applies part of one capped impulse and defers its
remaining part to the accumulator at move-data+0x114. The recombination therefore
still produces `min(A,max(0,C-q))` for one held wish direction; subdividing time
without changing direction does not increase gain. The new command planner
recalculates direction eight times against its predicted intermediate velocity.
Each fragment uses `dt/8`, and its actual added speed is bounded by that fragment's
available acceleration. No fragment is given a full tick's budget.

Runtime emits eight analog events at normalized `when=0,1/8,...,7/8`; the final
sentinel completes the last slice. The already verified event decoder and runner
use `when` to integrate before consuming the next event. The first delta is from
fresh historical axes; subsequent deltas are from the preceding generated axes.
Only the last axes are written to final base/native fields. A separate jump event
retains its existing time-zero behavior. No new engine hook, layout, signature,
cvar write or camera/model-angle mutation is introduced.

Exact pointer/payload ownership expands to eight analog slots plus jump. All
transitions and capacity are checked before appending; owned payloads are reused
on rebuild. Changed intent replans from the frozen original velocity/physics/state
for that command, while stable intent reuses the complete plan and both preserve
the selected jump edge. Ground/release handoff writes one physical-axis event and
neutralizes the remaining owned events. Original camera/action events retain
their relative order, including equal-time actions. A bounded stable merge places
generated times into that stream; nonmonotone original times are rejected before
new arena allocations. Failure restores pointer order and clears borrowed slots.

The comparison harness under `logs/revision8-comparison/` compiles the actual
`4f0a1aa` solver beside revision 8 and independently integrates emitted analogs
over their real modeled durations. Under synthetic wish speed 250, cap 30,
acceleration 12, friction 1 and dt 1/64:

| Scenario | Revision 7 | Revision 8 |
| --- | --- | --- |
| Speed-250 W-to-S capture within 5 degrees | 17 commands / 266ms | 6 commands / 94ms |
| Speed-30 W-to-S capture within 5 degrees | 32 commands | 1 command |
| Held W from speed 250, 128 commands | 421.543 speed | 593.662 speed |
| Diagonal circle at 90 degrees/sec, final speed / max lag | 421.113 / 5.569 degrees | 572.958 / 1.261 degrees |
| Diagonal circle at 500 degrees/sec, final speed / max lag | 377.992 / 20.074 degrees | 318.075 / 1.172 degrees |

The fast-circle speed reduction is an intentional direction-capture tradeoff.
The table is model output; the engine can subdivide our fragments further at
original camera/action times. Exact per-substep camera, duck/walk/fire modifiers,
collision response and prediction correction are not mirrored. The revision
requires a fresh offline test to establish actual gain and user-perceived control.

Both public and diagnostic x64 Release builds pass all ten CTest suites (29
steering checks and 49 replay checks). The timed independent integrator confirms
that eight fragments share one command's acceleration budget. At initial speed
250, the first command's squared-speed gain is 2268.238 versus 900 for one held
wish direction, within the unconstrained eight-fragment ceiling of 2537.842.
Tests also cover dense maximum-turn comparison, low-speed capture/recovery,
direct reversal, exact zero-velocity stop/restart, timestamp merge preservation,
lost/reused pool entries and 10,000 same-command rebuilds without growing beyond
eight analog messages plus the jump edge. The exact-stop regression exposed
float-overload `atan2` roundoff at pi; desired/velocity angle calculations now
use double inputs and zero velocity does not advertise an attained heading.
The installed-binary verifier passes with the existing layouts and bytes.
