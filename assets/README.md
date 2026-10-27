# Local CS2 HUD icons

Weapon and C4 icons use the actual installed CS2 HUD silhouettes from
`panorama/images/icons/equipment/*.vsvg_c` in `game/csgo/pak01_dir.vpk`.
The importer reads each resource's plain SVG DATA block and converts its original
Bezier outlines into a TrueType font. It preserves silhouette proportions and
uses Skia PathOps to normalize intersecting even-odd contours so interior holes
remain transparent. Both conversion packages run only in the importer.
Unsupported transforms, styling, strokes or SVG elements fail the import.

Generate the local font before building or launching this overlay:

```powershell
python -m pip install fonttools==4.59.1 skia-pathops==0.9.2
python tools/import_weapon_icons.py
```

For another installation, pass `--game 'D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game'`.
The default output is `logs/weapon-icons/cs2-hud-icons.ttf`; the neighboring JSON
records source resource paths, SHA-256 hashes and contour/bounds information.
Both are ignored local artifacts. No Valve artwork or generated font is
distributed in this repository. Installed game files are read without changes.
Rerun the import after a CS2 update to pick up changed HUD artwork.

The build supplies `CS2_WEAPON_ICON_FONT` with this font's absolute workspace
path. `WeaponIconConfigure()` reads it once per ImGui atlas before texture
creation. Glyph lookup always uses the current atlas, so destroying/recreating
an ImGui context cannot retain a stale font pointer. A missing font omits icons;
it never substitutes fabricated silhouettes. Held weapons, carried/dropped C4,
and planted C4 use their matching artwork. Drawing preserves the original aspect
inside the reserved ESP area, applying the configured color and opacity.
