# Player material creation and shader contracts

Installed Windows AMD64 **client**, patch 1.41.9.1, client 2000933, Source revision
11133601, Steam build 25966348 (current installed steam.inf). Static analysis
and file verification, October 18, 2026.
No client/server process was launched. Runtime visual appearance remains
unobserved. Sources and research are in this workspace; installed assets are
read without modification.

Run `python tools/verify_player_chams_materials.py`. Its frozen complete module
hashes, whole native function windows, RTTI/vtable checks, independently decoded
operands and installed shader/texture hashes reject an updated contract. It
checks `include/player_chams_material_offsets.h` without generating or fixing
constants. `--header` selects another input for negative tests. Resource-name
construction and blocking loading reuse the separately verified
`model_swap_offsets.h`/`inspect_model_swap.py` contract.

## Native KV3 lifecycle

All calls use the Microsoft x64 ABI. `client.dll+0x19B4860` constructs an opaque
CKV3Arena in caller storage with its second argument zero. Allocate **0x1190
bytes**, aligned to 16 to match the observed native stack caller. Minimum
language-level alignment is not inferred. The scalar deleting destructor
`client+0x19B4F10`, at +0x19B5006, independently passes 0x1190 as the delete size.
The constructor creates the native 16-byte-stride node pool and initializes a
root; a guessed standalone KeyValues3 layout is unnecessary.

`client+0x19C28E0(arena)` checks bit 2 at arena+0x1130, returning arena+0x78.
Parse through the exact tier0 named export:

```text
?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z
```

Its installed RVA is tier0+0x124950. The callable contract is
`bool(root, errorCUtlStringOrNull, nulTerminatedText, formatID, name, flags)`.
The format structure is a name pointer followed by two uint64 words, size 24,
alignment 8. Native client+0x1AD2840 contains
`{"generic", 0x469806E97412167C, 0xE73790B53EE6F2AF}`. Text format GUID is
`7412167c-06e9-4698-aff2-e63eb59037e7`. Native client+0x226F80 constructs,
gets the root, parses with empty name/flags zero, and destroys on success or
parse failure. The parser checks null error pointers before initialization or
error reporting at tier0+0x124A45/+0x124BB7/+0x124BFC.

`client+0x19B4EB0(arena)` sets EDX zero and tailcalls +0x19B4F10, releasing
native children/allocations without freeing caller storage. Destroy the arena
after synchronous material conversion, including parse rejection. Never free
the root separately. The complete lifecycle disassembly is retained under
`logs/research/chams-material/{client-arena*,tier0-parser*,kv3-contract.*}`.

## Material creation, selection and ownership

`CMaterialSystem2` RTTI identifies materialsystem2+0x12D920; slot 29 is
materialsystem2+0x3ADE0. The native ABI is
`void*(system, binding** out, name, KeyValues3* root, unsigned arg5, bool arg6)`.
CreateMaterial preserves out/name/root from RDX/R8/R9, calls +0xDC00 to convert
the root into material-owned parameter storage, then registers/initializes the
material before returning the output address. The runtime chooses arg5=1,
matching the native clone registration at +0x3AD7E, and arg6=true. The latter
is inverted into native shader load mode zero, matching the ordinary native
clone path. Material conversion stores
the supplied name at material+0x10: +0xDC55 forms that receiver for native string
assignment. Runtime checks the exact created material name before publication.

The final boolean's downstream contract is independently traced. +0x3AEFE
loads the sixth argument; +0x3AF0E inverts it, then +0x3AF15 passes the result
to +0xFD00. That function preserves the 32-bit enum and forwards it through
R9 at +0x10316/+0x10329 to +0x10700. Native callback RTTI at +0x14C950 names
this routine `CMaterial2::IssueAsyncLoadRequests`, with
`MaterialStaticComboLoadRequest_t` and `ShaderLoadMode_t` arguments. Its enum
zero branch at +0x10A74 submits native asynchronous requests and processing/
completion callbacks through +0x10B31/+0x10BAB/+0x10BE6. The nonzero branch
instead performs a synchronous filesystem read at +0x10CB0. The native clone
itself passes zero directly into +0xFD00 at +0x3AD97/+0x3ADA0. Therefore the
selected `true` follows that verified native asynchronous shader-loading mode;
it does not rely on guessing the boolean's purpose.

Material binding creation and KV3 conversion finish synchronously; native
shader-static-combo loading can complete later. The helper's ready status
reports created bindings, rather than observed GPU shader readiness. Native
request callbacks own their weak resource handle and copied material state;
the temporary KV3 arena is already consumed. Process-lifetime retained binding
references cover the asynchronous work. Per-view mode selection and normal
native shader binding handle subsequent draws. Static analysis does not claim
that shader completion or final appearance has been observed in a game run.
The verifier freezes both whole routines and exact enum-forwarding, clone
parity and callback RTTI anchors. Read-only disassembly is retained in
`logs/research/chams-create-bool*.txt`.

## Optional depth-disabled variants

There are four immutable bindings: Flat and Iridescent, each with ordinary
depth behavior and with depth disabled. Their colors use the same verified
per-instance RGBA path. `PlayerChamsMaterialsResolve` selects the depth-disabled
variant when its final `occluded` argument is true. Selection does not mutate
an existing shared material or change scene/entity state. The caller selects
one variant for the requested draw; no extra same-color pass is needed.

The depth-disabled recipes append `F_DISABLE_Z_BUFFERING = 1` and
`F_DISABLE_Z_PREPASS = 1` before the source object's closing brace. These names
come from the installed shader metadata, not an assumed material flag. The
first is feature index 1 for `csgo_unlitgeneric` and index 3 for
`csgo_character`; the prepass feature is the next index in both shaders.

Installed feature metadata maps render states `DepthEnable` and
`DepthWriteEnable` to native register type 5, IDs **0xFFFF0010** and
**0xFFFF0011**. Both expression-backed states contain the exact conditional
bytecode below, with `ii` being the independently checked feature index:

```text
1A ii 04 07 00 0F 00 07 00 00 00 00 02 14 00 07 00 00 80 3F 00
```

This is `F_DISABLE_Z_BUFFERING ? 0 : 1`: enabling the flag yields zero for
both depth testing and depth writing. The additional default-backed
`DepthWriteEnable` entry is already zero. Thus every depth-write entry exposed
by these selected shader feature tables resolves to zero for this recipe.
Normal variants retain their original flags and shader depth behavior.

The expression interpretation is cross-checked against the primary
[VRF 20.0 evaluator](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/20.0/ValveResourceFormat/Serialization/VfxEval.cs)
and its [conditional-expression test](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/20.0/Tests/VfxEvalTests.cs).
The test has the same feature/branch structure with opposite result constants;
the installed tables explicitly put float zero in the true branch and float
one in the false branch. The research source copies remain under
`logs/research/chams-material/` with the existing VRF MIT license/provenance.

`inspect_player_chams_depth.py` re-extracts both installed feature resources,
checks their full hashes, uses the provenanced VRF20 DATA reader, and verifies
the feature indexes, render-state IDs, source types, complete bytecode and
zero depth-write defaults independently. It is an optional research reproducer
requiring the existing provenanced CLI under `logs/research/vrf20/`; the main
compatibility verifier remains standard-library-only and pins this decoded
evidence through the complete installed shader hashes. Research output is
`logs/research/chams-depth/`; the initial report is
`logs/research/chams-material/depth-state-verification.json`.

These contracts establish the native material's disabled depth test/write
state. Final player visibility still depends on the native renderer emitting
the player packet and drawing it in the appropriate color phase. Frustum or
engine occlusion culling and material pass ordering are separate scene-draw
contracts; shader depth flags alone do not establish those behaviors. Live
through-wall appearance remains unobserved.

## Material selection and lifetime

`CMaterial2` RTTI identifies materialsystem2+0x129AD0. Its slot byte offset
0x30 is +0xBA30, receiving `(material, view+0x30)` and returning the native mode
variant. The player scene generator separately uses this same virtual slot;
see `CS2_PLAYER_CHAMS_EVIDENCE.md`. A material and its selected mode must be
substituted together in the current packet.

Output bindings own a reference count at +0x20: the native ownership exchange
at material+0x3ACE6 decrements the old binding, and +0x3AD0C increments the new
binding atomically. Runtime acquires a reference after validating the binding,
material RTTI and expected name, retaining all four material variants until process exit.
Stopping disarms callbacks and publication. Stopped and resident is not
physically unloaded; retaining references avoids releasing resources while
native rendering may still use queued work.

## Flat and iridescent shader inputs

The flat recipe uses installed `csgo_unlitgeneric.vfx`, `F_BLEND_MODE=1`,
`g_flModelTintAmount=1`, white `g_vColorTint` and the white color texture.
Its feature metadata declares these keys, and its vertex shader consumes the
CS2 position/UV/normal/blend layout and instance tint without requiring a
vertex COLOR attribute. `tools_solid.vfx` was investigated and rejected for
this final recipe because its vertex shader multiplies vertex COLOR, which
player meshes need not supply. Its retained research is not an active recipe
or a verified runtime dependency. Installed CS2
`csgo_character.vfx` features declare `F_IRIDESCENCE`, `F_TRANSLUCENT`,
`g_flModelTintAmount`, `g_vColorTint`, `g_flIridescentStrength`,
`g_flIridescentFresnelStrength`, `g_flIridescentHueShift` and
`g_tIridescentThickness_Mask`. The iridescent recipe enables both features,
sets model tint to 1, base tint white, strength 1, Fresnel strength 2 and hue
shift 0.5. Metadata and original DXBC assembly are retained under
`logs/research/chams-material/`; shader binaries stay in the installation.

White resource `materials/dev/primary_white_color_tga_21186c76.vtex` supplies
color, metalness and thickness-mask inputs. Normal resource
`materials/default/default_normal.vtex` supplies the normal input. Both are
installed core resources. Runtime resolves and preloads them through the
native JIT loader and requires native resource state 3 before material creation.
The verifier hashes the complete installed compiled texture resources, plus
features/VS/PS files for both shaders.

Color is the packet's existing packed RGBA at +0x50. The native scene path at
scenesystem+0x5C1AC copies that DWORD to the first field of a temporary instance;
+0x5C295..+0x5C2A0 copies its two 16-byte halves into the native GPU instance.
Original unlitgeneric VS DXBC loads instance offset zero with stride 32,
unpacks RGB/alpha, converts RGB from sRGB to linear and divides alpha by 255.
Its tint multiplier uses `g_flModelTintAmount` and `g_vColorTint` without
vertex COLOR. Original character VS DXBC performs
the same packed-color read, blending RGB through `g_flModelTintAmount` before
applying `g_vColorTint`. These establish a shader consumer for configured
packet colors. This identifies the native shared instance-buffer path and
shader consumer; a complete live GPU upload trace was not observed.
Iridescent appearance, animation/skeleton combinations and the
final live color remain subject to offline visual observation.

## Verification limits

The verifier passed against the installed binaries and eight exact shader/
texture assets. **18 deliberately wrong header variants** were rejected: all
13 numeric constants and all five entry-byte arrays. Artifacts are
`logs/research/chams-material/verification.json` and
`negative-mutations.json`. These are static contract checks, separate from the
native-adapter fixture suites and successful builds reported by the integrating
agent. They establish neither a live game run nor observed visual appearance.
