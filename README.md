# CS2 silent aim, world visuals, movement assistance, player ESP and D3D11 overlay

x64 client overlay with basic silent aim, movement assistance, player ESP and an explicit manual mapper. Installed game files are not modified. Existing ESP appearance work was preserved before silent aim in commit `2e8e1c7`.

For the next CS2 update, start with the [update guide](docs/CS2_UPDATE_GUIDE.md)
and [RE address index](docs/RE_ADDRESS_INDEX.md). One command checks all ten
components independently and saves build identities, source hashes and detailed
reports under `logs/compatibility/`:

```powershell
python tools/check_compatibility.py
```

## World visuals

The **World** and **Lighting** tabs provide independent controls, disabled by
default. Settings last for the current run and continue applying with the menu
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

## Basic silent aim

The **Aim** tab has an **Enable silent aim** checkbox, a **Hitbox** selector
(**Head**, **Chest**, **Stomach**, **Pelvis**) and an angular **Aim FOV** slider
(0.1–30 degrees). It starts **disabled**, with Chest and 5 degrees selected.
**F10** toggles it while the client is foreground. Fire normally; it never
presses attack. Menu visibility and ESP settings do not control aim acquisition.

The command callback selects the alive, non-dormant enemy whose selected point
is closest to the shot's native view direction within the FOV. Each selected
body region resolves its animated bone by name on the current model: `head_0`,
`spine_3`, `spine_2`, `pelvis`. These are bone landmarks, not authored hitbox
volume centers. Missing names, unsupported skeletons or unavailable cached poses
skip that player. There are no fixed bone indices or collision-hull fallbacks.
The reader uses the latest cached world pose and copied live eye offset; it
does not force animation refresh, mirror native eye interpolation or add lag
compensation. It retains no engine entity/model pointers across callbacks.

A separate verified input-history hook calls the original builder first, then
updates existing history angle messages for primary-fire input. A native attack
index handles short taps; held primary fire also updates the history consumed
by CS2's retained/interpolated continuation path. Native attack indices, button
states, timestamps, base/camera angles, movement and movement CRC stay unchanged.
No history messages or attacks are synthesized, and secondary fire is outside
this implementation. Previously retained native history can affect boundary
shots when enabling, disabling, stopping or changing targets; it is not
rewritten retroactively.

No hitchance, damage/minimum-damage, autowall, penetration, visibility tracing,
recoil compensation, spread correction, automatic shooting or rage systems are
included. An enemy behind a wall can be selected; walls, spread, recoil and the
server's timing/rules can prevent a hit.

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
in the same offline setup; the new log is needed to isolate the observed
failure. At this checkpoint no runtime root cause or hit-registration repair
has been established.

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
| F6 | Include/exclude teammates |
| F7 | Toggle armor text |
| F8 | Toggle auto bunnyhop |
| F9 | Toggle aggressive autostrafe |
| F10 | Toggle silent aim |
| Insert | Toggle the selected menu; ESP and acquisition keep running |
| End | Cooperative stop |

Keys are polled while the game window is foreground. They are not consumed, so
the game can also respond to overlapping bindings. The public menu additionally
supports left-click controls with the client's available cursor. Settings are
shared between the menu and hotkeys, and last for the current run.

## Movement assistance

Both features start enabled. Hold **Space** to activate them while playing the
authorized built-in offline bot match. **WASD**, including diagonal combinations,
selects camera-relative travel direction and rebuilds speed after slowing down.
When opposing keys overlap, the most recently observed press wins on that axis;
simultaneous first presses cancel. Diagonals remain available throughout a turn.
Releasing all movement keys stops assisted steering; no autostop is implemented.
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
Autostrafe uses full normalized input and eight timed direction updates per air
command. Each fragment spends only its share of that command's acceleration,
then replans against its predicted velocity. This avoids leaving one wish
direction capped for the rest of the tick. Near the requested heading it selects
a maximum-gain impulse; sharper corrections maximize direction capture and can
sacrifice speed. Intent behind the current velocity applies the available impulse
directly toward that requested axis, instead of taking a wide half-turn orbit.
There is no positive-gain requirement during corrections, retained entry-speed
ceiling or fixed 15-degree heading envelope. Reachable intermediate headings use
a bounded projection solve. Heading excursions scale with current physics/speed.
It changes normalized movement and direction buttons without rotating the camera.
Releasing WASD restores ordinary axes before integration and leaves coasting alone.
The first ground slice restores physical WASD axes after assisted air movement;
ground movement otherwise stays under ordinary game input control.

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
coasting with no autostop. For revision 8, also test W→S at low and running speeds,
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

On October 15, 2026, `game/csgo/steam.inf` reports patch **1.41.9.1**,
client/server **2000932**, source revision **11131657**, version date October 14,
2026. Steam appmanifest build ID: **25947352**. Steam's UpToDateCheck API reports
client version 2000932 as current. Executable SHA-256:
`8614097AAA2761B0A7157C0045861713E769FB1E8235F50FC68F411294DD60CF`;
client DLL SHA-256:
`659940C1DB362ED981D1DF10637CEC8C2A3D99409B77FC3B73E435E4649E36B0`.
Installation hashes and the API response are recorded in
`logs/compat-installation-20261015.json` and `logs/compat-steam-version-20261015.json`.

Both D3D11 and Vulkan renderers exist. The launched client (PID 5980) loaded `rendersystemdx11.dll`, `d3d11.dll`, `dxgi.dll`, and the game-local `D3DCOMPILER_47.dll`. This establishes D3D11 for that run. Live graphics objects are verified in DLL callbacks rather than inferred from installed DLL names. Loaded MSVC runtime modules included VCRUNTIME140 version 14.51.36247.0 and Windows UCRT; this does not identify the compiler used to build all game modules.

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

The default build uses a minimal dark public menu with **Visuals** and
**Movement** tabs. Visuals includes ESP, boxes, names, health bars and HP text,
distance labels, teammates, and armor text; Movement includes the two independent
movement toggles. Only implemented features appear. ESP has
unlimited range; there is no range slider or +/- range hotkey. Insert shows or
hides the menu; End cooperatively stops the overlay. Player graphics render
behind the menu. The menu can be dragged from its top strip and stays within
the viewport after a resize.

The public menu reads the installed Windows Segoe UI font for its typography,
with an ImGui default-font fallback. No system font file is copied or bundled.

The public menu polls the Windows cursor and left mouse button on the render
thread and scales client coordinates to the back buffer. Use the cursor at the
disconnected main menu or in an offline pause menu. Gameplay may lock/recenter
the cursor. Mouse clicks also reach the game, so choose a clear area of the
underlying menu; no engine input API or window-procedure interception is added.
Held clicks when showing/refocusing the menu are ignored until released.
Hotkeys remain available while the menu is hidden. Telemetry still writes to
workspace logs but does not appear in the public menu.

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
