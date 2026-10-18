# Custom player-model asset discovery

Inspected October 18, 2026. This is a **client** feature. The asset inspection
reads installed files and the user's original ZIP; it starts no client/server,
changes no installed files and does not establish successful engine loading,
animation, or appearance.

## Installation and provenance

Game directory:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
Executable: `bin/win64/cs2.exe`. The executable, `csgo/bin/win64/client.dll`,
`bin/win64/animationsystem.dll`, and `bin/win64/rendersystemdx11.dll` have AMD64
PE machine `0x8664`.

`csgo/steam.inf` reports patch **1.41.9.1**, client/server **2000933**, source
revision **11133601**, dated October 15, 2026, 10:15:52. Steam's appmanifest
reports build **25966348**.

| File | SHA-256 |
| --- | --- |
| `bin/win64/cs2.exe` | `8614097aaa2761b0a7157c0045861713e769fb1e8235f50fc68f411294dd60cf` |
| `csgo/bin/win64/client.dll` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` |
| `bin/win64/animationsystem.dll` | `fb2b4cd71468e880c3b3845e8e7a18c9d550dd3c4504e4a257a2110fb91c1235` |
| `bin/win64/rendersystemdx11.dll` | `34efae18998db8af3e9b9663006a45c3dada8dd34757bce880e27ea853ba51d8` |

The native Source 2 client is the target; no dedicated-server plugin, managed
plugin runtime, or server-framework API is involved. The project builds its
native x64 overlay with the static MSVC runtime (`MultiThreaded` in CMake).
Windows' installed `System32/vcruntime140.dll` reports 14.51.36247.0; this alone
does not establish that a running game selected that runtime. The inspected
game's `gameinfo.gi` specifies default renderer `-dx11`; both D3D11 and Vulkan
are installed. This investigation has no live loaded-renderer observation.

The user's original archive is
`C:\Users\dawid\Downloads\LynX's Custom Models [AG2].zip`. It contains 76,237
entries and 3,211 compiled model files. **All 3,211 model paths are already
present in the installation**. Its root `README.txt`, attributed to LynX,
directs copying `agents`, `characters`, `materials`, `models`, `particles` and
`phase2` into `game/csgo`. It provides no additional mounting, conversion or
animation-repair procedure. The archive and installed assets remain where the
user placed them; no redistribution or copying into the repository is needed.

The existing `gameinfo.gi` has `Game csgo`, `Game core`, and `Mod csgo` search
paths. The pack is extracted as loose resources under those existing roots.
No additional archive mount or `gameinfo.gi` edit is indicated by this layout.
Path availability is static evidence; native resource loading remains a
separate runtime result.

## Mixed compatibility in the pack

Filename suffixes and the ZIP's `[AG2]` label do not establish compatibility.
The four player-candidate roots contain **3,180** `.vmdl_c` files:

| Root | Compiled models |
| --- | ---: |
| `agents` | 243 |
| `characters` | 2,215 |
| `models` | 211 |
| `phase2` | 511 |

The other 31 models live under `materials` and `particles` and include effect
helpers. They are outside player discovery.

The bounded external-reference (`RERL`) scan finds 1,386 candidates referencing
the installed `animation/graphs/worldmodel/worldmodel.vnmgraph`, 72 referencing
other AG2 graphs, 1,055 legacy graph users, 664 other models and three helpers
without external references. **2,187** candidates have at least one missing
direct dependency. Old `characters/models/shared/animsets/animset_ct.vmdl` and
`characters/models/shared/animgraphs/player_ct.vanmgrph` are frequent missing
references. Fifty-two newer models reference unavailable custom
`animation/kxnrl/graphs/viewmodel.vnmgraph` and
`animation/kxnrl/graphs/worldmodel.vnmgraph`; examples include
`agents/fys/asuna/asuna.vmdl` and the `agents/maoling` Miyabi variant.

Resource validation requires the exact installed AG2 worldmodel graph and every
direct compiled dependency to exist as a loose resource in `csgo`/`core` or as a
valid indexed entry in their `pak01` archives. **743** candidates satisfy that
check. The catalog's arm/arms/helper/null/nohitbox filename exclusions remove
four more, leaving **739** picker candidates in the inspected installation.
These counts are discovery evidence, not an assertion that 739 appearances
were loaded or viewed successfully. Transitive material/graph payloads and
every model's rig/mesh fit are not exhaustively validated by this filter.

## Zenless Zone Zero examples

All seven resources below reference the installed AG2 worldmodel graph and have
every direct dependency present. Runtime model paths omit the compiled `_c`.

| Character | Game-relative runtime path |
| --- | --- |
| Ellen | `agents/models/hmn_ag2/zenless_zone_zero/ellen/ellen.vmdl` |
| Lycaon | `agents/models/hmn_ag2/zenless_zone_zero/lycaon/lycaon.vmdl` |
| Miyabi | `agents/models/hmn_ag2/zenless_zone_zero/miyabi/miyabi.vmdl` |
| Nico | `agents/models/hmn_ag2/zenless_zone_zero/nico/nico.vmdl` |
| Pulchra | `agents/models/hmn_ag2/zenless_zone_zero/pulchra/pulchra.vmdl` |
| Soukaku | `agents/models/hmn_ag2/zenless_zone_zero/soukaku/soukaku.vmdl` |
| Zhao | `agents/models/hmn_ag2/zenless_zone_zero/zhao/zhao.vmdl` |

The retained official [ValveResourceFormat 20.0 CLI](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.0)
under `logs/research/vrf20/` independently dumped selected model `DATA`
blocks. Its MIT license remains beside the tool; it is a research dependency,
not part of the build/runtime. Ellen has 116 model-skeleton bones; Miyabi has
121. Both include `pelvis`, `spine_3`, `head_0`, left/right upper/lower legs
and ankles, mesh-group masks `[1, 2]`, the installed worldmodel/UI/hudmodel
graphs, and the installed worldmodel/viewmodel VNM skeleton references.
Current skeleton side suffixes are uppercase `_L`/`_R`; legacy Ellen uses
lowercase `_l`/`_r`. Names alone must not be treated as interchangeable bone
indices. This is authored full-body metadata evidence for these examples.

## Parser and reproduction

`ModelResourceIndex` reads only bounded metadata: compiled model header version
12/resource version 1, a maximum 512-block directory, and at most a 4 MiB
`RERL`. Blocks and strings must stay inside the declared resource. Forty pack
models contain data appended after the declared resource; that tail is ignored
while all parsed blocks remain within the declared size. The VPK reader accepts
versions 1/2, reads only the directory tree (maximum 64 MiB), validates entries,
and checks the referenced archive's byte range or inline preload. Numbered
archive file sizes are cached; archive payloads are not read. Relative resource
paths cannot escape the installed roots.

The standalone fixtures cover loose and core resources, supported/legacy/custom
graphs, dependency absence, malformed headers/counts/offsets/strings, declared
resource tails, VPK v1/v2, numbered/inline/preload-only entries, missing or short
archives, failed refresh and path traversal. They do not launch the game.

```powershell
cmake --build build --config Release --target model_resource_validation_checks
.\build\Release\model_resource_validation_checks.exe
.\build\Release\model_resource_validation_checks.exe --scan 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo'
python tools/inspect_model_assets.py --out logs/research/model_assets/inspection.json
```

The Python tool uses only the standard library. Add `--details` for every
candidate or `--archive "C:\Users\dawid\Downloads\LynX's Custom Models [AG2].zip"`
to inspect the original archive's README/path inventory. Diagnostics are
restricted to workspace `logs/`; generated manifests and game assets are not
committed. Native engine contracts and local-pawn lifecycle are documented
separately from this asset-presence check.
