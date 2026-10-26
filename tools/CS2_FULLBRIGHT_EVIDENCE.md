# Native Fullbright view mode

Investigated October 25–26, 2026 for the Windows AMD64 CS2 client, 1.41.9.2 / client 2000935 / Steam build 26144732. Existing D3D11 callbacks are observed in project logs. This investigation reads installed files only; no game files or cvar values/flags are changed, no client/server is launched, and successful live Fullbright appearance is not yet claimed.

## User trial and root cause

The user reported no visible effect despite thousands of modified Fullbright inputs in `logs/overlay-19080-134374152043395173.log`. The original implementation intercepted native `mat_fullbright` integer producers, key `0xC6767B23`, in engine2/client. Those producers run, but they precede a separate native capability gate.

Client selector `0x1202FE0..0x12038C5` queries `Engine2/RenderingPipeline/ToolsVisModes`. Its cached capability is tested at `0x12035D3`; zero branches at `0x12035D5` to `0x1203773`, bypassing the Fullbright input read at `0x12035E6`. With no other visualization request it returns the default descriptor at `0x120367D`. Hook execution at the producers therefore did not imply effective Fullbright rendering. The capability storage is not modified by the repair.

## Coherent native selector boundary

Selector ABI: `RCX=source render context`, `RDX=CSceneView owner`, `R8=view information`; RAX returns a pointer to a native static rendering descriptor. The only admitted caller is client call `0x12090A4`, return `0x12090A9`, inside initializer `0x1208EA0..0x120944F`. Its argument window starts at `0x1209094`: R8 comes from current output+8, RDX from output[0], RCX from the current source context. The original selector must run once before the short override/normalization boundary; its full body may dispatch native rendering work and must not execute under the feature writer lock.

Native descriptor addresses are independently linked from both selector references and static constructors:

| Native descriptor | Client RVA | Token | Constructor span |
| --- | --- | --- | --- |
| CsgoForward | `0x2258030` | `0xBD79F698` | `0x128620..0x128742` |
| ToolsVis | `0x2258060` | `0xD102BCAD` | `0x128FF0..0x1290F4` |

The constructors initialize descriptor+8 from their exact strings and store the lowercased Murmur2 token, seed `0x31415926`, at descriptor[0]. The verifier recomputes both tokens and decodes constructor stores and selector LEAs. Other native specialized descriptors must retain their original result.

The selector obtains its destination context through owner virtual slot `+0x1A8` at `0x120300B`. Independently recovered scenesystem RTTI `.?AVCSceneView@@` identifies primary vtable `0x5F2178`; that slot resolves to scenesystem `0x11CD70`. Its complete eight-byte body is `48 8D 81 60 51 00 00 C3`: `lea rax,[rcx+0x5160]; ret`. It has no side effects, allocation, dispatch, or wait. Compatible derived owners are usable only when their resolved slot targets this verified accessor; sharing an object member offset is insufficient evidence.

The native ToolsVis branch uses `destination+0x110` as integer collection, calls client SetInt `0x16A9D0` at `0x12037AD` with key `0x50E7F81D` (`g_nToolsVisMode`), and returns the ToolsVis descriptor. That setter has ABI `void(collection, uint32 key, int value)`, preserves native ownership, and contains only key lookup/update and native overflow allocation. Its frozen complete span `0x16A9D0..0x16ABDE` contains no engine/render-job wait.

Immediately after selector return, the native initializer copies descriptor[0] to current output+`0x190` at `0x1209120`. It reads the same integer collection at `0x120912B`, with EDX=`0x50E7F81D` and R8D=0 (explicit native fallback at `0x120911D`), then stores the result at output+`0x1A8` at `0x1209133`. The override must publish the matching ToolsVis descriptor and integer1 together. Setting only the upstream input or only a final getter does not establish the matching shader variant and shader parameter.

## Shader proof from this installation

Original installed shader bytecode was decoded in `logs/research/fullbright-fix-20261025/shader_probe.py`; reproducible output is `verified-shader-summary.json`. All 24 decoded native ToolsVis dynamic pixel-shader variants compare `g_nToolsVisMode` against 1 and choose a textured fixed-light path. Mode 2 instead returns half albedo.

- `generic_pc_50_ps.vcs`, SHA-256 `d3496f6b90df7f1fd70640d6c9c28345d3e2914f32a3fc715bf7ff43676cdc6f`: ToolsVis static ID4, bytecode group3, eight dynamic variants, constant at CB0[2].x.
- `csgo_lightmappedgeneric_pc_50_ps.vcs`, SHA-256 `c45b654fb7d7cc53dd48827c8f3428bc0797c6a322a6de6bd87bb04fd4b3773d`: ToolsVis static ID6145, group145, 16 dynamic variants, constant at CB0[3].w.

The ordinary variants lack this constant. This supports choosing both the native ToolsVis rendering descriptor and integer1; it does not establish coverage or appearance for every material or shader.

## Context lifetime, disable and stop

CSceneView constructor scenesystem `0x11CED0` calls context constructor `0x4C4E0` at `0x11D2A0`; copy constructor `0x11EBE0` initializes a fresh inline context through the same helper at `0x11F1CD`. Destructor `0x11D5A0` calls context destructor `0x4C7E0` at `0x11DB7F`; that helper clears the integer headers and frees overflow nodes. These facts establish allocation/ownership but do not prove an existing owner resets its context before every selector call.

The initializer's own output object is fresh stack storage: its sole direct caller, client `0x11DBF60`, constructs it using `0x11CF9A0` before call `0x11DC025`. Its CSceneView owner is nevertheless an existing entry argument. No unconditional integer-map clear was found in the initializer before the selector. The repair must not rely on per-frame context reset or cache a prior owner pointer.

After the feature has first applied, every current native-default view is normalized to the native fallback0 when disabled or stopped, by the same verified native SetInt on that callback's current owner. An enabled native-default/ToolsVis view receives mode1 plus ToolsVis descriptor. Off/stopped native ToolsVis and specialized descriptors preserve native behavior. The normalization handles reused contexts without retaining stale owner pointers or changing global capabilities/cvars.

Stop disarms the enabled override, then drains the short native parameter writer/publication section. Original native rendering is outside this lock. Forwarding selector callbacks and their native-default normalization remain resident so a later view can clear an earlier1 even after stop. This is stopped and resident, not physically unloaded or detached. A submitted view may finish using its admitted mode; teardown must not split its descriptor and integer publication.

## Frozen compatibility evidence

`python tools/verify_fullbright.py` pins the complete installed AMD64 module hashes, checks all runtime fingerprints, independently decodes selector call/return, native descriptor names/stores/tokens, native output key/setter, integer collection and default fallback, and recovers the scene accessor through RTTI/vtable. It freezes complete selector, initializer, descriptor constructor, accessor and setter code scopes. Historical producer verification remains to reproduce why the first attempt could execute without changing visible rendering.

Installed modules under `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`:

| Module | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| bin/win64/engine2.dll | `e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29` | `0x6ADA3418` | `0xA15000` |
| csgo/bin/win64/client.dll | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` | `0x6ADA7340` | `0x29A0000` |
| bin/win64/scenesystem.dll | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` | `0x6ADA337D` | `0x978000` |

The installed-file verifier passes. All 16 negative header mutations reject (selector/caller, descriptor/tokens, scene accessor/slot/member, key/default/setter/identity, and all four runtime fingerprints); rejection output is retained in `logs/research/fullbright-consumer-20261025/negative-selector.json`. This is static evidence; parent integration reports builds, standalone fixture results and any subsequent live user trial separately.
