# Player-scoped native PVS visibility contract

Read-only installed-file investigation, October 23, 2026. Target: Windows AMD64
CS2 client patch **1.41.9.2**, Steam build **26144732**, existing D3D11 renderer.
No client/server process was launched or opened; installed files were unchanged.
The strict complete module hashes remain in `verify_player_chams.py` and its
dependencies. This evidence supplements `CS2_PLAYER_CHAMS_EVIDENCE.md`.

## Why material replacement can miss a player

The existing animation descriptor generation callback runs after scene visibility
selection. Replacing a generated packet's depth material cannot restore an object
rejected before generation. ESP entity availability does not establish scene
submission. The user reports ESP remaining visible while chams are absent behind
walls or before first sight; this is consistent with that upstream distinction,
but no live render observation has established a single exhaustive cause.

## Current native PVS predicate

Scene RVA **`0x36840..0x36885`** is a complete, 70-byte leaf function. Microsoft
x64 ABI: RCX is a linked PVS-record list; RDX is the current view's array of
32-bit PVS words. The native function reads each node's count at `+8`, then
index/mask pairs at `+0xC/+0x10` with stride 8. A mask intersection returns
`AL=1`; exhaustion, including a null list, returns `AL=0`. It reads its inputs
and writes only volatile general registers. It has no stack adjustment, calls,
nonvolatile writes, XMM writes, persistent mutation or allocation.

Complete function SHA-256:
`a3ec4468b4d47a449273babf46d5e907219953cef4210b29fe4dc0a174343036`.

There are two directly encoded callers in the current executable code:
`0x3D849` (return `0x3D84E`) and `0xF093C` (return **`0xF0941`**). This is not
a claim that indirect callers cannot exist. Restricting the adapter to the exact
second return address makes all other callers unconditional native passthrough
before inspecting a captured object.

## Exact scene-object provenance and rejection

The CPU culler begins at `0xF0460`. Its work dispatcher at `0xF0282` calls it
on batches claimed through atomic `lock xadd` at `0xF0210/0xF02F1`. The owning
cull orchestration references the installed diagnostic string
`CSceneSystem::Thread_CullView()` at `0xEFBE2`. This is a native scene-worker
path, not an entity-update thread. The adapter must remain read-only toward
entity/skeleton/scene state and use the existing validated borrowed-source
resolution contract.

At `0xF0780..0xF07A9`, RDI selects a native spatial record; its `+0x28` is
loaded into RSI as the actual scene object. At `0xF0922`, RCX is loaded from
**`[RSI+0xA0]`**. The native path requires this pointer and the current view PVS
to be nonnull, and requires object `+0x78` bit 3. At `0xF0939`, RDX receives
the view PVS. The call at `0xF093C` is followed by an `AL` test at `0xF0946`;
false increments the native PVS-reject counter and skips this object.

Independent Capstone 5.0.9 `regs_access()` inspection establishes **no RSI/ESI
write between `0xF07AD` and `0xF093C`**. Intervening calls preserve RSI under
the Microsoft x64 ABI. The exact frozen interval is **`[0xF07A9, 0xF0941)`**, SHA-256
`da433c53cbe05d7b5587df9253224862e2e969251db10200a3b7ddc98c450e1c`.
The explicit half-open interval avoids counting the return-site instruction.

Object `+0x18` is its descriptor, independently consumed by native packet
sorting at `0x10F5FA..0x10F5FD`. The already verified named animation descriptor
vtable and exact skeleton/full-serialized-owner association distinguish a
current player from an animated weapon, attachment, stale pawn or foreign
object. Object `+0xA0` must equal the predicate's incoming RCX.

## Scoped adapter ABI and lifecycle

A stackless MASM entry captures the native caller's current RSI and return
address with `mov r8,rsi; mov r9,[rsp]`, then tail-jump to a normal C++ function
with arguments `(pvsRecords, viewPvs, borrowedSceneObject, callerReturn)`.
This preserves both native arguments, the original return address, stack
alignment, caller-provided shadow space and all nonvolatile registers. The
native trampoline is still invoked with its original two-argument signature.

The adapter first obtains the native result. Native true remains true. Native
false may become true only when the callback is armed, the exact caller matches,
through-wall chams are enabled, and the current object satisfies all existing
player ownership, identity, skeleton, team/local and eligibility checks. Source
validation occurs after the native predicate and under the cooperative mutation
barrier before overriding the result. Other callers or failed source checks
return the original result.

No cvar, PVS list, scene flag, spatial record, view, model or entity field needs
to be changed. No borrowed scene/entity/PVS pointer is cached across calls, and
no restore pass is needed on death, round reset, skeleton replacement or stop.
The object was borrowed from the culler's current native record for this call;
the adapter does not extend its lifetime. Stop disarms the adapter and drains
the existing result-override barrier. Trampolines and stopped code remain
resident until normal process exit, including while a native call may return
through them. Stopped and resident is not physically unloaded.

This changes only the demonstrated PVS rejection. Native frustum, distance,
size/fade, visibility-box, model/mesh and later rendering behavior remain
independent. The batch meshlet GPU culler at `0x7EB00` was investigated but
provides no evidenced per-player D3D11 substitute and is not the chosen hook.
Changing object `+0x9A` bits 2..3 would affect size/fade, not this PVS predicate.

## Reproduction and verification limits

Run `python tools/verify_player_chams.py`. It freezes the complete predicate,
spatial object-load window, caller/boolean-consumer window and full RSI
provenance interval. It independently locates the source and caller in the
culler, decodes the relative call target and return address, decodes the PVS
member displacement, and checks all matching header constants/byte arrays.
No Capstone dependency is needed for ordinary verification.

Scratch discovery, disassembly, exact bytes/hashes and positive/negative
verification results live under `logs/research/chams-scene-culling/`.
Static verification proves the installed contract and rejects changed header
values; it does not prove live player appearance or eliminate other causes of
missing packets.

Public and diagnostic x64 Release builds succeed and each passes **58/58 CTest
suites**, including **195 player chams checks**. The fixture executes the actual
production MASM bridge with a known native-style RSI/caller and verifies both
selected and unrelated return addresses. Tests cover first-query admission,
settings changes, teammate/local selection, exact source ownership, stale and
replacement serials, unchanged native visibility, and stop while the native
predicate is pending. The full installed-file bundle passes **29/29** at
`logs/compatibility/20261023T154614158374Z/`. All nine altered PVS scalar/array
contracts reject (`logs/research/chams-scene-culling/negative-results.json`).
Both DLLs retain zero static TLS/delay import directories; results are saved in
`logs/chams-visibility-payload-checks.json`. Build/test logs use the
`logs/chams-visibility-*` prefix. No game was launched; live appearance remains
unobserved.

Telemetry now includes `pvs_tests` and `pvs_overrides` for the exact native
caller, plus `eligible`, `empty`, `material_misses` and `stale` for generation.
These distinguish native PVS overrides, an empty eligible generation, missing
material variants and changed source identity; counts alone do not prove final
visible pixels. Unchanged native geometry is used while materials are not ready.
