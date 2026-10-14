# World materials and lighting checkpoint — October 14, 2026

The user requested a pause to conserve usage. Do not treat this checkpoint as a
finished or live-verified fix. All investigators stopped; no game was launched.

## Saved baseline and observed behavior

- `70c0544` commits the previous world visuals before this follow-up.
- User confirms sky tint and brightness work. Surface override/flat textures have
  no visible effect; the prior sky lighting control did not deliver the desired
  scene lighting changes.
- `logs/overlay-40608-134364575022549814.log` shows 9,160,911 material callbacks,
  zero changed packets and zero recorded rejects. Old counters omitted early
  exits, so this does not identify which guard blocked the run.

## Implemented follow-up, still requiring integration verification

- Materials: removed unsupported bit24/bit32 view classification; native material
  mode resolution determines compatibility. Added bounded counters for every
  early exit and flat binding/mode failures. Removed unused packet RGB writes.
- **Surface tint remains unfinished.** Aggregate draws do not read packet RGB.
  Actual `g_vColorTint` is an authored material constant consumed at shader CB0
  byte32; injecting a generic draw parameter is insufficient. See
  `tools/CS2_SURFACE_TINT_EVIDENCE.md` and `tools/inspect_surface_tint.py`.
  Resume native material Bind/constant packing and GPU lifetime analysis before
  implementing a real tint override. Do not claim the existing tint slider works.
- Native local light color/brightness: point/spot/orthographic generated radiance,
  plus the latest area/barn GPU-record extension. Native source geometry updates
  are preserved. Area extension is written and binary-verified, **not built/tested**.
- Local-light disable: mirrors native point/spot and area gather early returns.
- Sun color/brightness and Game/Enabled/Disabled directional lightmaps: changes
  freshly generated, verified current-thread stack view constants, no scene/cvar
  writes. Directional mode Enabled maps to native float0; Disabled to float1.
- Shared settings and fourth **Lighting** tab are integrated in source. Working
  sky backend is unchanged. Reset lighting preserves the other world settings.
- New modules are wired into CMake and overlay preparation/activation/stop/logging.

## Checks actually completed

- Material revision: 27 standalone checks and frozen binary verifier pass.
- Local-light switches: Release target built; 8 checks pass.
- Sun/directional backend: Release target built and its CTest passed.
- Public menu/preview targets built. **public_menu_checks currently fails two
  new checks:** disabled lighting master blocks local-light control; local-light
  checkbox publishes native suppression setting. These use synthetic
  `ActivateItemByID`; investigate with real mouse input and correct item geometry.
- Last targeted CTest: material, switches, sun passed; menu failed (3/4 passed).
- No complete public/diagnostic follow-up build, full CTest pass, or live test.
  The existing overlay DLL may still be the previous baseline. Build and validate
  before loading this revision; stopped code remains resident until normal exit.

## Resume order

1. Repair the two menu checks and inspect the fourth-tab preview. Cover sun/mode
   reset and native settings bounds. Update local-light scope label for area support.
2. Build/test latest `world_lighting_checks` area extension; fix any failures.
3. Complete safe surface tint binding, or explicitly disable/remove its unsupported
   UI until implemented. Keep flat-material fixes and detailed runtime telemetry.
4. Run public/diagnostic Release builds and all 16 current CTest suites; run
   `verify_world_materials.py`, `verify_world_lighting.py`,
   `verify_lighting_switches.py`, `verify_environment_lighting.py`, and sky verifier.
5. Update README to the final supported behavior and actual results. Observe an
   authorized disconnected/offline/private run and inspect new stage counters.

Research reports: `CS2_LIGHTING_EVIDENCE.md`, `CS2_ENV_LIGHT_EVIDENCE.md`,
`CS2_BAKED_LIGHT_EVIDENCE.md`, and `CS2_SURFACE_TINT_EVIDENCE.md` under `tools/`.
Official ValveResourceFormat20.0 investigation tools, license and extracted
shader evidence remain under ignored `logs/research/vrf20` and related log paths.
