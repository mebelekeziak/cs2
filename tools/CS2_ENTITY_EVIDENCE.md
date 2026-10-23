# Installed-client entity layout evidence

## Current recertification — October 23, 2026

Installed Windows AMD64 client **1.41.9.2 / 2000935 / Steam 26144732**,
Source revision **11162119**. Client SHA-256
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`.

Separate scratch investigation `logs/research/26144732/core/` mapped all nine
named schema registrations and every consumed field from installed templates.
Eleven exact instruction windows were rediscovered using decoded instructions,
then checked against their initializer, lookup, prediction and load/store
relationships. Preserved prior client windows were required to match old frozen
evidence before comparison. Candidate searches alone did not authorize constants.
The entity initializer's complete body relocated to `0x15BAF50`; equivalent
handle lookup bodies occur at `0x39C585` and `0x39C815`. The verifier pins the
first, with its table reference independently agreeing with the initializer.
The prediction getter's larger 64-byte context uniquely locates `0xBF8080`;
its RIP address plus native pawn-slot displacement supplies the pawn global.

| Source constant | Previous | Current |
| --- | --- | --- |
| Entity system/list pointer slot | `0x271A908` | `0x271C8B8` |
| Local controller pointer slot | `0x253CB78` | `0x253EEF8` |
| Local pawn pointer slot | `0x2565718` | `0x25676D8` |
| View matrix | `0x256B020` | `0x256CFE0` |
| C_CSPlayerPawn::m_ArmorValue | `0x1EC8` | `0x1ED0` |

All other consumed schema fields, the embedded table `+0x10`, 0x70 identity
stride, full serialized handle layout and pointer/member distinctions remain
unchanged. `inspect_cs2.py` freezes new exact sites/bytes and source build identity;
there is no live dump or inferred uniform relocation. `entity_decoded.json`,
`discovery.json`, `function_candidates.json` and `details.json` preserve the
derivations. The frozen verifier passes. Four temporary incorrect headers
(global, armor member, stride, timestamp) reject; see `core/negative.json`.
No game/server process was launched or accessed. ESP appearance remains unobserved.
Build/test results are recorded separately in `docs/CS2_UPDATE_26144732.md`.
Earlier sections below are historical.

## Current recertification, October 21, 2026

Installed Windows AMD64 PE32+ client: patch `1.41.9.1`, client/server
`2000934`, Source revision `11150591`, Steam build `26087496`, version date
October 20, 2026, 11:41:22. `client.dll` SHA-256 is
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`. Installation remains the
Steam game path below. The previous timestamp was `0x6AD01314`.

Before changing frozen pins, `logs/research/26087496/core/investigate.py`
executed the existing static decoder against the new installed file. All
eleven exact machine windows, nine class registration templates, named fields,
global RIP-relative operands and entity-table layout matched at their original
RVAs. `entity-contracts.json` records the independently decoded values. All
consumed globals, schema members, handle layout and ownership relationships
remain unchanged. Only the shared header's PE timestamp and verifier SHA-256
pin changed; unrelated source changes were preserved.

`python tools/inspect_cs2.py` verifies the real header. Four alternate headers
with a wrong matrix global, health member, identity stride or old timestamp
are rejected (`logs/research/26087496/core/negative.json`). Unknown binary
hashes still reject before offsets are trusted. No client process was opened,
launched or injected; ESP appearance on this build remains unobserved. Combined
verification/build results are recorded in `docs/CS2_UPDATE_26087496.md`.

## Historical baseline

## Current static recertification, October 15, 2026

This concerns the Windows AMD64 CS2 client. The installed game now reports patch
`1.41.9.1`, client/server `2000932`, Steam build `25947352`. Its `client.dll`
SHA-256 is `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
PE timestamp `0x6AD01314`, SizeOfImage `0x299D000`. The installation path below
is unchanged. This update used **on-disk PE data and machine code**, without
launching/opening a game process, injecting, or modifying installed game files.
It did not produce a new live schema dump or observe ESP output.

The old ESP header rejected this client before acquisition because its PE build
identity still described `1.41.9.0`. Simply changing that identity is insufficient:
the four global pointer/matrix addresses moved, and pawn armor moved by four bytes.

| Header constant | Previous | Current |
| --- | --- | --- |
| `dwEntityList` / `dwGameEntitySystem` | `0x2711598` | `0x271A908` |
| `dwLocalPlayerController` | `0x25338A8` | `0x253CB78` |
| `dwLocalPlayerPawn` | `0x255C3C8` | `0x2565718` |
| `dwViewMatrix` | `0x2561CD0` | `0x256B020` |
| `C_CSPlayerPawn::m_ArmorValue` | `0x1ECC` | `0x1EC8` |

Every other schema field in `include/cs2_offsets.h` was independently decoded
from this installed image and remains unchanged. No upstream generated offsets
or presumed relocation delta supplied the new values.

### Static schema templates

The installed PE contains schema registration templates before runtime binding:
class-name pointer at `+0`, class size at `+0x18`, field count (`uint16`) at
`+0x1C`, field-array pointer at `+0x28`. Each field record is `0x20` bytes, with
field-name pointer at `+0` and member byte offset (`uint32`) at `+0x10`. These
are **on-disk registration templates**, distinct from the dumper's runtime
`SchemaClassInfoData` layout, which contains an additional leading base pointer.

The verifier checks each class name, field count and array address, walks that
class's records and finds requested members by exact field name. A matching
field-name string elsewhere in the image is insufficient.

| Class | Template RVA | Field-array RVA | Fields |
| --- | --- | --- | --- |
| `CEntityInstance` | `0x226AEF8` | `0x226AE90` | 3 |
| `CEntityIdentity` | `0x226B2C8` | `0x226B140` | 12 |
| `C_BaseEntity` | `0x21F4BE8` | `0x21F32C0` | 82 |
| `CBasePlayerController` | `0x2223A38` | `0x2223800` | 17 |
| `CCSPlayerController` | `0x2220498` | `0x221F810` | 70 |
| `CGameSceneNode` | `0x21FB988` | `0x21FB540` | 34 |
| `CCollisionProperty` | `0x21CC828` | `0x21CC4D0` | 17 |
| `C_CSPlayerPawn` | `0x2239A68` | `0x2238D10` | 105 |
| `C_BasePlayerPawn` | `0x21F4588` | `0x21F41F0` | 28 |

### Decoded globals and identity table

Entity-system store at `0x15BCD49` and load at `0xA17AFB` independently reference
`0x271A908`. Controller load at `0x933E80` references `0x253CB78`.
Prediction-address getter `0xBF7240` gives `0x2565620`; the pawn-pointer compare
and store at `0xBD0625` use `+0xF8`, giving pawn global `0x2565718`.
Matrix getter `0xC14173` uses `LEA RCX,[RIP+...]` to address `0x256B020`, then
scales its selected matrix index by 64 bytes.

The entity-system setter stores RCX, then jumps to initializer `0x15B8CF0`,
which copies RCX to RBX at `0x15B8D04`. At `0x15B8D6F` it forms RBX+`0x10`
and stores that address into table global `0x236FA70` at `0x15B8D84` (or null
when RBX is null). This independently establishes the table embedded at
system+`0x10` without relying on an old process address.

Current handle lookup at `0x39B8E0` loads this same table global. At `0x39B903`
it masks the index with `0x7FFF`, shifts by 9 for the bucket, masks the slot with
`0x1FF`, multiplies by identity stride `0x70`, and compares the complete handle
at identity+`0x10`. At `0x39B93E`, `MOV RCX,[RDX]` reads the object pointer with
no displacement. These operands imply 64 buckets of 512 slots. The identity
schema class size independently equals the machine-code stride `0x70`.
The diagnostic index operand at `0x9F7D2D` is still `0x2120`; it remains excluded
from runtime enumeration bounds due to the earlier live counterexample below.

### Reproduce and limits

```powershell
python tools/inspect_cs2.py > logs/research/esp-recert-20261015.json
python tools/inspect_cs2.py --header include/cs2_offsets.h
```

The default command now verifies every frozen constant in the real source
header against independently decoded static evidence. `--header PATH` selects
the header to **verify**, not a generated output destination. The verifier
requires the exact installed DLL hash, PE architecture, frozen machine-code
fingerprints and schema associations. It never rewrites source or silently
adapts to a new build. It requires no old dump JSON and uses standard-library
Python only. Optional `--pid`/`--modules` remain read-only capture facilities;
they were not used for this recertification.

Verification passed for the installed image and final source header. An
exhaustive negative check changed each of the header's 43 numeric constants
individually in temporary copies; all 43 mismatches were rejected. Root
integration also passed both public and diagnostic Release builds with 16/16
CTest suites in each. Current verification records are
`logs/compat-esp-20261015.json` and `logs/compat-esp-negative-20261015.json`.

The source retains the previously observed pawn designer label and geometry
policy. This work establishes current addresses/layout, not fresh gameplay,
visibility or projection observations. The historical live evidence below
belongs to `1.41.9.0`; a new authorized offline run is still needed to confirm
visible ESP behavior for `1.41.9.1`.

## Historical live investigation, October 13, 2026

The remainder records the previous `1.41.9.0` investigation. Its binary hashes,
addresses and old header-generation commands are historical, not the current
verifier contract. No installed game files were modified.

## Installation and generation

Installed client DLL:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256 `44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008`,
AMD64 (`0x8664`), PE timestamp `0x6AC7FFFA`, SizeOfImage `0x2994000`.
`steam.inf`: patch `1.41.9.0`, client/server `2000930`, source revision `11106093`,
version date October 8, 2026; appmanifest build ID `25815307`.

The official [a2x/cs2-dumper repository](https://github.com/a2x/cs2-dumper) documents
native read-only process-memory dumping while CS2 is at the main menu. Downloaded
the official [0.1.3 release binary](https://github.com/a2x/cs2-dumper/releases/tag/0.1.3)
to `logs/research/cs2-dumper.exe`. SHA-256 matched the GitHub release asset digest:
`501368ffb8f252b3cfb70fee6177b6ab4bbb21cad480724af17720d74ce44bbb`.
The inspected upstream source checkout is `4116de000e085d62bbd42334c67b35bda37bda4f`;
this is a source reference, not a claim about the release binary's build commit.
Upstream is MIT licensed; its checkout retains `LICENSE` in `logs/research/cs2-dumper`.
The binary/checkout are investigation artifacts, not runtime dependencies.

From the checkout directory, with the fresh disconnected-main-menu process
PID 14532 running:

```powershell
..\cs2-dumper.exe -o generated -f hpp,json -vv *> dump-console.log
```

Output: 29 offsets across 5 modules, 3301 classes and 568 enums across 18 modules.
`dwSensitivity` and `dwSoundSystem_engineViewData` patterns were stale and failed;
the requested player fields and globals were present. No offsets were copied
from the upstream pre-generated output directory.

Local JSON content hashes:

* `offsets.json`: `d8f824a6007381d3c484914ba6450e226a17483e5f2c83880115f2f4c059f2b6`
* `client_dll.json`: `8916976681b167a052160289fc37de99ced6beef3fbf2f63b02f0f251e33c4c7`

The historical `tools/inspect_cs2.py` extracted the minimal schema/global header and captured
read-only entity evidence using Windows `OpenProcess(PROCESS_VM_READ)` and
`ReadProcessMemory`. It uses standard-library Python only. `--pid` additionally
requires a module inventory captured for that exact process:

```powershell
(Get-Process -Id 14532).Modules |
  Where-Object ModuleName -In 'client.dll','engine2.dll','cs2.exe','schemasystem.dll' |
  Select-Object ModuleName,@{Name='Base';Expression={$_.BaseAddress.ToInt64()}},ModuleMemorySize,FileName |
  ConvertTo-Json | Set-Content logs/research/modules.json
python tools/inspect_cs2.py --pid 14532 > logs/research/installed-client.json
python tools/inspect_cs2.py --header include/cs2_offsets.h > logs/research/header-provenance.json
```

That historical tool refused header generation/entity reads when the installed
DLL or input dump changed. Current verification instead uses the independently
decoded static registrations/operands documented above.

## Entity identities and serialized handles

The locally generated `dwEntityList` and `dwGameEntitySystem` both equal
`client.dll + 0x2711598`; upstream labels an `int32` at system `+0x2120` as
highest index. **Live gameplay later disproved its use as an enumeration bound**
(details below). Keep it diagnostic only and enumerate the 64 table buckets.
Unlike a historical `0x78` identity stride, this exact client uses `0x70`.

Disassembly of the installed DLL at RVA `0x21D8E0` gives a complete entity handle
lookup. The function loads a table pointer from global RVA `0x2366770` and:

```text
21D8E4  mov edx, dword ptr [rcx + 0x10]
21D8EA  cmp edx, -1
21D8EF  mov r10, qword ptr [rip + 0x2148e7a] ; next RIP21D8F6 => RVA2366770
21D8FB  cmp edx, -2
21D900  mov r8d, edx
21D903  and r8d, 0x7fff
21D90D  shr rax, 9
21D914  mov r11, qword ptr [r10 + rax*8]
21D91D  and ecx, 0x1ff
21D923  imul rax, rcx, 0x70
21D927  add rax, r11
21D92C  cmp dword ptr [rax + 0x10], edx
21D92F  jne invalid
21D948  mov rcx, qword ptr [rcx]
```

Another independent lookup around RVA `0x29EA10` compares bucket index against
`0x40`, confirming 64 buckets of 512 identities, maximum index 32767.
Complete disassembly evidence is retained in
`logs/research/entity-disassembly.txt` and `logs/research/entity-shift.txt`.

Live PID14532 observations associate the table global with the generated system:
system `0x4382F474800`; table global contents `0x4382F474810`, exactly system+0x10.
Thus resolve identity as
`read_pointer(system + 0x10 + 8*(index>>9)) + 0x70*(index&0x1FF)`.
The table is embedded at system+0x10; do not dereference another table pointer.
Object pointer is at identity+0; the full 32-bit handle is at identity+0x10.
Exact equality against the requested handle validates both index and generation.
The full handle is not reflected by the schema and was obtained from disassembly.

All 13 live menu identities had matching identity->object and object->identity
backreferences (`CEntityInstance::m_pEntity`, schema offset0x10).
Controller index1: designer `cs_player_controller`, name `peroxide`, handle
`0x340001`, pawn handle `0x5D000B`. It resolved exactly to index11 designer
`c_cs_player_for_precache`, while `dwLocalPlayerPawn` was null. These were menu
samples. **The designer label also appears on real live bots, as subsequently
verified below; it cannot distinguish precache from active gameplay.** Other
observed menu designers include `worldent`, `cs_player_manager`, `cs_gamerules`,
`vote_controller`, and `c_cs_observer_for_precache`.

## Schema types, projection, and limits

Generated HPP type comments confirm `m_iszPlayerName` is inline `char[128]`,
`m_sSanitizedPlayerName` is `CUtlString`, `m_designerName` is `CUtlSymbolLarge`,
and `m_hPlayerPawn` is `CHandle<C_CSPlayerPawn>`. Use the inline name to avoid
assuming CUtlString internals.

Scene fields are schema-verified `Vector m_vecAbsOrigin` at0xC8,
`QAngle m_angAbsRotation` at0xD4, `float32 m_flAbsScale` at0xE0,
`bool m_bDormant` at0x103. Collision `Vector` mins/maxs are at0x40/0x4C.
`m_nodeToWorld` at0x10 is **CTransformWS**, not a matrix. Its exact structure
layout was not established and should not be assumed. Menu samples produced
eight floats `[0,0,0,1,0,0,0,1]`; that sample alone cannot establish component order.
Observed menu precache collision bounds were `[-16,-16,0]` to `[16,16,72]`.

Locally generated `dwViewMatrix` is RVA0x2561CD0; 16 live finite floats were
captured in `installed-client.json`. This establishes readable matrix data at
the exact generated location. Main-menu data alone does not prove projection
over live player entities or correct screen box placement. The user/private
offline gameplay test must verify player designers, changing serials, positions,
matrix projection and actual on-screen draw output. The reader's data provenance
and identity validation can be tested independently of visible ESP.

## Subsequent private bot-match evidence and corrections

The user entered a built-in private bot match in PID14532. A read-only capture
is retained in `logs/research/live-bot-client.json`. No server interaction was
performed by this research agent. Ten `cs_player_controller` identities were
observed. Seven resolved bot pawns within the initial diagnostic enumeration
bound had the exact designer label **`c_cs_player_for_precache`**, live map
positions, nonzero varied scene rotations, valid collision pointers and bounds.
For example Ulric's controller pawn handle `0x14100EE` resolves exactly to
identity index238 at `0x4384CBD6828`, with the same serialized handle and object
backreference, object `0x438EED49000`, origin
`[1302.48535,-832.17792,-167.96875]`, yaw84.9431 and bounds[-16,-16,0]..[16,16,72].
Ferris, Miguel, Ulysses, Waldo, Vox and Dios had the same designer label with
different live origins and exact serialized-handle matches. Filtering these
pawns to designer `player` or rejecting the observed `for_precache` label is
therefore incorrect for this installed client. Use the observed label together
with controller/handle identity validation, health/life/team and geometry.

The reported `+0x2120` value was270 in this sample, while the local controller's
pawn handle `0xA001A1` has index417 and other bot pawn handles have indices274
and281. Thus the upstream highest-index label cannot safely bound enumeration.
The updated tool defaults to scanning all 64 buckets (32768 slots); its optional
`--reported-bound` flag reproduces the narrower diagnostic scan explicitly.
The process exited before a second full-table capture, so health/life/team and
reverse controller-handle values for this sample were not captured. A fresh
process/private bot match is needed to verify those additional fields.

The old Source SDK documents world-aligned collision bounds for SOLID_BBOX in
[C_BaseEntity](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseentity.h),
and its [collisionproperty.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/collisionproperty.cpp)
applies rotation only to entity-space collision bounds. This is supporting
background, **not proof of Source2's current solid flags or pawn collision mode**.
The local schema exposes uint8 solid flags0x5A and solid type0x5B. Applying scene
yaw unconditionally to square collision bounds can change the projected box;
the actual current collision mode should be read and verified before making
an exact geometry claim. Current live samples all had absScale1.0.
