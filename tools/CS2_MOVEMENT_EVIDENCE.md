# Movement evidence for the installed client

Inspected October 13, 2026. Client AMD64, PE timestamp `0x6AC7FFFA`, image size
`0x2994000`, SHA256
`44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008`.
Installation/version are documented in README. No installed files were changed.
`python tools/inspect_movement.py` verifies the binary hash, relevant function
bytes, callback caller, vtable entry and engine tick constant without launching
or opening CS2. A changed build needs renewed investigation.

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
steering toward camera/WASD intent without changing camera angles. The revised
planner uses the same model to maximize heading change with a retained-speed
target. Continuous A/D turning is velocity relative; release uses heading
recovery with fractional-amplitude correction instead of alternating lateral
speed-gain commands.

Modern jump helper `0x8C2630` updates actual press time even when the current
`sv_jump_spam_penalty_time` rejects that press. Revised requests remain pending
across creation callbacks until takeoff or a bounded failed attempt; release
and retry follow actual simulation progress. Legacy `0x8C24C0` similarly
retains old press state until its cooldown expires. Ground command presses do
not prove perfect fractional landing time; that requires live observation.

## October 13 movement revision

Modern state at services+0x6C8 stores actual press, usable press and landing
tick/fraction pairs at +0x10/+0x18/+0x20. Runtime legacy selector wrapper is
`0x2529208`; `sv_bhop_time_window` wrapper is `0x2529040`. Modern jump compares
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

Full local disassembly and read-only investigation scripts are retained under
`logs/input-investigation/` and `logs/physics-investigation/`. These diagnostic
paths are ignored by Git; this summary and the verification tool are tracked.
