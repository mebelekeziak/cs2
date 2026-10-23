# Native console cleanup and project-message filter

## Current recertification — October 23, 2026

Installed AMD64 engine2 SHA-256
`e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29`,
timestamp `0x6ADA3418`, image size `0xA15000`; client build 26144732 / 2000935.

All nine native checked windows were rediscovered and decoded in scratch
`logs/research/26144732/core/discovery.json`. Primary named
CEngineConsoleLoggingListener vtable `0x61ACA0` independently resolves listener
`0x20AC20` (previous `0x2094B0`). Clear `0x1B8270` (was `0x1B7D70`) is
independently selected by its registration LEA `0x1B568E` and exact `clear`
name reference `0x1B56C2`. Queue `0x73BB0` (was `0x73720`) is called by the
decoded native caller `0x75DE7`, which supplies command buffer `0x979820`
(was `0x8DBF20`). Literal text still reaches named CCommandBuffer::AddText;
recursive lock `+0x43E2*12`, owner/recursion fields, 0x86D8 slot stride and
unlock atomics are unchanged. Argument count/vector remain `+0x438`/`+0x440`.

RIP-relative code fingerprints, header identity, function/caller RVAs and exact
engine hash changed together; native owner, deferred execution, listener
forwarding and stop behavior did not change. `inspect_console_control.py`
passes; incorrect buffer, queue, listener bytes and timestamp headers reject
(`core/negative.json`). No process or console was opened. Build/test results
are separate in `docs/CS2_UPDATE_26144732.md`; subsequent sections are historical.

Client feature, investigated October 18, 2026. No client was launched or opened,
no server was contacted, and installed files were read without modification.
Installation: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
`bin/win64/engine2.dll` is native AMD64 PE32+, SHA-256
`6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21`,
timestamp `0x6ACFCD53`, image size `0x971000`. This extends the existing D3D11
client overlay; it introduces no graphics dependency or plugin framework.

## Native contracts

RTTI `.?AVCEngineConsoleLoggingListener@@` identifies primary vtable `0x586708`.
Slot zero points at `0x2094B0`. The listener consumes RDX as logging context
and R8 as the already-formatted message. Its original path forwards that text
in RDX to the console object's virtual method at `0x209536..0x20954C`, with
the context's four color bytes at `+0xC` in R8. The detour forwards exactly the
same three arguments unless project-only filtering is enabled and the text
does not begin with `AIM_`. This is the same prefix emitted by the existing
project console sink. Other native logging listeners remain untouched. Turning
the filter off immediately forwards native records again; no channel flags,
verbosity, cvars, or installed configuration are changed.

The native `clear` registration loads its callback at `0x1B518E`, resolving to
`0x1B7D70`, and its name at `0x1B51C2`, resolving to string `clear` at `0x553778`.
The member-command callback uses RCX=game UI service, RDX=command context
(unused here), R8=command arguments. It tests the native availability predicate,
clears the text-console view through service slot 32, then clears Panorama
through the UI interface's `+0xC8` method. Replay must happen after the complete
callback, because replaying between those two clears would lose the new lines.
The detour preserves all arguments and calls the original first. It only replays
for a project-requested clear; ordinary manually entered `clear` retains its
normal empty-display behavior. Native count at argument-object `+0x438` bounds
the pointer array at `+0x440`; the original callback consumes its first pointer
to distinguish `clearall`. A fixed second argument, `cs2_project_logs`, is ignored
by the original handler and identifies our queued clear for replay. An intervening
manual clear cannot consume the project's pending replay.

Queue helper `0x73720` has native ABI `void(buffer*, int slot, const char*)`.
It copies RCX to RSI, EDX to EBX, and R8 to RDI. An independent native caller at
`0x75957..0x7596A` supplies command-buffer object `0x8DBF20` in RCX and calls
this helper. For slot zero, the helper enters its recursive lock at
`buffer + 0x32E98`, tracking thread ID/recursion before calling the named tier0
import `?AddText@CCommandBuffer@@QEAA_NPEBDHH_NN_K@Z` at `0x73850`. It supplies
the buffer's `+0x588` command queue and the copied string. Native atomic release
and waiter wake-up follow at `0x73856..0x73893`. UI requests submit only the
fixed literal `clear cs2_project_logs\n`, on slot zero. No user text is executed. Actual clear
and replay happen when the engine executes its native command, rather than
calling UI clear methods from Present or an input hook.

The frozen verifier checks the module hash, instruction windows, RTTI/vtable,
decoded caller object/callee, named AddText import and clear name/callback.
Runtime preparation checks module identity, image mappings, and those instruction
windows before queueing both hooks. Queue submission rechecks the loaded module
and unhooked queue bytes under a temporary loader reference. Detailed original
disassembly is in `logs/console-research/native-paths.txt`.

## History, lifecycle and verification

The existing console sink owns a fixed 256-line ring (each at most 1535 bytes
including newline). Only successfully emitted project lines enter it. Replay
uses the same verified plain/color sink and literal `%s` format, preserving
chronology and color without inserting duplicate history or changing file logs.
The sink's exclusive lock serializes writes, replay and stop. The controls use
an independent lock to drain queue submissions and clear/replay callbacks at stop;
the listener only reads atomics. Pending requests coalesce. Stop disables the
filter and cancels replay; queued native commands can still execute normally.
Hooks, trampolines and DLL remain resident until normal process exit.

`console_control_checks` substitutes the three native boundaries and exercises
filter forwarding, clear/replay order, literal command arguments, deferred clear,
coalescing, ring wrap, manual clear and stop. Existing console tests cover colors,
formatting, source invalidation and draining shutdown. Real menu interaction tests
click the new controls. These checks do not establish live console appearance.

```powershell
python tools/inspect_console_control.py
ctest --test-dir build -C Release --output-on-failure
```
