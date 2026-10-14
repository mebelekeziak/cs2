# Surface tint investigation checkpoint

October 14, 2026. Read-only investigation of installed CS2 1.41.9.0 AMD64
client assets and binaries. No client launch, injection, process read, or
installed-file modification. No surface tint runtime fix was implemented.

## Verified culprit and actual color consumer

The material worker established that aggregate DrawArray `scenesystem.dll`
RVA `0x42330` and DrawPrimitive `0x44550` do not consume draw-packet RGBA
`+0x50`. Changing those packet bytes is insufficient to tint those surfaces.

Installed CS:GO PC shader `shaders/vfx/generic_pc_50_ps.vcs`, SHA256
`d3496f6b90df7f1fd70640d6c9c28345d3e2914f32a3fc715bf7ff43676cdc6f`, has
variable index 79 `g_vColorTint`: float3, source type 0, empty attribute source
string. Source type 0 means an artist material parameter, not a per-draw
attribute. In static combo 0, its constant offset is eight float slots, i.e.
32 bytes in material shader CB0. Microsoft D3DDisassemble of the original
installed DXBC variant confirms immediately after the albedo sample:

```
mul r2.xyz, r2.xyzx, cb0[2].xyzx
```

The existing native frame shader-context mechanism therefore does not establish
that adding token `g_vColorTint` modifies this artist material constant.
The known token is `0x20A77985`, but a correct hash alone is not a consumer.

Installed `csgo_lightmappedgeneric_pc_50_ps.vcs`, SHA256
`c45b654fb7d7cc53dd48827c8f3428bc0797c6a322a6de6bd87bb04fd4b3773d`, uses
layered `g_vLayer1Tint`, `g_vLayer2Tint`, and `g_vLayerBorderTint`, source type 7
(artist parameter plus expression), rather than the same generic color field.
Do not assume one material constant offset or name covers all world shaders.

## Dynamic overlay color is a different shader variant

Both shader metadata sets expose `g_vFlatOverlayColor` as source type 1
(attribute), mapped to native context key `FlatOverlayColor`, token
`0x996C7F1E`. Generic shader variable index 80 is present in static combo IDs
4/5/6, whose `S_MODE_TOOLS_VIS` bit 4 is enabled. Ordinary static combos 0/1/2
omit it. `g_nToolsVisMode` is likewise a per-draw attribute, token `0x50E7F81D`.
Simply setting the overlay color in an ordinary material draw context would
not make it a functioning surface-tint override. Exact visualization-mode
semantics and variant selection remain unverified.

## Native bind path resume entry points

Material worker supplied native context constructor `scenesystem.dll 0x4C4E0`
and destructor `0x4BD70`, size `0x3A0`, parent context pointer `+0x380`, hash
`+0x388`. Aggregate DrawArray constructs/reuses it and chains ordinary scene
parameters; see `logs/research/material-drawarray-full.txt`.

`materialsystem2.dll` CMaterialMode bind thunk `0x16700` forwards into
`0x15830`, with a fifth stack boolean, and adjusts incoming R8 by `+0x10`.
The bind implementation uses frame-context and ancestor hashes to cache
generated bindings, then calls `0x15D40` at `0x15BA2` on cache miss.
`0x15D40` calls dynamic combo evaluation `0x2AA70`, pipeline/constant metadata
helpers `0xA58D0` / `0xA5D70`, and builds queued native output via `0x13A10`.
`0x13A10` retains underlying shader/native state pointers in its output; this
does not yet establish that a temporary stack context or material copy would
outlive all queued consumers. No safe artist-color constant-copy hook, full
shader coverage, or shutdown/lifetime barrier has been established.

Resume from the original shader CB0 consumer and native constant-packing/upload
path, proving frame-owned storage and cache invalidation before adding runtime
surface tint. A GPU constant-buffer copy scoped to exact verified world shader
variants is another possible approach, but it is not implemented or validated.

## Saved reproducible probe and provenance

`python tools/inspect_surface_tint.py` passed at this checkpoint. It re-extracts
the two installed shaders read-only into workspace research logs, verifies
their frozen hashes, prints their original KV3 DATA metadata, decodes the
original generic DXBC variant, and checks its real multiply instruction.
Result: `logs/research/surface-tint-report.json`; shader assembly:
`logs/research/surface-generic-ps0.asm`; complete original metadata:
`surface_generic_ps.metadata.txt`, `surface_lightmapped_ps.metadata.txt`.
Native continuation logs include `surface-bind-continuation.txt`,
`surface-bind-generator-complete.txt`, and `surface-variable-consumers.txt`.

Research tool: official [ValveResourceFormat 20.0 release](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.0),
Windows CLI under `logs/research/vrf20`; SHA256
`a4bb4f9945db985efb2cad90a90d06867de5ba7e0447a2a4a69a07dace781028`.
Its MIT LICENSE is retained in that directory. Its DATA reader handles the
original KV3 metadata; its VFX decompiler rejects VCS version 72, so the probe
uses the original DXBC rather than relying on an unsupported reconstruction.

Format provenance: official
[variable source enum](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/CompiledShader/Enums/VfxVariableSourceType.cs),
[static combo metadata](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/CompiledShader/VfxStaticComboData.cs), and
[compressed stream framing](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/CompiledShader/VfxStaticComboVcsEntry.cs).
The original Zstd dictionary `vfx_zstd_2bc2fa87.bin` from that project is
retained beside its LICENSE, SHA256
`e7ab9ea47f213132dce97bcf18c77c5fed91bea765af1ceb4b580ed0bf660f5b`.
Research-only Python `zstandard` is installed under
`logs/research/python-deps`; it is not a runtime/build dependency.

The probe creates workspace diagnostics only. Shader metadata and static
disassembly prove the parameter source and consumer, not live appearance.
