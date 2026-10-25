# Native viewmodel parameters and scoped projection — October 25, 2026

## Correction after the user's first live comparison

The user reports that the unscoped XYZ/FOV controls work, but the first scope
removal implementation still leaves the viewmodel enlarged. Static review
confirms an incorrect callback-order assumption in that implementation:
`ClientModeCSNormal::OverrideView` is **inside** the native view builder, before
the builder's final viewmodel FOV writes. Successful preparation and applied
world-FOV counters therefore did not prove that the viewmodel edit survived.

The installed normal-mode table `0x1CA1B50` has slot **15** (`+0x78`) pointing
to native view builder **`0xB9D7B0..0xB9DEEB`**, ABI
`void(mode*, view*)` (RCX mode; RDX caller-owned view). Slot **16** (`+0x80`)
points to the existing world-zoom hook target `0xD04490`. Exact native order:

1. Builder entry stores its mode argument in the incoming home slot at
   `[entryRsp+8]`, then retains the view in RBX.
2. `0xB9DA25` restores that saved mode into RSI from `[currentRsp+0x110]`.
   `0xB9DA3B` passes the same view in RDX and `0xB9DA3E` passes mode in RCX.
   `0xB9DA41` calls mode slot 16, including the existing zoom callback.
3. After the callback returns, `0xB9DBE2/0xB9DBF2` write the first
   zoom-subtracted/clamped viewmodel FOV at `view+0x49C`.
4. `0xB9DD41/0xB9DD51` recompute and overwrite that viewmodel FOV again.
5. `0xB9DE48` publishes the resolved aspect field. `0xB9DE6D/0xB9DE72`
   aspect-convert and write the final viewmodel FOV.
6. The builder copies final origin/angles to its remaining view outputs and
   returns at `0xB9DEEB`.

Consequently, a scope-removal `+0x49C` edit inside slot 16 is overwritten, and
its aspect read may precede initialization for that view. Scope-removal
projection replacement belongs **after the original slot-15 builder returns**,
using the same primary normal mode / slot-zero and fresh local source checks.
The existing world-zoom edit can remain in slot 16: its changed world FOV is
intentionally processed by the rest of the native builder. Do not move it to
the end without separately accounting for its pre-aspect semantics.

The frozen verifier now pins the **entire** native builder (0x73C bytes), both
normal-mode table slots, the mode home-slot restoration/call sequence, final
viewmodel stores and the aspect conversion. This corrects the earlier
"final normal view callback" assumption; it does not claim the corrected
post-builder visual result has already been observed.

Investigated the installed Windows AMD64 **client**, patch **1.41.9.2**,
client/server **2000935**, Source revision **11162119**, Steam build
**26144732**. Installation:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`;
executable `bin/win64/cs2.exe`, module `csgo/bin/win64/client.dll`.
The module SHA-256 is
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.
This was installed-file-only investigation: no client/server launch, process
access, live graphics-runtime observation or installed-file modification.
The existing client overlay uses D3D11; this investigation does not establish
which renderer a current live client has loaded.

Reproduce with `python tools/inspect_viewmodel.py`. Runtime contracts are in
`include/viewmodel_offsets.h`. The standard-library verifier rejects an
unfamiliar module hash, checks thirteen complete relevant native windows, binds
exact cvar registration names to their native consumers, decodes the helper's
two callers, checks the normal-mode virtual FOV getter and independently
compares producer/consumer FOV operands against the header. Research scripts,
disassembly, the passing report and header mutation results are under
`logs/research/viewmodel_20261025/`.

## Temporary parameter output

Native function `0x8CFA30..0x8CFBBD` has Microsoft x64 ABI:

```cpp
void Parameters(void* pawn, float* xyz, float* fov);
// RCX: pawn; RDX: writable three-float output; R8: writable float output.
```

Entry instructions save R8 into R15, RDX into R14 and RCX into RSI. All native
non-stack stores target the supplied XYZ/FOV buffers. No pawn, weapon, camera
services or cvar value is written. Callers ignore return registers. The helper
first resolves its native local source through `0xC7D470` and verifies the
player-pawn virtual predicate at `+0xAB8`. A matching local pawn uses native
cvars; another eligible pawn may use its replicated viewmodel settings at
`+0x1DC0..+0x1DCC`. The runtime must restrict edits to the current alive local
pawn and revalidate its full source identity around native execution.

Exact cvar registrations and consumers are:

| Name | Registration name operand | Wrapper operand | Helper consumer |
| --- | --- | --- | --- |
| `viewmodel_offset_x` | `0xB338E` | `0x2534780` | `0x8CFAD7` |
| `viewmodel_offset_y` | `0xB344E` | `0x2534790` | `0x8CFAE3` |
| `viewmodel_offset_z` | `0xB350E` | `0x25347A0` | `0x8CFAF4` |
| `viewmodel_fov` | `0xDFD3E` | `0x256CA90` | `0x8CFB06` |

Each consumer passes that wrapper to the existing native float getter
`0x160AF0`. The helper clamps X to `[-2,2.5]`, Y/Z to `[-2,2]` and base FOV
to `[54,68]` before returning. A hook can call native first and then add
selected XYZ deltas / replace base FOV in these current-call outputs, without
changing cvars or persistent entity fields. Calling the verified helper with
stack-owned XYZ/FOV outputs also obtains the native unzoomed base FOV.

The installed helper has two direct call sites:

* `0xCF12C2`, return `0xCF12C7`: normal-mode base viewmodel FOV getter.
  Caller supplies its resolved pawn, a stack XYZ buffer and a stack FOV float.
* `0x10CA7F0`, return `0x10CA7F5`: viewmodel transform. Caller supplies its
  pawn, three stack offset floats and one stack FOV float. The weapon lookup
  follows the helper; it does not restrict the parameters to scoped weapons.

The transform checks the hand state through `0x8D9CE0` and flips X for the
opposite hand. Its basis is produced by `0x1744D50`; offsets apply as
`origin += forward*Y + up*Z - right*X`. Native ironsight interpolation scales
XYZ by `(1 - ironSightFactor)`. This scaling, weapon animations, bob and sway
are separate from the parameters and remain the native caller's behavior.

## Scoped viewmodel magnification

The normal-mode primary RTTI table is `0x1CA1B50`. Its byte slot `+0xE0`
(index 28) points to `0xCF1270`, ABI `float(mode*)`, XMM0 result. The getter
gets the base FOV through the parameter helper above and can subsequently
apply weapon-specific / ironsight FOV adjustments. That getter is different
from the controller's world-FOV baseline getter used by the zoom feature.

Native view builder `0xB9DB8B..0xB9DBF2` computes:

```text
worldDelta = camera baseline FOV - current view world FOV
viewmodel FOV = max(5, normal-mode viewmodel getter() - worldDelta)
```

The baseline is camera-services virtual slot `+0x120`; the current world FOV
is the same view's float at `+0x498`. The native getter call at `0xB9DBC2`
uses normal-mode slot `+0xE0`. The destination is formed with `lea rdi,
[rbx+0x49C]` at `0xB9DBB8`. Native subtracts the world delta at `0xB9DBDB`,
writes that temporary result and clamps to 5 through `0x1B9C10`, then writes
the final float at `0xB9DBF2`.

Independent projection code `0xC783C7` loads world FOV at `+0x498`, whereas
`0xC7844E` loads viewmodel FOV at `+0x49C`. Their tangent ratio is calculated
at `0xC78499` to transform viewmodel positions. Thus shrinking viewmodel FOV
causes the enlarged weapon seen after removing the scope graphics.

Adding the world delta back to the *clamped* viewmodel FOV is incorrect for
strong zoom: base 68 minus delta 75 clamps to 5; adding 75 would yield 80.
For scope-overlay removal, the completed current-view `+0x49C` after the
slot-15 builder returns can instead
receive the unzoomed base returned through temporary parameter outputs. This
preserves the selected/native base FOV across both zoom levels without
changing world FOV, sensitivity, scoped state, recoil or weapon zoom state.

The final view stores FOV in a different projection domain from that base.
After both native viewmodel FOV calculations, `0xB9DE1B` tests byte
`view+0x551` with mask `0x02`. When that bit is **set**, conversion is skipped
and the raw base can be used. Otherwise `0xB9DE48` writes the resolved aspect
ratio into `view+0x4D4`. That ratio comes from the engine's virtual slot
`+0x2C0` at `0xB9DCA5`; a nonpositive result falls back to
`float(view+0x430)/float(view+0x438)` at `0xB9DE2F..0xB9DE3E`.
The completed aspect field already incorporates that fallback.

Native then converts both world FOV (`0xB9DE5B`) and viewmodel FOV
(`0xB9DE6D`) through function `0x1751F40`, ABI `float(float fov, float scale)`
(XMM0/XMM1 inputs, XMM0 result). Its exact formula is:

```text
scale = viewAspect * 0.75
projectedFov = 2 * atan(tan(baseFov * pi/360) * scale) * 180/pi
```

The literal `0.75f` is at `0x1B300C8`. Native half-degree-to-radian and
radian-to-degree factors are `0.008726646192371845f` at `0x1AC8070` and
`57.2957763671875f` at `0x1AED23C`; imported math calls are
`tier0.dll!V_tanf` and `tier0.dll!V_atanf`. Final current-view replacement
must perform this conversion with the copied final aspect field unless the
skip bit is set. Writing a raw base of 68 into the completed 16:9 view would
be narrower than the native unzoomed result of approximately 83.9 degrees.
The runtime should reject inaccessible/nonfinite/nonpositive aspect inputs
and retain the native current-view FOV in that case.

Static checks and synthetic runtime fixtures do not establish live weapon
appearance. Offline/private visual verification remains necessary. No new
session gates or installed-game configuration changes are required. Runtime
integration must retain the project's cooperative stop and resident callback
forwarding policy.
