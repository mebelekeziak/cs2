# Installed CS2 sky draw evidence

## Current recertification, October 23, 2026

Read-only installed Windows AMD64 client **1.41.9.2 / 2000935**, Source revision
**11162119**, Steam build **26144732** (version October 22, 14:13:16).
The installation remains
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
`bin/win64/scenesystem.dll` SHA-256 is
`0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b`,
PE timestamp `0x6ADA337D`, image `0x978000`.
`csgo/bin/win64/client.dll` SHA-256 is
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
timestamp `0x6ADA7340`, image `0x29A0000`.

Independent primary RTTI traversal for `CSkyBoxObjectDesc` gives the new
table **`0x5F6508`**, formerly `0x5F64D8`. Render slot `+8` still resolves
`0x154FB0`; generation slot `+0x20` still resolves `0x36C00`. The client lighting
kind store is unique at **`0x28267E`**, formerly `0x28269E`. Its owning updater
entry is `0x282370`; the `0x282667` window independently shows material handle
`+0x10A0`, creation dispatch `+0x138`, lighting output `+0x10F0`, then the
`[rax+0xC4]=0x0881EE9E` store. These moves were discovered separately, without
applying a uniform relocation.

All twenty frozen instruction windows pass after updating the client site
locations. Render remains seven-argument Microsoft x64 ABI; packet stride
`0x70`, last-source `+0x18`, kind `+0xC4`, visible kind, tint key, writer
`0x4DA70`, and return `0x1551A9` are unchanged. The complete render/constructor/
writer and client-updater windows were disassembled to workspace logs. The
writer still copies the temporary float4 synchronously; no lifetime/stop change.

`include/world_offsets.h` updates the two module identities, descriptor and
client producer; its three byte arrays remain unchanged. The frozen verifier
passes. Temporary alternate headers with changed timestamp, descriptor,
`DrawSource`, or `SkyRenderBytes` all fail; an unknown scene image also fails.
Evidence: `logs/research/26144732/world/probe.json`, `contracts.json`,
`client-updater.txt`, `header-bytes.json`, `verify_sky_draw-verified.json`, and
`validation.json`. Research used Capstone 5.0.9 from the documented ignored
dependency directory; the frozen verifier remains standard-library-only.
No client/server launch or fresh runtime/graphics/visual observation occurred.
Build/CTest results belong to the overall update record.

## Historical recertification, October 21, 2026

Installed client/server `2000934`, Source revision `11150591`, Steam build
`26087496`, patch `1.41.9.1` (version October 20, 2026, 11:41:22).
The AMD64 client image is still `0x299D000` bytes; timestamp changes from
`0x6AD01314` to `0x6AD7B41C`, SHA-256 from the historical value below to
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`.
The complete scenesystem hash/identity below is unchanged.

Scratch investigation before frozen-pin updates rechecked all twenty exact
scene/client code windows, RTTI-derived descriptor/vtable slots, the unique
lighting-kind store, surrounding material creation/output flow and every decoded
source constant. All original RVAs, seven render arguments, parameter-copy ABI,
kind values and lifecycle contracts remain unchanged. Research and decoded
output: `logs/research/26087496/core/investigate.py` and `sky-contracts.json`.
Only the sky header's client timestamp and verifier's client SHA-256 changed.

`python tools/verify_sky_draw.py` verifies the real header. Wrong producer RVA,
scene member, producer bytes and old client timestamp are all rejected through
`--header` copies (`logs/research/26087496/core/negative.json`). Strict binary
hash rejection remains. No live rendering was observed or client/server
launched. See `docs/CS2_UPDATE_26087496.md` for combined verification/builds.

## Historical baseline, October 15, 2026

Recertified October 15, 2026 against installed Windows x64 patch 1.41.9.1,
client/server 2000932, source revision 11131657, Steam build 25947352,
version date October 14, 2026, 16:57:53. Original investigation used 1.41.9.0.
All addresses below are RVAs of the named module. Run `python tools/verify_sky_draw.py`
to verify the specific binary fingerprints and machine instructions. No process
was opened, injected, launched, or changed during this investigation.

Installation root:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.

| Module under installation root | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| `bin/win64/scenesystem.dll` | `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5` | `0x6ACFCD4F` | `0x978000` |
| `csgo/bin/win64/client.dll` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` | `0x6AD01314` | `0x299D000` |

Both modules were verified as AMD64 PE32+. This recertification covers only the
sky draw and lighting-kind producer. The sky now uses its own scoped client PE
identity in `world_offsets.h`; it does not update or enable shared ESP/movement
offsets.

## Recurring sky draw path

MSVC RTTI `.?AVCSkyBoxObjectDesc@@` resolves its descriptor vtable to `0x5F64D8`.
The current type descriptor is `0x680DF8`, primary complete-object locator
`0x60C720`. The verifier derives the primary vtable from those RTTI links and
then reads its method slots; it does not accept an assumed vtable location.
Slot `+0x20` points to primitive generation `0x36C00`; it emits one `0x70` byte
packet with source scene at `+0x18` and packed draw RGBA initialized white at
`+0x50`. Primitive generation does **not** read source tint `+0xE8`, so packet
RGBA is insufficient evidence for sky shader tint handling.

Descriptor slot `+0x08` points to draw/render routine `0x154FB0`. Observed x64
arguments are RCX descriptor, RDX graphics command context, R8 packet array,
R9d packet count, argument 5 preserved (unused here), argument 6 view,
argument 7 draw statistics. Preserve every argument in a forwarding hook.
`0x154FCD..0x154FE0` computes `count * 0x70` and reads
`packet_array + count*0x70 - 0x58`, i.e. the last packet's `+0x18` source.
The function uses the last source to prepare the batch's sky shader parameters.

At `0x155149` and `0x155156`, the draw routine reads the scene's RGB float3 at
`+0xE8` and `+0xF0`, constructs an RGBA float4 on its own stack with alpha zero,
then calls `0x4DA70` at `0x1551A4` (return RVA `0x1551A9`). That call has
RCX = stack parameter collection, EDX = key `0x99E6002A`,
R8 = pointer to the stack float4. Subsequent code submits the completed
parameter collection synchronously, then destroys it at `0x1556A1`.

`0x4DA70` is a general float4 shader-parameter writer. It takes collection,
32-bit key, and pointer to four floats. At `0x4DC66` it loads all 16 input bytes
with `movups`, and at `0x4DC73` stores those 16 bytes into collection-owned
storage. It retains no pointer to the supplied input. The return register is
not a semantic contract established here; forwarding as a `uintptr_t` retains
the original RAX when needed, while callers shown in this path ignore it.

Runtime preparation checks both hook prologs, the descriptor slot, AMD64 PE
identity/timestamp/image size, and the client's exact lighting-kind producer
instruction with its matching PE identity. The offline verifier additionally
pins both complete SHA-256 fingerprints and derives each header operand.

## Distinguishing visible sky from lighting sky

Sky scene allocation at `0x1557EE` requests `0x110` bytes. Default construction
sets scene `+0xC4` to `0xE5FD3C88` at `0x155908`.
The current client original sky update at `0x282390`, at instruction
`client.dll + 0x28269E`, explicitly changes
the lighting scene's `+0xC4` to `0x0881EE9E` immediately after creation.
The new producer was independently located as the unique matching kind store,
then its surrounding material-handle `+0x10A0`, native creation slot `+0x138`,
and lighting output `+0x10F0` flow were checked. Its former instruction RVA
`0x28232E` is obsolete; it was not moved by applying an assumed image delta.
This is a direct local-binary distinction; no cached entity-to-scene mapping
is necessary during a draw. Source tint is already linear and includes the
native brightness factor before shader upload.

## Integration option and limitations

A draw hook can hold a scoped thread-local visible/lighting designation for
the duration of the original `0x154FB0` call. A hook on `0x4DA70`, active only
inside that scope and for key `0x99E6002A` at return RVA `0x1551A9`, can supply
a local modified float4, preserving alpha. The original writer immediately
copies it. This changes only current draw parameters, avoids scene mutation,
and responds on subsequent views without forcing the transient entity-update
queue to run. Nested scopes must restore the prior TLS value. Stop must leave
forwarding callbacks valid until a verified shutdown barrier or process exit.

This identifies an integration path; this document does not claim live visual
verification. Effects on reflection/environment lighting depend on which
lighting-sky views the native renderer actually refreshes. Baked lighting is
outside this verified draw path.

## October 15 recertification

All thirteen previously frozen scene-render, ABI, data-field and copy checks
matched the new scenesystem binary. Additional current checks cover primitive
scene/RGBA initialization, parameter submission/destruction, current client sky
updater entry, and the lighting material creation/output flow. The verifier pins
both new full hashes and compares every runtime header constant and hook/producer
byte array with the independently decoded evidence. Runtime changes are limited
to scoped build identities and the relocated client producer; the accepted
per-view TLS/callsite/key behavior is unchanged. No live client launch or visual
validation was performed for this recertification.
