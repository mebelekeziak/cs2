# Native third-person view and hull contracts

## Build 2000934 recertification — October 21, 2026

Rechecked the installed Windows AMD64 **client**, patch `1.41.9.1`,
client/server `2000934`, Steam build `26087496`, Source revision `11150591`.
`csgo/bin/win64/client.dll` changed from SHA-256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` to
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
its PE timestamp changed `0x6AD01314 -> 0x6AD7B41C`, with AMD64 and
SizeOfImage `0x299D000` unchanged. Installation and executable paths remain
as documented below. This was file-only investigation: no client/server
launch, process access, graphics-runtime observation or installed-file change.

All eight frozen entry/call checks and seven complete native windows match
byte-for-byte. Independent RTTI/vtable slots, native input constructor/pointer,
view member operands, 9-unit hull constant and source fingerprints pass without
any runtime constant changes. The Microsoft x64 argument/return contracts,
current-thread stack ownership and stop forwarding remain unchanged. Only the
verifier's exact client hash pin changed; an unfamiliar hash still rejects.

Reproduce with `python tools/inspect_third_person.py`. The fresh real-header
report is `logs/research/26087496/view_model/verified_third_person.json`.
Temporary wrong `InputPointer`, `ViewOrigin` and `OverrideViewBytes` headers
all reject; results are in that directory's `negative_checks.json`.

No fresh live appearance is claimed. Build/CTest integration results are recorded
in the project update handoff and README. The earlier investigation below is
historical provenance; its old module identity is superseded above.

## Earlier baseline investigation

Investigated October 16, 2026 against the installed Windows AMD64 CS2 client:

- Game root: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
- Client SHA-256: `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
- Runtime contract header: [third_person_offsets.h](../include/third_person_offsets.h).
- Static, standard-library verifier: `python tools/inspect_third_person.py`.

This is installed-binary evidence. No gameplay appearance, wall response or
camera smoothness was observed during this investigation. The ordinary
compatibility bundle is `logs/compatibility/20261016T153701136178Z/`.

## Native view application

RTTI identifies `ClientModeShared` primary vtable `0x1C68368`, whose slot 16
contains `0xB99250`. `ClientModeCSNormal` primary vtable `0x1C9FC20` uses
`0xD02E30` in the same slot. The game-specific override calls the shared
implementation at `0xD02E3A` before its later camera effects. Both implementations
have ABI `void(mode*, view*)`.

The shared implementation calls mode vtable byte slot `+0x100`. Both verified
mode tables point that slot to `0xB96570`, whose complete body is:

```text
mov eax, [rcx+0x28]
mov [rdx], eax
mov rax, rdx
ret
```

Thus mode `+0x28` is the current integer split-screen slot. The native base view
only proceeds for slot zero (`0xB99270..0xB99274`). It resolves the local pawn
using `0x96A8F0(int slot) -> pawn*`; that helper checks the controller pawn handle
and its current entity-list serial, returning null on missing/stale state.
Pawn virtual `+0xA30` is called with the view pointer before camera application.
For the installed `C_CSPlayerPawn` primary vtable `0x1C8A138` this slot targets
`0xB06D40`, a no-op. The current origin and angles already exist in the view;
the callback alone does not establish their earlier physical-input producer.

The shared view applies the native third-person camera to float3 origin
`view+0x4A0`, and writes float3 angles at `view+0x4B8`. It calls `AngleVectors`
`0x1742AF0` on its current camera pitch/yaw, subtracts forward times camera
distance, and optionally applies shoulder cvars. The origin operands are at
`0xB9931A..0xB9934B`; angle stores are at `0xB9948C..0xB99494`.

## Native third-person predicate and visibility

`0xB627A0` is a complete 19-byte native predicate, ABI
`int(input*, int slot)`. It returns the zero-extended byte at
`input + slot*0x928 + 0x229`. The full body is frozen in the header and verifier.
There are exactly three direct native callers in the installed code:

| Call instruction | Return site | Consumer |
| --- | --- | --- |
| `0xB1BE67` | `0xB1BE6C` | Local-pawn visibility branch, after comparing the entity against the current local pawn |
| `0xB1BF15` | `0xB1BF1A` | Entity-slot checked boolean wrapper |
| `0xB992A4` | `0xB992A9` | Shared native view camera branch |

The native query has no vtable function-pointer references in the installed
image. The camera call loads the input pointer from `client+0x223BD00`;
the on-disk relocated pointer targets embedded `CCSGOInput` at `+0x257B640`,
independently established by constructor LEA `0xEE574`.

The implementation can intercept that predicate for the current valid local
pawn, leaving the exact shared-camera return site on the ordinary first-person
branch. The shared view then supplies its existing native origin/angles for a
post-call camera translation, while native local-player consumers can see a
third-person result. It needs no persistent input-byte or command-angle writes.
Invalid pawn/source state and stop must forward the original predicate.

The ordinary native camera thinker `0xB62A20` is useful evidence, but is not the
chosen runtime entry: it directly reads shared cvars and clears third-person
state under its `sv_cheats` condition at `0xB62AAB..0xB62AD7`. Direct reads also
mean hooking an ordinary cvar getter cannot customize all native camera values.
The implementation must not change that cvar or its flags to obtain a camera.

## Synchronous native camera hull trace

The collision path inside the ordinary native camera thinker is
`0xB63416..0xB63716`. The trace call is at `0xB636F1`, and native code reads
result fraction `+0xAC` at `0xB636F6`. The native camera ignores the trace return
value: false can mean no hit and is not itself a trace error.

| Entry / global | Verified ABI or layout |
| --- | --- |
| `0xA1A000` | `bool(manager*, shape*, const float3* start, const float3* end, filter*, result*)`; six Win64 arguments, last two on stack |
| `client+0x2227058` | Pointer slot loaded at `0xB636C7` for the current trace manager |
| `0x36DDB0` | `void(shape*, const float bounds[6])`; mins then maxs; writes bounds through `+0x17` and hull discriminator `+0x28=2` for nondegenerate bounds |
| Shape storage | `0x30` bytes with 16-byte alignment is sufficient for the consumed shape |
| `0x1779300` | `void(result*)`; initializes the native trace result through `+0xBC`; trace also calls this internally |
| Result storage | `0xC0` bytes with 16-byte alignment; native result occupies `RBP-0x20` before the next shape at `RBP+0xA0` |
| Fraction | Float at result `+0xAC`, initialized to `1.0`; validate finite range before using |
| Filter storage | `0x48` bytes, alignment 8; native scalar deleting destructor `0x205CB0` passes size `0x48` |
| Filter vtable | `0x1AD4838`, entries `0x205CB0` (deleting destructor) and `0x22D900` (returns true) |
| `0xB722A0` | `void(filter*, pawn*, uint64 mask, uint8 layer, uint8 flags37)`; fifth parameter is a low byte in the Win64 stack argument slot |

Native camera hull extents are `(-9,-9,-9)` to `(9,9,9)`, from the original
float constants at `0x2234540..0x2234558`. Each call uses current-thread stack
objects; the synchronous trace returns before those objects expire. No heap
filter deletion is required for a matching stack filter.

The filter baseline follows the thinker constructor stores
`0xB63541..0xB63587`: zero a `0x48` byte buffer, put the relocated vtable at
`+0`, set uint32 fields `+0x20/+0x24/+0x28/+0x2C` to `0xFFFFFFFF`, uint16
`+0x34` to `0xFFFF`, bytes `+0x37=0x0F`, `+0x38=3`, `+0x39=0x49`, and
`+0x40=0`. Zero initialization also clears the native padding and auxiliary
fields. Call the native population helper with mask `0x80`, layer `3` and
flags `0x0F`; then set uint64 filter `+0x10 |= 0xC0000`. These are the native
thinker mask/layer/exclusion settings, not projectile-trace settings.

The helper fills mask `+0x08`, the excluded pawn handle `+0x20`, collision id
`+0x30`, owner handle `+0x28`, flags `+0x37` and layer `+0x38`. It obtains pawn
handle through `0x9DD600`, collision id through `0x9EA740`, and resolves the
owner handle from pawn `+0x520`. Using that helper preserves native exclusion
semantics without duplicating those handle rules. The distinct target-camera
path calls it at `0xB625D1` with a different mask/layer (`0xC3001`, 4); those
values must not be mislabeled as the ordinary thinker settings.

## Verification and limits

The verifier rejects any changed installed image hash, checks native function
bytes, mode RTTI/vtable ownership, decoded origin/angle/slot/global operands,
native input-object identity, header constants and all runtime entry arrays.
It records SHA-256 windows for native query consumers, base view, thinker,
shape initialization, filter population and current local-pawn resolution.

Camera translation should preserve the native view angles and use the current
callback only. Distance/shoulder/height settings can share one traced path from
the current native eye to the desired camera point. This differs from the
native optional shoulder cvar math: it does not add a separate aiming
convergence rotation. Collision must validate its result before publishing a
new origin, and a stopped hook must keep forwarding while its code remains
resident. Static evidence does not establish observed model animation,
visibility in every render pass, or private-session camera appearance.

The integrated runtime publishes only copied pawn/controller/identity/handle
values after a successful view edit. Later visibility queries compare those
values against a fresh live source. Failed traces/views clear that publication;
no retained engine address is dereferenced. Stop disarms new queries and waits
for active view edits, while native calls/trampolines remain resident.

Both public and diagnostic Release builds pass all 24 CTest suites. The camera
suite uses a memory-backed fake client and native callback substitutes; it does
not execute the installed trace code. It covers scoped/nested query decisions,
all trace arguments and stack layouts, full-displacement clipping, invalid
results, source/serial changes, opaque view preservation and a blocking native
callback during cooperative stop. Menu/config checks cover persistence and real
mouse-driven controls. The software D3D11 Camera preview was inspected. All 32
header/entry-array mutations reject; results are under
`logs/research/third-person-header-mutations.json`. The combined twelve-component
bundle is `logs/compatibility/20261016T154931008163Z/`. No live client run was made.
