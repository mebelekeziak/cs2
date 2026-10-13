# CS2 movement assistance, player ESP and D3D11 overlay

x64 client overlay with movement assistance, player ESP and an explicit manual mapper. Installed game files are not modified. Existing workspace work was preserved before ESP in commit `db1d345`, and before movement in commit `ec3c181`.


## Player ESP

Names, outlined player boxes, distance labels, health bars/text, optional armor
and team colors are implemented. ESP has unlimited range; defaults show both
teams and armor text is off. Boxes project the eight corners of the
current collision bounds using copied scene position/angles/scale. They enclose
the collision hull, not animated model bones. Team colors: CT blue, T amber.

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
Autostrafe first turns toward the requested heading while retaining modeled
horizontal speed, then uses maximum-gain air impulses near that heading to build
speed above the air projection cap. There is no retained entry-speed ceiling.
Gain-stage excursions remain within 15 degrees of intent in the supplied model.
No input leaves ordinary release behavior alone. It changes
normalized movement and direction buttons without rotating the camera.
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
movement-data finalization. It adds explicit time-zero analog and separate jump
transitions through verified game-owned protobuf allocation/reuse. Analog deltas
start from historical effective input, matching the engine's integration order;
final base values alone are ingested after that integration. Existing camera
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
checks and same-command ground-rewind regressions. The user still dislikes
autostrafe and has deferred further steering work.

Restart CS2 normally if an older DLL is still resident. Launch the offline bot
match, load using **Load Overlay.cmd**, hide the menu with Insert and hold Space
while moving. Test with F9 off to isolate bunnyhop, then enable F9 and steer with
WASD, including diagonals and low-speed recovery. Release WASD to check normal
coasting with no autostop. The launcher writes movement telemetry to the existing
`logs/overlay-PID-STARTTIME.log` alongside ESP diagnostics:

* `MOVE_BUILD_VERIFIED` checks input identity/layout; `MOVE_OWNER` records an
  observed command callback. Build verification alone does not prove a jump.
* `MOVE_CMD` samples inputs, synthesized buttons/analogs, velocity and current
  physics roughly every two seconds while assistance runs.
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
  prediction-only ground changes preserve the selected input.
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

Offsets were generated from this installed client and independently checked
against its machine code and live memory. The DLL checks client PE architecture,
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

On October 13, 2026, the installed executable and engine/client/render-system modules were AMD64 PE32+. `game/csgo/steam.inf` reported patch 1.41.9.0, client/server 2000930, source revision 11106093, version date October 8, 2026. Steam appmanifest build ID: 25815307. Executable SHA-256: `BB25389A10B381B0B50C984087F34E6B30F2919108751A0FDD97CF0DFC142C7B`.

Both D3D11 and Vulkan renderers exist. The launched client (PID 5980) loaded `rendersystemdx11.dll`, `d3d11.dll`, `dxgi.dll`, and the game-local `D3DCOMPILER_47.dll`. This establishes D3D11 for that run. Live graphics objects are verified in DLL callbacks rather than inferred from installed DLL names. Loaded MSVC runtime modules included VCRUNTIME140 version 14.51.36247.0 and Windows UCRT; this does not identify the compiler used to build all game modules.

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
