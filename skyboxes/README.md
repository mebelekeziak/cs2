# Custom skybox packs

This directory is the workspace asset root. Keep the original pack's compiled
`materials/...` paths here, including texture dependencies. The menu lists
single cubemap `.vtex_c` textures anywhere under `materials/`, alongside
installed built-in sky materials. A texture is
wrapped in the project's verified `sky.vfx` material recipe at runtime.
Arbitrary custom material shaders are not selectable.

Extract RAR packs with your archive manager, then import the folder containing
`materials/` (one enclosing folder is also accepted):

```powershell
python tools/import_skybox_pack.py 'C:\Downloads\Custom Skybox Pack v1 by LynX'
```

ZIP archives and Source 2 VPK files can be imported directly. The importer
preserves compiled `.vtex_c`/`.vmat_c` resources, checks their container layout,
rejects links/path traversal, and never overwrites existing files. After import,
use **Refresh skyboxes** in the menu. Imported names save as resource paths.

The importer does not convert Source 1 `.vtf`/`.vmt` files or PNG/JPG images.
Those need a CS2 Workshop Tools compilation step. Compiled files for another
Source 2 game may use incompatible formats/shaders. File checks establish a
candidate asset, not successful native loading or observed in-game appearance.

Retain the pack author's attribution and license separately. Assets from forum
downloads are not redistributed by this repository.
