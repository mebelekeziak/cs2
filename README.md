# CS2 player ESP and D3D11 overlay

x64 client overlay with player ESP and an explicit manual mapper. Installed game files are not modified. Existing workspace work was preserved before ESP in commit `db1d345`.


## Player ESP

Names, outlined player boxes, distance labels, a maximum distance filter, health
bars/text, optional armor and team colors are implemented. Defaults show both
teams within 150m; armor text is off. Boxes project the eight corners of the
current collision bounds using copied scene position/angles/scale. They enclose
the collision hull, not animated model bones. Team colors: CT blue, T amber.

| Key | Action |
| --- | --- |
| F1 | Toggle ESP |
| F2 | Toggle usernames |
| F3 | Toggle boxes |
| F4 | Toggle distance labels (range filtering remains active) |
| F5 | Toggle health bars and HP text |
| F6 | Include/exclude teammates |
| F7 | Toggle armor text |
| - / + | Change maximum distance by 25m, clamped to 10–1000m; keypad works too |
| Insert | Toggle the diagnostics panel; ESP and acquisition keep running |
| End | Cooperative stop |

Keys are polled while the game window is foreground. They are not consumed, so
the game can also respond to any overlapping bindings. There is no mouse menu,
window-procedure replacement or game input interception. Settings are per run.

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
or spectating), distance labels and range filtering are temporarily unavailable;
other valid player geometry can still render. Dormant, dead, invalid and local
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

The overlay displays player ESP and diagnostics. Insert toggles the panel; End requests cooperative stop when the game window is foreground. No game-window procedure is replaced. Rendering and ImGui lifecycle work occur on the verified Present thread; resource ownership is synchronized with ResizeBuffers. The DLL supplements the ImGui DX11 backend's state backup, releases its back-buffer references before resizing and invalidates device-dependent state when necessary.

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
