# Landing hull and jumpbug input contracts — October 23, 2026

Client-only, AMD64 Windows, installed CS2 **1.41.9.2**, client **2000935**,
Steam build **26144732**. The binary is read from
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA256 is `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.
No client/server was launched; installed files were not changed.

## Reference and implementation provenance

The user supplied [Velocity jumpbug.cpp](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/movement/impl/jumpbug.cpp),
blob `e71fbe84a055632b4e0f1af6a13ce12f43108f1e`. Its tracing adapter is
[tracing.cpp](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/systems/impl/tracing.cpp),
blob `bca174ebee9f7d3dee1b7c8a3a586afb7a63e5ea`. This implementation independently
expresses the descending-hull landing probe and timed Duck/Jump transitions;
it does not copy their source text or unverified offsets. No upstream license
grant was identified, so no upstream implementation text was imported.

Velocity's `movement_services + 1592` receiver is **not** the installed client
contract. The local implementation uses the installed native movement bbox
wrapper and verified `services + 0x7B0` receiver. It yields to physical crouch,
including existing timed crouch input, and accepts near-flat ground only
(`normal.z >= max(sv_standable_normal, 0.985)`). This deliberately avoids
Velocity's inconsistent steep-slope bookkeeping, which can insert subtick
steps even after marking its activity flag false.

## Installed native contract

`0x8BE590` constructs the movement-specific trace filter:
`filter*(filter*, pawn*, uint64 mask, int collision_group)`. Native movement
callers supply group **11**, distinct from camera/shot filters. It initializes
through byte `+0x40`; 0x48-byte aligned storage contains its complete object.
The installed filter's native initialization retains normal collision and
entity-skipping behavior; the adapter does not replace it with a shot mask.

`0x8E15D0` is the synchronous bbox wrapper:
`void(cache*, result*, start*, end*, bounds*, movement_filter*)`.
Examples `0x8E7054` / `0x8E708E` and `0x8C3937` / `0x8C3992` show the
`services + 0x7B0` receiver and six arguments. Bounds are six contiguous
floats, minimum then maximum. The wrapper first builds the segment bounds,
checks the native cache, and falls back to `0x8E0F10`. The latter uses native
shape initialization and the existing six-argument trace at `0xA1AE30`, and
handles specialized moving-entity results. The adapter calls the wrapper
synchronously on the command owner and retains no result/filter/cache pointers.

`0x177B560` initializes the 0xC0-byte native result; fraction is at `+0xAC`,
plane normal at `+0x90` (`z +0x98`), and the solid flag is at `+0xBB`.
The ground consumer at `0x8E70E8` checks `0 < fraction < 1`, rejects `+0xBB`,
and compares normal Z against `sv_standable_normal` at `0x8E7108`.
In that consumer, RSP equals RBP-0x100 and the result lives at RSP+0x70,
therefore RBP-0x90. The fraction load at RBP+0x1C resolves to result+0xAC,
while the normal-Z load at RBP+8 resolves to result+0x98. The vector starts
eight bytes before that Z member, at result+0x90.

`0x8E1C90` establishes the walking mask. Flags come from pawn `+0x3F4` and
the mask from pawn `+0xD50`. Bit **0x10 set** includes **0x20**; clear preserves
the mask. Specifically, `0x8E1CAE` reads flags, `0x8E1CB4` shifts by four,
`0x8E1CB7` tests bit zero, and the taken branch sets DL=1. At `0x8E1CD4`
the mask is read; `0x8E1CDE` ORs 0x20; `0x8E1CE4` CMOVE restores the
original mask only if DL=0. The adapter mirrors these installed instructions.

The actual gravity scale is pawn `+0x564`, schema
`C_BaseEntity::m_flActualGravityScale`. Native half-gravity at `0x8E03D7`
multiplies this field by the current gravity rule. Configured
`m_flGravityScale` at `+0x540` is a separate field and is not substituted.
`sv_gravity` wrapper **0x23BF1C0** is resolved from registration `0x6BB0B`;
`sv_standable_normal` wrapper **0x25344A0** from registration `0xB2C48`.
Each uses the already-verified pointer `+8`, float value `+0x58` getter.
The engine interval constant is the existing movement contract **0x1AC8074**.
The projection uses half gravity and a two-unit downward probe extension;
the extension and 0.985 slope cutoff are assistance tuning, not native defaults.

The two-unit extension finds a nearby contact plane; it is not part of the
command's physical displacement. Scheduling its raw trace fraction directly,
as the reference does, would jump early and can schedule contact beyond this
command. With copied normal `n`, unextended displacement `d`, extended
displacement `p`, and native fraction `f`, the adapter instead schedules
`when = f * dot(n,p) / dot(n,d)`. Both dot products must be finite and negative
(approaching the plane), and the converted fraction must lie strictly inside
`(0,1)`, including after conversion to float. Extension-only contacts reject.
The full three-dimensional normal participates, including on oblique near-flat
surfaces. No 0.001/0.99 timing clamp is applied, since shifting an accepted event
changes the predicted contact time. This solves the plane along the linearized
half-gravity segment; native later subdivisions and server simulation can differ.

Existing entity schema verification supplies the scene-node/current absolute
origin, pawn collision pointer and collision minimum/maximum members. These
values are copied, validated, and checked again after native callbacks,
alongside local pawn/services/identity/serial/flags/mask/gravity and cvar values.
The adapter never changes pawn movement fields. This is a current client
snapshot, not proof of the exact authoritative server landing/subtick state.

## Duck input evidence

Native crouch processing starts at `0x8C9CA0`. At **0x8C9D9D** it executes
`MOVZX EBX,byte[services+0x58]`, at **0x8C9DA9** `SHR BL,2`, then
**0x8C9DAC** `AND BL,1`. Therefore Duck is input bit **4** in the already
verified services button state. It is separate from the on-ground flag and
from the pawn's current duck posture.

## Verification and limitations

### Revision 13: native position and crouch ordering

The same installed client hash was rechecked on October 23. Ground
categorization at `0x8C3731` copies XY from `[moveData+0xC8]`; `0x8C3739`
copies Z from `+0xD0`. `inspect_jumpbug.py` independently decodes both operands
for the new read-only observer field. This distinguishes native movement
position from the scene-node origin used by the probe.

The per-slice caller invokes crouch processing at `0x8E0558`, then the modern
jump handler at `0x8E0582`. Finish-duck `0x8CC270..0x8CC53B` adjusts airborne
origin by half the standing/crouched hull size difference. Finish-unduck
`0x8CC9B0..0x8CCC05` uses the opposite factor; the inspected constants are
`0x1AC8080 = 0.5` and `0x1AED240 = -0.5`. They obtain the hull sizes from
the native vector provider, not hardcoded posture dimensions. Both routines
call ground categorization (`0x8C3640`) after moving the origin. The complete
two routines and crouch processor `0x8C9CA0..0x8CA6E7` are now frozen windows.
Disassembly is retained under `logs/research/jumpbug/`.

The observer logs native origin and includes airborne duck edges. This adds no
mutation or hook and does not establish that the producer's predicted contact
matches the subsequent crouch/uncrouch state. See the missed-attempt evidence
and recovery-only correction in `docs/MOVEMENT_REVISION13.md`.

`python tools/inspect_jumpbug.py` checks the installed whole-file identity,
fourteen complete function/consumer instruction windows, independent schema/cvar
operands, crouch bit decoding, and every source offset/fingerprint against
the frozen native contract. `--header` permits altered-source rejection tests.
The full contract is in `tools/jumpbug_contract.json`; investigation scripts
remain under `logs/research/jumpbug/`.

`tests/jumpbug_checks.cpp` supplies native-call fixtures that exercise the real
adapter: exact receiver/filter/hull/projection, initialized aligned storage,
landing/slope/fraction rules, invalid inputs, source changes during native
callbacks, native faults, crouch ownership, and unsupported locomotion. Timing
fixtures independently cover flat/oblique plane conversion, extension-only
contacts, nonapproaching motion, and unclipped early/late command times. These
fixtures and static checks cannot establish live jumpbug timing or effectiveness.
No statement of live client/server behavior is made.
