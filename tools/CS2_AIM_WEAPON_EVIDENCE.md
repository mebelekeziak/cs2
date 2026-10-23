# Current weapon model, recoil and readiness — October 16, 2026

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

All 29 supported weapon primary tables are independently rediscovered via
RTTI; `C_AK47` is now `0x1BDFBB0`. Spread byte slot `0xBD0` resolves
`0x80D490 -> 0x80E2C0`, inaccuracy slot `0xCF8` resolves
`0x80C1A0 -> 0x80CFD0`. Primary cooldown moves `0x94DE60 -> 0x94EC90`,
current-time query `0x75D250 -> 0x75E050`, native current-punch query
`0x8526B0 -> 0x8534E0`. Microsoft x64 argument/return contracts and owner
services remain unchanged. Services table is `0x1C16C08`, punch table
`0x1C0BFE8`, rules pointer slot `0x2567188`, debug wrappers
`0x25C0C48 / 0x25C0C58`.

This patch changes consumed `C_CSPlayerPawn` members: defusing
`0x1E9A -> 0x1EA2`, shots fired `0x1EB0 -> 0x1EB8`, wait-for-no-attack
`0x1EBC -> 0x1EC4`. Named schema and independently decoded readiness operands
at `0x818DE3 / 0x818E00 / 0x818E33` agree. Native inaccuracy's walking byte
moves `0x1E78 -> 0x1E80`, independently checked against `m_bIsWalking` schema
at code operand `0x80D0C9`. This field is read by the native query rather than
copied directly by the adapter. Remaining consumed weapon/readiness/punch
members retain their values.

The old cached spread helper was recompiled, so its old window cannot be
certified by a byte shift. Native spread distribution `0xD25630` now directly
calls helper `0x7CEA70` from `0xD258D6`; that helper calls cache initializer
`0x7CC0E0` at `0x7CEAA5`. Initializer dispatches builder `0x7CC330` and resolves
item data through item `+0x260` and native query `0x378D80`; it reads named
`m_nNumBullets` at `+0x730`. It stores `(numBullets <= 1)` at cache `+0x604`
and skips cached shotgun construction for that case. Helper `0x7CEA70`
checks the same `+0x604` and uses two independent named `tier0!RandomFloat`
imports at `0x7CEAFD / 0x7CEB17`, with ranges `[0,2*pi]` and `[0,1]`.
Its entire 198-byte body and 84-byte initializer data/gate chain are frozen;
caller targets, NumBullets schema operand and RandomFloat imports are checked
independently too. Negev shaping, debug override names and float constants,
angle basis, shot direction/range and punch evaluator remain checked. The
single-projectile distribution model and supported-weapon policy are unchanged.

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

## October 21, 2026 recertification — client 2000934

Installed Windows AMD64 client `1.41.9.1` / `2000934`, Source revision
`11150591`, Steam build `26087496`, at the same installation below. New client
SHA-256 `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`,
PE timestamp `0x6AD7B41C`, SizeOfImage `0x299D000`.

Before changing the frozen hash, separate scratch
`logs/research/26087496/aim_targets/recheck.py` ran the full `verify()` contract
against the new image. All twenty complete native-window hashes match, along
with twelve named schema registrations, 29 firearm RTTI tables and native
spread/inaccuracy slots, decoded direct calls/member/global operands, named
cvar and special-weapon consumers, imported RNG/trigonometry identities, float
constants, source fields, supported weapon table list and entry fingerprints.
All runtime constants and fingerprints are unchanged; only the exact client
hash pin in `inspect_aim_weapon.py` and header provenance were refreshed.
Unchanged complete callers/callees retain the argument/return ABI, command-owner
invocation and native memoization evidence below.

Final frozen command `python tools/inspect_aim_weapon.py` passes; full output
is `logs/research/26087496/aim_targets/inspect_aim_weapon.json`.

Research `negative.py` rejects four temporary headers: changed `Spread`,
`ActiveWeapon`, `SpreadBytes` and the first supported weapon vtable. The frozen
verifier still rejects unfamiliar client hashes before contract checks. No
client/server was launched, no native getter was invoked, and no live accuracy,
readiness or shot behavior was observed. Earlier build details below are
historical.

## Historical implementation evidence

This concerns the AMD64 Windows client in the documented Steam installation:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
The executable is `bin/win64/cs2.exe`; the investigated module is
`csgo/bin/win64/client.dll`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
timestamp `0x6AD01314`, image size `0x299D000`. The existing installation
discovery records patch 1.41.9.1, client/server 2000933, Steam build 25966348,
x64 native code and the project's D3D11 rendering path.

Before investigation, the combined compatibility pass completed successfully
for all thirteen pre-existing components in
`logs/compatibility/20261016T163107315054Z/`. Research read installed files only;
it did not launch CS2 or modify installed game files. Capstone 5.0.9 from the
existing ignored `logs/research/python-deps/` directory decoded instructions.
No research dependency is added to the DLL or ordinary verifier.

Run `python tools/inspect_aim_weapon.py`. The standard-library verifier freezes
the complete client identity, twenty native windows, twelve named schema
registrations, 29 firearm RTTI tables and their native spread/inaccuracy slots,
the named RNG/trigonometry imports and special weapon-name identities. It
compares every source contract and entry fingerprint in
`include/aim_weapon_offsets.h`; it never silently substitutes new offsets.

## Native spread and inaccuracy

The ordinary fire path calls weapon slot `+0xBD0` at `0x803D41`, then slot
`+0xCF8` at `0x803D55`. All 29 supported firearm RTTI tables resolve these to
`0x80D490` and `0x80C1A0` respectively. RTTI discovery, rather than an assumed
public index list, binds the exact installed class names to each table.

`float spread(weapon*)` at `0x80D490` reads current `m_weaponMode=0x1A00`
and the live subclass-data pointer at `weapon+0x388`. Its mode-indexed
`m_flSpread=0x750` is independently named in `CCSWeaponBaseVData`.

`float inaccuracy(weapon*, float* movementOut, float* airOut)` at `0x80C1A0`
accepts null optional outputs: both ordinary native consumers do this. It
combines the current accuracy penalty, movement contribution, airborne
contribution, turning inaccuracy and additional native terms, with native cvar
behavior and the native final upper clamp. It does not update the weapon's
accuracy penalty. The feature calls this getter on the native command owner;
it does not call an accuracy-update function, change weapon mode, force a
prediction pass or approximate inaccuracy from only a schema field.

`CCSWeaponBaseVData` additionally names `m_nNumBullets=0x730`,
`m_bIsFullAuto=0x72D`, `m_flRange=0x830` and `m_bIsRevolver=0x71E`.
`C_BasePlayerWeapon` names clip count `0x1928`, next primary tick `0x1918`
and its fractional ratio `0x191C`. `C_CSWeaponBase` names recoil index
`0x1A28`, reload `0x1A3C` and burst mode `0x1A2C`; gun schema names remaining
burst shots `0x1F24`.

## Distribution and ray endpoint

`0xD23D7B` calls the native spread-offset producer `0xD23FE0`, passing
current inaccuracy, spread and floating recoil index. Its imported calls are
bound to `tier0.dll!RandomSeed`, `RandomFloat`, `V_cosf` and `V_sinf`.
The default single-projectile model draws independent radii uniformly from
`[0,1]` and independent angles uniformly from `[0,2*pi]`. The radii are
**not square-root transformed**. Its tangent-plane outputs are:

```
x = cos(a) * inaccuracy * r1 + cos(b) * spread * r2
y = sin(a) * inaccuracy * r1 + sin(b) * spread * r2
```

For `weapon_negev` and recoil index below 3, both radii undergo the native
loop: start `i=3`, decrement `i`, square radius, repeat while `float(i)` exceeds
recoil index, then use `1-radius`. The exact special identity is resolved from
the name pointer at `0x223F760`; it is not guessed from a numerical item ID.
The Revolver's `1-r*r` alternative is conditional on mode 1, with its exact
`weapon_revolver` name pointer at `0x223F748`; revolvers are excluded by the
readiness/weapon support policy below.

The native shotgun-pattern producer at `0x7CB227` marks patterns unavailable
for `m_nNumBullets<=1`; its consumer at `0x7CDC25` consequently uses the same
uniform radial/angle fallback for the supported weapons. Multi-projectile
weapons are excluded, so no shotgun-pattern or pellet-combination estimate
is represented as single-bullet hit probability.

The native debug wrappers are independently matched by their registrations:
`weapon_debug_max_inaccuracy` at wrapper `0x25BECA8`, and
`weapon_debug_inaccuracy_only_up` at `0x25BECB8`. Live storage is reached via
wrapper `+8`, then value `+0x58`. The former forces both unit radii to one
after special shaping; the latter forces both component angles to `pi/2`.
The snapshot exposes these booleans so the sampler preserves their model.

`0x887E34` invokes angle-basis helper `0x1742AF0`, with outputs forward,
left and up. At zero roll these are `(cp*cy,cp*sy,-sp)`, `(-sy,cy,0)` and
`(sp*cy,sp*sy,cp)`. Native shot construction `0x887E78..0x887EFA` computes
`forward-left*x+up*y`, equivalent to `forward+right*x+up*y` using
`right=-left`. At `0x887F04..0x887F20` each raw direction component is
multiplied by native range **without normalization**. Thus the modeled segment
ends at `eye + rawDirection*range`; a normalized ray must scale its distance
by `length(rawDirection)` to retain that exact endpoint.

The feature uses its own deterministic samples. It never calls the native
spread-offset producer or the shared engine RNG, so estimating probability
does not reseed or advance native random state. This is a model estimate,
not a prediction of the server's next seed.

## Current predicted recoil

After selecting/interpolating history angles, the shot builder at `0x80E33D`
loads `pawn+0x1598`, calls `0x8525E0(service,out,shotTickFraction,true)`, then
adds the returned three-component punch to its chosen angles at `0x80E35C`.
`C_CSPlayerPawn::m_pAimPunchServices` independently names `0x1598`.

The current-prediction getter `0x8526B0(service,out,true)`, called by the native
pawn wrapper at `0x8CCA30`, evaluates the same predictable/unpredictable
components using native current-prediction time resolution `0x852860`.
`CCSPlayer_AimPunchServices` schema names both base times, angle/velocity
and unpredictable base angle. Runtime validates the exact service RTTI table
`0x1C0A048` and its owning-pawn pointer `+0x38` before calling.

The transitive evaluator `0x84D8B0` may refresh and grow **its own engine-owned
memoization caches** at service `+0x68` and `+0xB0`. This native getter is
therefore not claimed to be transitively read-only. The feature calls it only
on the native command/prediction owner and performs no direct recoil-state or
cache writes. It does not clear recoil, change recoil scale or synthesize a
shot to query punch. Its returned `AimAngles` is copied and checked finite.
The hitchance cone includes current predicted punch; eventual native firing
can evaluate a slightly different subtick/interpolated punch.

## Readiness and lifetime

The current pawn must be the live local source with controller/full serialized
pawn-handle ownership. `C_BasePlayerPawn::m_pWeaponServices=0x12F0` points to
verified `CCSPlayer_WeaponServices` RTTI table `0x1C14C78`; its `+0x38` owner
must be that pawn. Its named active handle `+0x60` resolves through the
existing full serialized-handle entity lookup. Recycled handles, mismatched
identity pointers, stale data, unsupported tables and missing fields reject
the snapshot before native calls.

Native predicate `int primaryCooldown(weapon*)` at `0x94DE60` compares
current predicted tick **and fraction** against the primary deadline. Its
ordinary primary-fire caller is `0x817F56`; the implementation calls the exact
predicate instead of guessing a current tick from wall time or dropping the
fraction. Native current-time helper `float*(out,scene)` at `0x75D250` supplies
the matching game-time domain for weapon-services `m_flNextAttack=0xD0`.
The services' own consumer at `0x902879..0x9028BF` uses this helper and deadline
before choosing its weapon action path.

Readiness additionally requires a nonempty clip, no reload, no freeze period
or frozen pawn flag, active player state, no defusing, no wait-for-release,
and a released preceding semi-auto shot. These are candidate eligibility
checks; the native game remains the final firing authority. In particular,
readiness does not prove a command fired, was received or hit a target.

Supported snapshots cover the 29 installed ordinary single-projectile firearm
tables listed in the header. Revolvers, tasers, shotguns, active burst mode
and a burst continuation reject the snapshot rather than extrapolating
unverified timing/pellet behavior. Scoped/silenced mode 1 is supported through
the same verified current-mode native getters. Empty/cooldown/reload snapshots
can still supply probability, with `ready=false` and a concrete reason.

The implementation revalidates current module/local ownership, complete weapon
handle, identity/data, mode/recoil/ammo, reload and burst state, data policy,
service deadline and aim-punch service immediately after model calls and
before a caller commits a command edit. Native-call SEH boundaries reject
unavailable outputs. No retained engine pointer becomes a cross-command cache.

`aim_weapon_checks` uses allocated standalone entity substitutes and mock native
callbacks. It covers all supported tables, exact ABI arguments, current punch,
both debug flags, readiness, stale/recycled identities, changed mode/data,
native-callback mutations and invalid numeric outputs. These tests do not
establish live CS2 weapon behavior or server hit registration.
