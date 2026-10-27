Dependencies are fetched without modification at these immutable commits:

* Dear ImGui v1.91.9b: https://github.com/ocornut/imgui/tree/f5befd2d29e66809cd1110a152e375a7f1981f06 (MIT).
* MinHook v1.3.4: https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537 (BSD; bundled HDE licenses are in its LICENSE.txt).

Original licenses remain in build/_deps and are copied beside the built DLL in licenses/.

The generated `src/features/skins/skin_catalog_data.inc` contains cosmetic metadata derived
from Valve's installed CS2 `scripts/items/items_game.txt`, English localization,
and inventory resource names. It retains source attribution, extracted-resource
hashes and the installed version in its header. No Valve models, textures, VPK
archives or complete item/localization source files are redistributed. The
extractor and catalog implementation are project code; no external skin catalog
or skin-changer source was copied.

The initial build-specific entity/schema constants were generated locally with
[a2x/cs2-dumper 0.1.3](https://github.com/a2x/cs2-dumper/releases/tag/0.1.3)
(MIT; copyright 2024 a2x). Its license is preserved in
`third_party/cs2-dumper-LICENSE.txt` and copied beside the built DLL. The dumper
is an investigation tool, not a DLL runtime dependency. Exact binary hashes,
source reference, dump provenance and installed-client disassembly evidence
are documented in `tools/CS2_ENTITY_EVIDENCE.md`. Current 1.41.9.1 constants
were independently recertified from installed PE schema templates and machine
operands without a new dumper run; the historical provenance/license is retained.

The Euler rotation uses the conventional pitch/yaw/roll mathematical formula,
cross-checked against Valve's Source SDK `AngleMatrix`. No SDK source code is
vendored or linked; Source 2 node fields come from the installed client's schema.

The optional custom character assets are supplied by the user as
`LynX's Custom Models [AG2].zip` and were already extracted in the CS2
installation. The pack's original `README.txt` attributes its curation to LynX.
No pack assets or third-party models are copied into this repository or its
build outputs. Original pack metadata and authorship stay with the user's
installation; inspection/provenance is recorded in
`tools/CS2_MODEL_ASSET_EVIDENCE.md`.
