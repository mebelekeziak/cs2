# Scoped weapon zoom and native view FOV

## Viewmodel scope removal — October 25, 2026

The first attempted correction inside the normal OverrideView callback was
overwritten by the enclosing native builder. Revision 2 moves viewmodel
correction into the separate Viewmodel module's completed slot-15 builder hook,
after both subsequent FOV writes and aspect conversion. The existing world
zoom/sensitivity hooks remain in this module; they do not write viewmodel FOV.
The user confirms the corrected scoped weapon size. See the exact native order
and base/projection conversion in [viewmodel evidence](CS2_VIEWMODEL_EVIDENCE.md).
Older sections below describe the original world-only implementation.

## Build 2000935 update — October 23, 2026

Verified the installed Windows AMD64 **client**, patch `1.41.9.2`,
client/server `2000935`, Steam build `26144732`. Client SHA-256 is
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`;
PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`. The installation remains
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
This supersedes the runtime values in historical sections below.

Header `zoom_offsets.h` changes, relative to build 26087496:

| Source symbol | Previous | Current |
| --- | --- | --- |
| `OverrideView` | `0xd02e30` | `0xd04490` |
| `ModeVtable` | `0x1c9fc20` | `0x1ca1b50` |
| `BaselineFov` | `0x93fc40` | `0x940a70` |
| `Scoped` | `0x1e98` | `0x1ea0` |
| `MouseConvert` | `0xba6ed0` | `0xba7d10` |
| `MouseConvertAlternate` | `0xba6d00` | `0xba7b40` |
| `MouseInput` | `0x257b640` | `0x257d780` |
| `MouseInputVtable` | `0x1c9db20` | `0x1c9fa58` |
| `MouseScale` | `0xb80180` | `0xb80fc0` |
| `MouseScaleUnit` | `0x1ac6074` | `0x1ac8084` |
| `MouseCaller` | `0xb655c3` | `0xb66403` |
| `MouseScopeDispatch` | `0xba68da` | `0xba771a` |

Rediscovery used all six named scoped-weapon primary RTTI tables and their
`+0xBF0` zoom-FOV method, `ClientModeCSNormal` slot 16, named schema registrations,
the current native baseline getter and the complete mouse caller/dispatcher/
conversion chain. `C_CSPlayerPawn::m_bIsScoped` moves `0x1E98 -> 0x1EA0`;
registration `0x223BAC8`, field array `0x223AD40`, count 105 and the visual
producer's `cmp byte [rbx+0x1EA0],0` independently establish the new field.
Weapon-services `+0x12F0`, active handle `+0x60`, weapon zoom-level `+0x1F20`,
desired FOV `+0x794`, view FOV `+0x498` and sensitivity adjust `+0x149C` remain.

All seventeen windows were fully decoded. FOV selection, projection use and
mouse argument ABI remain: normal
`void(int,float,float,float,float*,float*,int*,int*)`, alternate
`void(int,float,float,float,float,float*,float*)`. The embedded input constructor
still writes the independently found RTTI table; its literal baseline is 1.0,
and the dispatcher multiplies positive native scope adjustment before forwarding
XMM3. Indexed alternate-path Cvar storage moved, without changing this contract.
No algorithm, ownership or cooperative-stop change.

The frozen verifier still rejects unknown module hashes before using its sites,
and checks complete new windows, independently decoded operands/identities,
header constants and all fingerprints. Research/disassembly and the passing
`python tools/inspect_zoom.py` report are under
`logs/research/26144732/view_model/` (`refined.json`, `addresses.json`,
`schemas.json`, `verified_zoom.json`). All **28/28** temporary
header scalar/array mutations reject; this verifier also rejects a one-byte
changed workspace client copy. See `negative_checks.json` and
`unknown_hash_checks.json` there. No installed file was changed.

Static verification does not establish live appearance or gameplay behavior.
No client/server was launched, no process was accessed, and no graphics runtime
was observed in this pass. Build/CTest integration is recorded by the project
update record; stopped callbacks remain resident until normal process exit.

## Build 2000934 recertification — October 21, 2026

Rechecked the installed Windows AMD64 **client**, patch `1.41.9.1`,
client/server `2000934`, Steam build `26087496`, Source revision `11150591`.
`csgo/bin/win64/client.dll` changed from SHA-256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` to
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
its PE timestamp changed `0x6AD01314 -> 0x6AD7B41C`, with AMD64 and
SizeOfImage `0x299D000` unchanged. Installation and executable paths remain
as documented below. This was file-only investigation: no client/server
launch, process access, graphics-runtime observation or installed-file change.

All seventeen complete native windows, six scoped-weapon RTTI tables, named
schema registrations/fields, final view slot, mouse identity getter and full
scope/mouse conversion caller chain match the existing contracts. Independent
FOV/member/scale operands and every source fingerprint pass unchanged. No
runtime constant or hook/lifecycle behavior changed. Only the verifier's exact
client hash pin changed; an unfamiliar hash still rejects.

Reproduce with `python tools/inspect_zoom.py`. The fresh real-header report is
`logs/research/26087496/view_model/verified_zoom.json`. Temporary wrong
`MouseInput`, `Scoped` and `MouseConvertBytes` headers all reject; results
are in that directory's `negative_checks.json`.

No fresh live appearance is claimed. Build/CTest integration results are recorded
in the project update handoff and README. The earlier investigation below is
historical provenance; its old module identity is superseded above.

## Earlier baseline investigation

Investigated October 16, 2026 against the installed Windows AMD64 **client**:
patch 1.41.9.1, client/server 2000933, Steam build 25966348. Installation:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`;
executable `bin/win64/cs2.exe`, module `csgo/bin/win64/client.dll`.
The module SHA-256 is
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
PE timestamp `0x6AD01314`, SizeOfImage `0x299D000`, machine AMD64.
No live client or server was started for this investigation. The project uses
its existing D3D11 overlay for sliders; this pass establishes native view
contracts statically, without a fresh observation of the loaded graphics API.

Runtime constants are in [zoom_offsets.h](../include/zoom_offsets.h).
Reproduce frozen evidence with `python tools/inspect_zoom.py`. This verifier
uses the standard library, reads the installed PE only, and rejects another
module hash before trusting its schema/code windows. Optional Capstone 5.0.7
and pefile 2023.2.7 in the existing ignored research dependency directory
were used for discovery; the frozen verifier does not require them.

## Scoped source and level selection

The installed static schema templates independently identify these fields:

| Class | Registration / array / field count | Named field / member |
| --- | --- | --- |
| `CBasePlayerController` | `0x2223A38` / `0x2223800` / 17 | `m_iDesiredFOV=0x794` |
| `C_BasePlayerPawn` | `0x21F4588` / `0x21F41F0` / 28 | `m_pWeaponServices=0x12F0` |
| `C_CSPlayerPawn` | `0x2239A68` / `0x2238D10` / 105 | `m_bIsScoped=0x1E98` |
| `CPlayer_WeaponServices` | `0x222BDC8` / `0x222BD30` / 4 | `m_hActiveWeapon=0x60` |
| `C_CSWeaponBaseGun` | `0x221BF48` / `0x221BB00` / 7 | `m_zoomLevel=0x1F20` |
| `CCSWeaponBaseVData` | `0x221AAE8` / `0x2219F80` / 83 | zoom count `0x7F4`, FOV1 `0x7F8`, FOV2 `0x7FC` |

These are on-disk registration templates: name pointer at `+0`, field count
at `+0x1C`, field-array pointer at `+0x28`; each `0x20`-byte field record has
name pointer `+0` and offset `+0x10`. They are not runtime binding layouts.

The native zoom cycle at `0x8248AE..0x8248E3` reads/writes a **dword** at
weapon `+0x1F20`, increments the level, and resets to zero when it exceeds the
weapon's virtual zoom count. Native consumer `0x829222` passes that dword to
weapon vtable byte slot `+0xBF8` for transition time and `0x829234` passes it
to slot `+0xBF0` for selected FOV. The latter targets `0x80E420` for all six
verified scoped weapon classes. Its complete branches return VData `+0x7F8`
for level 1, `+0x7FC` for level 2, and zero for level 0/other values. Thus the
numbers identify actual native zoom levels rather than inferring them from
the current interpolated view FOV.

Before reading the gun-specific zoom member, runtime requires one of the
installed class's exact primary RTTI vtables:

| Class | Primary vtable RVA |
| --- | --- |
| `C_WeaponAug` | `0x1BDEB08` |
| `C_WeaponAWP` | `0x1BDF998` |
| `C_WeaponG3SG1` | `0x1BE33D8` |
| `C_WeaponSCAR20` | `0x1BF0E68` |
| `C_WeaponSG556` | `0x1BF1CF8` |
| `C_WeaponSSG08` | `0x1BF2B88` |

No designer-name guess or arbitrary class is treated as a gun. New weapon
classes require renewed evidence. The active weapon's full serialized handle
is resolved through the existing verified entity system: bucket table `+0x10`,
index mask `0x7FFF`, shift 9, bucket mask `0x1FF`, identity stride `0x70`,
entity pointer `+0`, full-handle serial `+0x10`. Runtime checks that serial and
the weapon's identity backreference. It also checks current local pawn and
controller globals, pawn identity/backreference, controller pawn handle,
positive health, zero life state and the scoped byte. A level other than 1/2
is a native-view no-op.

## Native baseline and interpolation

The installed `CCSPlayerBase_CameraServices` and `CCSPlayer_CameraServices`
primary tables (`0x1C0CD58`, `0x1C0D518`) both point byte slot `+0x118` to
native current-FOV function `0x88A630` and `+0x120` to baseline getter
`0xA681C0`. Current-FOV code gets the baseline through slot `+0x120`, uses
camera service `m_iFOV=0x298` when nonzero, and interpolates from `0x29C` using
time/rate `0x2A0/0x2A4`. The runtime does not write those persistent fields.

For an ordinary alive local pawn, baseline getter `0xA681C0` resolves its
controller and tail-calls `0x93FC40`. That function has ABI
`float(controller*)` (RCX input, XMM0 output), reads the controller's dword
desired FOV at `+0x794`, and uses the engine's default-FOV virtual slot
`+0x110` when the desired value is zero. Instructions `0x93FC5E..0x93FC62`
convert the resulting integer to float. Runtime calls this verified getter
on the current callback rather than caching an assumed 90-degree default or
duplicating the engine-default fallback. The observer-specific branches of
the general camera getter are outside the alive-local override scope.

Strength sliders select 0..200 percent for each native level independently.
Let B be the native baseline FOV, N the current interpolated native world FOV,
and s the selected percentage divided by 100:

```text
M = tan(B / 2) / tan(N / 2)
result = 2 atan(tan(B / 2) / (1 + s * (M - 1)))
```

The implementation converts degrees for trigonometry. Zero returns B exactly;
100 returns N exactly; 200 doubles the additional magnification above 1x.
This preserves each weapon's different native FOV and scales the current
native interpolation. It is not a fixed replacement of every weapon's native
FOV1/FOV2. Invalid/nonfinite inputs, FOV outside `(0,179)`, or a special native
view wider than the baseline retain the native view. The FOV hook changes only
current world FOV. Scope graphics, viewmodel FOV, recoil, weapon state,
shooting and server data remain native. Scoped mouse sensitivity has its own
independent current-call override described below.

## Scoped mouse sensitivity (October 18, 2026)

The installed build and SHA-256 above were rechecked without starting a client
or server. Named `C_BasePlayerPawn` schema fields independently identify
`m_flFOVSensitivityAdjust=0x149C` and `m_flMouseSensitivity=0x14A0` as floats.
Native pawn function `0xB227D0..0xB228B9`, ABI `void(pawn*)`, produces both.
It obtains integer baseline/current/minimum FOV through camera-services virtual
slots `+0x120`, `+0x118`, `+0x150`, uses the greater current/minimum FOV, and
initializes the adjustment to 1. If that FOV equals baseline, mouse sensitivity
is set to the native zero sentinel. Otherwise it computes
`adjustment = zoom_sensitivity_ratio * integerFov / integerBaseline` and
`mouseSensitivity = sensitivity * adjustment`. The baseline denominator uses
1 if its integer value is zero. Registrations `0xC957E..0xC95A7` and
`0xD81EE..0xD8217` bind the exact names `zoom_sensitivity_ratio` and
`sensitivity` to embedded cvar wrappers `0x255ACD8` and `0x2562A10`.

The current-call override avoids writing these persistent pawn fields or any
cvar. Exact mouse input caller `0xB65560..0xB6568E` calls `CCSGOInput` virtual
slot 24 (`+0xC0`) at `0xB655D1`, passing the float literal **1.0** from
`0x1AC6074`. The installed input primary RTTI table `0x1C9DB20` points that
slot to `0xB80180`, whose complete body is `movaps xmm0,xmm1; ret`: this getter
preserves that exact 1.0. The embedded input object at `0x257B640` and its table
are independently established by constructor operands `0xEE574`, `0xEE58C`
and `0xEE59A`, as in the movement verifier.

At `0xB65606`, the caller passes the result as XMM1 to native mouse dispatcher
`0xBA67B0`. At `0xBA68DC` that dispatcher resolves the pawn for its split-screen
slot using `0x96A8F0`, reads `pawn+0x149C` at `0xBA68E6`, and multiplies its
incoming 1.0 by that adjustment only when the adjustment is positive. Otherwise
the scale stays 1.0. It passes this completed scale in **XMM3**, the fourth
argument, to exactly one of these conversion methods:

| Function / sole direct call site | Microsoft x64 ABI |
| --- | --- |
| `0xBA6ED0` / `0xBA6934` | `void(int slot, float axis1, float axis2, float scale, float* angular1, float* angular2, int* mouse1, int* mouse2)` |
| `0xBA6D00` / `0xBA694C` | `void(int slot, float axis1, float axis2, float scale, float deltaTime, float* angular1, float* angular2)` |

There is no `this` argument: ECX contains the slot; XMM1, XMM2 and XMM3 hold
the three floats. In the alternate method, `deltaTime` is stack argument 5,
loaded at `0xBA6D81` from post-prologue `rsp+0xE0`; output pointers are stack
arguments 6 and 7. In the ordinary method, output pointers are stack arguments
5 and 6 and integer mouse outputs are stack arguments 7 and 8. Callers ignore
return registers; neither callback contract defines a return value.

The ordinary conversion obtains the native `sensitivity` with split-screen
cvar getter `0xBAA320`, then applies the passed scale, native vertical scale and
the engine's native angular coefficients to its original float/integer output
buffers. The alternate curve conversion also applies the passed scale only
after its native curve calculation (`0xBA6E03`, `0xBA6E6C`). Both therefore
support the same call-local replacement without duplicating input curves,
rounding, angular coefficients, cvar fallback, or output-buffer behavior.
The separate `0xB62A20` path reads `m_flMouseSensitivity` directly for native
camera/mouse-delta bookkeeping; it is not the angular conversion intercepted
here, and its stored fields remain untouched.

For selected sensitivity percentage `p` in 0..100, the replacement fourth
argument is `1 + (nativeScale - 1) * p / 100`. Zero restores the exact original
unscoped angular sensitivity; 100 forwards the original native argument
unchanged, including the user's native zoom ratio. Intermediate values blend
the full native scale rather than attempting to cancel only FOV. This setting
is independent of the two world-FOV strengths and applies to both zoom levels.
The existing scoped-zoom master switch also controls sensitivity.

The callback requires split-screen slot zero, an alive scoped local pawn and
one of the same six verified active weapon classes/levels. It snapshots and
revalidates source identity immediately before forwarding to native conversion;
invalid/nonfinite inputs retain the original scale. Native receives all other
arguments unchanged and owns every output write. The existing dynamic TLS and
shared callback lock cover view and both mouse callbacks, so nested calls
forward unchanged and stop drains all in-flight edits before retaining native
forwarding. No persistent sensitivity state needs restoration, and stopped
code remains resident until process exit.

`inspect_zoom.py` now pins the complete mouse input caller, scale getter,
dispatcher, both conversion functions, pawn sensitivity producer and both
cvar registration windows. It checks exact schema fields, input RTTI/table,
constructor operands, the 1.0 literal, direct call targets, decoded cvar
producer/consumer relationships, and every added source constant/byte array.
Static evidence and memory-backed callback tests do not establish live mouse
feel; no scoped client run was performed in this investigation.
All 15 deliberate mutations of the nine new constants and six new byte arrays
were rejected by the frozen verifier; results are recorded in
`logs/research/zoom-sensitivity-header-mutations/results.json`.

## Final current-view hook and lifecycle

`ClientModeCSNormal` primary table `0x1C9FC20` has slot 16 pointing to
`0xD02E30`, ABI `void(mode*, view*)`. Its first native call at `0xD02E3A`
invokes shared OverrideView `0xB99250`, already used by the third-person
feature. The normal wrapper can later overwrite world FOV at `0xD0309F` for
its special camera path. Hooking the normal wrapper **after the original
returns** ensures zoom modifies the completed normal view and composes with
the existing shared camera hook. Mode `+0x28` must be split-screen slot zero;
the value is independently established by native getter `0xB96570`.

Native view builder `0xB9CA16` forms world-FOV address `view+0x498` and passes
it as the sixth argument to camera-services view producer at `0xB9CB0B`.
The normal wrapper store at `0xD0309F` independently uses `view+0x498`.
Projection consumer `0xB51E2D..0xB51EAE` loads this float and passes it to
native projection construction `0x855240`. Adjacent `view+0x49C` is a distinct
viewmodel FOV; origin/angles are `+0x4A0/+0x4B8`. Runtime writes exactly four
current-view bytes at `+0x498` after validating the computed float.

Before the native callback, runtime snapshots copied local/source identity,
weapon services, weapon full handle/identity and zoom level. It re-resolves
all of them afterward, then calls the baseline getter and revalidates again
before its view write. A source change skips the edit; no retained engine
pointer is dereferenced on a later frame. Current module identity and normal
mode/slot are rechecked. Dynamic TLS suppresses edits for reentrant views.
No loader/session gates, shared cvar writes or network interactions are added.

Preparation verifies AMD64 PE identity, entry bytes and the normal-mode slot,
then queues a resident MinHook hook outside DllMain. Stop disarms new edits and
drains the shared view lock. Later callbacks retain native forwarding. This
is stopped and resident, not physical DLL unload; no code/trampoline is freed
while an engine callback may return through it.

## Verification and limits

`inspect_zoom.py` pins seventeen instruction windows across the native normal-view
wrapper, baseline getter/camera baseline, current-FOV interpolation, view-FOV
producer/projection consumer, weapon FOV selection, zoom-state cycle/use and
native scoped mouse sensitivity production/conversion.
It separately checks exact named schema registrations, RTTI/vtable slots,
decoded member operands, every source constant and all source arrays.
Entity layouts come from the existing entity header and its independent frozen
compatibility check, included in the combined verification bundle.

`zoom_checks` uses memory-backed fake engine sources and native callback
substitutes. It covers level 1/2 selections at 0/100/200, the tangent
magnification formula and monotonicity, custom/invalid baselines, all six
class tables, exact forwarding and opaque view-byte preservation, invalid
serials/backreferences and nonlocal/dead sources, switches during original or
baseline callbacks, nested views, null views, stopped forwarding and a blocking
native callback during concurrent cooperative stop. These tests do not execute
the installed engine routines. Static verifier success/build success cannot
establish visible scope appearance or gameplay behavior. Those remain
unobserved until the user's authorized offline/private session.

Historical October 16 verification: both public and diagnostic x64 Release builds succeeded. The native zoom
suite passes **556 checks, zero failures**. All **13/13** installed static
verifiers pass in `logs/compatibility/20261016T160815546576Z/`; all **13/13**
deliberate header-constant/array mutations reject in
`logs/research/zoom-header-mutations/results.json`. Both built DLLs retain zero
static TLS and zero delay-import directories. All 25 CTest suites pass in each
build, including menu/config interaction checks; verification is also recorded
in the README. The software D3D11 menu preview was visually inspected
with zoom 1 at 0% and zoom 2 at 150%; no live scoped render was observed.

**October 18 sensitivity verification:** public and diagnostic x64 Release
builds succeed and each passes 42/42 CTest suites. The expanded `zoom_checks`
passes 1,794 checks, including normal and alternate conversion forwarding,
0/50/100% sensitivity, both zoom stages and all six classes, independence from
the FOV sliders, unchanged source/entity bytes, invalid/stale/dead sources,
nonprimary slots, invalid scales, nested mouse/view callbacks, and cooperative
stop during either blocked native conversion. Menu/config regressions cover
actual slider drags, resets, fractional persistence and missing older keys.
The software D3D11 preview `logs/zoom-sensitivity-menu.bmp` was inspected.

All 21 installed-file compatibility checks pass in
`logs/compatibility/20261018T140828198745Z/`; that bundle's zoom header/verifier
hashes match the final sources. All 15 added sensitivity constant/array
mutations reject in
`logs/research/zoom-sensitivity-header-mutations/results.json`. Both payloads
retain zero TLS and delay-import directories. No client/server was launched;
live scoped sensitivity and visible zoom remain unobserved.
