# Menu input and cursor contract

Inspected October 20, 2026. This is client-side x64 Windows/D3D11 menu work.
No client or server was launched for this inspection. Installed files were read
without modification. Export RVAs below are evidence, not hardcoded hook targets;
the adapter resolves named exports from the loaded SDL module.

## Installed dependencies

Game root:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.

| File under `bin\win64` | Identity |
| --- | --- |
| `SDL3.dll` | AMD64, file version `3,5,0,0`, 2,891,928 bytes; PE timestamp July 20, 2026; linker 14.44 |
| `SDL3.dll` SHA256 | `e7c0c22aaadc2836242ba001aa488773eceb4609005a7f02464206ca59a86481` |
| `inputsystem.dll` SHA256 | `bed6c10a27f1629054f2e4552a7bf6f9325dc9e08d1295ca367fc14141f1e07d` |
| `tier0.dll` SHA256 | `990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e` |

The repository already uses static MSVC runtime linkage and its existing
MinHook dependency. The SDL integration must use the game's loaded SDL module;
it must not load a second SDL instance or require installed game-file edits.

## Actual native event consumption

`inputsystem.dll` does not import SDL functions directly through its normal or
delay import directory. It imports `tier0!Plat_LoadSDL` and
`tier0!Plat_RequireSDL`. The latter returns the SDL function table at tier0 RVA
`0x3D6DC0`. `Plat_LoadSDL` supplies its corresponding names array at
`0x39BDC0`. In that table:

| Name | Table byte offset |
| --- | --- |
| `SDL_PumpEvents` | `0x1A8` |
| `SDL_PeepEvents` | `0x1B0` |
| `SDL_PollEvent` | `0x1B8` |
| `SDL_GetGlobalMouseState` | `0x1D8` |
| `SDL_SetWindowRelativeMouseMode` | `0x1F8` |
| `SDL_GetKeyboardState` | `0x250` |

At inputsystem RVA `0x5680`, the loop obtains the function at table `+0x1B0`
and calls it at `0x56A2` with a stack event buffer, count 1, action 2, minimum
type 0 and maximum `0xFFFF`. If no event is returned, `0x56AD` calls table
`+0x1A8`, then `0x5736` repeats the event retrieval. The event reaches native
handlers at `0x574C` and `0x5759`. Thus **hooking only SDL_PollEvent would miss
this installed client's primary event path**. Peep's GET action needs coverage.

These call windows were inspected using the installed MSVC `dumpbin /DISASM`
with virtual-address ranges. No native signature or vtable offset is used by
the menu adapter.

## Public SDL ABI

Declarations were checked against SDL's official documentation and
[SDL headers at commit 3b2bc951500ca6a1f428af77e036e732d9a8afe3](https://github.com/libsdl-org/SDL/tree/3b2bc951500ca6a1f428af77e036e732d9a8afe3/include/SDL3),
dated October 19, 2026. The installed library exports every function below.

| Function | Declaration, using opaque `SDL_Window` and fixed-width aliases | Export RVA |
| --- | --- | --- |
| `SDL_PumpEvents` | `void(void)` | `0x6A350` |
| `SDL_PeepEvents` | `int(SDL_Event*, int, SDL_EventAction, Uint32, Uint32)` | `0x6A2F0` |
| `SDL_PollEvent` | `bool(SDL_Event*)` | `0x6A310` |
| `SDL_GetKeyboardFocus` | `SDL_Window*(void)` | `0x68BD0` |
| `SDL_GetWindowProperties` | `Uint32(SDL_Window*)` | `0x696C0` |
| `SDL_GetPointerProperty` | `void*(Uint32, const char*, void*)` | `0x68E70` |
| `SDL_GetWindowRelativeMouseMode` | `bool(SDL_Window*)` | `0x696D0` |
| `SDL_SetWindowRelativeMouseMode` | `bool(SDL_Window*, bool)` | `0x6B410` |
| `SDL_GetWindowMouseGrab` | `bool(SDL_Window*)` | `0x69630` |
| `SDL_SetWindowMouseGrab` | `bool(SDL_Window*, bool)` | `0x6B3A0` |
| `SDL_CursorVisible` | `bool(void)` | `0x678F0` |
| `SDL_ShowCursor` | `bool(void)` | `0x6B4E0` |
| `SDL_HideCursor` | `bool(void)` | `0x69950` |
| `SDL_ResetKeyboard` | `void(void)` | `0x6A990` |
| `SDL_GetKeyboardState` | `const bool*(int*)` | `0x68BF0` |
| `SDL_GetGlobalMouseState` | `Uint32(float*, float*)` | `0x68860` |
| `SDL_WarpMouseInWindow` | `void(SDL_Window*, float, float)` | `0x6BAA0` |
| `SDL_WarpMouseGlobal` | `bool(float, float)` | `0x6BA90` |

For Windows x64 these are ordinary C ABI calls; SDL3's `bool` must not be
substituted with SDL2's integer return types or integer-coordinate signatures.

[Relative mouse mode](https://wiki.libsdl.org/SDL3/SDL_SetWindowRelativeMouseMode)
hides and constrains the cursor while focused. Disabling it flushes pending
motion. Window/cursor mutations belong on SDL's main thread, including
[mouse grab](https://wiki.libsdl.org/SDL3/SDL_SetWindowMouseGrab) and
[cursor visibility](https://wiki.libsdl.org/SDL3/SDL_CursorVisible).
The existing render thread should publish intent; the native event-pump owner
performs the transition. Match the SDL window's
[`SDL_PROP_WINDOW_WIN32_HWND_POINTER`](https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties)
property, whose literal is `SDL.window.win32.hwnd`, against the selected
swapchain HWND before changing it.

[SDL_ResetKeyboard](https://wiki.libsdl.org/SDL3/SDL_ResetKeyboard) clears its
keyboard state and produces key-up events. These releases and mouse-button
releases must reach the native client so actions held before menu opening do
not remain latched. The menu ignores already-held controls until release.

The official [event header](https://github.com/libsdl-org/SDL/blob/3b2bc951500ca6a1f428af77e036e732d9a8afe3/include/SDL3/SDL_events.h)
defines a 128-byte `SDL_Event` union, aligned to 8 on AMD64. Relevant offsets
are type 0, reserved 4, timestamp 8 and window ID 16. Wheel fields are device
20, float x 24, float y 28, direction 32, float mouse x/y 36/40 and integer
x/y 44/48. `SDL_EventAction` values are ADD 0, PEEK 1 and GET 2. Keyboard
down/up are `0x300/0x301`; text editing/input `0x302/0x303`; mouse
motion/down/up/wheel are `0x400/0x401/0x402/0x403`.

## Why window messages alone are insufficient

The official [Windows event backend](https://github.com/libsdl-org/SDL/blob/3b2bc951500ca6a1f428af77e036e732d9a8afe3/src/video/windows/SDL_windowsevents.c)
reads raw packets using `GetRawInputBuffer` in `WIN_PollRawInput`; its WM_INPUT
dispatch implementation is disabled. It also reconciles some mouse-button
state asynchronously. The installed SDL imports `USER32!GetRawInputBuffer` at
IAT RVA `0x1E8720`. This supports covering SDL event output rather than relying
solely on swallowing WM_INPUT or legacy window messages.

[SDL_SetEventFilter](https://wiki.libsdl.org/SDL3/SDL_SetEventFilter) also leaves
internal input state updated when it drops an event. Output filtering therefore
needs synchronous getter coverage for native polling. Preserve unrelated window,
focus, quit and lifecycle events. GET filtering must continue past suppressed
events; ADD and PEEK must retain their queue semantics. Wheel forwarding must
occur once per consumed event, including flipped direction and fractional
deltas. Do not feed ImGui directly from arbitrary SDL threads; queue wheel
deltas for the render owner.

## Verification boundary

Static inspection establishes installed exports and the native event retrieval
path. It does not establish live cursor feel, visual cursor appearance,
in-game input exclusivity or teardown timing. Use standalone adapter checks for
transitions and event contracts, and reserve live observation for the authorized
disconnected/offline/private environment. Cooperative stop must restore state
on the owner thread and retain callbacks/code until normal process exit:
**stopped and resident is not physically unloaded**.

## Implemented adapter and standalone checks

`src/ui/menu_input.cpp` resolves 20 named SDL exports and queues 13 MinHook
callbacks. The SDL pump changes relative mode, mouse grab and cursor visibility
only on the selected HWND's owning thread, after matching SDL's HWND property.
Native mode/hide requests are deferred while captured and the latest requested
values are restored on hide, focus loss or cooperative stop. A resident window
procedure observes focus and destruction; it forwards ordinary messages rather
than claiming to suppress the buffered raw path.

The Peep/Poll output adapter suppresses key/text presses, mouse motion, mouse
button presses and wheel events while capture is effective. Releases, quit and
window/lifecycle events continue to the native client. Synchronous keyboard and
mouse getters mask captured input. `SDL_ResetKeyboard` also clears cached native
keyboard state after each owner pump, producing releases for held keys. Initial
mouse-button releases pass through SDL's existing window procedure. Wheel
values accumulate with float precision and drain once on the render thread;
horizontal sign follows ImGui's SDL backend and flipped directions are normalized.

The capture flag is armed only after cursor API calls succeed. HWND mismatch
or cursor API failure leaves game event delivery active. A foreign pump thread
does not call main-thread SDL video functions or restore another owner's state.
Stop disarms capture immediately; pending cursor restoration requires another
native owner pump. Hooks, trampolines and the forwarding window procedure remain
resident until normal process exit.

**Standalone verification, October 20, 2026:** `menu_input_checks` passes
**42 checks**, covering opening/closing, latest requested mode restoration,
focus loss/refocus, exact HWND matching, wrong owner threads, cursor API failure,
held state releases, getter masking, GET compaction/refill, queue-preserving PEEK,
ADD passthrough, non-consuming null Poll, lifecycle/quit forwarding, fractional
and flipped wheel values, recenter suppression and cooperative stop.

```powershell
.\build\Release\menu_input_checks.exe
.\build\Release\menu_input_checks.exe --installed-sdl
```

The optional `--installed-sdl` smoke passes **6 checks**. It loads the installed
SDL3 DLL into the standalone fixture process using its absolute path, initializes
MinHook, prepares and activates all 13 actual exported-function hooks, then
checks inactive PeepEvents/keyboard/pump trampoline forwarding. The keyboard
getter reports the expected 512 scancodes. It passes `nullptr` as the diagnostic
logger and uses SDL's uninitialized/no-video state: it does not call SDL_Init,
create an SDL window, launch CS2, inject into CS2 or start a server. This confirms
the installed export stubs are hookable and the adapter forwards through their
trampolines. It does not establish live in-game cursor behavior or exclusivity.
