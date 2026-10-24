# Server spread-seed audit, Steam build 26144732

Investigated October 24, 2026, using installed AMD64 files only. No game process,
server connection, native function invocation, or configuration change was used.
`python tools/inspect_server_spread_seed.py` verifies complete module hashes,
frozen instruction windows, independently decoded producer/consumer operands,
named imports, schema records and named movement-service lifecycle slots.
This standalone investigation does not add a client compatibility component.

| Module | SHA-256 |
| --- | --- |
| server.dll | `2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c` |
| client.dll | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` |

Game root is `C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game`;
both modules are under `csgo/bin/win64/`. Addresses below are RVAs in these exact
files. Research and replay artifacts are in
`logs/research/26144732/spread_remote_audit/native/`.

## Server seed selection differs from the client helper

The client helper `0xD24D70` hashes twelve bytes containing quantized float32
pitch/yaw and int32 shot tick. The server counterpart `0x2AB530` first loads
`int32[0x1DAEE64]` and compares it with `int32[0x1DAEE60]` at
`0x2AB53D..0x2AB54F`. If unequal, it returns the former value immediately.
If equal, it computes the same twelve-byte angle/tick SHA1 model. The server
quantizer `0x2A5720` normalizes, doubles, floors and halves the angle, matching
the client structure. The named tier0 SHA1 Reset/Update/Final imports verify the
algorithm and payload lengths independently.

Both installed server shot callers (`0x9FE824`, `0xA05671`) call this selector,
pass its result into `0x2AB5F0`, then the distribution path loads that seed from
its argument (`0x2AB9C2`), increments it at `0x2ABA75`, forwards R9D at
`0x2ABB5E`, and calls distribution `0x2AC060` at `0x2ABBA0`. The distribution
calls the named tier0 `RandomSeed` import at `0x2AC0E4`. Thus the alternate
server seed can affect actual shot distribution; it is not merely debug data.

## Independent server command-seed producer

Registration `0xD9F30..0xD9FD5` names `sv_usercmd_custom_random_seed` via
`0xD9FB0`, with this exact description: "When enabled server will populate an
additional random seed independent of the client". Its wrapper is `0x2195408`;
the callback's `0xC84896` load reads wrapper+8. The registration passes a boolean
creation block whose default byte at block+4 is one (`0xD9F94`), with raw
registration flags `0x80000`. This establishes the registered default, not the
active value of any session or another server installation.

`CCSPlayer_MovementServices`' independently identified RTTI vtable `0x19297A0`
contains the command-seed callback `0xC84840` at slot byte offset `0x150`.
The command runner calls that slot at `0xC82204` before command execution,
then calls slot `0x158` at `0xC823AE` afterward.

The callback resolves the movement service's pawn controller using its verified
`CBasePlayerPawn::m_hController` field (`+0xC98`), and requires the controller's
`CBasePlayerController::m_bPredict` (`+0x5AD`). These names and offsets come from
server schema registration `0x1DE47D8` and `0x1E1E3E8`, respectively. This is
**not an established local/listen/remote discriminator**. Its writer
`0xB6A5CE..0xB6A60E` reads the `cl_predict` user-info string when applicable.

The callback takes the command's ordinary seed, calls setter `0x3F2130` to write
`0x1DAEE60`, and tests the live custom-seed Cvar. When enabled it calls named
`Plat_FloatTime` and `RandomInt(0, INT_MAX)`, hashes a sixteen-byte payload
containing a time-derived double, random int32 and zero int32, then reads the
first digest DWORD. Setter `0x3F2140` writes it to `0x1DAEE64`. When disabled,
the same setter receives the ordinary seed, making the two globals equal.
The server shot helper's selector uses these exact two independently decoded
setter destinations.

## Synchronization status is a boolean, not the seed

The derived end callback `0xAC9DC0` calls `0xAA3950` before base seed cleanup.
`0xAA3968..0xAA398E` compares the same two globals and updates
`CCSPlayerController::m_bFireBulletsSeedSynchronized` to their equality.
Server schema registration `0x1E1A328` places this member at `+0xA39`; client
registration `0x2222498` places the corresponding member at `+0x97C`.
The client has a consumer at `0x883C06`, but its meaning beyond that boolean
read has not been traced here. No server seed value replication was established.
The client SHA helper itself has no input from the independent server producer.
Do not infer an engine prediction contract or add a feature gate from this
field's name alone.

## Observation limits and supplied-log evidence

The inspected server registers a separate random-seed mode. If that branch is
active, client angle/tick hash searches do not determine its independently
generated seed. The active server build, Cvar value and `m_bPredict` flag were
not observed here; this audit does not show that the branch ran in the supplied
log. The evidence does not establish a universal listen-versus-dedicated split.

The later corrected Scout shots in
`logs/overlay-10152-134372468703772196.log` show raw indexed history two to five
ticks ahead of the adapter's clamped clock. Independent pure replay of the
saved client seeds and rays makes this a separate concrete timing problem:
seventeen shots in the second clock epoch have median best current/next model
error about 5.17 degrees, versus about 0.64 degrees using their raw history
integer tick and the same copied punch/accuracy. Thirteen of those seventeen
best-fit raw history among the inspected nearby ticks. Reconstructed punch is
validated against both saved seed/prediction pairs; this is diagnostic replay,
not proof of a server seed or a supported future-punch API. Some shots still
have unexplained differences, and old clamped-time punch can itself be stale.

The server-only path is a verified model limitation. It is not a proven cause
of this log's misses, and it does not justify inventing a client-only remote
no-spread guarantee.
