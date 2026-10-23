# Autofire direct line of fire

## Current static recertification, October 23, 2026

Installed Windows AMD64 client: patch `1.41.9.2`, client/server `2000935`,
Steam build `26144732`, Source revision `11162119`, version date October 22,
2026, 14:13:16. Installation and executable remain the documented Steam paths.
Client SHA-256 is `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`.

Investigation read installed files only. Independent scratch code located new
native windows, decoded them with Capstone 5.0.9, and compared instruction and
data contracts with the preserved preceding image. Relative instruction operands
were decoded, and jump tables, schema members, numeric constants and named
registrations were checked separately. No uniform relocation was assumed.
Research, all 71 current native windows, complete disassembly and a sixteen-case
negative-header report are retained under `logs/research/26144732/ballistics/`.
Unknown file hashes remain rejected. No client/server process was launched or
opened, and no installed file changed. Live behavior remains unobserved.

The bullet-filter producer relocates `0x887F56 -> 0x888D86` and its call at
`0x888DA1` independently resolves constructor `0x352920 -> 0x352CC0`.
All native mask/layer/flag/exclusion operands and serial ownership are unchanged.
The complete constructor remains synchronous with the same five-argument ABI.

Zero-bounds ray initialization relocates `0x36DDB0 -> 0x36E880`, DidHit
`0x360720 -> 0x360D00`, and result initializer `0x17794AE -> 0x177B70E`.
The selected entity consumer is `0x9A25C5`; trace entity `+8`, flag `+0xBB`,
fraction `+0xAC`, result size `0xC0` and ray kind zero stay unchanged.
Complete TraceShape (1294 bytes, including its chained unwind fragments) at
`0xA1AE30..0xA1B33E` and InitializeTrace (600 bytes) at
`0x177B560..0x177B7B8` retain the preceding build's complete instruction
contracts after decoding relative/RIP operands, with no other byte changes.
Both complete functions are now frozen in the occlusion verifier as additional
checks. The shared third-person verifier independently establishes trace manager,
six-argument trace ABI, shape/result constructors and header contracts.

`python tools/inspect_aim_occlusion.py` passes all 10 native windows and shared
checks. Negative changes to `InitializeFilter`, `ShotMask`, `TraceEntity` and
`InitializeFilterBytes` reject.

## Historical static recertification, October 21, 2026

Installed Windows AMD64 client: patch `1.41.9.1`, client/server `2000934`,
Steam build `26087496`, Source revision `11150591`, version date October 20,
2026, 11:41:22. Installation/executable remain unchanged. Client SHA-256 is
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`.

Before frozen identity updates, a separate scratch invocation reran all eight
complete occlusion windows, constructor entry, point-ray branch, native filter
operands and shared third-person trace layout/manager checks. All passed at
their existing RVAs. An independent byte/hash audit and Capstone 5.0.9 decoded
windows, including `0x887F71 -> 0x352920`, remain in
`logs/research/26087496/aim_ballistics/`. The six-argument synchronous trace ABI,
filter ownership/serial rules, result widths, mask/layer and zero-bounds ray
semantics are unchanged. No runtime constant or instruction fingerprint changed.

The verifier retains its shared `inspect_third_person.EXPECTED_HASH` identity,
now recertified for this client, and rejects unfamiliar binaries.
`python tools/inspect_aim_occlusion.py` passes. Temporary changes to
`InitializeFilter`, `ShotMask`, `TraceEntity` and `InitializeFilterBytes` each
fail for their corresponding source mismatch; results are in the sixteen-case
shared negative report. No client/server was launched or opened, and installed
files were not modified. Live line-of-fire behavior on this build remains
unobserved. Earlier investigation below is historical.

Investigated October 18, 2026, against the installed Windows AMD64 client
`game/csgo/bin/win64/client.dll`, SHA-256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
The installation root is recorded in [the address index](../docs/RE_ADDRESS_INDEX.md).
This is client command-owner work; no server or live client run was used.

Runtime: [aim_occlusion.cpp](../src/aim_occlusion.cpp),
[aim_occlusion_offsets.h](../include/aim_occlusion_offsets.h).
Portable verifier: `python tools/inspect_aim_occlusion.py`.
Discovery used the existing `tools/inspect_world_materials.py` and local
Capstone 5.0.7 dependency under ignored `logs/research/python-deps/`.

## Native shot-filter producer

The native `FX_FireBullets` path is anchored by its diagnostic strings at
`0x1CA96B8`, `0x1CA96F0`, and `0x1CA9730`, referenced at
`0xD23A89`, `0xD23AB4`, and `0xD23AEA`. That path calls `0x887D60` at
`0xD23EB6`. This caller calculates shot direction from angles and spread and
constructs the trace filter at `0x887F56..0x887FBA`:

| Operand | Producer | Value |
| --- | --- | --- |
| Local pawn | `0x887F56` | Current service pawn from `[rdi]` |
| Layer | `0x887F60` | `3` |
| Flags byte `+0x37` | `0x887F66` | `0x0F` |
| Inclusion mask | `0x887F6B` | `0x1C300B` |
| Filter constructor | `0x887F71` | Calls `0x352920` |
| Filter flags byte `+0x39` | `0x887F84` | OR `2` |
| Exclusion mask `+0x10` | `0x887F90`, `0x887F9F` | OR `0x4000000000` |

`0x352920` has ABI `filter*(filter*, pawn*, uint64 mask, uint8 layer,
uint8 flags37)`. Its full body through `0x352A55` constructs the native vtable,
baseline fields, mask, excluded local pawn, collision id, owner handle, and
flags. The return register is the input filter pointer. Using that complete
constructor preserves the native owner/serial and collision-id rules. Storage
is a zero-initialized, 8-byte-aligned `0x48`-byte current-thread stack buffer.
The shared native filter vtable and deletion size are established in
[third-person evidence](CS2_THIRD_PERSON_EVIDENCE.md).

This shot filter differs from the camera mask (`0x80`), layer/exclusions; those
camera policy values are not used. The native bullet path passes its filter to
`0x8830E0`, which changes flags during its multi-hit processing and enters
`TraceManager` multi-hit path `0xA19E90`. Autofire does not invoke that native
penetration/damage path. It reuses the constructed filter policy with the
already-established synchronous single-shape trace contract.

## Ray, result, and conservative decision

The synchronous call is the same verified ABI as the camera trace:
`bool(manager*, shape*, const float3* start, const float3* end, filter*, result*)`
at `0xA1A000`, with manager pointer loaded from client slot `0x2227058`.
Shape/result buffers are `0x30`/`0xC0` bytes with alignment 16.

The complete shape initializer `0x36DDB0..0x36DE04` compares corresponding
minimum/maximum bounds. Six zero bounds take `0x36DDD6..0x36DDEF`, set shape
byte `+0x28=0`, copy zero origin and zero radius, then return. This supplies a
ray rather than the third-person camera hull.

Trace initializer `0x1779300` sets fraction `+0xAC=1.0` at `0x17794D3`,
entity `+8=0` at `0x17794F0`, and bytes `+0xBB/+0xBC=0` at `0x17794DD`.
Native `DidHit` helper `0x360720..0x36073F` returns true when fraction is less
than `1.0` or byte `+0xBB` is nonzero. This proves a native blocking-hit flag;
the investigation does not assign it a more specific start-solid/all-solid
field name. The selected-entity comparison at
`0x9A1795` reads `[rbp-0x18]` from result base `[rbp-0x20]`, independently
establishing entity offset `+8`. These reads establish the usable collision
semantics; the runtime does not infer a complete trace-result struct.

The segment runs from the copied current local eye to the authored selected
capsule center used by autofire aiming. A finite fraction in `[0,1]` is required.
The native blocking-hit flag, zero-fraction, foreign first-hit entities, and any world hit before
the endpoint block automatic fire. A first hit on the selected pawn or an
unobstructed full segment clears. There is no near-endpoint tolerance through
cover. The trace boolean is not used as a success flag: native camera consumers
ignore it because false can also represent an unobstructed trace.

The runtime verifies PE AMD64 identity and native entry bytes during prepare.
Each call resolves its manager afresh, checks the current local pawn, and
rechecks manager/client/local-pawn identity after native callbacks. Native
exceptions, unavailable contracts, invalid vectors, missing geometry, and
changed sources fail closed. The owning pipeline independently validates full
target identities and authored geometry around each check and repeats the
check when a delayed request is ready to publish. All native stack objects
expire after the synchronous call; no callbacks or pointers are retained.

This is deliberately conservative: penetrable cover also blocks autofire. It
does not simulate penetration or damage, search other target points, establish
visibility of the entire capsule, or prove that spread samples all clear cover.
Manual attack behavior remains under the existing manual path. Static evidence
and fake callback tests do not establish observed live wall behavior.

## Verification

The verifier pins the installed client hash and native producer/consumer
windows, decodes the shot operands independently of the source header, checks
the complete zero-bounds branch and shared trace layout, and checks the runtime
entry array. `--header PATH` and `--shared-header PATH` allow deliberate source
mutations without altering checked-in headers or installed game files.

`aim_occlusion_checks` uses a memory-backed fake client and callback substitutes.
It covers entry/build rejection, exact ray/filter arguments and alignment,
clear/blocked results, selected/foreign first-hit entities, the native hit flag,
nonfinite/out-of-range fractions, an obstacle one float before the endpoint,
source/manager changes during callbacks, unavailable contracts, and native faults.
Pipeline regression tests cover obstruction changes before delayed publication,
cleanup and CRC refresh, and unaffected manual/preview behavior.

The standalone suite passed 173 checks with zero failures. All 21 deliberate
source constant/entry-array mutations rejected, including the shared native
trace constants; the report is
`logs/research/aim-occlusion-header-mutations/results.json`. These results are
synthetic/static verification and do not represent live wall observation.
