# Native aim telemetry console sink

## Current recertification — October 21, 2026

Installed Windows AMD64 client: patch **1.41.9.1**, client/server **2000934**,
Steam build **26087496**, Source revision **11150591**, version date
October 20, 2026, 11:41:22. Installation and executable paths remain those
listed below. New client SHA-256 is
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, image size `0x299D000`. The tier0 identity below
is unchanged.

Scratch investigation in `logs/research/26087496/events_console/` searched the
new image for all three exact native caller anchors and rediscovered their
original sites. The Msg and ConColorMsg import slots, format-string targets,
argument registers, named tier0 exports and all eleven callee instruction
windows pass unchanged. `discover.py` records the anchor matches and decoded
instructions in `discovery.json` (Capstone 5.0.9); `probe.py` runs the existing
full verifier with candidate identities only in memory, before production pins
were changed. `probe-result.txt` retains the successful evidence.

Only the verifier's exact client hash changes; **no console source constant,
export RVA, fingerprint, ABI or ownership contract changes**. This rechecks
the caller/callee contracts described below, including synchronous formatting,
four-byte copied RGBA and temporary module-reference lifetime. Unfamiliar hashes
still reject. The older client identity below is historical evidence.

`python tools/inspect_aim_console.py --output
logs/research/26087496/events_console/console-final.json` passes. Temporary
source mutations to Msg RVA, tier0 timestamp and Msg prologue all reject;
a deliberately unfamiliar client hash also rejects. Exact diagnostics are in
`negative.json`. No game/server process was started or opened, installed files
were not changed, and live console visibility remains unobserved. Repository
build/CTest results are reported with the combined update, separately from
these static checks.

## Original October 18 investigation

Static verification date: October 18, 2026. Client feature; no dedicated server
or plugin framework is involved. No game was started or installed file changed.

The installed files are below
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`:

| File | SHA-256 | PE timestamp | Image size |
| --- | --- | --- | --- |
| `bin/win64/tier0.dll` | `990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e` | `0x6ACFCBF4` | `0x401000` |
| `csgo/bin/win64/client.dll` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` | `0x6AD01314` | `0x299D000` |

Both images are native Windows AMD64 PE32+. The sink uses the existing loaded
module's named `Msg` export; it adds no import dependency or hook. The renderer
remains the existing D3D11 integration and does not determine the logging ABI.

## Installed ABI evidence

`tier0!Msg` exports RVA `0x7EE60`. Its first four instructions save RCX, RDX,
R8 and R9 into their AMD64 argument home slots. After `push rdi; sub rsp,0x840`,
`0x7EEA5` reloads the original RCX from `[rsp+0x850]` as the format string;
`0x7EEB8` obtains the variadic argument area from `[rsp+0x858]`, beginning with
the saved original RDX. `0x7EECA–0x7EEEB` supplies this argument list, format,
stack buffer and `0x800` capacity to the native formatting routine. The formatted
line is passed to the native logger at `0x7EF11–0x7EF32`, with numeric severity
2. No return value is consumed by this project.

The installed client's import directory resolves `Msg` from `tier0.dll` into
IAT slot RVA `0x1AAD998`. At `0x153C4F`, `mov rdx,rax` supplies a string. The
following `lea rcx,[rip+...]` at `0x153C52` supplies the literal `T     %s\n`
at `0x1AC5CA0`; `0x153C59` calls that IAT slot. The second independent caller
at `0x153C72/0x153C75/0x153C7C` passes the format `UWT1  %s\n`.
These callers and the callee establish the native variadic printf interface;
no guessed C++ virtual method or signature is used.

### Native color output

The same installed tier0 exports `?ConColorMsg@@YAXAEBVColor@@PEBDZZ` at
RVA `0x7F710`. Its complete 25-byte prologue saves RDX, R8 and R9 into argument
home slots. `0x7F737` retains RDX as the format and `0x7F73A` retains RCX as the
color pointer. `0x7F756` points the varargs list at the original R8 home slot.
At `0x7F7A6`, a DWORD load copies the color; `0x7F7DB–0x7F7E9` attaches those
four bytes to logging metadata before dispatch at `0x7F80F`.

The installed client's IAT slot is `0x1AAD5F8`. At `0x1C6149`, it constructs
color value `0xFFD5EB34` in four stack bytes, passes their address in RCX at
`0x1C6151`, the format in RDX at `0x1C6156`, and calls the imported export at
`0x1C615D`. The frozen verifier checks each producer/consumer window and the
decoded import target. Research disassembly is retained in
`logs/shot-reasons-color-research.txt`.

Valve's published [Color declaration](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/Color.h)
describes four bytes in RGBA order; its [console declaration](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/tier0/dbg.h)
documents the color-reference/format/varargs interface. Those older Source SDK
declarations provide supporting context; the installed CS2 export, native
caller and callee above establish the current boundary. No SDK implementation
is copied or linked. This project uses an independent four-byte value struct,
with a compile-time size assertion.

## Runtime ownership and bounds

`AimConsoleInitialize` must run outside DllMain. It checks the loaded tier0
machine type, timestamp, image size, named-export RVA, executable image mapping,
and the complete 28-byte variadic prologue. Every write acquires a temporary
loader reference and repeats those checks, including the cached module/export
identity, before calling. The temporary reference is released afterward; this
feature does not permanently pin or unload the engine module.

The optional color export receives its own executable-span, named-export RVA
and full-prologue checks, repeated before each colored call while the same
temporary module reference is held. If it is absent, changed or invalidated,
the verified plain `Msg` path still prints. A colored record uses exactly one
call with `color, "%s", line`, so no partial-line color state can leak into
other messages. Hits are green, misses red, unconfirmed results/warnings amber,
status cyan, and verbose diagnostics gray. Classification reads only the fixed
record prefix and outcome position; player names cannot change the palette.
The workspace file writer receives the original text, with no escape codes.

The sink only runs from telemetry consumers. Input hooks keep enqueueing bounded
records; they perform no console/file I/O. Each emitted record contains at most
1,534 input bytes followed by one newline and a terminator. Embedded CR/LF and
other control bytes except tabs become spaces. The native format is always the
fixed literal `%s`, so percent sequences inside diagnostics are text.

`AimConsoleStop` closes the sink under the same lock used by writers and waits
for existing calls to return. The native logger itself is not hooked. This sink
has no unload barrier for other overlay hooks: stopped and resident is not
physically unloaded. Console visibility remains subject to the game's console
and native logging policy. No live console display observation is claimed.
The existing workspace file log remains the persistent diagnostic record when
the native sink is unavailable.

Recheck without launching CS2:

```powershell
python tools/inspect_aim_console.py --output logs/hitchance/aim-console-static.json
```

`aim_console_checks` substitutes only the external native target and exercises
printf formatting, literal percent text, control bytes, bounded records,
per-write source invalidation, failed initialization, and cooperative stop
during an in-progress native call. It also checks each native color, opacity,
colored printf/control handling, optional-color invalidation/plain fallback,
and shutdown while a colored call executes. Installed binary/source contracts are
checked separately by `inspect_aim_console.py`.
