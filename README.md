# CS2 third person, anti aim, silent aim, world visuals, movement assistance, player ESP and D3D11 overlay

x64 client overlay with local player model selection, third person, configurable anti aim, silent aim, movement assistance, player ESP and an explicit manual mapper. Installed game files are not modified. Existing ESP appearance work was preserved before silent aim in commit `2e8e1c7`.

For the next CS2 update, start with the [update guide](docs/CS2_UPDATE_GUIDE.md)
and [RE address index](docs/RE_ADDRESS_INDEX.md). One command checks all twenty-seven
components independently and saves build identities, source hashes and detailed
reports under `logs/compatibility/`:

```powershell
python tools/check_compatibility.py
```

**CS2 update, October 21, 2026:** compatibility is recertified for installed
patch **1.41.9.1**, client/server **2000934**, Steam build **26087496**. All
**27/27 installed-file checks pass**. No consumed runtime addresses, layouts or
hook fingerprints changed. Updated exact build identities and three scope
consumer verification windows whose native data references moved. Public and
diagnostic x64 Release builds succeed without project warnings; both pass
**56/56 CTest suites**. Both DLLs retain zero static TLS/delay imports. See the
[update record](docs/CS2_UPDATE_26087496.md) for evidence and validation. No
client/server was launched; live behavior on this build remains unobserved.
Use the existing **Load Overlay.cmd** workflow, restarting CS2 normally first
if an older DLL is resident. Stopped and resident is not physically unloaded.

## Visualize ragebot

Under **Aim**, enable **Visualize ragebot** and choose **Point color**, including
opacity. The option starts disabled with white points and saves in local
configs as `aim.visualize` and `aim.visualize_color.0` through `.3`. Older
presets restore these defaults.

Small outlined dots show the **damage-path scan**, before hitchance and firing
timing: a selected hitbox center must be in aim FOV/range, have a valid bullet
path and meet minimum predicted damage. With **Autowall** enabled, penetrable
cover uses the existing native penetration/damage checks. Blocked or insufficient
damage points stay hidden. Reachable points can appear while moving, below
hitchance, braking, or waiting for weapon cooldown. They indicate potential
damage, not permission to fire this command. All markers use one chosen color;
multipoints are not added. Points work with chams and ESP disabled.

The visualization shares the command-owner target enumeration and weapon sample.
It checks copied candidates for damage before shot-ranking shortcuts, including
lower-ranked alternatives. This adds path/damage work while enabled; both
shot-ranking shortcuts and final shot rechecks remain in place. The preview
does not synthesize attack, change angles or acquire quickstop movement.

Enable **Silent aim** alongside visualization. Damage scans run before a shot
with **Autofire** either on or off, and continue during cooldown with a supported
loaded weapon. Preview-only commands skip hitchance sampling. The actual
autofire/manual-shot gates retain their existing behavior. Empty clips,
unavailable weapon data, empty scans, focus loss, local pawn replacement/death,
visualization disable and cooperative stop clear or hide the points. Copied
centers expire after 100 ms if callbacks stop; rendering also checks current
serialized player handles and hides snapshots made with different shot settings.
No native pointers cross into the point renderer.

Use the existing build and **Load Overlay.cmd** workflow. Restart CS2 normally
before loading the rebuilt DLL if an older copy is resident: **stopped and
resident is not physically unloaded**. Software D3D11 previews, without CS2:

```powershell
.\build\Release\public_menu_preview.exe .\logs\aim-visualization-menu.bmp 4
.\build\Release\public_menu_preview.exe .\logs\aim-visualization-points.bmp 17 0 1000 600
```

The point preview uses a synthetic silhouette and copied centers; it checks
the production point renderer, not live game appearance.

The standalone preview supplies synthetic damage-path centers directly to the
draw path. Integrated pipeline fixtures separately verify path filtering and
visibility before firing. The reference investigation and the distinction from
shot readiness are recorded in [visualization notes](docs/AIM_VISUALIZATION.md).

**Verification, October 20, 2026:** public and diagnostic x64 Release builds
succeed without project warnings and each pass **56/56 CTest suites**. The aim
pipeline passes 299 checks, including visibility before manual trigger input,
below hitchance, during quickstop/cooldown, and unchanged input/attack ownership.
Blocked, insufficient-damage, empty-clip and stale candidates are covered. The
visualization suite passes 23 checks; RGBA/config tests and software D3D11
previews pass. Both DLLs retain zero static TLS/delay imports. No CS2 client or
server was launched; live point appearance remains unobserved.

## Ragebot autowall

Under **Aim**, enable **Autowall** alongside **Autofire** to allow shots through
penetrable cover. It starts disabled and saves as `aim.autowall` in local
configs; older presets leave it disabled. **Minimum damage** applies after
penetration, range loss, hitgroup scaling and armor. Candidate ranking,
hitchance and quickstop use that resulting damage. With the toggle off, the
existing direct-path requirement applies.

Blocked central rays use the installed client's native multi-hit bullet trace
and penetration calculation, including surface properties, thickness and the
native four-penetration budget. Another player's body blocks this prediction.
The complete path is evaluated again immediately before publishing an automatic
shot; changed geometry, settings or damage inputs cancel the owned attack.
Direct shots keep the existing fast path. Manual fire keeps its existing behavior.
Hitchance models the selected capsule and spread; it does not simulate every
spread ray through cover. Client prediction is not a guarantee of server damage.

Build and load using the existing commands and **Load Overlay.cmd**. Restart
CS2 normally before loading a rebuilt DLL if an older copy remains resident;
stopped and resident is not physically unloaded. This adds no hooks or game-file
changes. See [native autowall evidence](tools/CS2_AUTOWALL_EVIDENCE.md). Run
`python tools/inspect_autowall.py` or the combined compatibility command to
check the installed contracts. Development and testing remain offline/private.

**Verification, October 20, 2026:** public and diagnostic x64 Release builds
succeed without project warnings, each passing **55/55 CTest suites**. The
autowall adapter passes 358 synthetic checks; damage and integrated pipeline
suites pass 230 and 235 checks. All **27/27 installed-file compatibility checks**
pass, and 50 altered autowall contracts reject. Both menu previews were inspected.
Both DLLs retain zero static TLS and delay imports. No CS2 client/server was
launched; live penetration and damage accuracy remain unobserved.

## Smoke, flash and grenade border auras

Under **Visuals → Grenades**, enable **Remove smoke**, **Remove flash**,
**Smoke border aura** and **Molotov border aura** independently. Each starts
disabled and saves with local configs. Both auras have separate RGBA colors;
**Aura width** controls their glowing border from 1–6 pixels. **Reset grenades**
restores defaults. Older presets use disabled grenade settings.

Borders appear only after smoke deploys or fire ignites. Smoke follows live,
positive-density voxel columns projected onto the detonation plane. Molotov
and incendiary fire follow the union of active flame patches using their native
visual half-width, retaining gaps and separate islands. These are footprint
outlines, rather than exact animated silhouettes or guaranteed damage boundaries.
The overlay draws them through scene geometry. Smoke aura remains available
with smoke removal enabled; all four settings work independently of player ESP.

Smoke removal suppresses native volume/mask draws and the inside-smoke opacity
result, preserving simulation data. Existing inside haze may briefly decay
through native interpolation when enabling it. Flash removal suppresses the
white overlay and afterimage shader alpha inputs while native timers and sound
continue. Neither feature writes persistent grenade/pawn state or game files.
Area snapshots update at most every 30 ms, expire after 250 ms, and clear on
disable, source replacement or cooperative stop. Scanning and geometry work
use a separate lock from the short render publication.

Use the existing build and **Load Overlay.cmd** workflow. Restart CS2 normally
before loading a rebuilt DLL if an older copy remains resident: **stopped and
resident is not physically unloaded**. Static contracts are recorded in
[area evidence](tools/CS2_GRENADE_AREAS_EVIDENCE.md) and
[removal evidence](tools/CS2_GRENADE_EFFECTS_EVIDENCE.md), with both verifiers
included in the combined compatibility command. Standalone software D3D11
previews, without launching CS2:

```powershell
.\build\Release\public_menu_preview.exe .\logs\grenades-menu.bmp 15 100000
.\build\Release\public_menu_preview.exe .\logs\grenades-aura.bmp 16 0 1000 600
```

The second preview uses synthetic copied footprints; it verifies drawing only.
No live grenade appearance check is claimed.

**Verification, October 20, 2026:** public and diagnostic x64 Release builds
succeed without project warnings, with **53/53 CTest suites passing in each**.
The native area fixture passes 53 acquisition/lifecycle checks; removal checks
cover both flash alpha parameters, native bookkeeping and unchanged input data.
Both software D3D11 previews were inspected. All **25/25 installed-file
compatibility checks** pass; 28 incorrect area-header contracts and 17 incorrect
removal contracts are rejected. Both payloads retain zero static TLS and delay
imports. No CS2 client/server was launched.

## Player chams

Under **Visuals**, open **Player chams** and turn on **Enable player chams**.
Choose **Flat** for a solid unlit material or **Iridescent** for the character
shader's angle-dependent effect. Customize the selected style's **Flat color**
or **Iridescent color**, including alpha. Both style tints save independently.
**Through walls** starts checked and selects the style's material with depth
testing and depth writes disabled. Uncheck it for normal depth-tested chams.
Enemy players are selected by default. **Chams on teammates** and **Chams on local player** independently
extend the selection; use third person to inspect your own player.

Chams starts disabled, works independently of ESP and saves with local configs.
**Reset player chams** restores disabled defaults. Existing presets without
chams fields also restore these defaults. The existing build and
**Load Overlay.cmd** workflow applies; restart CS2 normally before loading a
rebuilt DLL when an older copy remains resident. Cooperative stop retains code
until process exit: **stopped and resident is not physically unloaded**.

The player scene, resource and shader contracts are documented in
[player chams evidence](tools/CS2_PLAYER_CHAMS_EVIDENCE.md). Check the installed
files with `python tools/verify_player_chams.py` or the combined compatibility
command. Standalone menu previews, without starting CS2:

```powershell
.\build\Release\public_menu_preview.exe .\logs\player-chams-flat-menu.bmp 13 100000
.\build\Release\public_menu_preview.exe .\logs\player-chams-iridescent-menu.bmp 14 100000
```

**Verification, October 18, 2026:** public and diagnostic x64 Release builds
succeed, with **47/47 CTest suites passing in each**. The player adapter runs
139 ownership/packet/lifecycle checks; material creation runs 132 lifecycle and
failure checks. Shared-owner, mouse-driven menu and config checks also pass.
Both menu previews were inspected. Both payloads retain zero static TLS and
delay imports. All **22 installed-file compatibility components** pass,
including player ownership, native material creation and eight exact shader/
texture assets; 33 deliberately incorrect chams header contracts and 12
mutated ordering instruction windows are rejected.
Automated verification did not launch CS2. A subsequent user-run session log
confirms hook installation, real D3D11 rendering and chams packet edits; the
agent has not visually verified player appearance, animation, alpha blending
or actual occluded rendering. Native scene culling and pass order still apply,
and correct sorting of overlapping translucent players is not established; opaque alpha is
the default. First enable can pause while native resources are requested.

## Local player model

Under **Visuals**, scroll down and open **Player model**. Turn on **Enable local
player model**, search the character list, then select a model. Use **F6** to
inspect the full-body model in third person. The option starts disabled, works
independently of ESP, and saves its toggle and selected resource path in local
configs. **Reset player model** disables the swap and clears the selection.

Discovery reads the loose character pack beside the loaded `client.dll`, under
`game/csgo/{agents,characters,models,phase2}`. The installed LynX AG2 pack yields
**739 selectable models**, including the seven `hmn_ag2/zenless_zone_zero`
characters: Ellen, Lycaon, Miyabi, Nico, Pulchra, Soukaku and Zhao. Searching
`zenless_zone_zero` finds that group. Discovery excludes obvious arms/helpers,
unsupported animation graphs, malformed model metadata and missing direct
dependencies; it checks both loose files and the installed CS2/core VPK indexes.
These checks establish available resources, not correct appearance for every
custom skeleton. After adding pack files, restart the client and load again to
rebuild the catalog. Saved paths absent from the catalog stay inactive.

The native game-thread frame callback loads the selected resource through the
engine's blocking JIT manifest, verifies the returned model name, then uses the
native model setter. A first load can pause that frame. Stable models are not
reloaded every frame, and failed selections wait for a toggle, selection change
or new pawn before retrying. No installed files or mount settings are changed.
The supplied pack remains in its installation; it is not redistributed here.

Disabling/resetting restores the latest captured native model on the same
validated pawn. Respawns, native model changes and full serialized entity
identity changes invalidate the previous state. Cooperative stop disarms new
swaps and queues restoration on the next native owner callback; if that
callback no longer runs, restoration remains pending until process exit.
**Stopped and resident is not physically unloaded.** Restart CS2 normally before
loading a rebuilt DLL when an older copy is resident, then use the existing
**Load Overlay.cmd** workflow. All observation stays in the permitted offline
or private environment.

The native and asset contracts are documented in
[model-swap evidence](tools/CS2_MODEL_SWAP_EVIDENCE.md) and
[pack evidence](tools/CS2_MODEL_ASSET_EVIDENCE.md). Verify installed native files
with `python tools/inspect_model_swap.py` or the combined compatibility command.
For the software D3D11 menu preview:

```powershell
.\build\Release\public_menu_preview.exe .\logs\player-model-menu.bmp 8 100000
```

**Verification, October 18, 2026:** public and diagnostic x64 Release builds
succeed with **40/40 CTest suites passing in each**. The native adapter has 89
checks covering loading failures, model changes, stale identities, death,
reentrant callbacks and cooperative stop/restoration; resource parsing has 31
malformed/missing/valid metadata checks. Mouse-driven picker/config checks pass,
and the software D3D11 menu preview was inspected. All **21 installed-file
compatibility checks** pass in `logs/compatibility/20261018T125010645709Z/`;
40 deliberately incorrect model header contracts are rejected. Both payloads
retain zero static TLS and delay imports. No client/server was launched, and
live character appearance and animation remain unobserved.

**Startup crash correction, October 18:** the user's matching dump identifies
an unsupported-model rejection during catalog discovery. That rejection threw
a C++ exception whose image-base lookup failed in the manually mapped payload,
before hooks activated or any character loaded. Expected metadata/filesystem
errors now return status directly. The unchanged production mapper reproduces
the old failure in an isolated catalog fixture; the corrected fixture and
installed-pack scan complete with zero first-chance C++ exceptions. See
[startup crash evidence](tools/CS2_MODEL_STARTUP_CRASH.md). The pack's model
filters and native loading contracts remain in place. Rebuild through the same
commands and restart CS2 normally before using **Load Overlay.cmd** again.
Public and diagnostic Release builds each pass **41/41 CTest suites**, including
the mapped catalog regression; the parser runs 75 rejection/validity checks.
The installed catalog still has 739 entries. Both payloads retain zero static
TLS and delay imports. No CS2 client/server was started for this correction;
live character appearance remains unobserved.

## First-person scope replacement and custom scoped crosshair

Under **Visuals → Scoped zoom**, enable **Remove scope overlay** to replace the
first-person scope graphic and scope blur with the existing custom white
crosshair. This switch works independently of **Override scoped zoom**, ESP and
third person, starts disabled, and saves with local configs. **Reset scoped zoom**
also restores the native scope overlay. Older presets leave removal disabled.

Sniper scopes and AUG/SG556 iron sights are supported. The native acquisition
callbacks still run; replacement changes only transient visual outputs for the
matching alive local scoped pawn/weapon. Weapon scope state, magnification, sensitivity and firing remain
under their existing controls. The native graphic stays available when a fresh
custom-crosshair accuracy sample is unavailable. Unscoping, death, source changes,
disable and cooperative stop invalidate the replacement. No installed game files
are changed. See [scope rendering evidence](tools/CS2_SCOPE_OVERLAY_EVIDENCE.md).

A custom white four-point crosshair appears automatically at screen center
only while the alive local player is actually scoped with a supported scoped
weapon and the third-person camera has successfully applied, or the first-person
scope has been replaced using the option above. It follows the
reference's slender tapered arms, tiny center dot and subtle glow. Native
spread plus inaccuracy gently opens the center gap by at most three pixels at
1080p, smoothed over approximately 90 ms; the outer radius stays at 18 pixels.
The graphic scales with viewport height and stays visible with the menu hidden.

This works independently of ESP, aim assistance and the scoped zoom override.
Unscoping, leaving the eligible camera/replacement view, death, stale/replaced entities, missing accuracy
data or cooperative stop hides it. Accuracy getters run on the existing native
command callback; Present consumes copied values and validates current scope
and camera state. Samples and camera publications expire after 250 ms without
updates. The first-person option adds two verified visual-output hooks; the
existing third-person drawing path adds no hooks or command edits.

Build and load with the existing commands below. Restart CS2 normally before
using **Load Overlay.cmd** if an older DLL is resident; stopped and resident
is not physically unloaded. Standalone software-D3D11 previews (synthetic
scoped/camera state, without starting CS2):

```powershell
.\build\Release\public_menu_preview.exe .\logs\scoped-crosshair-still.bmp 11 0 1920 1080
.\build\Release\public_menu_preview.exe .\logs\scoped-crosshair-spread.bmp 12 0 1920 1080
```

See [crosshair implementation evidence](tools/CS2_SCOPED_CROSSHAIR_EVIDENCE.md).

**Verification, October 18, 2026:** public and diagnostic x64 Release builds
each pass **44/44 CTest suites**, including 199 scoped-state checks and 22
drawing checks. Both software-D3D11 crosshair previews were inspected; all
21 installed-file compatibility checks pass. Both DLLs retain zero static
TLS and delay imports. No client/server was launched; live scoped appearance
and spread feel remain unobserved.

**First-person extension verification, October 20, 2026:** public and diagnostic
x64 Release builds each pass **54/54 CTest suites**. Scope suppression has 32
adapter checks and shared crosshair state has 403 checks. All **26/26 installed-file
compatibility components** pass, including modern sniper stencil/crop and AUG/SG556
geometry/filter paths; 33 incorrect scope contracts/windows are rejected. The
Scoped zoom menu preview was inspected. Both DLLs retain zero static TLS and
delay imports. No CS2 client/server was launched; live scope removal remains
unobserved. Restart CS2 normally before using **Load Overlay.cmd** with the
rebuilt DLL if an older copy is resident.

## Scoped weapon zoom

Under **Visuals**, scroll down, open **Scoped zoom**, then enable **Override
scoped zoom**. **Zoom 1** and **Zoom 2** independently control the weapon's
first and second scope stages. Each slider ranges from 0–200%: **0% removes
magnification**, **100% preserves the weapon's normal zoom**, and higher values
increase magnification. Weapons with one scope stage use Zoom 1 only.

**Zoom sensitivity** independently blends scoped mouse sensitivity from 0–100%:
**0% uses your ordinary unscoped sensitivity**, **100% uses CS2's native scoped
sensitivity**, and intermediate values blend the two. It applies to both scope
stages; the Zoom 1/2 magnification sliders do not change this selection.

The setting starts disabled, works independently of ESP and third person, and
saves with local configs. **Reset scoped zoom** disables it and restores all
three sliders to 100% and restores the native scope overlay. Older presets use
these defaults, preserving native sensitivity. Magnification and sensitivity
controls adjust the current world-view FOV and scoped mouse sensitivity. The
separate **Remove scope overlay** switch controls scope replacement; weapon
state and firing behavior are unchanged.

The implementation uses the final game-specific camera callback, current
serialized active-weapon identity and the native unscoped FOV getter. Sensitivity
blends the scope multiplier argument in both native mouse-conversion branches;
CS2 still applies your base sensitivity and processes the mouse input. Changes
apply only to the current view/conversion call, with no persistent entity or
console-variable writes.
See [scoped zoom evidence](tools/CS2_ZOOM_EVIDENCE.md); recheck installed contracts
with `python tools/inspect_zoom.py` or the combined compatibility command.
Build and launch through the existing **Load Overlay.cmd** workflow. Restart CS2
normally before loading the rebuilt DLL if an older copy is resident; **stopped
and resident is not physically unloaded**. No live gameplay appearance check is
claimed. For the standalone menu preview:

```powershell
.\build\Release\public_menu_preview.exe .\logs\scoped-zoom-menu.bmp 7 100000
```

**Verification, October 16, 2026:** public and diagnostic x64 Release builds
succeed, with **25/25 CTest suites passing in each**. The zoom suite runs 556
checks covering both scope levels, FOV math, stale sources, nested views and
cooperative stop; menu tests exercise real mouse drags to exact 0% and 200%.
The software D3D11 Scoped zoom menu preview was inspected. All thirteen
installed-file compatibility checks pass in
`logs/compatibility/20261016T160815546576Z/`; thirteen deliberately incorrect
zoom-header contracts were rejected. Both DLLs retain zero static TLS and
delay imports. Live scoped appearance remains unobserved.

**Sensitivity follow-up, October 18, 2026:** public and diagnostic x64 Release
builds succeed, with **42/42 CTest suites passing in each**. The zoom suite runs
**1,794 checks**, including both native mouse conversion signatures, sensitivity
endpoints/blending, preserved input/output arguments and entity bytes, stale
sources, nested callbacks and concurrent cooperative stop. The menu/config
checks cover dragging, reset, persistence and older presets; the software D3D11
preview was inspected. All **21/21 installed-file compatibility checks** pass in
`logs/compatibility/20261018T140828198745Z/`, and 15 deliberately incorrect new
sensitivity contracts are rejected. Both DLLs retain zero static TLS and delay
imports. No CS2 client/server was launched; live scoped mouse feel remains
unobserved. Restart CS2 normally before using **Load Overlay.cmd** with the new
build if an older copy is resident.

## Third-person camera

Under **Visuals**, scroll down and open **Camera**, then enable **Enable third
person**, or press **F6** while CS2 has focus. Camera settings are independent
of ESP and continue applying with the menu hidden. They start disabled and save
with local configs; older presets restore camera defaults.

- **Camera distance:** 30–300 units, initially 120.
- **Shoulder offset:** −60 to +60 units, initially centered; positive moves right.
- **Height offset:** −60 to +60 units along world Z, initially zero.
- **Camera collision:** initially enabled. A native 9-unit hull sweep clips the
  complete eye-to-camera displacement, including shoulder and height offsets.
- **Reset camera:** disables third person and restores every camera default.

The current local pawn must be alive with valid controller/entity ownership.
Missing or stale state, death and other view slots use the native camera.
Disabling or stopping restores native behavior on subsequent callbacks, without
leaving camera input bytes or console variables changed. The implementation
changes only the current view origin and the native local-player visibility
query; command angles, current view angles and FOV are preserved. The normal
game-specific camera effects still run afterward. No aim/parallax correction
or camera smoothing is added.

The native view, visibility query and collision contracts are documented in
[camera evidence](tools/CS2_THIRD_PERSON_EVIDENCE.md). Recheck the installed files
with `python tools/inspect_third_person.py` or the combined compatibility command.
Use the existing build and **Load Overlay.cmd** workflow below. Restart CS2
normally if an older DLL is resident: cooperative stop drains camera edits,
but **stopped and resident is not physically unloaded**. Testing remains limited
to the disconnected menu, offline mode or the designated private environment.

**Verification, October 16, 2026:** public and diagnostic x64 Release builds
succeed, with **24/24 CTest suites passing in each**. Camera regressions use
standalone native-callback substitutes to check geometry, hull/filter arguments,
origin-only edits, stale sources, visibility fallback, nested views and draining
stop. Menu/config tests cover all controls, resets and persistence. The software
D3D11 Camera preview was inspected; no live client appearance or wall-response
observation is claimed. Both DLLs retain zero static TLS and delay imports.
All twelve installed-file compatibility checks pass in
`logs/compatibility/20261016T154931008163Z/`, for installed patch 1.41.9.1,
client/server 2000933, Steam build 25966348. All 32 camera-header mutations were
rejected. With the preview build enabled, reproduce the Camera image with:

```powershell
.\build\Release\public_menu_preview.exe .\logs\third-person-menu.bmp 6 100000
```

## Local configs

Open **Configs** in the public menu, enter a name, then click **Save / overwrite**
to save all current Visuals (including Player chams, Camera, Scoped zoom and Player model), Movement, World, Lighting and Aim settings. Select
a saved preset and click **Load config** to apply it. Type a different name to
save another preset; **Refresh list** picks up files added outside the menu.
Loading replaces current options, including feature enable switches. Presets
are loaded manually; startup keeps the existing defaults.

Versioned text presets live in `configs/` under this workspace, independently of
the build directory, and persist after CS2 exits. They are ignored by Git.
Names accept up to 64 ASCII letters, digits, spaces, hyphens and underscores;
reserved Windows names are rejected. Save replaces an existing preset only
after the new file is completely written. Invalid files report an error without
partially applying settings. The diagnostic interface has no config controls.

The name field supports typing, Shift/Caps Lock, Ctrl+A, cursor keys,
Backspace and Delete. Held character keys enter once per press. Input is polled
while the visible menu has focus. The public menu captures gameplay input while
open; closing it restores the client's cursor/input mode.

**Verification, October 16, 2026:** public and diagnostic x64 Release builds
succeed, with all **18/18 CTest suites passing in each**. Tests cover every
saved field/color, separate presets, overwrite, malformed files without partial
application, bounds, names and real menu save/load interactions. The software
D3D11 Configs preview was inspected. Both DLLs retain zero static TLS and delay
import directories. No live CS2 input or appearance observation is claimed.

**Keyboard follow-up, October 16:** renderer initialization previously left
ImGui's `NoKeyboard` flag enabled, discarding the polled keys and text in the
client. That flag now applies only to diagnostics; public-menu setup explicitly
enables keyboard input. The menu regression check starts with keyboard input
disabled, then verifies setup, field focus, Ctrl+A, typing and saving/loading.

## World visuals

The **World** and **Lighting** tabs provide independent controls, disabled by
default. Settings can be saved in configs and continue applying with the menu
hidden. **Reset world visuals** resets all world controls; **Reset lighting**
resets only lighting. The previous world implementation was committed as
`70c0544` before this follow-up; `6ef58ef` preserves the paused implementation.

**Sky:** RGB tint, sky probe/reflections tint and brightness (0–4) adjust the
existing sky. Each native sky draw receives a fresh copied shader parameter;
entity fields and sky resource handles remain unchanged. Probe/reflections tint
applies when the engine refreshes those views. It does not replace scene lighting
or baked illumination, and no external sky texture loader is provided.

**World surfaces:** one **Flat world textures** checkbox selects the existing
`materials/dev/reflectivity_90.vmat` through the native material-mode path.
Overrides affect newly appended static-world aggregate draw records, with native
mode and vertex-stream compatibility checks. Separate props, decals and animated
player/weapon paths are outside this implementation. Unavailable or incompatible
flat materials leave the original draw unchanged.

The previous flat path incorrectly treated material-selection view bits as a
camera-view filter. That restriction has been removed. Detailed bounded counters
now identify early exits, material binding and mode failures. The user's prior
log contained callbacks but no changed packets; its incomplete counters cannot
identify the exact guard that blocked that run. The repaired path still needs
an offline visual check.

The old **Surface tint** slider has been removed: aggregate world draws do not
read the packet RGB field it changed. Actual albedo tint requires shader-specific
authored-material constants and a verified resource-lifetime implementation.
It is currently unsupported; see [surface tint evidence](tools/CS2_SURFACE_TINT_EVIDENCE.md).

**Lighting:** enable the separate lighting master to adjust:

- Local-light RGB tint and brightness (0–8), covering native point, spot,
  orthographic, area and barn-light contributions.
- Disable local lights, following the native gather routines' disabled path.
- Sun RGB tint and brightness (0–8).
- **Add ambient light:** independent RGB color and intensity (0–4, initially
  0.25). This adds native diffuse fill before surface coloring and fog, so
  shaded surfaces can share the chosen ambience. It starts disabled; zero
  intensity or black adds nothing. This does not affect the HUD through a
  screen filter.
- Directional lightmaps: **Game value**, **Enabled** or **Disabled**.

These controls change current-view GPU records or freshly
constructed frame-stack constants on their native callbacks. They do not write
persistent scene entities, shared cvars or installed game files. Disabling local
lights leaves baked illumination intact; directional-lightmap mode follows the
engine switch and does not disable all baked lighting or recompute shadows.
Ambient fill does not subtract, replace or recolor existing baked irradiance;
it adds illumination to the current map lighting. **Reset lighting** also
disables ambient fill and restores its default color/intensity.

The October 15 ambience follow-up traced the old local-light implementation
beyond CPU counters. The callback previously described as area-light packing
actually packs cubemap reflection volumes: its tint is not a local diffuse
lighting control. The corrected path targets the local-light GPU records used
by the installed world shaders. See the [comparison and scope](docs/CS2_AMBIENCE_RESEARCH.md)
and [native-light evidence](tools/CS2_LIGHTING_EVIDENCE.md). Screen color grading
is unchanged by this follow-up.

**Follow-up verification, October 15, 2026:** both public and diagnostic x64
Release builds succeed, with **16/16 CTest suites passing in each**. All seven
installed-binary compatibility checks pass; the fresh report is
`logs/compatibility/20261015T131101141927Z/`. The ambient shader probe verifies
twelve original generic/lightmapped variants. Temporary header mutations reject
incorrect ambient offsets/producers/bytes and local RGB/caller/prolog values.
The real Lighting menu preview was inspected, and mouse-driven tests cover the
ambient master, intensity, resets and independence from other settings.
No new live client appearance observation is claimed. Restart CS2 normally and
use the rebuilt **Load Overlay.cmd** workflow for the next offline/private
check; stopped old hooks remain resident until process exit. Under **Lighting**,
enable **Override lighting**, then **Add ambient light** and choose its color
and intensity. Local tint/brightness now applies at the final native light GPU
packing stage; baked lamps/irradiance remain separate from dynamic local lights.

**Screen color:** exposure (−3 to +3 EV), contrast, saturation, gamma, RGB tint,
vignette and sharpening process the game image, including its HUD, before this
project's ESP/menu. These change the displayed image rather than individual map
lights or material assets. Neutral or disabled grading performs no GPU pass.
Active grading adds a texture copy/resolve and one fullscreen draw; live GPU cost
has not been measured.

The screen pass saves/restores D3D11.1 context state, preserves alpha, uses a
separate source texture and retains no swap-chain back-buffer references between
frames. Resources rebuild on size/format/device changes and release before
resize/stop. Unsupported resources leave the frame unchanged. sRGB follows the
actual RTV/SRV format; HDR/PQ transfer functions are not inferred.

Engine changes disarm cooperatively and drain active parameter/packet changes.
Resident hooks continue forwarding native calls; stopped code remains resident
until normal process exit. No session-detection or server prerequisites were added.

**Verification, October 15, 2026:** public and diagnostic x64 Release builds
succeeded, with **16/16 CTest suites passing in each**. Coverage includes native
lighting packet scope, shutdown, setting bounds, resets, real mouse-driven UI
controls and software-GPU screen output/state/resource checks. World and Lighting
menu previews were inspected. The five installed-binary verifiers passed for
patch **1.41.9.1**, client/server **2000932**, Steam build **25947352**. They compare
frozen code fingerprints and decoded operands, rather than substituting offsets
automatically. Evidence: [world](tools/CS2_WORLD_EVIDENCE.md),
[sky](tools/CS2_SKY_DRAW_EVIDENCE.md), [materials](tools/CS2_MATERIAL_EVIDENCE.md),
[local lights](tools/CS2_LIGHTING_EVIDENCE.md),
[light switches](tools/CS2_ENV_LIGHT_EVIDENCE.md) and
[sun/directional lightmaps](tools/CS2_BAKED_LIGHT_EVIDENCE.md).

**Compatibility:** see the current ESP/movement compatibility update below.

The user confirmed that the world and lighting options work on 1.41.9.1. The
latest supplied run is `logs/overlay-26684-134365408103402107.log`: native world
callbacks execute, while the old ESP and movement build checks reject the client.
Testing remains limited to the disconnected menu, offline mode or a
user-controlled private session under the repository instructions.

The existing public/diagnostic build commands below include these features.
Standalone previews accept a tab (World=2, Lighting=3), scroll position and
optional viewport width/height:

```powershell
.\build\Release\public_menu_preview.exe .\logs\world-menu.bmp 2
.\build\Release\public_menu_preview.exe .\logs\lighting-menu.bmp 3
.\build\Release\public_menu_preview.exe .\logs\lighting-narrow.bmp 3 0 180 150
python tools/inspect_world.py
```

## ESP and movement compatibility — October 15, 2026

Checkpoint `d69477f` preserves the project before this update. The latest
runtime log confirms that the old client identity rejected ESP and movement on
1.41.9.1. The shared header now targets the installed AMD64 client timestamp
`0x6AD01314`, image size `0x299D000`, with recertified entity, local-player and
view-matrix globals. Static schema metadata also moves pawn armor from
`0x1ECC` to `0x1EC8`; the remaining consumed player fields retain their layouts.

Movement now uses the current input object/vtable, command finalizer, ingestion,
subtick allocation/append, jump helper, speed helper, Cvar wrappers and physics
constant addresses. Command layouts and consumed movement fields were checked
against the installed metadata/code. The jump and steering algorithms are
unchanged. Existing world and lighting behavior is preserved.

Both public and diagnostic x64 Release builds succeed, and **16/16 CTest suites
pass in each**. The ESP, movement and five world binary verifiers pass. Temporary
header mutations confirm rejection of all 43 ESP constants and 70 movement
constants/fingerprints; reports are in `logs/compat-*-20261015.json`.
Recheck everything with `python tools/check_compatibility.py`, or run the
individual tools while investigating:

```powershell
python tools/inspect_cs2.py
python tools/inspect_movement.py
python tools/inspect_world.py
```

These tools read installed files without launching or injecting into CS2. Their
frozen evidence rejects a changed build; it does not apply guessed replacement
offsets. Current entity/movement evidence is static, with no new live dump or
gameplay observation. Successful builds and standalone checks do not establish
that ESP draws players or autobhop executes in a match on this build. Use the
rebuilt **Load Overlay.cmd** workflow for the next offline/private check.

## Configurable anti aim

Under **Aim**, enable **Enable anti aim**. It starts disabled and is saved with
configs. Warmup works normally. During each round's starting freeze/countdown,
new anti-aim angle edits pause automatically and resume on the first command
after that period ends. Missing or unreadable round state also pauses new edits.
The toggle stays enabled; retained-command movement/view compensation continues.
Scroll below the logging controls for the anti aim settings:

- **Yaw mode:** Static, Jitter, Spin or Random.
- **Yaw base:** View relative follows your mouse heading; World absolute starts
  each command from a fixed world heading.
- **Yaw offset:** -180 to +180 degrees, added to the selected base.
- **Yaw range:** 0-360 degrees total width for Jitter/Random. A width of 60
  alternates between -30/+30 around the offset in Jitter mode, or samples
  within that range in Random mode.
- **Spin speed:** -180 to +180 degrees per command; the sign selects direction.
- **Pitch mode:** Down (+89), Up (-89), Level, Keep view pitch, Custom or
  Jitter. Custom and both jitter pitch values range from -89 to +89 degrees.
- **Change interval:** 1-64 commands per jitter side or random yaw sample;
  yaw and pitch jitter share this interval.
- **Reset anti aim:** disables it and restores the original view-relative
  yaw +180/downward pitch +89 preset.

All options save in local configs. Older enabled-only presets retain the
original angles. Pattern phase comes from command sequence, so native rebuilds
and prediction replay do not advance it; speed/interval use commands, not
render frames or seconds. World absolute selects the command's initial yaw;
native mouse yaw deltas still apply within that command. No new native offsets,
hooks or launch requirements are introduced.

The physical mouse view and native shot-history source remain independent.
The verified native command-replay callback otherwise copies outgoing angles
back to the camera; its wrapper preserves the current physical input view
across that reset. It captures a fresh view each time, rather than freezing the
camera to an earlier command. View effects from ordinary game camera logic
are retained. Model/animation state is not written.

Movement correction runs after existing bunnyhop/autostrafe planning and before
the original movement checksum finalizer. It transforms base forward/left,
subtick direction edges and analog deltas. General rotations convert exact
single-direction subticks into equivalent analog events, derive final direction
buttons from the rotated axes, and clear their digital changed/tap bits while
preserving unrelated buttons. The original axis-aligned preset retains its exact
signed-button permutation. Pitch subtick deltas become zero except in Keep view
pitch mode; yaw deltas continue following the physical turn. A native arena-owned
time-zero analog event repairs the historical movement basis when enabling,
disabling, stopping, changing modes or passing through actions.
No attack, jump, timestamp, history-angle or movement-physics override is added.
Primary attack and use, including short subtick taps, pass through with physical
angles. Secondary attack (right-click) keeps anti aim active. Existing silent aim
retains its own shot-history stage.

The runtime targets ordinary walking/air movement, including arbitrary wrapped
yaw and upside-down pitch within a fixed horizontal-basis hemisphere. Exactly
vertical pitch, nonzero roll, a subtick pitch crossing a vertical boundary,
ladder/noclip/swimming and unavailable layouts pass through. General rotations
also reject mixed direction/action subticks and inconsistent nonzero omitted
scalar payloads without partially editing the command. Flattened WALK
forward is singular at exactly vertical pitch; the separate full-3D math helper
is not substituted for the game's WALK rules. There is no claim of support for
every movement mode or observer camera.

Restart CS2 normally before using the rebuilt **Load Overlay.cmd** workflow if
an older DLL is resident. Keep testing at the disconnected menu, offline or in
the designated private environment. End and explicit stop disarm new angle
edits. Movement handoff and camera compensation for retained commands remain
resident; **stopped and resident is not physically unloaded**.

Installed-binary contracts and standalone checks are described in
[anti aim evidence](tools/CS2_ANTI_AIM_EVIDENCE.md). No live camera smoothness,
character facing, collision response or server consumption is established by
these checks. No game launch, injection or server interaction was performed
for this implementation.

Standalone previews of the configured controls (no CS2 launch):

```powershell
.\build\Release\public_menu_preview.exe .\logs\anti-aim-jitter-menu.bmp 9 550
.\build\Release\public_menu_preview.exe .\logs\anti-aim-pitch-menu.bmp 9 100000
.\build\Release\public_menu_preview.exe .\logs\anti-aim-spin-menu.bmp 10 100000
```

**Configurable-settings verification, October 18, 2026:** public and diagnostic
x64 Release builds succeed, with **42/42 CTest suites passing in each**. New
checks cover every pattern and pitch mode, deterministic rebuilds, sparse axes,
digital press/release conversion, guarded-memory rollback, 6,144 copied-command
history/boundary handoffs and 32 malformed-input cases across arbitrary and
axis-aligned custom angles. Mouse-driven menu/config checks cover conditional
controls, reset, complete persistence and older presets. The three software
D3D11 previews above were inspected. The expanded installed-client anti aim
verifier passes, including raw subtick decoding and exact digital-unit dispatch.
Both payloads retain zero static TLS and delay imports. No CS2 client/server
was launched; live movement, animation and server behavior remain unobserved.

**User feedback, October 18, 2026:** the configurable settings work in the user's
client, but high spin rates cause unintended diagonal movement. This remains an
unresolved movement-correction defect in this checkpoint. The standalone
world-vector and copied-command tests pass, but do not reproduce the complete
native input/prediction pipeline or establish correct movement during fast spin.

**Movement correction, revision 5, October 18:** command rebuilds now obtain
their starting movement from the native controller ring's **previous sequence**,
instead of whichever command prediction most recently ingested. Rebuilding N
after simulating N previously selected N's already-rotated axes and could omit
the time-zero correction needed when N restarts from N-1. That produces sideways
movement as spin/jitter changes the heading. Bunnyhop/autostrafe planning uses
the same predecessor. Copied publication history still identifies its physical
basis; missing or unrecognized edited predecessors pass through.

Native replay also copies outgoing direction buttons into physical input's
previous-button state. Its wrapper now advances those direction bits from the
current physical input, alongside preserving the view. Real presses, releases
and reversals advance normally; native action buttons and edge resets remain
native. The command lookup and input fields were decoded from the installed
client and added to the compatibility verifier. Build and launch commands are
unchanged. Restart CS2 normally before loading this revision through
**Load Overlay.cmd** if the older DLL is resident.

The original code fails the added physical-input replay regression. New checks
cover 9,216 pattern-command replays, 4,608 same-sequence prediction rebuilds,
10,000 changing view/key transitions and stale/missing predecessor slots.
Public and diagnostic x64 Release builds each pass **42/42 CTest suites**.
All **21 installed-file compatibility checks** pass in
`logs/compatibility/20261018T134556585408Z/`; four deliberately incorrect input
or lookup contracts are rejected. Both payloads retain zero static TLS and
delay imports. These are standalone regressions; no CS2 client/server was
started and live movement confirmation remains pending.

**Verification, October 16, 2026:** public and diagnostic x64 Release builds
succeed; **22/22 CTest suites pass in each**. This includes dense/randomized
angle-basis checks, 10,000 changing physical replay views, 9,000 movement-basis
handoffs, native arena growth/reuse, pooled-command rebuild rejection, native
CRC presence changes, tiny deltas and real menu/config interactions. All eleven
installed-binary checks pass in `logs/compatibility/20261016T144820900361Z/`.
The Aim menu preview was inspected. The consumed client DLL retains the verified
hash; the installed version reports 1.41.9.1 / client-server 2000933 / Steam
build 25966348. These are static and standalone results, without live gameplay.

**Bot-match failure follow-up, October 16:** the supplied run
`logs/overlay-21024-134366360999930010.log` loaded this feature and enabled it,
but recorded **zero edits** and **7,879 rejected commands**. Native base-angle
production clears each component's presence bit when its value is zero. The
first validator incorrectly demanded all three bits, so ordinary zero-roll
commands never reached mutation. Earlier fixtures explicitly set all three
bits and missed this producer contract.

Revision 2 accepts native sparse zero components and changes their presence
bits together with angle values, restoring both on command rebuilds. Regression
fixtures cover omitted zero pitch/yaw/roll, transitions into and out of zero,
native arena growth and restoration. Render-owner `ANTI_AIM revision=2` reports
now include the exact rejected stage and observed presence/container fields,
callback counts, last outgoing angles, and whether native ingestion retained
the edited angles. These counters distinguish mutation from subsequent native
processing; they still do not establish character facing or server receipt.
The corrected DLL requires a normal CS2 restart before reloading. No new live
post-fix behavior is claimed.

Both repaired public/diagnostic Release builds succeed and all **22/22 CTest
suites pass in each**. All eleven installed-binary checks, including the newly
frozen sparse-angle producer branches, pass in
`logs/compatibility/20261016T150659692451Z/`. Both DLLs retain zero static TLS
and delay-import directories. Build identities and the original failure summary
are under `logs/anti-aim-repair/`.

**Movement and pitch follow-up, October 16:** checkpoint `b6c6137` preserves
revision 2 before this repair. The user confirmed that local view works, but
reported reversed, very slow WASD movement. In
`logs/overlay-39816-134366368765959678.log`, commands are edited and their angles
reach native ingestion, while `edited_ingests` remains zero. The old history
recognition required the original command/base/angle addresses and a live
restoration-cache entry. Native copying and same-sequence rebuilds do not retain
that identity. Consequently a held forward input repeatedly received an
incorrect time-zero reversal instead of retaining its corrected history.

Revision 3 publishes copied command values independently of mutation ownership.
It recognizes local-source/sequence, exact angle/analog/direction values, and
keeps up to eight rebuild variants for each of 256 sequence slots. Source
changes invalidate this history; unknown or expired variants use physical
passthrough until history is recognized. Steady corrected input now uses exact
sign changes, so it adds no repeated handoff and retains tiny analog values.
Native movement simulation, speed caps and local view handling are unchanged.

The requested ground-facing look is now **+89 degrees**; **0 degrees is level**.
Native direction math and the installed ordinary pitch defaults were checked.
The Aim menu describes downward pitch. `ANTI_AIM revision=3` diagnostics report
recognized edited/physical ingests, unknown ingests and boundary-event counts.
No post-repair gameplay result is claimed.

**Revision 3 verification:** public and diagnostic x64 Release builds succeed,
with **23/23 CTest suites passing in each**. Coverage includes actual command
copies with distinct protobuf addresses, same-sequence variants, source changes,
1,000 complete movement handoffs, exact tiny-input correction and downward
pitch. All eleven binary checks pass in
`logs/compatibility/20261016T152438007018Z/`. No game was launched, injected or
controlled during this repair. Restart CS2 normally before loading the rebuilt
DLL through **Load Overlay.cmd**.

## Quickstop

Under **Aim**, enable **Autofire**, then **Quickstop**. Quickstop starts off and
saves with presets; older presets keep it off. Silent aim must also be enabled.
A live enemy within FOV, with current capsule geometry, a clear direct path and
a ready weapon, can start braking when sampled hitchance is below your minimum.
Covered, out-of-range and otherwise invalid candidates do not start a stop.
An immediately shootable enemy takes priority over a candidate needing braking.

Braking opposes observed horizontal velocity in physical camera space, tapers
near rest, and uses fresh native accuracy on subsequent commands. Autofire
requests the shot at **the configured minimum**, with no hidden percentage
margin. A stop already in progress retains braking on its firing command.
There is no guessed stationary-accuracy prediction or promise that a requested
threshold can be reached: spread, recoil, stance, scope and target geometry can
still limit hitchance after movement has stopped.

During weapon cooldown, including bolt-action recovery, ordinary user movement
resumes. When the weapon becomes ready, a remaining eligible target below the
threshold starts another stop. Losing the target/path, changing sources or
settings, disabling aim/autofire/quickstop, or losing focus ends the cycle.
Braking applies only to normal grounded walking; jumping, air movement, swimming
and ladders keep their normal movement behavior. Manual primary fire remains
available. The existing aim range, geometry, occlusion and fire-readiness checks
continue to govern automatic shots.

Movement edits include direction buttons and a time-zero analog subtick, with
restoration before prediction rebuilds and a handoff to physical input when
released. They run before anti aim and native checksum generation. See
[quickstop implementation evidence](tools/CS2_QUICKSTOP_EVIDENCE.md).
Build/load using the existing commands and **Load Overlay.cmd**. Restart CS2
normally before loading a rebuilt DLL if an older copy remains resident;
**stopped and resident is not physically unloaded**.

**Verification, October 20, 2026:** public and diagnostic x64 Release builds
succeed; **48/48 CTest suites pass in each**. The aim pipeline runs 171 checks,
including stop/fire/cooldown/restart and target/source/settings changes. Native
command fixtures cover 1,000 repeated restores, anti-aim pool growth, partial
rebuild ownership, timed-input handoffs and failed allocation rollback. Settings,
old presets and mouse-driven menu checks pass; normal/narrow software-D3D11 menu
previews were inspected. All **22 installed-file compatibility components** pass.
Both DLLs retain zero static TLS and delay imports. No CS2 client/server was
launched; live deceleration, recovery timing and movement feel remain unobserved.

## Silent aim, hitchance and autofire

The **Aim** tab has an **Enable silent aim** checkbox, independent **Hitgroups**
checkboxes (**Head**, **Chest**, **Stomach**, **Pelvis**, **Arms**, **Hands**,
**Legs**, **Feet**) and an angular **Aim FOV** slider
(0.1–180 degrees from the current view direction). At 180 degrees, targets
directly behind are within the angular limit. It starts **disabled**, with Chest and 5 degrees selected.
**F10** toggles it while the client is foreground. Menu visibility and ESP
settings do not control aim acquisition.

Select any combination. Chest scans all three authored chest capsules; Arms
scans both upper arms and forearms; Legs scans both thighs and calves; Hands
and Feet each scan both sides. Stomach uses the authored stomach bone
`spine_0`, correcting the previous `spine_2` chest landmark. The mask saves in
local configs, and older presets migrate their single selection. Clearing all
eight groups leaves aim acquisition, autofire and quickstop inactive.

Enabled landmarks within FOV compete independently using their current
authored capsule, the same command's weapon snapshot, estimated hitchance and
native direct-ray damage. Automatic candidates must pass minimum damage and
hitchance. Among passing shots, **a lethal shot takes priority over a nonlethal
shot**. Lethality uses the conservative whole-HP damage estimate versus current
target health. When several shots are lethal, higher hitchance wins; excess
overkill adds no priority. Otherwise selection maximizes **hitchance × damage
capped at remaining target health**. Equal scores prefer higher hitchance, then
smaller angle, then stable entity/landmark order. A qualifying lethal headshot
therefore beats a certain 90-damage body shot against 100 HP. Headshots below
the configured hitchance remain ineligible. Immediately shootable regions take priority
over regions needing quickstop; only clear, ready regions passing minimum
damage can become quickstop fallbacks. Selection considers enabled regions on
all enemies in FOV, without pruning farther candidates before evaluation.

Manual fire uses the same lethal-first ranking when estimates are available,
without imposing automatic thresholds. If every estimate is unavailable or
blocked, it retains nearest enabled landmark aiming and physical fire input.
These are estimated selected-capsule probabilities and central-ray damage;
ranking does not guarantee actual hit registration. Existing build/load commands
apply; restart CS2 normally before loading a rebuilt DLL if older code remains
resident. Stopped and resident is not physically unloaded.

**Multihitgroup verification, October 20, 2026:** public and diagnostic x64
Release builds each pass **49/49 CTest suites**, including **202 integrated aim
pipeline checks** and **173 hitchance/geometry checks**. All **23 installed-file
compatibility components** pass in
`logs/compatibility/20261020T132158578600Z/`; all 18 deliberately incorrect
landmark name literals are rejected. Normal and 320×520 Aim menu previews were
inspected. Both DLLs retain zero static TLS and delay imports. Health changes
invalidate the copied ranking state before publication. No CS2 client/server
was launched; live shot selection and hit registration remain unobserved.
See [multihitgroup evidence](tools/CS2_AIM_MULTIHITGROUP_EVIDENCE.md).

**Lethal-shot ranking correction, October 20, 2026:** the original expected-HP
score could favor a nonlethal 90-damage body shot at 100% over a lethal headshot
at 70–89% against a full-health enemy. Lethal-first ranking now resolves this
after the existing minimum gates. The integrated regression exercises both
candidate orders and quickstop settings with 70 minimum damage and 70%
hitchance, and verifies that a below-threshold head remains excluded. The
minimum-damage slider is still an eligibility gate; it grants no bypass for
lethal shots. Restart CS2 normally before loading this rebuilt DLL.
Public and diagnostic Release payloads rebuild successfully and each pass
**53/53 CTest suites**, including **213 integrated aim pipeline checks**.
Both retain zero static TLS and delay imports. No client/server was launched
for this correction; live behavior of the new ranking remains unobserved.

**Autofire** starts off. Enable it to request primary fire when the current
estimate meets **Minimum hitchance** (0–100%, initially 50%) and the weapon is
ready, with a reachable line of fire to the selected target (direct, or penetrable
with **Autowall** enabled). **Spread samples** controls the complete estimate (64–1024, initially
256). **Minimum damage** (1–100 HP, initially 1) also gates autofire. These controls save in local configs; older presets restore these
defaults. Manual primary fire stays available at any threshold, including when
the weapon or geometry cannot be modeled. Autofire does not suppress manual input.

Damage prediction models one unobstructed bullet using the current weapon's base
damage, range modifier, headshot multiplier and armor ratio; current target armor
and helmet; and the target team's head/body damage scales. The current native
ray supplies the first hitgroup and distance, including an arm intercepted on a
chest aim. It honors `mp_damage_headshot_only`. Unknown native hitgroups,
unavailable inputs, immunity and active healthshot damage modifiers prevent an
automatic request. **Autowall** extends this prediction through penetrable cover
using native residual bullet damage before the same hitgroup/armor calculation.

The slider compares the estimate rounded down to whole HP, without capping it to
remaining target health or adding a lethal-shot override. Both hitchance and
minimum damage must pass. A low-damage candidate cannot hide an eligible enemy
or start quickstop. Damage inputs are checked again after native tracing and
before publication; changes cancel owned attacks through the existing checksum
path. The estimate describes the central direct ray: spread, recoil, movement,
server hooks and event rounding can change the observed result.

Shot result lines append `| predicted 35.5 (target) | actual 34 (target)` when
both are known. Actual damage comes only from confirmed native hurt events;
misses, missing events and command timeouts retain `actual unknown`. Damage to a
different victim is labeled separately, and ambiguous shot association remains
separate evidence. `AIM_SHOT_DETAIL` records the configured minimum, predicted
hitgroup and target/total actual damage. See [damage evidence](tools/CS2_AIM_DAMAGE_EVIDENCE.md)
and run `python tools/inspect_aim_damage.py` to verify the installed contracts.
Build and load using the existing commands; restart CS2 normally before loading
the rebuilt DLL when an older copy remains resident.

**Verification, October 20, 2026:** public and diagnostic x64 Release builds
each pass **49/49 CTest suites**, including 221 damage checks, 182 integrated aim
pipeline checks, 260 occlusion checks, 140 shot-log checks and 108 logging-runtime
checks. All **23 installed-file compatibility components** pass, and 61 deliberately
incorrect damage contracts are rejected. Normal and narrow software-D3D11 Aim
previews were inspected. Both payloads retain zero static TLS and delay imports.
No CS2 client or server was launched; live predicted-versus-actual accuracy and
hit registration remain unobserved.

The estimator copies the selected region's authored `cstrike` capsule from the
current enabled model mesh and hitbox set, transforms it with the current bone
pose, and aims at its center. It samples both native spread components with
independent uniform radii and angles, including the Negev's early-recoil radial
law and the installed debug spread modes. A deterministic four-dimensional
low-discrepancy sequence avoids changing the game's RNG and gives stable results
for unchanged inputs. Estimates retain the complete requested sample count;
when both spread components are zero, all rays are identical and one geometric
intersection gives exactly the same count. The logged percentage is
`hits / samples * 100`, including finite weapon range. This estimates intersection
with the selected capsule, rather than any capsule on the enemy.

**Multihitgroup performance correction, October 20, 2026:** spread offsets are
prepared once per decision and shared across enemy capsules. Capsule invariants
are computed once per estimate. Automatic preparation samples weapon readiness
first and skips target scans during cooldown, reload or other firing blocks;
quickstop releases and selection resumes on the next ready command. With
Autofire off, acquisition runs when real manual shot history arrives rather
than during every idle preparation callback.

An exact upper bound skips work only when a candidate cannot improve the
current winner, even with a lethal hit at 100% hitchance. After actual sampling,
the same bound can avoid a native trace if a lethal hit at that measured chance
still cannot win. Both bounds retain health, centered angle and stable tie
rules; they add no enemy cap, sample reduction, cross-command pointer cache or
selection delay. Winning targets still receive fresh path/state checks before
request and publication. Repeated nested target validation at the same boundary
was removed; the volume validator already performs it.

The synthetic five-enemy/six-capsule benchmark at 1,024 samples measures
**5.183 ms → 1.801 ms** median for copied-geometry sampling, including shared
pattern preparation. At 256 samples it measures **1.147 ms → 0.445 ms**.
These timings exclude native acquisition/tracing and are not live FPS results.
`AIM_PERFORMANCE` in the existing log reports decision microseconds and cumulative
candidate, estimate, shared-pattern, trace, bound and weapon-scan-skip counts.
Build and run the standalone benchmark without launching CS2:

```powershell
cmake --build build --config Release --target aim_hitchance_benchmark --parallel
.\build\Release\aim_hitchance_benchmark.exe
```

Restart CS2 normally before loading the rebuilt DLL through **Load Overlay.cmd**.
Stopped and resident is not physically unloaded.

Performance verification: both public and diagnostic Release builds pass
**53/53 CTest suites**, including **226 integrated pipeline checks** and
**3,389 hitchance/geometry checks**. The estimator matches the frozen scalar
reference exactly for the tested counts, models, flags and geometry. The
five-enemy regression verifies 30 competing capsules share one spread pattern;
with a perfect lethal winner, only one candidate needs sampling and native
selection tracing, followed by fresh request/publication traces. All **25
installed-file compatibility components** pass in
`logs/compatibility/20261020T141705084077Z/`. Both payloads retain zero static
TLS and delay imports. No client/server was launched for this correction;
live FPS remains unmeasured.

Spread, inaccuracy, range and current predicted aim punch come from the current
weapon's verified native model. Punch shifts the sampled cone; outgoing aim is
not recoil compensated. The native punch query can update its own memoization
cache. Snapshots are read on the native command owner, without accuracy-penalty
updates or cached ESP/entity pointers. Authored geometry unavailable, unsupported
shape/space, non-unit pose scale, stale sources or an invalid native model produce
an unavailable estimate and prevent automatic fire. Manual aim falls back to the
existing named bone landmark when authored geometry is unavailable.

The verified firearm set covers single-projectile primary fire. Shotguns,
revolver, taser, active burst mode, knives, grenades and unknown classes stay manual.
Autofire checks native primary cooldown, ammo, reload, weapon-service cooldown,
semi-auto release, freeze, defusing and local player state. It requests the shot
before native command CRC finalization and anti aim, so anti aim yields normally.
A scoped temporary input press asks the native history recorder to create its
own attack record; the normal builder produces the serialized attack index.
Temporary input events/held/index state are restored. Only the generated current
command claims primary fire; no physical key or persistent attack latch is held.
Rebuild cleanup preserves human takeover. If state or history changes before aim
can be applied, the owned attack is cancelled and the original native finalizer
regenerates its CRC. Native recorder arena/timing caches are not rewound.

**Console cleanup:** under **Aim**, enable **Our logs only** to clear native
CS2 messages from the console and keep incoming output to this project's `AIM_`
logs. **Clear console** clears the display and restores the latest 256 project
lines, in order and with their original colors. Enabling the filter also requests
a clear. Disable it to show new native messages again; previously cleared native
messages cannot be recovered. The filter starts off each session and is not saved
in configs. Workspace log files keep their existing contents and logging options.

Requests use CS2's locked command queue; clearing and replay run after its native
clear command on the command owner. Controls are disabled if the installed engine
contract cannot be verified. See [console-control evidence](tools/CS2_CONSOLE_CONTROL_EVIDENCE.md).
Build and use **Load Overlay.cmd** as usual; restart CS2 normally before loading
the rebuilt DLL if an older copy is resident. Stopped and resident is not physically
unloaded. Native display behavior still needs an offline/main-menu check.

**Verification, October 18, 2026:** public and diagnostic x64 Release builds
pass all **37/37 CTest suites** each; all **20 installed-file compatibility
checks** pass. Menu previews at normal/narrow widths were inspected. These
results cover substituted native boundaries and static contracts; live CS2
clear/filter behavior has not been observed.

**Realtime debugging:** **Aim > Shot logs** defaults on. The console and
`logs/overlay-<run>.log` receive one result per observed local native
`weapon_fire` event, including manual fire. Published, ready attacks that receive
no fire event also produce a separate command timeout; they never count as native
shots or consume shot IDs. Typical output:

```text
AIM_SHOT #1 HIT Mayer in chest for 34 (66 HP) | weapon ak47 | target Mayer [0x00008006] | HC 78% (200/256) | delay 22ms
AIM_SHOT #2 MISS spread | weapon ak47 | target Mayer [0x00008006] | HC 78% (200/256) | capsule miss 1.00u
AIM_SHOT #3 MISS no impact | weapon ak47 | HC unknown
AIM_SHOT #4 UNCONFIRMED shot overlap | weapon ak47 | HC unknown
AIM_SHOT cmd #42 UNCONFIRMED unregistered shot | weapon ak47 | target Mayer [0x00008006] | HC 78% (200/256) | no fire event after 1000ms
```

Console result lines use **green for confirmed damage**, **red for misses**,
**amber for unconfirmed results and warnings**, **cyan for logger status**, and
**gray for verbose diagnostics**. Colors use the installed native color-print
export, with one bounded call per line; the workspace file remains plain text.
If that optional export cannot be verified, the verified plain console sink
continues working. Outcome words remain visible alongside the colors.

Results settle after a 250ms outcome window plus a 60ms ordering allowance.
Damage, remaining HP and hitgroup come from native damage events. Hitchance and
intended target come from a recent compatible weapon/command snapshot; they can
be unknown. Receipt delay is a local event timing measurement, not network ping.
Reasons use short labels, with numerical evidence in `AIM_SHOT_DETAIL`,
`AIM_SHOT_REASON` and `AIM_SHOT_GEOMETRY` file records (also visible with verbose
diagnostics):

| Reason | Evidence |
| --- | --- |
| `spread` | Unspread aim crosses the saved capsule; every mapped impact ray misses it within the saved spread/inaccuracy cone. Reports closest capsule clearance. This is an inference against the copied snapshot. |
| `impact short` | Every mapped impact ends before the capsule, although its extended direction crosses it. A later impact that reaches the capsule prevents this reason. |
| `no impact` | Native fire was observed, with no mapped impact or hurt within the result window. |
| `unregistered shot` | A published, ready attack has no compatible observed native fire within 1000ms plus the 60ms ordering allowance. The line names the command rather than inventing a shot ID. |
| `shot overlap` | An effect fits multiple possible shots. Damage remains visible separately as `AIM_HIT HIT ...` with the possible shots. |
| `late event` | Evidence exceeds the timing allowance without multiple shot candidates. |
| `interrupted` | Source/lifecycle changed or logging stopped before settlement. |
| `tracking overflow` | Bounded tracking storage lost required evidence. |
| `unknown: ...` | A specific explanation follows: missing aim context/capsule, aim missing the capsule, impact outside the saved cone, impact crossing without damage, conflicting impacts, or unavailable direction. |

The events contain no shot ID, so overlapping bursts and late-event guards still
retain uncertainty instead of choosing a shot by FIFO or intended target. A
`MISS` means no confirmed damage within the receipt window. A short impact does
not identify world geometry, material or penetration; an unregistered command
reports missing local event observation, not proof of server rejection. The
project has no verified server seed, resolver, lagcomp/backtrack model, local
prediction comparison or penetration scan, so their proposed reasons
are omitted. Even 100% sampled hitchance does not prove a seed misprediction.
Command rebuilds share one timeout; competing compatible commands suppress
timeouts when a fire could acknowledge any of them. Cooldown-only manual input
and estimates cannot create attack timeouts. Lifecycle changes and cooperative
stop close unfinished observations as unconfirmed. A lost
local snapshot retains only copied full handles for up to one second to preserve
late damage/death evidence; a new valid pawn or lifecycle event resets immediately.
Native events have no lifecycle cookie, so a delayed same-pawn event across
rounds still cannot be distinguished exactly from a fresh receipt.

**Verbose aim diagnostics** defaults off. Enable it to also show the existing
`AIM_HITCHANCE`, preparation, firing and numeric shot detail records in the
console. Those technical records always stay in the session file. Both logging
preferences are saved in presets and can change independently of autofire.
Console lines omit internal queue/thread prefixes. `AIM_CONSOLE_READY` confirms
the native console sink; `AIM_SHOT_LOG_READY` confirms native event interests
registered on the engine owner. Unavailable native contracts produce an explicit
file/console status. Callbacks copy/enqueue bounded values; render-owner output
performs sink I/O outside tracker locks. Listener storage remains resident and
inert after stop until normal process exit.

Autofire separately traces the direct eye-to-capsule-center path on the native
command owner, before requesting fire and again before publishing its aimed
history. Obstructed or unavailable traces prevent a request; newly blocked paths
cancel the owned automatic attack and regenerate its native CRC. Source, target,
geometry, weapon and settings are revalidated after tracing. No clear-path result
is cached across callbacks. Manual primary input remains available.

Before multihitgroup selection, autofire selected the angular-nearest **eligible** enemy within Aim FOV. A nearer
enemy with unavailable geometry, insufficient hitchance or a blocked path does
not prevent another eligible enemy from being selected. At 180 degrees, this
includes enemies behind the current view. Candidates share one current weapon
snapshot; the selected path is traced again immediately before requesting fire
and at history publication. The multihitgroup ranking described above now
chooses among all enabled regions; nearest-landmark targeting remains the
manual fallback when modeled shots are unavailable.
The minimum hitchance still applies regardless of FOV, including when scoped
weapon accuracy is low. `AIM_HITCHANCE` now records angular `separation`, outgoing
`aim` and `eligibility_rejected` to distinguish selection from firing gates.

**Wide-FOV correction, October 18, 2026:** public and diagnostic Release builds
each pass **44/44 CTest suites**, including 113 integrated pipeline checks and
real entity-scan regressions for a 180-degree fallback. All **21 installed-file
compatibility checks** pass in `logs/compatibility/20261018T144006926027Z/`.
Both DLLs retain zero static TLS and delay imports. No client/server was launched;
live behavior of this correction remains unobserved. Restart CS2 normally before
using **Load Overlay.cmd** with the new build if an older copy is resident.

With **Autowall** off, the conservative direct-path check blocks all cover.
With it enabled, blocked rays use the native penetration and damage path
described above. Neither mode traces every spread sample. Hitchance remains a
geometric selected-capsule estimate. Native shot-time
interpolation, future recoil/spread changes and server timing can differ from
this snapshot; a clear center path and high percentage do not guarantee a hit.

Contracts and rediscovery:
[weapon model](tools/CS2_AIM_WEAPON_EVIDENCE.md),
[authored geometry](tools/CS2_AIM_GEOMETRY_EVIDENCE.md),
[native autofire](tools/CS2_AUTOFIRE_EVIDENCE.md),
[autofire occlusion](tools/CS2_AIM_OCCLUSION_EVIDENCE.md),
[console](tools/CS2_AIM_CONSOLE_EVIDENCE.md),
[shot result events](tools/CS2_SHOT_EVENTS_EVIDENCE.md).

**Occlusion verification, October 18, 2026:** public and diagnostic x64 Release
builds succeed, with **36/36 CTest suites passing in each**. Native-callback
substitutes cover world/entity blockers, selected-pawn hits, native blocking flags,
near-endpoint cover, invalid results, missing/changed managers and native faults.
Integrated command tests cover fresh request/publication traces, late cover,
cancellation with native CRC regeneration, rebuilds and physical-primary
takeover. Both DLLs retain zero static TLS and delay imports. These are static
and standalone results: all nineteen installed-file checks pass in
`logs/compatibility/20261018T091353099219Z/`, and all 21 deliberately incorrect
occlusion-header contracts were rejected. No live wall-response test is claimed. Restart CS2
normally before loading the rebuilt DLL through **Load Overlay.cmd** if an older
copy is resident; stopped and resident is not physically unloaded.

**Verification, October 16, 2026:** public and diagnostic x64 Release builds
succeed, with **31/31 CTest suites passing in each**. The new suites include
143 estimator/geometry checks, 221 native-weapon snapshot checks, 52 autofire
command checks, 53 integrated command-pipeline checks, console formatting/stop,
atomic settings publication and real menu/config interactions. Tests cover
thresholds, native shot construction, stale state, manual takeover, disable,
focus loss, cooperative stop, cancellation and CRC regeneration. All seventeen
installed-file verifiers pass in
`logs/compatibility/20261016T164801935369Z/`; 60 deliberately incorrect new
contracts were rejected across the four new verifiers. The software D3D11 Aim
preview was inspected in `logs/hitchance-menu.bmp`. Both DLLs retain zero static
TLS and delay imports. These are static and standalone checks; live autofire,
native console visibility and hit registration remain unobserved.

**Autofire correction, October 18:** the supplied
`logs/overlay-4016-134366430011225970.log` records valid 100% estimates with
`ready=1`, `autofire=1` and a 0% minimum, but zero requested/published automatic
commands. Its 1,079 decisions came from the 1,079 manual primary-input commands.
The preparation reader incorrectly required scalar angle presence `0x7`.
Native base views serialize nonzero components independently and ordinarily
use `0x3` for pitch/yaw with zero roll omitted, so preparation stopped before
acquisition and requesting. The subsequent history callback overwrote the
shared status, hiding this rejection behind “awaiting primary-fire history.”

The reader now accepts native sparse zero components, cleared retained angle
messages and the native null-message zero default. It preserves base/camera
bytes and still rejects inconsistent, unreadable and nonfinite payloads.
`AIM_PREPARE_DIAGNOSTICS` reports preparation calls, completed evaluations,
view rejections, other blockers, exact stage, scalar/parent presence and view
values independently of manual history diagnostics. The native attack recorder
and hitchance sampler are unchanged by this correction. Regression fixtures
reproduced the original failure before the fix, then verify automatic request
and aimed history publication without manual input for sparse and zero views.
They also cover an exactly 0% estimate with a 0% minimum. The user subsequently
confirmed the corrected autofire works in an offline bot match. This report does
not establish live behavior of the new shot result logger.

Both corrected public/diagnostic Release builds succeed and all 31 suites pass
in each, including 75 integrated pipeline checks and 71 command/view checks.
All seventeen installed-file checks pass in
`logs/compatibility/20261018T082358165777Z/`. The expanded native-view verifier
rejects 27 mutated instruction windows and an incorrect parent-angle presence
constant. Both DLLs retain AMD64, zero static TLS and zero delay imports.

**Shot logging verification, October 18:** public and diagnostic x64 Release
builds succeed with **35/35 CTest suites passing in each**. New suites cover
91 native event adapter checks, 61 tracker/formatting checks, 85 integrated
logging lifecycle checks and 46 settings/routing checks. The existing pipeline
now passes 80 checks, including logging changes during an automatic request and
stale manual-shot metadata. Real menu/config checks cover both new controls.
All eighteen installed-file checks pass in
`logs/compatibility/20261018T085349765675Z/`; 26 incorrect event header contracts
were rejected. The software D3D11 preview was inspected in
`logs/shot-logging-menu.bmp`. Both final DLLs retain AMD64, zero static TLS and
zero delay imports. These are static and standalone observations; native event
delivery and the new shot logger's in-game output await an offline client check.
The working autofire correction was checkpointed as `5aa11f3` before this stage.

**Reason/color follow-up, October 18:** both public and diagnostic x64 Release
builds succeed with **36/36 CTest suites passing in each**. Regressions cover
spread cone/aim/impact geometry, short and later penetration impacts, overlapping
and late evidence, command timeouts, command rebuilds, lifecycle/stop/disable,
capacity loss, native outcome colors and plain fallback. Installed event,
weapon and plain/color console contracts pass their static verifiers; reports
and build/test logs are `logs/shot-reasons-*`. Both payloads remain AMD64 with
zero static TLS and delay imports. Actual in-game event attribution and console
color appearance remain unobserved; verification used standalone native-boundary
substitutes and installed-file inspection.

Restart CS2 normally before loading the new DLL through **Load Overlay.cmd**
if a previous copy is resident. Open **Aim**, enable silent aim, set the minimum
and sample count, then enable **Autofire** in the authorized offline/private
environment. Stopped code remains resident until normal process exit.

The command callback selects the alive, non-dormant enemy whose selected point
is closest to the shot's native view direction within the FOV. Each selected
body region resolves its animated bone by name on the current model: `head_0`,
`spine_3`, `spine_2`, `pelvis`. These establish region acquisition; an available
authored capsule then supplies its center. Missing names, unsupported skeletons or unavailable cached poses
skip that player. There are no fixed bone indices or collision-hull fallbacks.
The reader uses the latest cached world pose and copied live eye offset; it
does not force animation refresh, mirror native eye interpolation or add lag
compensation. It retains no engine entity/model pointers across callbacks.

A separate verified input-history hook calls the original builder first, then
updates existing history angle messages for primary-fire input. A native attack
index handles short taps; held primary fire also updates the history consumed
by CS2's retained/interpolated continuation path. Native attack indices, button
states, timestamps, base/camera angles, movement and movement CRC stay unchanged
for manual shots. Autofire adds only its owned native attack request before CRC
generation as described above; secondary fire is outside this implementation.
Previously retained native history can affect boundary
shots when enabling, disabling, stopping or changing targets; it is not
rewritten retroactively.

Damage/minimum-damage, autowall, penetration, visibility tracing, recoil
compensation and spread correction are outside this implementation.

The existing build and **Load Overlay.cmd** workflows include this feature.
Restart CS2 normally before loading if an older DLL remains resident. End and
the existing stop command drain aim mutations cooperatively; forwarding hooks
remain resident until process exit. **Stopped and resident is not physically
unloaded.** No loader/session gates or server prerequisites were added.

**Verification, October 15, 2026:** public and diagnostic x64 Release builds
succeed, and all **17/17 CTest suites pass in each**. Synthetic command tests
cover taps, held fire, exact preservation of camera/base/buttons/CRC/metadata,
invalid and read-only history, changed snapshots and base-message aliasing.
Target tests cover region selection with dynamic indices, FOV, enemy filtering,
missing poses/bones, ownership/model/skeleton changes and dead local players.
Mouse-driven menu checks exercise enable, FOV and all four region choices.
All ten installed-file compatibility checks pass; the three new verifiers
freeze history producer/consumer, eye and named-bone evidence. This is static
and standalone verification: no live client shot, camera behavior, hit
registration or full-auto burst has been observed in this pass.

```powershell
python tools/inspect_silent_aim.py
python tools/inspect_silent_aim_targets.py
python tools/inspect_silent_aim_hitboxes.py
.\build\Release\public_menu_preview.exe .\logs\aim-menu.bmp 4
```

Evidence: [shot history](tools/CS2_SILENT_AIM_EVIDENCE.md),
[eye/entity acquisition](tools/CS2_SILENT_AIM_TARGET_EVIDENCE.md),
[named animated points](tools/CS2_SILENT_AIM_HITBOX_EVIDENCE.md).
`AIM_BUILD_VERIFIED` records compatibility, `AIM_HISTORY` samples applied history
changes through the existing telemetry queue and `AIM_STOPPED_RESIDENT` records
mutation shutdown. These logs do not prove a hit. Runtime testing remains
limited to the repository's authorized disconnected/offline/private environment.

**First live report:** the user tested Head and Chest in built-in bot-practice
deathmatch and reported that shots still followed the crosshair. The supplied
run `logs/overlay-1708-134365459549632995.log` verifies startup and live opposing
players, but contains no `AIM_HISTORY` write records. The original build logged
only successful edits, so that run cannot identify the rejecting condition.
Do not treat its successful build/static checks as working aim observation.

The diagnostic follow-up now writes bounded **`AIM_DIAGNOSTICS`** records about
every two seconds from the render owner, including hook counts, enabled/active
state, shot/target/write counts, exact command-read rejection reason and target
acquisition/bone-read failure stages. No file I/O was added to the engine hook.
These diagnostics are always available in the public build; no additional
flags or launch workflow are needed. They distinguish empty history, absent
primary input, local identity failures, missing pose/bones and FOV rejection.
Restart normally, use **Load Overlay.cmd**, enable Aim and repeat a few shots
in the same offline setup. Diagnostic checkpoint `0e94841` did not yet establish
a repair.

**Confirmed acquisition blocker and correction:** the next practice run,
`logs/overlay-37968-134365466201047143.log`, records 744 primary-input commands
and zero selected targets, with `bone_failure=skeleton vtable`. A read-only live
snapshot independently found player scenes using vtable RVA `0x1ACDE48`.
This is `NetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance`, which
inherits `CSkeletonInstance` at offset zero and uses the same skeleton query.
The first implementation accepted only the standalone base class's vtable,
incorrectly rejecting the actual player wrapper before bone lookup.

The reader now accepts both explicitly verified skeleton classes. The frozen
verifier also checks the player's wrapper RTTI/base offset, body-component
schema field and constructor. Regressions using the observed wrapper fail for
both Head and Chest before the correction and pass afterward; unknown classes
remain rejected. Fix commit: `fa7845c`.

**Observed corrected result:** after a normal restart and reload in the same
bot-practice setup, `logs/overlay-36880-134365469299477046.log` records successful
Chest acquisition and history-angle changes (106 applied commands by
16:10:25 Warsaw time), with zero bone and write rejections in that snapshot.
The user confirmed: **shots redirected toward the bot**. This establishes the
corrected basic firing behavior in that offline test. It does not establish hit
guarantees, every weapon or a separate live Head-selection result. Public and
diagnostic Release builds each pass all 17 CTest suites; all ten static
compatibility checks pass after the correction.

## Player ESP

Names, outlined player boxes, distance labels, health bars/text, optional armor
and team colors are implemented. ESP has unlimited range; defaults show both
teams and armor text is off. Boxes project the eight corners of the
current collision bounds using copied scene position/angles/scale. They enclose
the collision hull, not animated model bones. Team colors: CT blue, T amber.

The **Visuals** tab now includes collapsible ESP appearance controls:

- RGBA colors for individual teams or one custom box color, with opacity.
- Full rectangles or corner boxes, line thickness, outlines and optional fill.
- Name fonts: menu font, Segoe UI Bold, Tahoma or Consolas; adjustable name size,
  a separate name color or the box color, plus detail-label color and size.
- Text outlines and a shared configurable box/text outline color.
- Health-bar color, optional low-to-full-health color interpolation, width and
  an independent numeric-health toggle.

The live sample uses the same renderer as player ESP, showing both team styles
when space allows. **Reset ESP appearance** restores styling while preserving
the feature checkboxes. Settings last for the current run. Additional fonts are
read from the Windows Fonts directory into the overlay's own atlas before its
GPU texture is built; unavailable fonts fall back to the menu font. No font
files are copied or distributed. Changing fonts or sizes does not rebuild GPU
resources during a frame.

The public menu is now **560×620**, up from 370×310. It clamps to smaller
viewports, wraps its tabs and keeps long sections scrollable. The standalone
preview's default canvas is 608×668 so the enlarged menu fits.

The pre-customization workspace is saved in checkpoint `fa090a2`. Public and
diagnostic x64 Release builds pass all **16/16 CTest suites** each. Added checks
cover mouse-driven RGBA/opacity edits, fonts and styles, rendered geometry and
glyph metrics, reset independence, missing-font fallback and viewport clamping.
Standalone software-D3D11 menu previews were inspected. These are standalone
checks; no new live client appearance observation is claimed.

| Key | Action |
| --- | --- |
| F1 | Toggle ESP |
| F2 | Toggle usernames |
| F3 | Toggle boxes |
| F4 | Toggle distance labels |
| F5 | Toggle health bars and HP text |
| F6 | Toggle first/third-person camera |
| F7 | Toggle armor text |
| F8 | Toggle auto bunnyhop |
| F9 | Toggle aggressive autostrafe |
| F10 | Toggle silent aim |
| Insert | Toggle the selected menu; the public menu takes over gameplay input while open |
| End | Cooperative stop |

Keys are polled while the game window is foreground. With the public menu
closed, the game can also respond to overlapping bindings. F6 only changes the
camera; teammate visibility is controlled by **Show teammates** in Visuals.
Settings are shared between the menu and hotkeys, and last for the current run.

## Movement assistance

Both features start enabled. Hold **Space** to activate them while playing the
authorized built-in offline bot match. **WASD**, including diagonal combinations,
selects camera-relative travel direction and rebuilds speed after slowing down.
When opposing keys overlap, the most recently observed press wins on that axis;
simultaneous first presses cancel. Diagonals remain available throughout a turn.
Releasing all movement keys stops assisted steering and allows normal coasting.
The separate **Aim > Quickstop** option brakes only for eligible automatic shots.
The steering reads physical W/A/S/D before synthesized input, so generated
side movement cannot latch the turn after release. The **Movement** menu tab
has independent toggles, also available through F8/F9 with the menu hidden.

Bunnyhop retains a pending press across command callbacks instead of releasing
it on the next command. Engine input/press feedback, takeoff and the controller's
simulation tick counter drive bounded retries and release acknowledgement.
After observing press processing, another grounded simulation tick without a
new buffer signal can start an earlier release/retry. Cooldown is measured from
the conservative observation time, without mixing engine stamp clock domains.
When the server's automatic bunnyhop mode is already enabled, held jump remains
down throughout flight. The code reads that setting without changing it.

**October 16, revision 9: private-session jump corrections.** The latest
revision 8 log contained 18 command sequences with opposite jump outputs during
rebuilds. Creation-tick corrections and predicted controller-clock rewinds were
incorrectly treated as reasons to discard the selected input. Revision 9 retains
the same sequence's native jump state and timed edge across both corrections.
A simulation rewind on a new command preserves the pending/releasing phase,
restarts its waits conservatively and discards processing proof from the old
prediction branch. It cannot turn an outstanding release into another press
before engine release acknowledgement. Repeated callbacks consume no simulation
wait time. Continuous corrections can conservatively postpone retries.

Both x64 Release variants build and pass all 18 CTest suites, including captured
release/rewind regressions and 10,000 combined clock/ground rebuild corrections.
All ten installed-binary compatibility checks pass; no offsets changed. This is
code and standalone verification, with no post-fix client/server observation yet.
Restart CS2 normally before **Load Overlay.cmd**; test with F9 off first to
isolate jumping. Logs identify `MOVE_IMPLEMENTATION revision=9`, and
`MOVE_REPLAY` includes `clock_corrections` with the current/prior command clocks.
See [movement evidence](tools/CS2_MOVEMENT_EVIDENCE.md#october-16-revision-9-prediction-correction-fix).

**October 22, revision 10: Axion autostrafe port.** Autostrafe now uses the
MIT-licensed Axion steering algorithm: `atan2(15, horizontal speed)` with a
45-degree clamp, velocity-yaw correction above speed 80, and command-sequence
parity to alternate near the requested heading. Smoothing is now **50**, giving
correction `2 * ideal angle` (the initial port used Axion's default 99, or
`0.04 * ideal angle`). At speed 250 that changes correction from about 0.137
degrees to 6.867 degrees. Larger corrections steer more aggressively and can
spend more momentum during a turn. The threshold for returning to alternating
strafes also widens; movement near an aligned heading keeps the same ideal angle.
This replaces the previous
eight-fragment solver with **one full-input wish per command**, emitted at time
zero through the existing analog transition path. It follows Axion's turning
behavior, including its wide-turn correction for behind-velocity intent; it no
longer applies the prior solver's direct reversal braking.

The port fixes Axion's W-only early return, uses our physical WASD arbitration
and positive-left input convention, and avoids its hardcoded command ring and
retained static keys. It changes normalized movement and direction buttons
without rotating the camera. Rebuilds preserve sequence parity, refresh steering
from frozen command-start input, and keep the selected jump edge. Releasing WASD
restores ordinary axes before integration; landing restores physical ground
input. Replay retains capacity to neutralize retired analog events. The existing
bunnyhop implementation is unchanged. License and exact source revision are in
[Axion provenance](third_party/Axion-CS2-RAGE-CHEAT-PROVENANCE.txt) and
[MIT license](third_party/Axion-CS2-RAGE-CHEAT-LICENSE.txt).

Build and load using the existing commands and **Load Overlay.cmd**. Restart CS2
normally before loading the rebuilt DLL if an older copy is resident. Logs
identify `revision=10`, `steering=axion_wasd`, `smooth=50`, `subticks=command1`.
Stopped and resident is not physically unloaded. Standalone verification does
not establish whether this port feels better in actual offline/private gameplay.

**Initial port verification, October 22 (smoothing 99):** public and diagnostic x64 Release builds succeed;
both pass **56/56 CTest suites**, including Axion angle/branch fixtures,
independent analog-physics integration, directional trajectories and replay/
handoff regressions. Installed movement contracts pass. Both DLLs retain zero
static TLS and delay imports. No client or server was launched.

**Smoothing-50 verification, October 22:** rebuilt both public and diagnostic
Release DLLs; all six movement, jump, steering, benchmark, memory and replay
CTest suites pass in each build. Updated correction fixtures match the new
setting. No game was launched; live feel at 50 remains unverified.

Air acceleration, air wish-speed cap, surface friction, effective server/weapon
speed limit, surface speed factor, stamina, jump cooldown and engine tick quantum come from the current engine
state. No match presets or fallback cvar values are used; no game cvar is written.
Missing/invalid physics leave the affected assistance inactive. The exact
installed-client input callback, C++/protobuf layouts and helper ABI were checked
against local metadata and machine code. See
[movement evidence](tools/CS2_MOVEMENT_EVIDENCE.md) and
[read-only build verifier](tools/inspect_movement.py).

Command mutation runs on the engine input callback before its original
movement-data finalization. It adds timed analog and separate time-zero jump
transitions through verified game-owned protobuf allocation/reuse. The first
analog delta starts from historical effective input; later deltas start from
the preceding generated axes, matching the engine's integration order. Final
base values alone are ingested after that integration. Existing camera
deltas/times, attacks and other actions retain relative order. State is discarded on
pawn/service/handle changes, command rollback, loss of focus or ownership change.
Dead/frozen/swimming/waterjump pawns and ladder/noclip/observer modes retain normal
input. End or an explicit stop disarms movement and waits for mutations to finish;
hooks/trampolines and stopped code remain resident until normal process exit.

**Previous live result:** the user's offline run of `e4c98b2`, captured in
`logs/overlay-34356-134363765387104894.log`, showed observed missed-hop pauses,
including a 393ms grounded interval, and confirmed fixed sideways steering.
This prompted the pending-jump/continuous-turn revision and explicit subtick
integration transitions.

**Earlier live failure:** revision 2 (`90a6db4`) in
`logs/overlay-39840-134363788528589904.log` generated zero assisted jump presses.
The legacy-jump reader was given the value-pointer slot instead of the Cvar
wrapper start, so jump-rule reads failed. Revision 3 corrects that address and
reports the individual failed read through `MOVE_JUMP_RULES`. The user also
reported large airborne FPS drops; that log did not measure callback cost.

**Revision 3 verification:** public and diagnostic x64 Release builds and all nine
CTest executables pass. Tests cover pending/retried jumps, clock rollback,
transition decoding, circling/release trajectories, extreme caps and durations,
guarded memory copies, the Cvar pointer-slot mistake and bounded deferred logging.
The read-only verifier independently resolves Cvar registration operands and
rejects a source header containing the previous erroneous address.

The revision 3 air solver evaluated at most 13 candidates instead of scanning/refining
angles; its standalone median dropped from 11.29 to 0.99 microseconds per
command on the same input corpus. Command reads use guarded in-process copies
instead of a kernel read per scalar. Movement callbacks enqueue bounded records;
Present drains at most eight records, with overflow counted. Log writes use the
OS file cache; forced storage flushing occurs at cooperative stop instead of
every line. File writes still run on the render thread. `MOVE_PERF` measures
assistance callback work, including command-lock acquisition, separately for
airborne commands; it excludes the original engine finalizer and rendering.

**Latest live result:** the user reported nearly flawless bunnyhop for revision 3
(`cf1a3b2`), but poor steering and severe FPS drops. Its log
`logs/overlay-20344-134363800072269150.log` records airborne assistance means
around 11–12 milliseconds, compared with tens of microseconds in idle/ground-only
windows. A fast standalone solver did not resolve that runtime cost.

**Revision 4 test:** directional WASD steering replaces continuous circles and
release-forward recovery. Twenty-five independent steering checks cover all
eight directions gaining from standstill/low speed, reversal, camera changes,
rebuilding after a slowdown, cap/duration extremes and no assisted movement on
release. The analytical planner evaluates at most two candidates. `MOVE_STAGE`
splits live callback cost into controls, pawn/layout reads, jump, engine speed
lookup, speed modifiers, physical keys, solver, subticks/history, append, apply
and diagnostics; `MOVE_PERF` separately records command-lock waits.
The user's live profiling run (`logs/overlay-15980-134363808628744714.log`) showed
layout preflight alone averaging about 4 milliseconds, plus several hundred
microseconds per subtick/history/append check. The engine speed lookup and solver
took only a few microseconds; lock waits were negligible. Those preflight paths
issued repeated VirtualQuery calls. Revision 5 replaces them with guarded,
byte-preserving writable probes on the engine callback's owned command storage.
It does not cache page permissions across commands.

That run also exposed missed hops: unchanged simulation jump stamps after
grounded press attempts. The producer rebuilds native buttons even when reusing
a command sequence, which invalidates the earlier assumption that such commands
could be skipped. Revision 5 repairs rebuilt commands using the cached plan,
preserving its timing and reusing matching generated events instead of appending
duplicates. Public and diagnostic x64 Release builds pass all ten CTest suites,
including 17 replay checks and writable-probe failure/preservation checks.
These changes need a fresh live run. Ground
command timing and a full tick acceleration calculation do not establish perfect
fractional landing timing. Collisions, prediction/substep timing and the game's
jump/speed rules can change the resulting speed. Exact per-substep duck, walking
and firing speed transitions are not mirrored by the air solver.

**Revision 6 landing follow-up:** revision 5's live run
`logs/overlay-14292-134363818907289145.log` records callback means around
50–60 microseconds, confirming the millisecond preflight stalls were removed.
However, 92 held-input landing episodes had a repeated six-command-tick
press/release/retry pattern; prediction corrections complicate the raw ground
flag transitions. The engine clears the usable stamp on successful jumps, so an
invalid stamp alone cannot diagnose a rejected press. The concrete replay bug
was treating predicted ground changes as new input: 164 of 625 commands received
opposite jump outputs during rebuilds. Revision 6 keeps the selected input for
the same command sequence/base/tick despite that prediction result. It restores
physical ground axes before integration, shortens observed-processing retries,
and adds a read-only before/after engine jump trace to distinguish actual jump
execution from later prediction corrections. Engine friction and the current
bunnyhop speed rule still apply; no match setting is changed.

**Observed revision 6 result:** the user reported that bunnyhop works perfectly
in the offline match. The snapshot of
`logs/overlay-32264-134363831973223334.log` had zero opposite jump outputs across
442 commands. Of 176 qualifying held-Space landing episodes, 139 took off after
one command tick and 37 after two; observed wall-time median was 16 ms, compared
with 94 ms in the earlier run. Median observed landing-to-takeoff speed change
was zero. Engine tracing confirmed immediate client ground-clear/jump executions;
prediction duplicates are not counted as independent authoritative jumps.
Both Release menu builds pass all ten test suites, including 39 jump-planner
checks and same-command ground-rewind regressions. The user still disliked
autostrafe in this run.

**Revision 7 aggressive steering (baseline `4f0a1aa`):** replaced the zero-gain
turn policy with one keeping half the best squared-speed gain. Physical keys and camera yaw are sampled before
command replay; changed steering refreshes analogs from the frozen command-start
velocity, physics and planner state. Repeating an unchanged command retains its
output. The existing jump choice and edge remain stable across either kind of
rebuild, including prediction-only ground changes. Newest opposing keys win,
and release restores historical assisted axes through a time-zero transition.

The implementation derives its projection choices from the locally verified CS2
accelerator. Research references were Valve's published
[Source air acceleration](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp)
and a public [CS2 directional autostrafer](https://github.com/cacamelio/weave-v3-csgo-and-cs2/blob/main/cs2/counterstrike2/feature/movement/movement_.cpp).
The latter supplied control-design background; its code, hardcoded angles and
runtime layouts were not imported. Neverlose's implementation is not available
for verification.

**Revision 7 verification:** both public and diagnostic x64 Release builds pass
all ten CTest suites, including 30 steering checks and 37 replay checks. The
steering tests independently integrate emitted analogs, compare strong turns
with a numerical oracle, and cover diagonal circles, immediate direction
changes, camera wrapping, low-speed recovery and varied/fractional physics.
The installed-client read-only verifier passes for the same inspected build.
The standalone solver benchmark remains below one microsecond per command in
the recorded runs; this does not measure the complete live callback.

An independent two-second model comparison at 360 degrees/second with a diagonal
held gives 393 → 416 final speed and 7.3 → 1.0 degrees mean heading error. The
before/after harness and complete outputs are under `logs/revision7-comparison/`.
Very fast intent rotation at rising speed can exceed available turning authority;
useful gain and arbitrary direction changes cannot both be guaranteed by ordinary
air physics. These were model results; the user's later offline run is below.

**Observed revision 7 result:** `logs/overlay-18352-134363851916287038.log`
confirms revision 7, ordinary cap 30 / acceleration 12 and working input refresh.
The user reported improved control but weak acceleration and slow W→S reversals.
Active snapshots still predicted gain while several flight observations lost
speed; those copied snapshots include prediction/collision effects. All recorded
ingestion mismatches followed Space release, so this log does not establish an
active command-ingestion failure. Body/leg animation is not a physics measurement.

**Revision 8 direction capture and timed acceleration:** removes the gain floor
during corrections and adds the eight timed updates described above. A W→S
request now cancels unwanted momentum directly before rebuilding speed. Timed
analogs interleave with original camera/action messages without changing their
relative order. Rebuilds reuse the whole sequence or regenerate it from frozen
command-start state when intent changes; the selected jump edge stays stable.
Release/ground handoff reduces it to one physical-axis transition and neutralizes
the remaining owned events. Allocations and ownership remain game-managed.

The independent model harness in `logs/revision8-comparison/` compares baseline
`4f0a1aa` with revision 8 under the same synthetic ordinary physics. From speed
250, W→S reaches the requested heading within 5 degrees after 6 commands (94ms),
versus 17 commands (266ms). From speed 30, that takes 1 command versus 32. Holding
W for two collision-free seconds gives 594 final speed versus 422. A diagonal
rotating 90 degrees/second gives 573 versus 421, with maximum heading error 1.3
versus 5.6 degrees. At 500 degrees/second, the new policy trades speed (318 versus
378) for heading control (maximum lag 1.2 versus 20.1 degrees). These are timed
model outputs, not observed client/server behavior. Revision 8 needs an offline
gameplay test; exact per-substep camera/duck/walk/fire modifiers and collisions
can change its predictions. Camera/model angles are not manipulated for twitching.

**Revision 8 verification:** public and diagnostic x64 Release builds pass all
ten CTest suites, including 29 steering and 49 replay checks. They independently
decode/integrate all generated deltas, verify the shared acceleration budget,
capture/reversal behavior, exact-stop precision, action ordering, retained jump
edges, release/landing handoff and bounded ownership across 10,000 rebuilds.
The installed-client verifier passes. The standalone complete-command benchmark
evaluates exactly eight fragment candidates and records about three microseconds
per command on its corpus; it excludes the engine callback and rendering. No
revision 8 client run has been performed yet.

Restart CS2 normally if an older DLL is still resident. Launch the offline bot
match, load using **Load Overlay.cmd**, hide the menu with Insert and hold Space
while moving. Test with F9 off to isolate bunnyhop, then enable F9 and steer with
WASD, including diagonals and low-speed recovery. Release WASD to check normal
coasting with Aim Quickstop disabled. For revision 8, also test W→S at low and running speeds,
cycle W → WA → A → AS → S → SD → D
→ DW and turn the camera with a diagonal held. The launcher writes movement telemetry to the existing
`logs/overlay-PID-STARTTIME.log` alongside ESP diagnostics:

* `MOVE_BUILD_VERIFIED` checks input identity/layout; `MOVE_OWNER` records an
  observed command callback. Build verification alone does not prove a jump.
* `MOVE_CMD` samples physical intent, synthesized buttons/analogs, velocity,
  requested heading, predicted speed and current physics roughly every two
  seconds while assistance runs. Predictions are model outputs, not observations.
* `MOVE_JUMP_RULES` reports successful reads or the specific unavailable jump
  field. `MOVE_CMD` separately records jump-rule and jump-plan validity.
* `MOVE_PERF` reports mean/max assistance callback microseconds, airborne
  mean/max microseconds and dropped log records, roughly every two seconds.
  Queued records retain their producing thread and creation uptime; the outer
  UTC timestamp identifies the later drain.
* `MOVE_STAGE` reports calls and mean/max microseconds for each completed stage
  in that window. This isolates engine calls and command preparation from the
  planner. Both timing records exclude the original finalizer and rendering.
* `MOVE_LAND` / `MOVE_TAKEOFF` record grounded transitions and horizontal speeds;
  these copied state observations need comparison with actual gameplay.
* `MOVE_JUMP` records emitted press/release, pending phase, simulation progress,
  observed engine input and actual/usable/landing stamps. Clock domains are not
  assumed identical.
* `MOVE_CONSUMED` compares requested values with movement-service values after
  the engine ingests the command. Observed ingestion is still separate from
  physics/output confirmation. Every mismatch and first ingestion of a jump
  edge records incoming native bits and engine buttons before/after ingestion.
* `MOVE_REPLAY` records rebuilt native bits and retained/restored event counts;
  repeated commands reuse a plan. Command tick changes can trigger a replan;
  prediction-only ground changes preserve the selected jump input. Changed
  physical keys/camera refresh steering only; `steering_refreshes` counts this.
* `MOVE_GROUND_HANDOFF` records restoration of physical axes before the first
  ground slice. `MOVE_JUMP_EXEC` records the engine jump helper's immediate
  integration velocity, ground flag, buttons and stamps before/after execution.
* `MOVE_STATE` / `MOVE_SOURCE` / `MOVE_STOPPED_RESIDENT` explain inactivity,
  lifecycle changes and completed mutation shutdown.

Entity acquisition now runs on **every selected Present**, replacing the first
20Hz implementation. The view matrix is also copied for every rendered frame.
Actual refresh follows the client's frame rate. Logging remains at roughly two
second intervals. Acquisition uses read-only memory copies on the render thread,
never calls engine entity methods, and rejects mismatching full serialized handles,
identity backreferences, changed pawn links and changed source/scene identities.
This is a best-effort copied snapshot, not an engine-synchronized transaction.
All state is discarded when its module/render source changes or the overlay stops.

Current offsets are verified against the installed client's machine code and
embedded schema metadata. Historical offsets also had live-memory evidence;
that observation does not carry over to a new build. The DLL checks client PE architecture,
timestamp and image size before applying them. This is a build compatibility check,
not a session gate. Never substitute online pre-generated offsets. See
[entity evidence](tools/CS2_ENTITY_EVIDENCE.md) and
[read-only inspection/header tool](tools/inspect_cs2.py).

The first live bot test exposed two corrected assumptions: active bot pawns use
`c_cs_player_for_precache` too, and the dumper's reported highest index can be
below live pawn indices. The reader enumerates all 64 verified identity buckets
and resolves controller pawn handles directly. A wrong pawn class has its own
counter and diagnostic record. No cached raw entity pointer is used for drawing.

Distance uses the conventional Source unit-to-meter factor 0.0254 and the local
live pawn's position. If a live local pawn is unavailable (for example while dead
or spectating), distance labels are temporarily unavailable; valid player
geometry still renders without a range limit. An invalid distance suppresses
only that player's distance label. Dormant, dead, invalid and local
players are excluded. Near-camera edges are clipped and offscreen rectangles
are rejected. No visibility trace or skeleton rendering is implemented. Exact
Source2 world-aligned collision flag semantics have not been verified; applying
scene yaw may broaden the square collision box relative to the game's hull.

## ESP diagnostics and verification

Use a workspace log path such as `logs/esp.log`. The DLL appends to it; keep logs
from each run separate when comparing builds.

* `ESP_BUILD_VERIFIED` records the actual module and checked build identity.
* `ESP_SOURCE` / `ESP_INVALIDATE` / `ESP_STATE` record source and lifecycle changes.
* `ESP_SCAN` records occupied slots, controllers, live players, dead/dormant pawns,
  stale handles, rejected classes, bounds failures, read failures and acquisition time.
* `ESP_PLAYER` records each accepted name, serialized handle, team, health, armor,
  position, distance, world corners and screen rectangle. Projection codes are
  0 visible, 1 invalid input, 2 behind camera, 3 offscreen, 4 degenerate.
* `ESP_REJECT` records rejected pawn designer labels.
* `ESP_DRAW` distinguishes filtering/projection results, actual players drawn,
  ESP vertices and total overlay vertices. Vertex submission alone is not proof
  of visible output; the screenshot/user observation provides that confirmation.
* `ESP_SETTINGS` logs initial settings and each hotkey change.

Every-frame acquisition and telemetry run independently of panel/ESP visibility.
Logging stops after cooperative cleanup completes.

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The retained `esp_checks` executable checks known projection geometry, invalid
inputs, clipping, filter accounting and actual ImGui primitive generation; it
uses synthetic data, has no window and does not load the game. It is enabled by
CMake's `BUILD_TESTING` (use `-DBUILD_TESTING=OFF` to omit it).

October 13, 2026: Release build succeeded without project warnings; all ten
checks passed. PID 7972 mapped successfully at `0x180000000`. The user's offline
bot-match screenshot showed names, team-colored boxes, meter labels and health
bars; the panel showed nine live players and four drawing. Matching
`logs/esp.log` recorded ten controllers, changing health/positions, nine or ten
live players, valid matrix/distance data, zero read/bounds/class failures and
roughly 1ms acquisition samples. This verified the initial 20Hz version.
The subsequently requested every-Present refresh build compiles and passes the
same tests; **its live performance still needs testing after a normal restart**.
The running PID 7972 mapping remains the earlier 20Hz version; it was left active.
Use End or normal game exit to stop that copy. Do not map another copy into it.

The first test process PID 14532 was intentionally cooperatively stopped for a
rebuild; `logs/esp-test.log` records `STOPPED_RESIDENT`. The vanished panel was
cleanup, not a crash or physical unload. The user then exited that process.
Computer UI control was stopped with Escape; the active DLL is independent of it.

## Inspected installation

Client: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\cs2.exe`.

On October 21, 2026, `game/csgo/steam.inf` reports patch **1.41.9.1**,
client/server **2000934**, source revision **11150591**, version date October 20,
2026, 11:41:22. Steam appmanifest build ID: **26087496**. Steam's UpToDateCheck API
reports client version 2000934 as current. Executable SHA-256:
`8614097AAA2761B0A7157C0045861713E769FB1E8235F50FC68F411294DD60CF`;
client DLL SHA-256:
`5A46A09B521BBD607012146364DAFAE2ABFA50364246638391990D4C7DFB9D49`.
Installation hashes and the API response are recorded in
`logs/compatibility/26087496-final/summary.json` and
`logs/research/26087496/core/steam-current.json`.

Both D3D11 and Vulkan renderers exist. Historically, the launched client (PID 5980) loaded `rendersystemdx11.dll`, `d3d11.dll`, `dxgi.dll`, and the game-local `D3DCOMPILER_47.dll`. This establishes D3D11 for that run. Live graphics objects are verified in DLL callbacks rather than inferred from installed DLL names. Loaded MSVC runtime modules included VCRUNTIME140 version 14.51.36247.0 and Windows UCRT; this does not identify the compiler used to build all game modules. This update made no fresh runtime/graphics observation.

The October 15 executable/client/engine/D3D11 PE inventory also confirms AMD64
PE32+ (`logs/compat-pe-20261015.json`). The user's latest PID 26684 log records
an actual D3D11 device, feature level 11.1, on the NVIDIA GeForce RTX 3060 Laptop
GPU at 1920x1080. No new process was launched for the compatibility update.

## Build (PowerShell)

Requires Visual Studio 2022 Desktop development with C++, a Windows SDK, CMake and Git. The verified local toolchain is MSVC 19.42.34436.0 with SDK 10.0.22621.0. Dependencies are pinned; first configure requires network access.

```powershell
Set-Location 'C:\Users\dawid\Documents\ChatGPT\cs2'
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
cmake --build build --config Release --parallel
```

Outputs: `build/Release/cs2_overlay.dll`, `build/Release/mapper.exe`, `build/Release/cs2_loader.exe`, and preserved third-party licenses in `build/Release/licenses/`. Use Release; the manual mapper intentionally supports a restricted payload contract rather than arbitrary Windows DLLs.

## Public and diagnostic interfaces

The default build uses a minimal dark public menu with **Visuals**, **Movement**,
**World**, **Lighting**, **Aim** and **Configs** tabs. Visuals includes ESP, boxes, names, health bars and HP text,
distance labels, teammates, and armor text; Movement includes the two independent
movement toggles. Only implemented features appear. ESP has
unlimited range; there is no range slider or +/- range hotkey. Insert shows or
hides the menu; End cooperatively stops the overlay. Player graphics render
behind the menu. Drag anywhere across the full **CS2** title bar, including its
edges and padding. The menu stays within the viewport after a resize. Scroll
overflowing settings with the mouse wheel or drag the section's scrollbar.

The public menu reads the installed Windows Segoe UI font for its typography,
with an ImGui default-font fallback. No system font file is copied or bundled.

Opening the public menu releases and shows the mouse during gameplay; pressing
Esc to pause first is no longer needed. Mouse/keyboard gameplay input is
captured while the focused menu is open. Movement assistance, quickstop,
automatic firing and aim edits wait while the menu owns input. Closing the menu,
losing focus or requesting cooperative stop restores the native cursor mode on
the SDL event owner callback. If that callback stops running, restoration stays
pending; stopped code remains resident until process exit.

The render thread polls cursor/buttons and config-name keys, scales client
coordinates to the back buffer and consumes queued wheel events. Held clicks
when showing/refocusing the menu are ignored until released. Overlay hotkeys
remain available. Telemetry still writes to workspace logs but does not appear
in the public menu. See [menu input evidence](tools/CS2_MENU_INPUT_EVIDENCE.md)
for the installed SDL contracts and verification limits.

**Menu QoL verification, October 20, 2026:** public and diagnostic x64 Release
builds each pass **50/50 CTest suites**. Checks cover dragging across the whole
title bar, wheel/scrollbar interaction, cursor capture/restoration, focus and
owner changes, fractional wheel deltas, F6 preserving teammate visibility and
cancelling pending automatic shots on menu opening. All 23 existing installed-file
compatibility components pass. Both payloads retain zero static TLS and delay
imports. Standalone normal/narrow menu previews were inspected. No CS2
client/server was launched; live gameplay cursor feel remains unobserved.
Restart CS2 normally before using **Load Overlay.cmd** with the rebuilt DLL if
an older copy remains resident.

`CS2_DIAGNOSTIC_UI` selects the interface independently of compiler optimization.
Keep both builds in separate directories. The original crude panel retains its
technical counters and hotkey summary in the diagnostic build (with unlimited
range). Its mouse input remains disabled.

```powershell
# Public interface (also used by Load Overlay.cmd).
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DCS2_DIAGNOSTIC_UI=OFF
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

# Original diagnostic interface, with Release optimization.
cmake -S . -B build-diagnostic -G 'Visual Studio 17 2022' -A x64 -DCS2_DIAGNOSTIC_UI=ON
cmake --build build-diagnostic --config Release --parallel
ctest --test-dir build-diagnostic -C Release --output-on-failure
```

Launch `build/Release/cs2_loader.exe` for the public menu or
`build-diagnostic/Release/cs2_loader.exe` for diagnostics. Each launcher uses its
adjacent mapper and DLL. Restart CS2 normally before changing between builds;
stopped code remains resident until process exit. The UI change is verified
using standalone rendering/interaction checks without loading CS2; client
mouse behavior and cursor locking still require observation in an authorized
session.

Optional standalone visual preview, rendered by the same menu code through
software D3D11 without starting or loading CS2:

```powershell
cmake -S . -B build -DCS2_BUILD_UI_PREVIEW=ON
cmake --build build --config Release --target public_menu_preview --parallel
New-Item -ItemType Directory -Path logs -Force | Out-Null
.\build\Release\public_menu_preview.exe .\logs\public-menu-preview.bmp
```

October 13, 2026: public and diagnostic x64 Release builds succeeded. All three
CTest executables passed in each build, covering projection/unlimited range,
loader reservations, menu clicks/shared settings, disabled controls, dragging,
and viewport resizing. The public menu was visually inspected from its actual
offscreen D3D11 render. This does not establish live client input behavior.

## One-click loader

Start CS2 and leave it at the disconnected main menu or in offline Practice.
Double-click **`Load Overlay.cmd`** in the project folder, or double-click
**`build/Release/cs2_loader.exe`** directly. No PID, DLL path or injection command
is needed. The launcher immediately loads into the single running `cs2.exe`
using the existing mapper and adjacent DLL. It reports missing builds, no client,
multiple clients and mapper failures in its own window. It does not start CS2.
Environment verification remains the operator's responsibility.

The native launcher has a status window and an **Open logs** button. Each run
writes `logs/mapper-PID-STARTTIME.log` and `logs/overlay-PID-STARTTIME.log`.
Initialization success still requires checking the game for visible output.
Closing the launcher leaves the mapper/overlay running. **Insert** toggles the
panel; **End** requests cooperative stop. Stopped code remains resident until
normal game exit; restart CS2 before loading another copy or a rebuilt DLL.

Startup allows up to **60 seconds**, with a slow-start notice after 15 seconds.
A timeout reports **pending**, rather than failure: native startup can still
finish afterward. An unavailable completion result reports **unconfirmed**.
Only a completed bootstrap with native status/exit 3 and `OverlayStart` result 0
reports initialization success. Remote allocations remain resident on every
post-dispatch outcome; the launcher never terminates the bootstrap or unloads
its image. Cooperative stop retains its separate 15-second wait.

The October 18 user-run trace explains the previous false failure: catalog
discovery took about 16 seconds, exceeding the old 15-second mapper wait;
hooks installed at `15:12:45.061Z` and the real D3D11 callback followed at
`15:12:45.068Z`. See `logs/mapper-21888-134368096359813585.log` and its matching
overlay log. The correction is verified with deterministic delayed-completion,
timeout/failure classification and real local-event checks; no new injection
is performed for this fix.

`logs/loader-PID-STARTTIME.state` prevents repeat loads made through this launcher,
including uncertain/failed mapper runs. Process start time distinguishes reused
PIDs. Pre-dispatch failures roll back the reservation and allow retry. Do not
delete a state file to force reloading a running process. Previous manual CLI
loads are not tracked by the launcher; restart normally before switching from
manual loading. Keep the launcher, mapper and overlay together in the build
output directory. No game files, protection settings or server configuration
are changed.

`loader_checks` verifies Windows argument parsing and reservation rollback,
concurrent rejection and persistence across launcher processes without loading
CS2. Run it with the existing CTest command above.

## Launch and inject

All runs must remain at the disconnected main menu or an authorized offline/private session. Environment verification is an operator responsibility. The loader has no session/name/protection-module gates. Do not connect to public multiplayer. No dedicated server was configured. The user verified ESP in built-in offline Practice with bots.

The test launch used only presentation options, with no security settings or protection changes:

```powershell
Start-Process -FilePath 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-applaunch 730 -windowed -w 1280 -h 720 -novid'
```

After confirming the disconnected main menu, inspect the PID, then use that numeric PID explicitly:

```powershell
Get-Process cs2 | Select-Object Id,MainWindowTitle
New-Item -ItemType Directory -Path '.\logs' -Force | Out-Null
.\build\Release\mapper.exe --pid 9944 --dll 'C:\Users\dawid\Documents\ChatGPT\cs2\build\Release\cs2_overlay.dll' --log 'C:\Users\dawid\Documents\ChatGPT\cs2\logs\overlay-final.log'
```

Replace 9944 with the current PID; the number above identifies the final verification session only. Inspect the supplied log for verified target methods, the actual swap chain/device, and rendered-frame evidence. A mapper success code means initialization returned successfully; it does not by itself prove a visible frame. Map once per process. Restart normally before loading a rebuilt DLL; resident callbacks keep using the code that was originally mapped.

The mapper does not use LoadLibrary injection or call any LoadLibrary variant. It relocates PE sections, resolves imports and forwarders from the target modules' actual export tables, applies section permissions, registers x64 unwind metadata for the image and bootstrap, calls the DLL's CRT entrypoint, then invokes `OverlayStart` outside the loader lock. It does not conceal the mapping or change the game's security settings. Static TLS, delay imports, CLR and CFG-enabled payloads are unsupported and rejected. Missing dependencies must already be loaded; the mapper does not load them remotely. This is a restricted manual mapper, not a complete implementation of the Windows loader. No exceptions may escape the startup functions; the bootstrap does not catch them.

## Overlay and shutdown

The overlay displays player ESP and the selected public or diagnostic menu. Insert toggles the menu; End requests cooperative stop when the game window is foreground. No game-window procedure is replaced. Rendering and ImGui lifecycle work occur on the verified Present thread; resource ownership is synchronized with ResizeBuffers. The DLL supplements the ImGui DX11 backend's state backup, releases its back-buffer references before resizing and invalidates device-dependent state when necessary.

The exact stop command uses the base printed by the successful mapping operation:

```powershell
.\build\Release\mapper.exe --stop --pid 9944 --base 0x11e16000000 --dll 'C:\Users\dawid\Documents\ChatGPT\cs2\build\Release\cs2_overlay.dll'
```

Replace the PID/base with the values from your own mapping and use the same unchanged DLL file. Stop validates the remote image headers against that file. Stop is a request, completed by a subsequent render callback. Export/CLI result 1 means the stop was pending when checked; result 0 means already stopped. Consult `STOPPED_RESIDENT` in the DLL log for completed cleanup. If Present is no longer running, cleanup remains pending. Hooks/trampolines, mapped code, dynamic TLS, logger and unwind registration remain resident until normal process exit because no complete callback shutdown barrier has been verified. **Stopped and resident is not physically unloaded.** Do not free the mapping, remove hooks, force thread termination, or inject a second copy into that process. Restart normally before another mapping.

Diagnostics are written by the DLL only to the supplied path under the compiled workspace root. ImGui automatic INI and log output are disabled.

## Historical overlay-skeleton results

Verified October 13, 2026; DLL timestamps below are UTC (Warsaw is UTC+2 on this date).

* Full x64 Release build succeeded with MSVC 19.42 and SDK 10.0.22621.0. Final compilation produced no project warnings. CMake emits an upstream MinHook minimum-version deprecation notice during configure. DLL inspection verified zero static TLS and delay-import directories, AMD64 architecture, an exception directory and both exports. No separate test application was built or run.
* First run: PID 5980, user-confirmed disconnected main menu. Manual mapping at `0x1c5569b0000` returned bootstrap status 3, thread exit 3, image unwind registration 1 and OverlayStart 0. The DLL's real swap-chain Present/ResizeBuffers vtable entries matched its verified DXGI probe methods. Observed DXGI RVAs were `0x19530` and `0x37180` in this Windows build; these are diagnostics, never hardcoded hook offsets.
* Actual adapter: NVIDIA GeForce RTX 3060 Laptop GPU, vendor `0x10de`, device `0x2520`, feature level `0xb100` (11.1). First draw submitted 588 vertices and 894 indices at 1920x1080; original Present returned `S_OK`. The user confirmed visible overlay output.
* Insert visibility toggles were logged. User changed resolution to 1366x768; CS2 replaced its swap chain on the same render thread. The DLL released and rebuilt renderer resources, rendered again and observed `S_OK`. User confirmed the overlay remained correct. **No actual ResizeBuffers callback was observed in these runs**: its path is implemented and its target verified, while tested resolution changes exercised swap-chain replacement. Device removal and render-thread migration were not runtime tested. Graphics-state preservation was implemented and reviewed; no D3D debug-layer state comparison was run.
* End completed cooperative cleanup at `13:20:50.823Z`, logging `STOPPED_RESIDENT`. User confirmed the overlay vanished. A CLI stop query subsequently returned 0. The user exited CS2 normally before final verification; there was no physical unload or forced process/thread termination.
* Final mapper includes its own bootstrap unwind table. It was rebuilt cleanly and mapped successfully in the freshly launched PID 9944 at `0x11e16000000`; status/exit 3, image unwind registration 1, OverlayStart 0. The unchanged DLL again verified the real methods/device, drew at 1280x720 and then 1920x1080 after a same-thread swap-chain replacement, with `S_OK` after both. CLI stop returned 1 while cleanup was pending; the subsequent log confirmed `STOPPED_RESIDENT`. CS2 remained responsive. The final process is left at its menu with stopped code resident.

Logs: `logs/overlay.log` and `logs/overlay-final.log`. These are written by the DLL; the mapper only prints its bootstrap/stop result to the invoking console.

Historical skeleton SHA-256 hashes (superseded by the ESP build):

* `cs2_overlay.dll`: `00A479502B231C564A5856BFA1B21270D0112438247D69670147181B953A17E6`.
* `mapper.exe`: `66D4FEBCAC30C7EBC21A9B3E79522623D967A5BD0AF6941ABFF050CD0F5A67CC`.

## References and fallback

[DXGI ResizeBuffers](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers) requires outstanding back-buffer references to be released. [RtlAddFunctionTable](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable) provides x64 unwind registration.

Valve documents injection restrictions in [CS2 Trusted Mode](https://help.steampowered.com/en/faqs/view/09A0-4879-4353-EF95). If this client rejects loading, stop without bypassing those restrictions. An external overlay using supported Windows graphics/window APIs is the alternative; it would require changing the requested scope. A supported private-server plugin is appropriate for server-side features, but does not provide a client Present hook.
