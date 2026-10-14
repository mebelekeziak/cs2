# Installed CS2 sky draw evidence

Read-only investigation of the installed Windows x64 client, version 1.41.9.0.
All addresses below are RVAs of the named module. Run `python tools/verify_sky_draw.py`
to verify the specific binary fingerprints and machine instructions. No process
was opened, injected, launched, or changed during this investigation.

`scenesystem.dll` SHA-256:
`bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831`.

## Recurring sky draw path

MSVC RTTI `.?AVCSkyBoxObjectDesc@@` resolves its descriptor vtable to `0x5F64D8`.
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
The client original sky update at `client.dll + 0x28232E` explicitly changes
the lighting scene's `+0xC4` to `0x0881EE9E` immediately after creation.
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
