# Current native shooting origin

## October 23, 2026 recertification - client 2000935

Installed Windows AMD64 client `1.41.9.2` / `2000935`, Source revision
`11162119`, Steam build `26144732`, version date October 22, 2026 14:13:16.
Installation remains `C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game`;
executable `bin/win64/cs2.exe`, module `csgo/bin/win64/client.dll`.
Client SHA-256 `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`.

Research is preserved in `logs/research/26144732/aim_sources/`: `schema.json`
records independently located named schema templates, `relocations.json` and
`address_mapping.json` record candidate/call-target rediscovery, and
`disassembly.txt` contains decoded current instruction windows (Capstone 5.0.7).
Preserved prior-file comparison uses only windows matching the former frozen
window hashes; the old negative-test sample is not accepted as a valid complete
baseline image. New named schema/RTTI/caller checks and full module pins remain
mandatory. No uniform relocation delta is assumed.

The October 22 current-native-origin feature is re-established against this
new image. `CCSPlayer_WeaponServices` primary table moves
`0x1C14C78 -> 0x1C16C08`; byte slot `0x100` still resolves the complete
31-byte `AimVector*(services*, AimVector*)` getter, now
`0xA7FD00 -> 0xA80B30`. It loads owner pawn `+0x38`, dispatches pawn slot
`0x568`, and returns the caller's output pointer. `C_CSPlayerPawn` primary table
moves `0x1C8A138 -> 0x1C8C068`; current getter
`0x93C660 -> 0x93D490`, base eye `0x15FAB0 -> 0x15FA90`, view-offset slot
`0x5A0` target `0x160770 -> 0x160750`, interpolator
`0x160580 -> 0x160560`, scale-enabled transform `0x16A3B0`, absolute origin
`0x219A60`.

The named `C_BasePlayerPawn` schema moves to registration `0x21F67E8`, field
array `0x21F6450`, still 28 fields. Weapon services `+0x12F0`, camera services
`+0x1328`, observer services `+0x1308` agree with independent native getters.
History producer `0x9138D0 -> 0x914700` dispatches services slot `0x100` at
`0x9147C6`; named historical interpolator `0x903A30 -> 0x904860` independently
falls back through that slot at `0x904AB4`. Bracketing helper moves
`0x903EE0 -> 0x904D10`. Both callers retain the prior ABI and ownership.

All eight native windows are refrozen. The interpolation window now ends at
the function's return at `0x160724` (453 bytes), excluding following padding
and the old window's incomplete bytes of the next function; the following view-offset getter is frozen
separately. Full module hash and decoded caller/schema/slot checks remain exact.
The getter has no explicit time input and still reports current native engine
prediction/interpolation context. This update adds no historical time selection
and no claim of equivalence to every input-history/server shooting origin.

`final_checks.py` records five passing frozen verifier commands in `frozen.json`
and their full outputs in per-verifier JSON files. Twenty deliberately wrong
headers are rejected in `negative.json`, covering source functions, vtables,
new pawn members, resource layout, weapon table records and entry fingerprints.
All five verifiers also reject the preserved old negative-test client sample;
see `hash_rejections.json`. All module/asset hash guards remain exact. This is installed-file verification:
no client/server was launched, no native getter was executed, and no live aim,
accuracy, bone pose, capsule or shot-registration observation is claimed. Existing
command-thread ownership, cache invalidation and cooperative residency rules
remain in force. Earlier build-specific addresses below are historical.

## Historical evidence

This concerns the installed Windows AMD64 **client**, patch `1.41.9.1`,
client/server `2000934`, Source revision `11150591`, Steam build `26087496`.
The game root is
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`;
executable `bin/win64/cs2.exe`, investigated module
`csgo/bin/win64/client.dll`. SHA-256:
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`.
This extends the existing native client input pipeline. It does not establish
a fresh loaded graphics API; the existing project renderer is D3D11.

The existing twenty-seven-component compatibility baseline passed before this
investigation. Research reads installed files and writes workspace logs only.
No client/server was launched, no real native getter was executed, and no shot
accuracy, packet receipt or hit registration was observed.

## Native getter and ABI

The installed RTTI primary table for `CCSPlayer_WeaponServices` is `0x1C14C78`.
Its **byte slot `0x100` (index 32)** points to `0xA7FD00`, not a copied index
from an external implementation. Disassembly establishes the Microsoft x64 ABI:

```cpp
AimVector* currentOrigin(void* weaponServices, AimVector* out);
// RCX = services, RDX = caller-owned float3 output, RAX = out.
```

The complete 31-byte function loads the owner pawn at services `+0x38`, calls
the pawn's virtual byte slot `0x568` with the same output pointer, then returns
that output pointer. The native history producer at `0x9138D0` independently
sets `RCX = services`, `RDX = stack float3` and dispatches services slot `0x100`
at `0x913996`. Native history interpolation falls back to the same current
getter at `0x903C84`. These independent callers establish both the ABI and the
getter's role in native shooting-position acquisition.

The installed `C_CSPlayerPawn` primary table is `0x1C8A138`; slot `0x568`
resolves to `0x93C660`. This routine handles native camera-owner/spectator
branches and otherwise calls base eye routine `0x15FAB0`. The base eye routine
calls absolute-origin getter `0x219A80`, dispatches pawn slot `0x5A0`, and adds
the three returned offset components to that origin. Slot `0x5A0` resolves to
`0x160770`, which uses the native view-offset interpolation accessor `0x160580`
and scale-enabled vector transform `0x16A3D0`. This is the concrete difference
from simply copying the current `m_vecViewOffset` wrapper's first float3.

Static schema registration `C_BasePlayerPawn` at `0x21F4588`, 28 records in
array `0x21F41F0`, independently names weapon services `+0x12F0`, camera services
`+0x1328` and observer services `+0x1308`. Native getter operands agree with the
camera/observer fields. Existing entity contracts own health, life state,
dormancy, controller ownership, full handles and scene identity.

## Timing and ownership limits

The native getter takes **no explicit timestamp**. It returns the current native
origin in the engine's current prediction/interpolation context. Calling it is
appropriate only on the existing native command owner, never from Present or a
background/render thread. Native origin and interpolation getters may refresh
their own engine-owned caches; the adapter performs no direct entity writes.

The native history producer `0x9138D0` first builds a tick/fraction time pair,
then copies the current native origin into a fixed 32-entry ring. Entries have
20-byte stride, time tick/fraction at `+0/+4` and origin at `+8/+12/+16`;
the ring storage begins at services `+0xE8`, with head/count at `+0x368/+0x36C`.
These details were independently found through native callsites and the named
`GetInterpolatedShootPosition` logging anchor at `0x1C14F40`; the runtime adapter
does not consume those raw history fields.

The historical routine `0x903A30` accepts a tick/fraction query and interpolates
bracketed recorded origins through helper `0x903EE0`; if no bracket exists it
falls back to the current getter. Native firing-origin wrapper `0x80D210` calls
that historical routine at `0x80D2CA` after deriving an adjusted time through
`0x15EE5C0`. Consequently **a current native origin is not evidence that it
matches every earlier input-history timestamp or the server's final origin**.
This implementation does not choose older origins, forge history timestamps,
extrapolate a future origin, or claim to eliminate every cause of missed shots.

Further read-only inspection established the firing wrapper's precise inputs:
`Vector3* wrapper(Vector3* out, pawn*, TickFraction* time, Vector3* compare,
TickFraction* compareTime)`, with the fifth argument on the stack. Direct native
callers at `0x80DA71`, `0x80DD0E` and `0x80DF03` pass null comparison inputs.
The supplied time is adjusted by a controller-derived tick offset before the
historical query. The initial attack path converts input-history render time
through controller-aware helper `0x75B8F0`, while held-fire continuation uses
retained command state and fire-time interpolation. Native metadata helper
`0x80D700` obtains its current command indirectly through the pawn's command
owner, rather than accepting the command being edited. The current callback has
not established ownership equivalence for that internal current-command pointer.
Using the newest history entry's time or invoking that helper during a history
callback therefore does not establish the firing wrapper's required time.
This is why the adapter deliberately uses the independently verified current
origin rather than introducing an unsupported timestamp approximation.

## Adapter lifetime and invalidation

`AimShootOriginPrepare` validates the installed PE identity, four native entry
fingerprints and all three relevant vtable targets before enabling a context.
`AimShootOriginRead` checks the current module/globals, pawn/controller identity
backreferences and full reciprocal handles, alive/non-dormant scene, exact pawn
and services tables/slots, and services owner. Camera/observer service identities
are copied along with the origin, scene, pawn identity and handles.

The output is poisoned before the call. A missing/foreign return pointer,
partial write, nonfinite/out-of-bounds coordinate, SEH exception, or changed
source after the query rejects the result. `AimShootOriginStillCurrent` repeats
the complete native query and compares the copied source and exact coordinates.
Even sub-unit movement during staging invalidates the result. Contexts and
snapshots are owned by the existing command pipeline; no engine pointers escape
to the renderer or survive a lifecycle without revalidation.

## Reproduction and standalone checks

```powershell
python tools/inspect_aim_shoot_origin.py
cmake --build build --config Release --target aim_shoot_origin_checks
.\build\Release\aim_shoot_origin_checks.exe
```

The standard-library verifier pins the entire installed module hash, eight
native instruction windows, both named RTTI tables, named schema fields,
independently decoded dispatch/call/member operands, the named historical
anchor, every source constant and all four runtime fingerprints. `--client`
selects an installed image and `--header` selects an existing source header to
check; neither generates replacements or writes installed files.

Standalone `aim_shoot_origin_checks` passes **61 checks** covering the exact
native boundary arguments, copied position, lifecycle ownership, late movement,
source replacement during/after the getter, malformed partial/nonfinite outputs,
foreign return pointers, exceptions, module replacement and preparation rejection.
Its native-boundary substitute never invokes the installed engine.

Research scripts, Capstone 5.0.9 disassembly, verified output and negative results
are under `logs/research/26087496/shoot_origin/`. `negative.py` rejects **25**
independent wrong contracts: 13 source constants, four source fingerprints and
one corrupted byte in each of eight native windows. Those fixtures do not alter
the real source header or installed DLL. Static/fixture success does not establish
live shooting behavior.
