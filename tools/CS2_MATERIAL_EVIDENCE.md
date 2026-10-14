# Installed native world-material evidence

Read-only local binary investigation on October 14, 2026; client-side world
draw generation, AMD64, Microsoft x64 ABI. No game process was launched or
loaded during this investigation. Installed game files were only read.

Installed files below share directory
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64`:

| DLL | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| scenesystem.dll | bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831 | 0x6AC7FF95 | 0x978000 |
| worldrenderer.dll | e026fdb32408d1ac3062ab15beee7f87cc6072ee0c4c9b0910c423e0549512e9 | 0x6AC7FF6F | checked by full hash |
| materialsystem2.dll | 336abe089468bd5b218f773f09da3942fa9346b7f49f24d9e3591722fc54c5e6 | 0x6ABD5B1E | 0x186000 |

The worldrenderer PE timestamp is informational; its full hash freezes the
descriptor lookup below. Run the standard-library verifier before building:

```powershell
python tools/verify_world_materials.py
```

It checks all three DLL hashes, AMD64 metadata, the callback entry, native
descriptor registration, flat-material name/binding, field access operands,
vector append and material-mode dispatch. These independently decoded values
must equal `include/world_material_offsets.h`. A changed binary requires new
investigation; there is no automatic offset update. The optional generic
`tools/inspect_world_materials.py` is research assistance and uses the locally
installed Capstone under `logs/research/python-deps`; runtime and the verifier
do not depend on it.

## Actual world scope and callback ABI

`worldrenderer.dll` world object creation starts at RVA 0x1E920. At 0x1E966 it
loads the exact descriptor lookup string `AggregateSceneObjectDesc`. The
scenesystem RTTI locator resolves the `CAggregateSceneObjectDesc` primary vtable
at 0x5E4538. The constructor references it at 0x3A88A. Vtable +0x20 is the
primitive generation method RVA 0x41F30. This is a world renderer descriptor,
separate from `CAnimatableSceneObjectDesc`, skybox, monitor and UI descriptors.

The callback's four arguments are retained from RCX/RDX/R8/R9 in R13/RDI/RBP/R15
at 0x41F5A/0x41F57/0x41F50/0x41F49. Their consumers establish:

1. Descriptor receiver.
2. Aggregate scene object.
3. `ISceneObjectDesc::GeneratePrimitivesArgs_t` pointer; +0x10 is the scene view.
4. `DrawListVector_t` pointer, consumed by native append at 0x4207E.

The final instructions at 0x4230F restore registers and return at 0x42321.
There is no fifth argument read, no result written, and no pointers retained
by our code. Native parallel-job RTTI names elsewhere independently identify
`KickoffBatchList(..., DrawListVector_t&, GeneratePrimitivesArgs_t&, ...)`.

The original method can dispatch work and is called outside the mutation lock.
It produces the current frame's draw list before our hook changes the newly
appended records and before its caller consumes those records. Calling the
original every time preserves game lifecycle and filtering behavior.

## Native draw-list layout

Native append is RVA 0x369B0. Aggregate generation calls it with RDX=0 at
0x42079, so its ordinary list branch at 0x36AF6 is the one used here.

| Head offset | Native role | Operand evidence |
| --- | --- | --- |
| 0x00 | fixed storage pointer | 0x36B09 |
| 0x08 | fixed capacity | 0x36AFA |
| 0x0C | consumed fixed count | 0x36AF6 / 0x36B02 |
| 0x10 | overflow count | 0x36B11 / 0x36BBF |
| 0x18 | overflow storage pointer | 0x36B87 / 0x36BDA |
| 0x20 | overflow capacity | 0x36B15 / 0x36BBC |
| 0x24 | overflow allocator flags | 0x36B1E |

The native multiplication at 0x36B05 and 0x36BC6 establishes a 0x70-byte
`CMeshDrawPrimitive_t` stride. Overflow storage can be reallocated by the
original method; the mutation code reads its new pointer after that method
returns, uses the old count as its start, and preserves older records.
Fixed-storage identity/capacity must remain unchanged. Count rollback,
invalid counts/capacities, storage arithmetic overflow or an unreadable field
reject mutation. Each new record must belong to the same scene object at
record +0x18 and already contain non-null render material/mode pointers.

## World tint and flat-material selection

The aggregate record constructor 0x371E0 receives packed RGBA in R9D and writes
it at +0x50 (0x37214). Aggregate generation supplies 0xFFFFFFFF from
0x5DE4E4 at 0x42099. Other geometry generation uses the same record, converting
linear float4 to sRGB packed RGB and linear alpha with helper 0x49C8D0;
stores at 0x49C97C/0x49C9DA/0x49CA4E/0x49CA55 prove byte channel order.
The override multiplies original packet RGB by the selected surface tint and
preserves alpha. It does not write the source entity/model/material color.

The engine owns a native debug material binding at scenesystem +0x9217D8.
Its name is `materials/dev/reflectivity_90.vmat`, associated with that binding
by the initializer at 0xBF390/0xBF39E. The original aggregate method itself
selects this binding at 0x42189, dereferences the material at 0x42195, and sets
native override bit0x10 in record flags+0x62 at 0x42198.

The material is stored at record+0x20 (0x421D7). The mode is stored at +0x28
(0x421DE / 0x421F4). The original mode lookup at 0x421EA is exactly
`material->vtable[0x30 / 8](material, sceneView + 0x30)`, returning the selected
mode pointer. Our hook uses that exact dispatch, first requiring the method
address to fall inside the verified loaded materialsystem2 image. This native
call happens outside our mutation lock; disarm is rechecked afterward.
Neither the binding nor material reference is cached across callbacks.

For primitive type1/type2 at mesh+0x80, native override uses alternate vertex
streams from scenesystem+0x921390 / +0x9213A8 at 0x421A9 / 0x421B7. The hook
matches those replacements at record+0x48; a missing required stream leaves
that packet unchanged. Other types preserve their existing stream.

The original native selection requires scene-view flags+0x48 bit24 set and
bit32 clear, tested at 0x42164/0x4216B. The hook follows those view constraints.
It does not modify console variables, shader code, installed textures or
material files. Flat mode replaces qualifying aggregate world surface
materials with the existing built-in reflectivity material. It is a flat
surface/debug appearance, not arbitrary custom texture loading. Baked/global
lighting remains whatever that native material and the engine render.

## Shutdown, scope and current evidence limit

The hook remains a passthrough when disabled or stopped. Stop first disarms,
then takes the exclusive mutation lock so any packet write finishes. Pending
original/native mode calls run without that lock; a later disarm check prevents
their packet mutation. Original callbacks/trampolines and mapped code remain
resident until normal process exit. No packet, scene object or material reference
is saved between callbacks. Subsequent frame generation creates baseline
packets; there is no long-lived engine material state to restore.

Coverage is the verified aggregate world geometry path. It does not assert
coverage of every static prop, decal, grass, particle, separate instanced path
or specialized shader. It excludes the distinct animatable player/weapon and
UI descriptor callbacks. Custom sky asset replacement and fog removal were
not implemented by this path. Fog remains an independent engine subsystem.

Standalone tests exercise tint/opacity, packet neighbors, older and foreign
packets, overflow reallocation, invalid bounds/counts, exact native material
tuples/stream selection, descriptor/view exclusion, disable/regeneration and
cooperative stop while the fake original is still running. Builds/tests and
the frozen verifier are evidence of implementation correctness only. No live
client render, material appearance or actual map coverage has been observed.
