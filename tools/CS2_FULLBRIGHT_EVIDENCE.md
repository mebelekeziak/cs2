# Native Fullbright source recipe

Investigated October 25–26, 2026 for the Windows AMD64 CS2 client, 1.41.9.2 / client 2000935 / Steam build 26144732. Existing project logs identify D3D11 rendering. The investigation reads installed files only. No installed files, cvars or rendering capability storage are changed. This static evidence does not claim a successful live trial of the repaired appearance.

## User trials and corrected approach

The first attempt changed only the native `ToolsVisMode` source integer (`0xC6767B23`). It ran thousands of times without changing the visible lighting. The subsequent attempt forced the ToolsVis rendering descriptor and `g_nToolsVisMode=1`; the user reported completely black world geometry while the sky and HUD remained visible. The debug label confirmed entry into a ToolsVis variant, which did not establish that the complete native rendering inputs were present.

The installed native `mat_fullbright=1` producer also supplies white ambient light and disables direct and indirect lighting. Those source values were missing from the earlier approaches. ToolsVis selection is independently capability-filtered, so its rejection does not imply that native Fullbright1 requires forcing that diagnostic rendering mode. The repair follows the complete native lighting recipe and preserves the original selector result, the marker integer value and all capability/cvar state. No ToolsVis descriptor, debug integer or global capability is forced.

Historical selector addresses remain in the header/verifier to reproduce the failed attempt. They are not the runtime hook contract for this repair. Earlier research described ToolsVis mode2 as half albedo; the decoded shader instructions instead show half of a previously computed lighting result. Mode2 is not used by this repair.

## Exact admitted source callback

Client native SetInt at `0x16A9D0` has ABI `void(integerCollection, uint32 key, int value)`. Runtime admission requires both key `0xC6767B23` and return address `0xE5B0E4` from the call at `0xE5B0DF`. The source producer window `0xE5B0CC..0xE5B0E4` loads the original `mat_fullbright` local from `[RBP+0x1700]`, passes collection `[RBP+0x6C0]`, and writes the marker. The original marker value is forwarded unchanged. The auxiliary client producer and engine producer remain historical evidence; they are not admitted callbacks.

The current source root is exactly `integerCollection-0x110`, or native stack `[RBP+0x5B0]`. While enabled, the repair updates these three existing source keys using native setters:

| Source key name | Native token | Fullbright1 value |
| --- | --- | --- |
| AmbientColor | `0xFF90C40E` | float4 `(1,1,1,1)` |
| DirectLighting | `0x1F900C45` | integer `0` |
| IndirectLighting | `0xDB409112` | integer `0` |

The name tokens are independently recomputed using the installed lowercased Murmur2 contract and seed `0x31415926`. `ToolsVisMode` independently hashes to the admitted marker key.

Client float4 setter `0x16AE00..0x16B00D` has ABI `void(rootContext, uint32 key, const float4*)`. Its entry preserves R8 as the vector address and EDX as the token, searches the root float4 collection, and copies exactly 16 bytes with `movups` at `0x16AFF7/0x16B004`. It must receive the root, whereas SetInt receives root+`0x110`. Native lookup/update paths and overflow allocation remain owned by the engine. Both setter bodies and runtime entry fingerprints are frozen by the verifier.

## Native producer and reset proof

The main native call starts at `0xE599E0`; its chained unwind/code regions extend to `0xE5B4AB`. Its prologue establishes RBP relative to its private stack frame and allocates `0x17D8` bytes. The source root `[RBP+0x5B0]` is cleared at `0xE5A4E3..0xE5A4F7` (size `0x1F8`) and initialized through `0x15AC60` at `0xE5A7ED`. Optional native source copying at `0xE5A805` precedes the lighting resets below. All later exits from the admitted region reach root destruction through `0x15BE40` at `0xE5B47E`, before restoring the stack/registers at `0xE5B483` onward. The hook never retains this stack pointer.

Every admitted producer visit first writes AmbientColor at `0xE5AFF7`, using constant `client+0x1AC80E0`: `(0,0,0,1)`. The cvar value is read at `0xE5AFEE` and saved to `[RBP+0x1700]`. `sub ecx,1; je` at `0xE5B002..0xE5B007` selects the actual native Fullbright1 branch:

- `0xE5B080` loads constant `client+0x1AD2FC0`: `(1,1,1,1)`.
- `0xE5B09F` writes it to source AmbientColor.
- `0xE5B0A4` clears R8D; `0xE5B0B3` writes DirectLighting=0.
- `0xE5B0B8` copies EDI into R8D; `0xE5B0C7` writes IndirectLighting=0. EDI was explicitly zeroed at `0xE5A9D8` and is not modified before this branch, as frozen in the complete owner scope.

For ordinary cvar0, the branch at `0xE5B020` falls through `0xE5B022..0xE5B041`: the native direct/indirect boolean cvar values initialize both keys via SetInt at `0xE5B02A/0xE5B0C7`. Fullbright2 initializes these keys through its separate branch at `0xE5B043`. Thus all three recipe keys already exist before marker return `0xE5B0E4` for every admitted native path. The repair updates existing current-source values after native initialization. Feature-off and Stop forward that freshly initialized source; they require no retained owner pointer or persistent-view integer normalization. A render task already submitted with the enabled recipe may finish.

## Source consumers and preserved forward rendering

The native view initializer `0x1208EA0..0x120944F` obtains the current owner's inline output context through its verified virtual accessor at `0x1208EF0`, keeping the result in R13. It calls the original selector at `0x12090A4`, return `0x12090A9`, with source `[RBX+0x38]`. The repair leaves this entire selection path intact. The cached ToolsVis capability test at `0x12035D3` may bypass the diagnostic marker read and retain CsgoForward.

After selection, the same initializer unconditionally calls `0x1218720` at `0x12092BA`: RCX=R13 destination root, RDX=`[RBX+0x38]` source root. This helper resolves source AmbientColor through the key vector at `client+0x1D30A80` (four `0xFF90C40E` tokens), including inline/overflow/parent lookup. Its resolved vector is written at `0x1218836` to destination ambient shader key `0x8310AEF1`. The latter key's spelling is unresolved; its frame ambient RGB destination is established by the existing native buffer builder evidence (`logs/research/ambient-lighting-report.json`, frame offset `0x120`). This bridge also runs when the original selector returns CsgoForward.

DirectLighting/IndirectLighting are read from source collection+`0x110` at client `0x121B8DD/0x121B8F1`. Their zero values set the native render job mask bits `0x40000/0x80000` at `0x121BB75..0x121BB8D`. The scene path independently consumes the same keys at `0xBACCC/0xBACE2`. Detailed copies are in `logs/research/fullbright-black-20261026/client-lighting-flags.txt` and `scene-lighting-flags.txt`. Ambient propagation and native recipe logs are in `logs/research/fullbright-native-mode2-20261026/ambient-fullbright1-chain.json`.

## Compatibility verification

`python tools/verify_fullbright.py` pins the installed module identities, independently resolves named mat_fullbright storage, exact admitted call/return/key, root-versus-integer operands, both setter targets, all source key name hashes, white/default float4 constants, the native Fullbright1 branch and ordinary source resets. It freezes the complete source owner, float4 setter and ambient bridge scopes alongside the historical selector evidence. The header contains runtime fingerprints for the admitted setter/producer, float4 setter, native recipe, ambient reset and ordinary lighting reset.

Installed modules under `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`:

| Module | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| bin/win64/engine2.dll | `e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29` | `0x6ADA3418` | `0xA15000` |
| csgo/bin/win64/client.dll | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` | `0x6ADA7340` | `0x29A0000` |
| bin/win64/scenesystem.dll | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` | `0x6ADA337D` | `0x978000` |

Static verifier and negative mutation results are reported separately from integration builds, callback fixture tests and the next user appearance trial. Cooperative Stop disarms the source recipe and drains admitted native writes; forwarding callbacks remain resident until normal process exit. Stopped and resident is not physically unloaded.
