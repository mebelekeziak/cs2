# Team-specific cosmetic customization

This feature targets the existing Windows AMD64 CS2 client/D3D11 overlay.
Installed baseline: patch 1.41.9.2, client/server 2000935, Steam 26144732.
The installed-file inventory and runtime contracts are separate from observed
gameplay: no new client or server launch is needed for the automated checks.

## Settings and persistence

`skin_settings.h` defines independent T and CT loadouts. Each includes up to
128 sparse weapon records keyed by item definition ID, plus its own knife and
glove record. Paint kit, wear, seed, enabled state, StatTrak and name belong to
each record. UI navigation does not copy settings between sides. The runtime
selects the side from the current local pawn rather than the menu editor.

Settings start disabled. Names accept printable ASCII only and reject outer
spaces, configuration delimiters and comment markers. Sanitization bounds wear
and seeds, rejects invalid definition IDs, and collapses duplicate sparse weapon
identities. Runtime application additionally requires an installed compatible
definition/paint pair; arbitrary config IDs are not native function arguments.

The existing atomic config writer saves `skins.enabled`, `skins.t.*` and
`skins.ct.*` fields. Knife/glove records use `.knife`/`.gloves`; weapons use
`.weapon.<sparse slot>`, with `.definitionIndex` storing the actual stable ID.
All records, including disabled choices, are saved. Loading parses into a local
snapshot and publishes only after the entire file succeeds. A one-time field
index avoids scanning thousands of fields again for every input line. Unknown
keys remain compatible with the existing config policy. Missing keys use
defaults; old presets clear overrides rather than inheriting the current skin
loadout. Saving/loading is explicit in Configs; startup does not auto-load.

## Installed catalog

`tools/generate_skin_catalog.py` reads the installed `pak01` VPK resources
without modifying them. `scripts/items/items_game.txt` supplies definition IDs,
inherited prefab metadata, model paths, paint IDs and wear bounds. English
localization supplies display names. Generated inventory-icon names and
collection/loot-list references establish the compatible paint pairs.

The parser merges repeated top-level sections and resolves prefab inheritance.
It matches exact definition/paint names so M4A4/M4A1-S and identically named
Doppler phases remain distinct. Generated source is deterministic and records
the schema/localization hashes and `steam.inf` identity. No third-party skin
list is used. Valve item/localization names retain their source attribution.

The generated catalog contains 68 definitions (35 weapons, 23 knives, 10 gloves),
1,479 non-default finishes and 2,128 compatible non-default pairs. All
non-default paints have at least one supported definition. The unused
`weapon_knifegg` definition remains in the metadata but is hidden in the picker.
Default T/CT knives and gloves retain their installed default-only finish sets.

Run `python tools/generate_skin_catalog.py --check` to compare checked-in data
with the installed resources. Regeneration is explicit and the resulting source
diff should be reviewed after an update. This verifies metadata, not native
runtime compatibility or the appearance of a loaded item.

## Native application and lifecycle

`skin_changer.cpp` runs after the original shared stage-8 callback. Preparation
checks the installed client PE identity and native function fingerprints.
Application verifies the current local controller/pawn relationship, complete
serialized entity handles and weapon ownership. Selection comes from the pawn's
current T/CT team. Changed or unavailable lifecycle sources invalidate cached
ownership; old entity pointers are not reused for restoration.

Cosmetic attributes use the verified native name setter and ID remover. The
engine allocates its attribute records. The changer owns only paint, seed,
wear, kill count and score-type attributes and selected item scalar fields.
Other attributes remain intact. Restoration preserves both current and initial
attribute values. Duplicate or set-bonus variants of owned attributes are
rejected because the native setter/remover cannot round-trip their semantics.

Material refresh follows the installed `regenerate_weapon_skins` command's
native sequence: reset the entity composite owner, then regenerate the weapon.
Knife definition changes also preload the selected installed model through the
existing verified resource loader and call the native model setter. Glove
changes edit the embedded economy item and request the native glove attachment
rebuild. Paint and seed are numeric floats; StatTrak is an integer bit payload,
matching the installed native fallback initializer.

Blocking knife loads recheck settings, team, life state and exact source
identity before applying. Disabling or stopping requests restoration through
subsequent native owner callbacks. Incomplete rollback remains pending;
unexpected error-model assignments restore their original model. An independent
native replacement supersedes earlier ownership. Disconnect or serial reuse
invalidates stale state. Code and callbacks remain resident until process exit;
stopping does not physically unload the DLL.

## Verification and observation limits

Both public and diagnostic x64 Release builds compile without project warnings
and each passes **66/66 CTest suites**. All **32/32 installed-file checks** pass.
The native cosmetic suite passes **55 checks**, settings pass **149**, compiled
catalog validation passes **10,945**, and Python parser tests pass **7/7**.
Both DLLs retain zero static TLS and delay-import directories. Automated checks cover
the catalog parser and full compiled catalog, settings, config transactions,
real mouse-driven menu choices, native ownership/lifecycle simulation and model
preloading. Software D3D11 previews show distinct T/CT knife choices. Installed
verification covers nine named schemas, fourteen native code windows and the
catalog's exact reproduction from installed data. Forty-five altered native
contract headers are rejected.

These checks do not establish in-game appearance. No client/server was launched
for this change. In particular, first-person knife rendering, animations,
glove appearance and all legacy-paint mesh variants remain visually unobserved.
The native material generation handles legacy materials and the installed
legacy helper feeds animation state, but the audit does not independently
establish every legacy mesh selection. No guessed old viewmodel or mesh-mask
offsets were introduced. Test these cases in the authorized offline environment
before treating appearance as verified.
