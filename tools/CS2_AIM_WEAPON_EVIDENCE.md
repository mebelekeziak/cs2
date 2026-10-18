# Current weapon model, recoil and readiness — October 16, 2026

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
