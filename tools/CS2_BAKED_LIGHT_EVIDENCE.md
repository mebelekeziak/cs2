# Installed native sunlight and directional lightmap evidence

Recertified October 15, 2026 for CS2 **1.41.9.1**, client **2000932**, Steam
build **25947352**, Source revision **11131657**. Current `scenesystem.dll`
SHA-256 is `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`,
AMD64 timestamp `0x6ACFCD4F`, image size `0x978000`.

Before changing either runtime metadata or verifier pins, all 15 frozen static
lighting checks and six runtime byte arrays matched the new installation.
Full historical instruction bytes also matched unchanged: pure memory-copy
investigation (356 instructions), buffer variant construction/upload (276),
sun selection (210), and per-view buffer preparation (467). The additional
complete light-gather comparison is recorded in `CS2_ENV_LIGHT_EVIDENCE.md`.
The independently decoded copy target/return remain `0x569120`/`0x1B0CD1`,
buffer size `0x7DE0`, sun RGB `0x79A0`, directionality float `0x40`, and upload
vtable slot `+0xF8`. Every mechanism below remains unchanged. No live process
was accessed during recertification.

The recertified frozen verifier and static-lighting inspection both passed.
The targeted MSVC x64 Release environment-lighting check executable rebuilt
successfully and passed CTest. Its callback/data checks do not establish live
appearance; no game launch or full DLL build was performed in this subtask.

## Original investigation

Read-only investigation on October 14, 2026, for the AMD64 CS2 client
installation documented in [CS2_WORLD_EVIDENCE.md](CS2_WORLD_EVIDENCE.md).
No client was launched, opened, injected or changed. `scenesystem.dll` SHA-256:
`bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831`.

`python tools/verify_environment_lighting.py` pins installed bytes and independently
decodes the runtime header operands. `python tools/inspect_baked_lighting.py`
checks the additional static-lighting investigation. Neither generates offsets.

## Native per-view output

The native routine beginning at RVA `0x1AF7B0` constructs three `0x7DE0` byte
lighting constant buffers on its own current thread stack. The first begins
at `RBP-0x30`; subsequent variants begin at `RBP+0x7DB0` and `RBP+0xFB90`.
At `0x1B0CB5..0x1B0CCC`, native arguments are `R8d=0x7DE0`,
`RDX=RBP-0x30`, `RCX=RBP+0x7DB0`, followed by a call to pure memory-copy
helper `0x569120`. The return address is `0x1B0CD1`. At `0x1B0D83`,
the second native copy constructs the third variant from the second.

At `0x1B0F40..0x1B0F91`, the routine constructs an array of these base/variant
pointers and submits them through graphics context vtable slot `+0xF8`, with
the same explicit `0x7DE0` buffer size. This establishes that changing only
the destination of the first copy would leave the first submitted buffer
unchanged. The implementation changes the freshly constructed stack source
before the first copy; native copies propagate its values to the variants.

The general helper `0x569120..0x56978D` is native memmove-style code; its
observed input contract is destination, source, byte count in `RCX/RDX/R8`.
It returns the original destination in `RAX`. No renderer/job waits or scene
lifecycle operations occur inside it. Other callers are forwarded untouched.

## Sun radiance

The selected sunlight record is in light-binner `+0xEBD0`.
The builder loads that pointer at `0x1AFA54`; at `0x1AFA8C`, `0x1AFAA4`,
and `0x1AFAB4`, it reads float RGB at record `+0xCC/+0xD0/+0xD4` and
stores the values at `RBP+0x7970/+0x7974/+0x7978`. Relative to the actual
buffer base `RBP-0x30`, this is **`buffer+0x79A0`**. Adjacent
`buffer+0x7990` comes from different source fields and is not RGB radiance.
The fourth component at `buffer+0x79AC` comes from record `+0x1C4` and is
preserved. No sun direction, shape, shadow transform or texture handle changes.

The selection routine `0x1A7FD0` can use explicit sunlight record `+0xEBD8`
directly. Its fallback selects an unbounded light through `0x4A4EE0`, copies it
with `0x1A7B10`, stores the selected copy in `+0xEBD0`, and removes that record
from the local-light array. Hooking only that fallback's `0x8D010` copy at
return `0x1A7B43` would miss explicit sunlight. The final per-view buffer
checkpoint covers both branches without retaining or changing those records.

The source RGB is freshly reconstructed on each builder call. Tint and
brightness multiply this current baseline once, avoiding accumulated scaling.
Absent sunlight produces the native zero baseline; multiplying it does not
create an artificial light.

## Directional lightmaps

Registration at `0x11659` refers to exact local string
`r_directional_lightmaps` at `0x5FC620`, with wrapper `0x923C40`.
At `0x1AF917`, the builder loads its native value pointer `0x923C48`,
tests its boolean at `+0x58`, and produces float zero when enabled and float
one when disabled. Store `0x1AF939` writes this native constant to `RBP+0x10`,
or **`buffer+0x40`**. The per-view upload above consumes the resulting buffer.
The three menu modes map to preserving the game value, zero, and one.
The implementation requires the original constant to be exactly zero or one.
The native `XMM13` initializer at `0x1AF849` loads the verified float one
constant at `0x5FFF40`; the enabled branch explicitly clears `XMM0` to zero.

This adjusts directional lightmap shading. It does not replace irradiance
textures, change their baked colors, or disable all baked lighting.

## Ownership and shutdown

The hook requires the exact caller, byte count, and native contiguous source/
destination relationship. `GetCurrentThreadStackLimits` verifies both complete
buffers belong to the current callback's thread stack. No pointer is retained
after the callback. All override writes and the narrow native memory copy run
under the cooperative stop barrier. Full native rendering and GPU submissions
run outside it. Stop disarms new overrides and waits for those writes/copies;
already prepared native frames may finish. Code remains resident and forwards
until normal process exit.

Standalone checks exercise actual pointer forwarding, original return value,
stack ownership rejection, fresh baseline, neutral/reset behavior, finite-value
validation, preservation of unrelated bytes, native variant propagation, and
stop during a modified memory copy. These are callback/data checks; they do
not establish live map appearance or GPU cost.

## Why `sc_disable_baked_lighting` is not a runtime toggle here

The exact string at `0x5EEF00` is registered at `0x8799` with wrapper
`0x922D18`. Its value pointer `0x922D20` is used at `0x42101`, `0x5499B`,
`0x59618`, `0x762F5`, `0x7A91F`, and `0x80F23`. These paths control
material/primitive binding: they omit a baked texture pointer or skip helper
`0x79EC0`. That helper binds irradiance resources and selects native shader
combinations. Material setup/caches are involved; suppressing that helper
alone does not establish a recurring per-view reset and can leave stale
material variants. No global cvar or cached-material writes were introduced.

The installed `materialsystem2.dll` contains `g_flLightMapScalar` as packed
shader metadata near RVA `0x10532F`. Its presence alone does not establish a
writable engine field, a recurring setter, or the lightmap shader's source
buffer offset. No fullbright or arbitrary ambient scalar is inferred from it.
Installed worldrenderer `r_lightmap_size`, `r_lightmap_set` are load/resource
configuration, rather than verified live brightness parameters.
