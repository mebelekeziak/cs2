# Native local-player model swap evidence

## Build 2000935 update — October 23, 2026

Verified the installed Windows AMD64 **client**, patch `1.41.9.2`,
client/server `2000935`, Steam build `26144732`. Client SHA-256 is
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`;
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`. The installation remains
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
This supersedes the runtime values in historical sections below.

Header `model_swap_offsets.h` changes, relative to build 26087496:

| Source symbol | Previous | Current |
| --- | --- | --- |
| `FrameStage` | `0xb6d700` | `0xb6e540` |
| `FrameVtable` | `0x1c630f0` | `0x1c65030` |
| `SetModel` | `0x9643c0` | `0x9651f0` |
| `SetModelBinding` | `0x964400` | `0x965230` |
| `ModelInterface` | `0x2560eb0` | `0x2562e60` |
| `ModelNameGetter` | `0x17762b0` | `0x1778510` |
| `HostTimestamp` | `0x6acfcca7` | `0x6ada3394` |
| `ResourceSystemTimestamp` | `0x6acfcc17` | `0x6ada32b3` |
| `Tier0Timestamp` | `0x6acfcbf4` | `0x6ada3248` |
| `EngineTimestamp` | `0x6acfcd53` | `0x6ada3418` |
| `EngineImageSize` | `0x971000` | `0xa15000` |

Rediscovery used `CSource2Client` primary RTTI slot 36, its native previous-job
barrier and jump table, plus five independently located engine stage producers.
Stages 4/5/6/7/8 now load engine interface slot `0x7340F8` at
`0x482BF / 0x48457 / 0x4863B / 0x486C9 / 0x487BE`, pass the stage in EDX and
call byte slot `+0x120`. Client stage 8 dispatches to `0xB6E96F`; stage 10's
worker submission remains the distinct `0xB6E6C4` branch. The callback still
waits/releases/clears `this+0x4B0` before dispatch. Apart from relocated references,
jump-table entries and source-line immediates, this owner's contract is unchanged.

`GameModelInfo001` independently binds the setter's interface slot, whose
`+0x60` method resolves through host RTTI. Named skeleton/model-state schema and
native getters retain skeleton `+0x1E0 -> binding -> CModel+8 -> path`.
Strong reference acquisition/release remain atomic operations at binding `+0x20`.
Setter remains `void(entity*, const char*)`; project code does not manually
retain/release the returned engine-owned model binding.

Host initializer/resolver and tier0 purge windows are byte-identical. Resource
system RTTI and slots remain at the prior RVAs; changed complete windows were
decoded again. The blocking loader constructs a one-name manifest, writes
blocking flags `01 00` at stack `+0x50`, calls its `+0xB0` slot resolving to
`0x1AA80 -> 0x1AA90` (R8B=1 synchronous thunk), preserves native JIT-manifest
retention/release at `+0xC8`, and resolves the result through `+0x278`. The exact
blocking-loader diagnostic string, name hash/type members, native name flags and
named `CBufferString::Purge` export all pass. Native loading ownership and
stage-owner restoration behavior remain unchanged.

The frozen verifier still rejects unknown module hashes before using its sites,
and checks complete new windows, independently decoded operands/identities,
header constants and all fingerprints. Research/disassembly and the passing
`python tools/inspect_model_swap.py` report are under
`logs/research/26144732/view_model/` (`refined.json`, `addresses.json`,
`schemas.json`, `verified_model_swap.json`). All **40/40** temporary
header scalar/array mutations reject; this verifier also rejects a one-byte
changed workspace client copy. See `negative_checks.json` and
`unknown_hash_checks.json` there. No installed file was changed.

Static verification does not establish live appearance or gameplay behavior.
No client/server was launched, no process was accessed, and no graphics runtime
was observed in this pass. Build/CTest integration is recorded by the project
update record; stopped callbacks remain resident until normal process exit.

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

All fifteen complete native windows across client, engine, host, resource
system and tier0 match byte-for-byte. The four non-client module hashes are
unchanged. Native frame RTTI/slot and stage-8 barrier, named model interface,
resource-name initializer, blocking loader/export, schema/strong-reference
members and source fingerprints pass unchanged. No runtime address, resource
ownership or lifecycle change is needed. Only the verifier's client hash pin
changed; all five exact module pins remain mandatory.

Reproduce with `python tools/inspect_model_swap.py`. The fresh real-header
report is `logs/research/26087496/view_model/verified_model_swap.json`.
Temporary wrong `ModelInterface`, `ModelBinding` and `SetModelBytes` headers
all reject; results are in that directory's `negative_checks.json`.

No fresh live appearance is claimed. Build/CTest integration results are recorded
in the project update handoff and README. The earlier investigation below is
historical provenance; its old module identity is superseded above.

## Earlier baseline investigation

Investigated October 18, 2026 from the installed AMD64 PE files. This concerns
the CS2 client, patch 1.41.9.1. All addresses below are RVAs within the named
module. No signatures or offsets were taken from online dumps. Static evidence
does not claim a live model appearance observation.

Run `python tools/inspect_model_swap.py` to verify all five module hashes,
complete relevant native windows, independent schema/operand/RTTI anchors,
named export, source constants and runtime fingerprints. The verifier reads
installed files only and never writes offsets or changes game assets.

## Installation and identities

Game root: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
The launcher is `bin/win64/cs2.exe`; the project uses its observed D3D11 overlay.
The native model path is independent of the graphics API. Client DLL is at
`csgo/bin/win64/client.dll`. Host is `csgo/bin/win64/host.dll`; the other modules
below are under `bin/win64/`.

| Module | Timestamp | Image size | SHA256 |
| --- | --- | --- | --- |
| client.dll | `6AD01314` | `299D000` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` |
| engine2.dll | `6ACFCD53` | `971000` | `6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21` |
| host.dll | `6ACFCCA7` | `160000` | `56efa66a333edb347f044c9bcbc5236bd7d006e478623b3364e5d0a7d681b9cc` |
| resourcesystem.dll | `6ACFCC17` | `96000` | `0004439e37b6ffd5a37683f9f288726584be2a3b4ccc6674af8303cde0343027` |
| tier0.dll | `6ACFCBF4` | `401000` | `990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e` |

## Native setter and resource identity

`client+0x9643C0` is a complete 49-byte wrapper with ABI
`void(entity*, const char* runtimeModelPath)`. RCX is preserved as the entity;
RDX is moved into R8 as a C string. The wrapper obtains `GameModelInfo001` from
`[client+0x2560EB0]`, calls byte slot `+0x60` with a stack output pointer in RDX,
then calls `client+0x964400` with that resulting resource binding.

The interface identity is independently anchored by client initialization at
`0xB65124..0xB65134`, which passes literal `GameModelInfo001` to the native
factory and writes exactly `0x2560EB0`. Host `CGameModelInfo` primary RTTI vtable
is `0x11D800`; byte slot `+0x60` is `host+0x16560`.

The binding setter follows the native body component/skeleton, assigns its
model through `client+0x1E0390 -> client+0xAC3710`, updates collision bounds
through `0x963540`, and invokes the native model-effects virtual callback.
The skeleton setter calls `0xAC35A0` on the embedded `CModelState`. That function
increments the new binding's reference count at `+0x20`, decrements the old
binding's count, and stores the new binding. The project must let these native
functions own resource references and must not assign `m_hModel` directly.

| Value | Independent evidence |
| --- | --- |
| `CSkeletonInstance::m_modelState=0x140` | Named schema registration `0x21FF428`, field array `0x21FF300`, 7 fields |
| `CModelState::m_hModel=0xA0` | Named schema registration `0x21FE008`, field array `0x21FDE40`, 14 fields |
| Skeleton model binding `+0x1E0` | Schema sum above and native entity model getter `0x21C840` |
| Resource binding `+0` -> `CModel*` | Native entity getter `0x21C89D` and skeleton setter `0xAC3652` |
| `CModel+0x08` -> model-name C string | Complete native getter `0x17762B0`; actual string consumer `0xAC374E..0xAC3759` |

For the original model, copy the path synchronously from the current validated
scene node's skeleton `+0x1E0 -> binding -> CModel+0x08 -> string`. Retain the
copied string and full entity identity, never a borrowed model binding or raw
string pointer. `CModelState::m_ModelName` is a different network/resource-name
representation and is not used as an ordinary C string pointer here.

## Explicit loading is necessary

Host model resolution creates a native `CResourceName` and calls
`resourcesystem` byte slot `+0x278` (`0x16540`). This routine is
FindOrRegisterResourceByName. For an unseen model path it can register a binding
with an error-resource placeholder. Its warning explicitly identifies a path
missing from a manifest. Calling the setter alone therefore does not establish
that a custom model was loaded.

Use the native JIT blocking manifest loader before assignment:

| Contract | Module / value | ABI / ownership |
| --- | --- | --- |
| Resource-name initializer | host `0xC4380` | `bool(name*, const char* path)`; native allocation/normalization/hash/type construction |
| Native name storage | `0xE0` bytes | Zero initialize; set DWORD `+0x04 = 0xC00000C8`, exactly as host resolver does |
| Native name hash / type | `+0xD0`, `+0xD8` | Both 64 bits; model resource type is `0x6C646D76` (`vmdl`) |
| Resource-system pointer | host `0x1445E0` | Borrow current manager on each owner callback |
| Manager primary vtable | resourcesystem `0x65348` | `CResourceSystem` RTTI |
| JIT blocking load | resourcesystem `0x16B20`, byte slot `+0x148` | `binding*(system*, const name*, const char* debugOwner)` |
| Temporary name cleanup | tier0 `0x53D60` | Export `?Purge@CBufferString@@QEAAXH@Z`, ABI `void(name*, int 0)` |

The name includes a native CBufferString with 200 bytes of inline storage;
longer valid paths can allocate through the native allocator. `Purge` must run
on both successful and failed initialization/loading. A C++ free/delete is
incompatible with this representation. The initializer stores a 64-bit path
hash at `+0xD0` and a 64-bit extension type at `+0xD8`; pass an uncompiled
`.vmdl` runtime path and keep `_c` only on discovered compiled filesystem assets.

The JIT function first checks native resource state. For a resource needing a
load it submits a one-path request to native manifest slot `+0xB0`, with the
blocking load flag and the caller's persistent debug-owner string. It either
retains the loaded manifest in its JIT-manifest vector or releases the failed
manifest through native slot `+0xC8`, then resolves the binding through `+0x278`.
It contains the exact diagnostic function name
`CResourceSystem::BlockingLoadResourceByNameIntoJustInTimeManifest`. Native
manifest submission `0x1AA80 -> 0x1AA90` follows its synchronous path for this
request. The project neither manually retains nor releases the returned binding.

After loading, validate the returned binding's model/name against the requested
normalized path. A null/error resource is a load failure: keep the existing
model. Revalidate the pawn identity, model and current user intent after a
blocking load because native callbacks can run while it completes. Resolve and
assign only on the established native owner, and avoid repeated failed loads
every frame. This workflow does not modify mounted assets, installed files,
search paths, server configuration or manifests on disk.

## Recurring owner and lifecycle boundary

Client `CSource2Client` primary RTTI vtable is `0x1C630F0`. Slot 36 (byte
`+0x120`) is `client+0xB6D700`, `void(this*, int stage)`. Its own decorated lambda
RTTI names `FrameStageNotify@CSource2Client`. The original callback's prologue
waits for any prior job in `this+0x4B0`, releases that job and clears the pointer.
Its switch subtracts three from the stage and indexes its own jump table.

The engine's network-update routine synchronously dispatches the same client
interface in this order:

| Stage | engine2 callsite | Native client branch |
| --- | --- | --- |
| 4 | `0x47E3E` | `0xB6D7EF` |
| 5 | `0x47FD6` | `0xB6DA0B`, named `FrameNetUpdate` scope |
| 6 | `0x481BA` | `0xB6DB98`, named `FramePostDataUpdate` scope |
| 7 | `0x48248` | `0xB6DBE7`, closes the postdata scope |
| 8 | `0x4833D` | `0xB6DB2F`, final network/entity-manager cleanup |

All five producers load the same Source2Client pointer from
`[engine2+0x6969D8]`, set EDX to the documented stage, then dispatch byte slot
`+0x120`. Stages 6/7 surround native client network-data dispatch through byte
slot `+0x108`; remaining native entity cleanup finishes before stage 8.
Apply after the original stage-8 callback returns. This keeps the operation on
the native network-update owner after its previous-job barrier and entity
updates. Later stage 10 (`client` branch `0xB6D884`) submits worker work;
Present and other render callbacks are not this mutation owner.

Forward every original callback, including nested and stopped calls. Use a
reentrancy scope around original plus project work. Validate the exact frame
owner vtable and current module identities before edits. Copy model paths,
validate full serialized pawn/controller handles and reciprocal ownership,
and discard stale state on identity/scene/team changes. Restoration must use
the same native setter on this owner; a stop request from another thread can
queue restoration and drain an active assignment but cannot safely invoke the
setter from that other thread. If the client no longer issues owner callbacks,
the pending restore cannot be reported as completed. Hooks and stopped code
remain resident until normal process exit.

## Validation limits

The installed-file verifier proves frozen static contracts. Native substitute
regressions prove the adapter's own behavior with mocked callbacks. Neither
proves that a particular custom skeleton, its materials, or its animation graph
renders correctly in the installed client. An actual appearance check remains
restricted to a disconnected menu, offline mode, or the designated private
environment. Client loading at the disconnected menu does not require a server.
