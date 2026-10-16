# Scoped weapon zoom and native view FOV

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
view wider than the baseline retain the native view. Only current world FOV
is changed; scope graphics, sensitivity, viewmodel FOV, recoil, weapon state,
shooting and server data remain native.

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

`inspect_zoom.py` pins nine instruction windows across the native normal-view
wrapper, baseline getter/camera baseline, current-FOV interpolation, view-FOV
producer/projection consumer, weapon FOV selection and zoom-state cycle/use.
It separately checks exact named schema registrations, RTTI/vtable slots,
decoded member operands, every source constant and all three source arrays.
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

Both public and diagnostic x64 Release builds succeeded. The native zoom
suite passes **556 checks, zero failures**. All **13/13** installed static
verifiers pass in `logs/compatibility/20261016T160815546576Z/`; all **13/13**
deliberate header-constant/array mutations reject in
`logs/research/zoom-header-mutations/results.json`. Both built DLLs retain zero
static TLS and zero delay-import directories. All 25 CTest suites pass in each
build, including menu/config interaction checks; verification is also recorded
in the README. The software D3D11 menu preview was visually inspected
with zoom 1 at 0% and zoom 2 at 150%; no live scoped render was observed.
