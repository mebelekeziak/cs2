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

## Loadout editor

The revised Skins tab uses direct T/CT and Weapons/Knife/Gloves switches. At
normal menu width, the searchable item browser sits beside the selected item's
summary and compatible finish picker. Narrow windows stack the two sections and
use compact team labels. Blue item names mark saved overrides. Browsing a weapon
does not allocate a config record or change a loadout. Selecting a knife/glove
model or a finish activates the choice, including resuming paused customization.

**Pause** retains both loadouts while suspending application; **Resume** restores
their enabled overrides. **Use stock item** clears only the browsed item on the
selected side. **Wear & details** groups wear, seed, StatTrak and custom name
controls. Name editing retains unfinished text between keystrokes, so typing an
internal space no longer loses it before the next letter. Valid text publishes
through the existing settings validator; invalid drafts do not enter configs.

Doppler variants receive readable phase/gem labels derived from exact catalog
tokens. Paint IDs remain visible for precise identification and search. Default
T and CT knives have distinct labels. Every weapon stays available on either
side for picked-up weapons. Runtime status and the action to reset both loadouts
are under **Loadout options**. The editor preserves the native application code
and config field schema of the working checkpoint `d0e1746`.

The UX revision passes all 66 suites in both Release builds. Mouse tests cover
automatic activation, independent sides, pause/resume, stock reset while paused,
resetting both loadouts, opposing-side weapons, exact Doppler phase selection,
incremental spaced-name entry, invalid drafts, compact team controls and config
restoration. Standalone software D3D11 previews were inspected at normal and
narrow viewport widths, including the expanded details section:

```powershell
build/Release/public_menu_preview.exe logs/skins-loadout.bmp 20
build/Release/public_menu_preview.exe logs/skins-details.bmp 23 220
build/Release/public_menu_preview.exe logs/skins-narrow.bmp 20 0 240 500
```

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

### Knife animation correction

The user's subsequent trial reports missing knife-specific animations while
the other skin behavior works. The initial implementation changed the item
definition and model without changing the weapon's separate subclass/VData.
Native animation state reads that VData's animation-class/name and skeleton.
All 22 selectable installed knife definitions have matching, distinct subclass
entries and animation skeletons in `scripts/weapons.vdata_c`.

The correction resolves the selected item's descriptor with the installed
native item-to-subclass getter. It validates descriptor scope, class, ID and
canonical item name. It captures original subclass ID and VData together with
the original item/model. The selected model is installed before publishing the
new subclass ID and invoking the named native subclass-change callback: the
weapon lifecycle compares the current weapon and HUD-model bindings, retires a
stale HUD model, and lets native recreation use the selected subclass data.
It refreshes VData through engine code rather than assigning a borrowed data
pointer directly or forcing a guessed animation sequence.

Restoration runs on the owner callback with the original model/subclass. Native
identity changes invalidate cached state. Stable settings check for native
subclass drift and repair it; cosmetic-only edits do not reset a matching
subclass repeatedly. Per-transition `SKIN_KNIFE_ANIMATION_ASSIGNED` and
`SKIN_KNIFE_ANIMATION_RESTORED` log records identify the entity handle, item
definition, old/new subclass IDs, VData names and owner thread. The UX and
persisted field schema are unchanged.

The new installed-file check is `python tools/inspect_knife_animations.py` and
is included in the combined compatibility bundle. See
[the native contract](../tools/CS2_KNIFE_ANIMATIONS_EVIDENCE.md). Post-fix animation
playback remains unobserved until an authorized offline trial.

Verification for the animation correction: public and diagnostic x64 Release
builds each pass **66/66 CTest suites**, including **127** native cosmetic
assertions. All **33/33 installed-file checks** pass; **22** altered animation
headers reject. Builds have no project compiler warnings and both DLLs retain
zero static TLS/delay imports. Fixtures cover model-before-subclass/HUD ordering,
different CT/T subclasses, native ID/VData drift, cosmetic-only edits, descriptor
validation, reentrant stop, serialized identity changes, failed restoration and
callback-induced geometry changes. No game or server was launched.

### Initial skin and editor verification

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

The user subsequently reports that the skin changer "works nicely" in their
session. This is positive user observation, without a recorded per-item visual
test matrix. No client/server was launched by the agent for this change.
First-person knife rendering, animations, glove appearance and every
legacy-paint mesh variant have not been independently audited in-game.
The native material generation handles legacy materials and the installed
legacy helper feeds animation state, but the audit does not independently
establish every legacy mesh selection. No guessed old viewmodel or mesh-mask
offsets were introduced. Test these cases in the authorized offline environment
before treating appearance as verified.
