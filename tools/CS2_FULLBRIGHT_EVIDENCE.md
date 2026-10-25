# Native fullbright render-context parameter

## User trial — October 25

The user reports no visible change when toggling Fullbright. Session
`logs/overlay-19080-134374152043395173.log` records the real D3D11 callback
and thousands of modified native Fullbright inputs, so hook execution alone
does not establish effective rendering. The downstream native capability and
render-mode path remain under investigation. Custom skyboxes are independently
confirmed working after their ownership repair.

Investigated October 25, 2026, for the Windows AMD64 CS2 **client**,
1.41.9.2 / client 2000935 / Steam build 26144732. Read-only installed path:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll`.
SHA-256 `e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29`,
PE timestamp `0x6ADA3418`, image size `0xA15000`. This extends the existing
D3D11 overlay without another graphics dependency. No process was opened or
client/server launched during this investigation; no new runtime/API observation
or visible appearance is claimed. The client-specific producer and render-mode
consumer also require the same installation's `game/csgo/bin/win64/client.dll`,
SHA-256 `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
timestamp `0x6ADA7340`, image size `0x29A0000`.

## Independently decoded native contract

The registration at `0x19E64` resolves the exact `mat_fullbright` string at
`0x623F28`; the following RCX LEA at `0x19E6B` selects wrapper `0x9BAFB0`.
The native context builder loads that wrapper's storage pointer (`+8`) at
`0x24331E`, reads its **32-bit integer** at `+0x58` at `0x243333` and saves it
to `RBP+0x1158`. A native debug branch can replace that local with 1. The
local is loaded into R8D at `0x24340C`; EDX receives key `0xC6767B23` at
`0x243413`; RCX receives the current context's integer collection at
`0x243418`; the direct call at `0x24341B` targets `0x2485A0`, returning to
`0x243420`. These are independent native registration and consumer operands,
not addresses taken from an internet offset list.

The complete `0x2485A0..0x2487BA` helper copies RCX to RBX, EDX to ESI and
R8D to EDI. It compares the uint32 key against the collection's inline groups,
walks its overflow nodes when needed, then writes the integer value. When
there is no vacant slot, it allocates a native 0x70-byte overflow node through
the allocator, initializes and links it, then inserts the same key/value.
There are no engine render, job dispatch or wait calls in this helper. Native
callers ignore its return registers; the wrapper uses a void result.

The client has its own render-context writer; overriding only the engine
producer would leave this important path unhandled. Client registration
`0x10883A` names the same `mat_fullbright` and selects wrapper `0x25CC3C8`.
The value-read at `0xE5AFC8` resolves its storage pointer `+8`, reads int
`+0x58` at `0xE5AFEE`, stores `RBP+0x1700`, then loads R8D from that local
at `0xE5B0CC`. The same key is loaded at `0xE5B0DA`, followed by a call
at `0xE5B0DF` to client integer writer `0x16A9D0` (return `0xE5B0E4`).
The auxiliary producer `0x7996FA..0x799718` reads another independently named
`mat_fullbright` wrapper, `0x24C0A98`, selected by registration `0x92CDA`,
and calls the same writer. Other explicit fixed-mode client writers are
forwarded unchanged.

The complete client writer spans `0x16A9D0..0x16ABDE` across native unwind
fragments. It has the same RCX context, EDX key and R8D integer ABI. The
overflow path calls allocator thunk `0xC32810` (native allocator slot +8)
and pure three-vector zero initializer `0x15A360`; neither introduces a
render or job wait. It preserves native allocation and collection ownership.

The native client render-mode consumer independently queries key `0xC6767B23`
at `0x12035E6` and saves the returned integer in ESI. Nonzero enters the
native tool-mode parameter branch. At `0x12037AD`, ESI becomes the native
output integer under key `0x50E7F81D`; other rendering toggles retain their
own native calculations. That consumer retains the engine's
`Engine2/RenderingPipeline/ToolsVisModes` capability branch, identified by its
literal string reference at `0x12035BD`. The implementation changes the
same cvar-backed render input that reaches this native consumer rather than
inventing a separate shader switch. This does not establish appearance on
every shader/material, and native capability checks still apply.

Full disassembly was saved under `logs/research/fullbright_20261025/` for the
registration, integer setters, client render-mode consumer and producer scopes.
`verify_fullbright.py` freezes their complete code hashes and exact installed module hashes, decodes the cvar
name/wrapper/value chain and setter target/key, and compares all runtime header
constants and byte arrays to the installed operands. Runtime prepare verifies
AMD64 identities and both setters, producers, value-read and registration windows.

## Mutation, ownership and teardown

The detour forwards the original collection, key and integer unchanged except
for the three exact return address/key pairs with `fullbrightEnabled` enabled. That
case passes 1, the native `mat_fullbright 1` render-context value, to the native
setter. It executes on the native producer's own thread. It does not cache a
context or entity pointer, change cvar values/flags, submit console commands,
edit game files or alter lighting settings. The feature is independent of the
other lighting controls.

The next native context uses the current native value when disabled. Stop
disarms before taking the setter barrier and waits for any modified setter
already executing. Previously submitted GPU frames can finish; future contexts
forward native values. There is no persistent source mutation to restore.
Hooks, trampolines and the DLL remain resident until normal process exit;
stopped does not mean physically unloaded.

`world_fullbright_checks` substitutes only the native setter boundary and
checks exact collection/key/value forwarding, unrelated call sites, independent
lighting controls, reset/disable, repeated frames, stopped reactivation and
draining an in-flight modified writer. These are static and standalone checks,
not proof of live fullbright appearance. The native exposure/tonemap system
and map/material-specific behavior remain in effect.

The initial engine-only isolated MSVC 19.42 x64 Release fixture built without
project warnings and passed **1,034 checks**; the expanded client-writer
fixture is included in the parent integration build. The installed-file verifier passes. Temporary
header changes to both timestamps, both caller returns, auxiliary caller,
client setter, parameter key and setter fingerprint all reject (**8/8**);
results are retained in
`logs/research/fullbright_20261025/negative.json`. Integrated DLL builds and
the combined compatibility run are reported by the parent integration.
