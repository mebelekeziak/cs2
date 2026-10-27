# Grenade prediction native contracts

Targets the Windows AMD64 **client** and its existing D3D11 overlay. Installed
game root is `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`;
executable is `bin/win64/cs2.exe`. On October 26, 2026, `csgo/steam.inf`
reports patch 1.41.9.2, client/server 2000935, Source revision 11162119,
version date October 22, 2026 14:13:16. Client SHA-256:
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`.
Its PE identity is AMD64, timestamp `0x6ADA7340`, image size `0x29A0000`.
Installed files establish this baseline; no fresh loaded graphics/runtime or
in-game trajectory observation is implied.

`python tools/inspect_grenade_prediction.py` verifies the installed schemas,
RTTI class tables, complete native query windows, native launch callers and
the values/fingerprint in `include/grenade_prediction_offsets.h`.
`--header PATH` permits workspace copies for negative verification. It does
not launch/open CS2, inject, contact a server or modify installed files.

## Native practice-preview backend — October 27, 2026, revision 4

This extension uses the same installed Windows AMD64 client and D3D11 overlay
baseline above. `steam.inf` still reports 1.41.9.2 / 2000935 / revision 11162119.
The initial combined compatibility bundle passed all 39 components in
`logs/compatibility/20261027T065307097370Z/`. Research reads installed PE files;
no process was opened, game launched, injection performed or server contacted.
Capstone 5.0.9 was used for discovery from `logs/research/python-deps/`.
Disassembly is in `logs/research/26144732/native-grenade/`.

### Entry points, argument producers and ownership

The named registration `client+0x9A270..0x9A316` establishes
`sv_grenade_trajectory_prac_pipreview` and wrapper `0x24C8F60`.
The client practice producer `0x7E0A50..0x7E0BD3` checks its value, obtains
the weapon's native kind through vslot `+0xD58`, then calls:

1. `void* Create(int nativeKind, void* weapon, float duration)` at `0x10CBA50`.
   The third argument uses XMM2 under Windows x64 ABI; the practice producer
   supplies zero. The factory constructs a real client `CGrenadeTracer` entity,
   sets native `grenade_type` and `tracer_duration` spawn attributes, and installs
   the optional weapon's full handle in the tracer. RTTI establishes the exact
   primary vtable `0x1D0DAA8`; this is not a raw heap buffer to initialize ourselves.
2. The existing launch query `0x7C7940`, with the original verified arguments.
3. `void Simulate(void* tracer, const VectorWS* start, const Vector* velocity)`
   at `0x10CC800..0x10CCC72`.

The independent `cl_sim_grenade_trajectory` console implementation at
`0x10D9630..0x10D971F` calls the same factory with a null weapon, runs the same
simulator and then calls native removal `0xA1C6C0..0xA1C775`. This establishes
the temporary create/simulate/remove lifecycle used by our adapter. Native
removal transfers retirement to the engine's entity lifecycle; it is not a
manual C++ destructor/free or a DLL unload. No picture-in-picture cvar or
practice server setting is changed by the overlay.

`GrenadeKind` is not the native kind enum. Vslot bodies for the six recognized
held classes establish HE=0, flash=1, smoke=4, decoy=3, Molotov/incendiary=2.
The latter retain separate overlay icons and the native launch velocity of
their actual weapon. Factory failure, class mismatch and malformed identity
do not authorize calling guessed cleanup or other entity virtuals.

### Simulation output and clock ownership

The native simulator configures the tracer's native collision property, sets
move type 10 and collision group 16, and invokes its native physics virtual
`+0x498`. The inherited physics dispatcher and tracer collision callback
`0x10D2A80..0x10D2E1E` own sweep/remainder/bounce behavior. Our local path does
not perform its own integration, velocity clipping or collision retries.

The simulator temporarily replaces GlobalVars pointed to by `client+0x2232088`:
four-byte slots `+0x30`, `+0x34`, `+0x44`, `+0x50`, `+0x58`. Its normal loop
restores them. The adapter snapshots these immediately before the synchronous
call and restores the same five slots in `__finally`, including native faults.
It checks that the global pointer still identifies the original source and
does not write a replacement map/lifecycle's globals. Other slots are untouched.

Path storage at tracer `+0x10D0` is a 24-byte vector descriptor containing
24-byte vector descriptors of 12-byte world positions. Descriptor count is
`+0`, data `+8`, capacity `+16`, flags `+20`. Native append at `0x10C98F0` and
segment creation at `0x10D4430` establish these widths and strides independently.
The collision callback starts another segment; copied segment boundaries give
bounce markers at the native sampled positions, not an independently recovered
contact timestamp or exact unsampled contact point. The endpoint is copied
from the native current absolute-origin query `0x219A60..0x219A8C`, after its
scene-cache refresh, and appended even when the last sampled point precedes it.

At most 128 descriptors and 4,096 copied nodes, including the endpoint, are
accepted. Empty output, excessive counts, count/capacity disagreement, unreadable
buffers, nonfinite positions, changed full identity or failed retirement reject
the whole result. Tracer ownership is scoped to a single frame-owner call;
the exact identity/back-link/handle and RTTI class are revalidated before reads
and retirement. The adapter submits removal even after a failed simulation or
copy if that exact owned entity still exists. No engine pointer is published.

The native samples have no per-point timestamps. `GrenadeEnd::NativePreview`
marks a completed engine preview; its point times/final age are unspecified
zeroes. It must not be presented as a measured server detonation deadline.
The native horizon is bounded: HE/flash use 1.5+0.125 seconds, smoke/decoy
10+0.125, fire uses the live Molotov fuse+0.125; low native speed can stop earlier.
The adapter rejects a nonfinite, negative or greater-than-30-second fire fuse
before allocation, preventing an extreme live setting from extending its
synchronous native loop without a usable bound.
The `+0x10CD` byte is native curve-processing state, not a detonation flag, and
is deliberately not used as one. Native preview is not proof of future server
agreement for moving objects, breakables or server scheduling.

### Runtime integration and verification limits

Held paths now use this native backend and retain the existing full-handle
throw handoff/end detection. Native held failure never calls the custom flight
integrator. Native local work takes priority over bounded projectile tracing;
the synchronous native routine itself cannot be preempted by our six-ms trace
budget. Its actual cost remains unmeasured in-game. Completed saved paths need
no repeat simulation after handoff. Reentrant stop disarms publication, lets
the synchronous native call finish, restores its clock and retires its entity.
Stopped callbacks remain resident until normal process exit.

Enemy thrown predictions, and local projectiles discovered without a prepared
forecast, retain the previous estimator. The native API verified here models a
full throw from launch, not a drop-in query for arbitrary partially simulated
projectiles. Logs identify both backends, native success/failure, and
`estimated_local_fallback` rather than labeling every projectile native.

`python tools/inspect_grenade_native.py` verifies the exact client hash, eleven
frozen native windows, independent caller targets, RTTI, kind getters, vector
layout, clock operands and entry fingerprints in `grenade_native_offsets.h`.
It is also called by the existing grenade component in the 39-part combined
bundle. `--header PATH` accepts workspace copies for negative verification.
Native and owner fixtures check aggressive input forwarding, copied output,
cleanup, clock restoration, malformed data and lifecycle rejection. They do not
execute installed engine physics or establish high-speed trajectory alignment.
No in-game native preview has yet been observed for this change.

Final verification: both x64 Release builds pass 80/80 CTest suites with no
project compiler warnings. Native adapter: 79 checks; owner runtime: 163.
All 39/39 compatibility components pass in
`logs/compatibility/native-grenade-preview-20261027/`. All 23 individual native
scalar, kind, clock-slot and fingerprint mutations reject; reports are
`logs/research/26144732/native-grenade/verified.json` and `negative-results.json`.
Both DLLs retain zero static TLS and delay-import directories; exact hashes
are in the adjacent `payload-contract.json`. Build and CTest output is in
`logs/native-grenade-build-{public,diagnostic}.log` and
`logs/native-grenade-ctest-{public,diagnostic}.log`.

## Native launch query

`client+0x7C7940..0x7C7B07` accepts `(weapon,pawn,outPosition,outVelocity,
bool currentAngles)` using the Windows x64 ABI. Native callers at `0x7E0B82`,
`0x7E0BA4` and `0x10D96CF` establish argument order. The query always computes
both outputs. Its boolean return reports whether stashed pawn parameters were
used; **false is not a failed query**. Consumers initialize output vectors and
validate their finite values independently of the return.

The query calls `0x8CDFB0..0x8CE229` to copy the pawn's view angles, eye,
center and velocity. That complete function only queries engine state and
writes its output pointers. With `currentAngles=true`, current weapon-service
view angles replace stashed angles when the stashed origin/velocity are still
valid. Engine getter internals may refresh interpolation caches. Use the native
frame owner and revalidate pawn/weapon identities after the call; retain no
entity pointer in the overlay publication.

The strength selection honors the installed prediction interpolation wrapper
at grenade `+0x1F30`, including the native near-half-strength snap. Treating
that schema field as an arbitrary standalone float misses this behavior. The
query invokes weapon virtual slot `+0xBC8` (all six grenade RTTI classes point
at `0x7CEB60`) for native throw speed, and calls the complete launch builder
`0x7C7B10..0x7C7F1A`. The implementation calls this verified query rather than
reconstructing its pitch, strength, movement and origin recipe.

The builder's launch-origin sweep uses `InitializeShape=0x36E880`,
`InitializeTrace=0x177B560`, `TraceShape=0xA1AE30`, manager global `0x2229020`,
and native filter vtable `0x1AD6848`. Its hull is ±2.02, mask `0x200003001`,
layer 4, and filter byte `+0x39` is `(old & 0xC9) | 0x49`.
This is a launch-origin filter; its layer must not be inferred from the
server grenade spawn function's move-type argument 10.

## Copied source fields and projectile identities

The verifier decodes the installed static registration records: class name
`+0`, class size `+0x18`, field count `+0x1C`, field array `+0x28`; field
records are 32 bytes and their member offset is at `+0x10`.

| Class | Registration / array | Relevant installed fields |
| --- | --- | --- |
| `C_BaseCSGrenade` | `0x221A2A8 / 0x221A0D0`, 14 fields | held `0x1F22`, pin `0x1F23`, throw time `0x1F28`, strength wrapper `0x1F30` |
| `C_BaseCSGrenadeProjectile` | `0x221AA18 / 0x221A800`, 16 | initial position `0x12B0`, initial velocity `0x12BC`, bounces `0x12C8`, effect tick `0x12D8`, spawn `0x12E8`, effect began `0x12FC` |
| `C_BaseGrenade` | `0x2213578 / 0x22133E0`, 12 | live `0x126A`, detonate time `0x1270`, thrower `0x1290`, original thrower `0x12AC` |
| `C_BaseEntity` | `0x21F6E78 / 0x21F5550`, 82 | abs velocity `0x3F8`, elasticity `0x53C`, gravity scale `0x540`, actual gravity scale `0x564` |
| `C_MolotovProjectile` | `0x221A3C8 / 0x221A390`, 1 | incendiary discriminator `0x1348` |

Native `0x7DE3F0..0x7DE434` obtains the projectile's **world group ID** from its
identity `+0x38`, calls existing `CurrentTime=0x75E050(outTime,worldGroup)`, and
copies that float to spawn time `+0x12E8`. This establishes the producer's
clock query, but does not prove that network replicas run that producer or
that this member contains a server throw-birth timestamp. The inherited detonation time
has no established client producer/network population in this investigation.
The runtime therefore uses spawn age and verified grenade-kind timing instead
of treating an arbitrary positive inherited detonation field as a deadline.

RTTI establishes six held grenade classes and five projectile classes. Molotov
and incendiary share `C_MolotovProjectile`; use the copied discriminator.
Full entity handles and identity back-links distinguish reuse of an entity
slot. Resolve a thrown grenade's thrower handle only after recognizing a real
projectile class. Held enemy weapons do not supply predicted throws. Native
client thrower resolver `0x724B70..0x724CB2` first resolves `m_hThrower`, then
falls back to `C_BaseEntity::m_hOwnerEntity` (`+0x520`) if the first handle is
unavailable. Its owner fallback requires a pawn. The native grenade class
association is established by both `C_BaseGrenade` and
`C_BaseCSGrenadeProjectile` vslot `+0x8D0 = 0x7212F0`, whose body calls this
resolver at `0x72131B`. The runtime validates an exact current player-pawn
class and full handle on both attempts rather than calling pawn predicates.

## Static-world physics

Read-only server-file evidence uses SHA-256
`2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c`.
This does not require a running server. Complete spawn, bounce, filter,
collision-setup, gravity and velocity-clip windows are frozen in the verifier.

`server+0x9D8730..0x9D89D1` creates a ±2 hull, sets collision group **16**
and installs interactions-with mask `0x200003001`. The initial interactions-as
mask `0x8000100000` has bit 20 cleared and bit 17 set for this group, giving
`0x8000020000`. Do not mistake the spawn helper's `DL=10` for trace layer:
it is move type. Physics filter `0xBE3250..0xBE33AB` copies interactions-with,
interactions-as and exclude to filter `+8`, `+0x18`, `+0x10`, copies group
to `+0x38`, and sets flags `+0x39` to `0x4B`. The existing generic client
filter adapter reproduces the shared static-world subset; the extended native
physics filter's moving-object callbacks are outside that subset.

The native initial gravity scale is **0.4**, elasticity **0.45**. The actual
effective gravity field is `m_flActualGravityScale`, not the configured scale:
the native setter copies configured gravity to actual unless disabled, when
actual becomes zero. **Zero is valid** and is not replaced with 0.4.
Use the existing verified live `sv_gravity` value with that effective scale.

Grenade bounce override `0x9D3E60..0x9D502F` uses entity elasticity alone
for ordinary world impacts, clamped to `[0,0.9]`. It does not apply the generic
base entity bounce's surface-elasticity product. Native velocity clipping
`0xE94610..0xE9469A` computes
`v' = v + normal * (max(-2*dot(v,normal),0) + 0.03125)`.
Floor normal threshold is 0.7; shallow upward planes above 0.1 can stop a
low-speed grenade too. Rest threshold speed squared is 400, and
fast floor damping begins above speed squared 96000. If normalized reflected
velocity dot normal exceeds 0.5, native damping multiplies velocity by
`1 - dot + 0.5`. Moving entities, players and breakables have additional
native behavior and remain approximate.

Native HE and flash creation establish **1.5 seconds** fuses. Smoke checks
3D speed `<=0.1` and age `>=1.188` before detonation, polling on the next
native tick while unmet; no fixed maximum airborne smoke fuse is assumed.
Decoy first checks at age **2 seconds**, then every **0.2 seconds**, and
detonates at speed `<=0.2`. These are installed native values, not legacy
grenade-prediction constants.

Client named registration `0x990F0..0x99187` establishes wrapper
`molotov_throw_detonate_time = client+0x24C9180`, default **2 seconds**.
Registration `0x9A480..0x9A533` establishes
`weapon_molotov_maxdetonateslope = client+0x24C9190`, default **30 degrees**
with range 0–90. Read live wrappers using the existing cvar pointer `+8` and
float value `+0x58` contract. Convert the slope's degrees to a cosine for
normal comparisons. Native active Molotov fuse checks run each tick.

## Verification and observation limits

Complete frozen windows include the launch query, builder, pawn-source query,
projectile spawn-clock producer and throw-speed getter. Shared native shape,
filter and trace primitives retain their separate third-person and occlusion
verifiers. A matching entry prefix alone is not the launch contract.

The real installed files and header pass. All **53** individual altered
header constants, launch fingerprint and class-table mutations reject;
reports are under `logs/research/26144732/grenade_prediction/verified.json`
and `negative-results.json`. This includes soft-floor/rest timing constants,
client schema fields, masks/groups, cvar wrappers and launch ABI entry bytes.

Static verification and synthetic fixtures establish source/ABI handling and
specified simulation behavior. They do not establish live server agreement or
visible trajectory alignment. Physics interaction with dynamic geometry,
player collisions, breakables and changing surface properties needs explicit
comparison in the user's offline/private environment. No public multiplayer
or local server session was opened for this investigation.

## Enemy capture and endpoint lifecycle follow-up

The user reports the local preview works and enemy thrown trajectories do not
appear. The newest bounded overlay log contains feature initialization and
area snapshots but no per-projectile rejection counters, so it cannot identify
the rejection that occurred. No CS2 process remained when read-only diagnostics
were attempted. The fixes below address verified adapter defects; their visible
result still needs the user's offline/private comparison.

`CEntityIdentity::m_worldGroupId` is unsigned 32-bit at `+0x38` (class registration
`0x226D2F8`, array `0x226D170`, twelve fields). It is not a split-screen slot.
Complete `CurrentTime=0x75E050..0x75E0C8` only distinguishes zero and nonzero
group bits. The native spawn producer passes those bits unchanged. Rejecting a
negative interpretation of `0xFFFFFFFF` has no native basis and can discard a
valid projectile before simulation.

Native absolute velocity query `0x219AC0..0x219B8D` accepts
`(entity,outFloat3)` and returns that same output pointer. For ordinary move
types it calls `0xAE69C0` before copying the cached `m_vecAbsVelocity` field;
that helper checks dirty flag `entity+0x374 & 0x1000` and refreshes derived
velocity. Move type 5 instead queries the native physics object. Reading
`+0x3F8` directly can therefore observe stale cache contents. Use the verified
query on the existing frame owner, validate the copied result and revalidate
entity identity after it returns. The complete query and entry fingerprint are
included in this feature's installed-file verifier.

The generic projectile constructor initializes `ExplodeTick` to zero and
`ExplodeBegan` to false. Native update `0x7D93E0..0x7D955C` dispatches the effect
when the tick becomes nonzero, then sets the began flag at `0x7D940D`.
Neither field is an effect-end timer. Smoke's separate update is
`0x7D9560..0x7D96DC`. Native smoke deployment `0x7D8EF0..0x7D8F74` begins at
`SmokeTickBegin != 0 || SmokeDidEffect != 0`; this precedes its voxel geometry.
Clear a smoke path at that transition rather than following the smoke volume
until dissipation. `SmokeEffectSpawned` is also a deployed-state rejection.

`C_MolotovProjectile` has no verified client deployment flag beyond the common
fields. Its `+0x1350` and `+0x1360` members are particle bindings and must not be
read as booleans. The successful server ignition path in
`0x9BDC90..0x9BE560` creates an inferno, initializes active fire at `0x9BE3DA`,
sets server-only detonation state at `0x9BE50F`, then immediately removes the
projectile at `0x9BE516`. That successful path does not call the generic
explosion tick producer `0x9BE780..0x9BE8E0`. Molotov/incendiary paths must
therefore also disappear when the exact tracked projectile full handle is
removed or recycled; they must not inherit the inferno's lifespan.

The expanded real-header verifier passes and all **59** altered header
constants, both entry fingerprints and class-table checks reject. Follow-up
reports preserve the earlier baseline under
`logs/research/26144732/grenade_prediction/followup/`.

## Repeated-throw retention follow-up

The user reports that the first retained grenade works while subsequent
throws appear inconsistently. In the bounded revision-2 logs reviewed by the
primary agent (overlay logs for PIDs **38356** and **34672**), real local projectile snapshots often
have `projectiles=1 local=1 retained=0 clock_reject=1`; enemy snapshots also
show four observed projectiles with four clock rejects. Trace failure and
budget exhaustion are not the reported rejection mechanism in these samples.
The existing counter combines several branches and records no exact scalar
clock/spawn values, so it does not identify which time comparison failed.

All recognized projectile primary vtables use `+0x48 = 0x7DE3F0`. The known
entity creation batch calls slot `+0x48` with its creation argument at
`0x15C3F42` and `0x15C3FB7`. This supports a client creation callback, not a
networked server-birth contract. The projectile constructor zero-initializes
`+0x12E8` through `0x15B140`. The only other direct writer found in the native
grenade paths is `0x7DE41D` in that creation callback. Neither complete network
data-update body (`0x7D93E0` or `0x7D9560`) initializes this member. No proof
establishes that every network replica passes through the creation callback.
Static schema metadata counts do not settle replication: known replicated
fields also have zero metadata in these templates, and a generic network
deserializer could write a member without a direct field-offset instruction.

The exact current-time implementation also has distinct clock contexts:
nonzero world groups use global current time, while group zero calls the
game-rules clock. `C_GameRules` schema identifies total paused ticks at `+0x30`,
pause-start tick at `+0x34` and the paused flag at `+0x38`.
`0xBAA0A0` dispatches game-rules vslot `+0x220 = 0xBA8740`, which subtracts
paused ticks and clamps time during a pause. This does not prove that a
specific observed reject came from a group or pause change; native spawn and
frame queries use the same function when their contexts match.

Consequently, an observed live local projectile must not lose its already
prepared trajectory merely because the optional spawn-age calculation fails.
Revision 3 associates a saved prepared path with a newly observed full local
projectile handle using the observed owner, kind, frame generation and a
confirmed distinct throw occurrence, before scene/spawn/clock-dependent
resimulation. Native-clock rollback does not erase an already retained path
whose exact projectile is still observed alive. Endpoint retirement continues
to follow the observed explosion/deployment or full-handle removal described
above. This change preserves the existing trajectory physics; it does not
claim an exact server-birth reconstruction or revise the uncached remote-age
model.

## Retained trajectory lifecycle, historical revision 2

The user reports that local prediction looks good and that enemy paths were
absent. The clock and velocity defects above are independently established;
their contribution to that particular run has not been observed because no
client remained running and the earlier revision logged no rejection counts.

The owner performs a complete identity/lifecycle scan before spending its
native tracing budget. Every recognized live grenade is observed even when
another prediction exhausts the six-millisecond tracing window. Unfinished
paths rotate fairly; the first complete copied forecast stays unchanged and
requires no more simulation. A standalone tracker caps storage at 64 full
projectile handles and 4,096 nodes per path. Incomplete scans do not evict
unseen entries. Missing temporary velocity/owner/physics data does not erase
an already complete path whose projectile identity remains live.

Local projectiles are admitted with the local prediction switch, independently
of enemy prediction. A recent prepared forecast can transfer to a matching
local projectile after the cached full weapon handle confirms release through
`PinPulled == 0 && ThrowTime > 0`; the projectile's spawn must follow that
prepared sample. This also permits delayed input readiness after its first
observation. Cancelled previews do not supply a committed forecast. Once
attached, switching weapons has no effect on the retained trajectory.

Estimated fuse/rest times determine the predicted endpoint, never the lifetime
of an already captured full path. Actual generic effect start, smoke deployment
or exact projectile removal/recycling ends the retained path. Thus smoke/fire
paths disappear at deployment/ignition, as the user requested, without following
the later smoke/inferno effect lifespan. Disabling the relevant setting,
changed local source, clock rebase, disconnect/death or cooperative stop clears
state. Publication still expires without fresh owning frames. Stopped code
remains resident until normal process exit.

`GRENADE_PREDICTION revision=2` reports recognized projectiles, local/enemy
admissions, retained paths, input/clock/physics/trace rejections, deferred budget
and prepared transfers at a bounded two-second cadence while active. These
records are diagnostics, not a server acceptance or live alignment claim.


## Repeated-throw handoff implementation, revision 3

The previous cache rearmed a transferred same-weapon path only after observing
`PinPulled == 1 && ThrowTime == 0`. A quick subsequent throw could skip that
40-ms observation window and keep the previous transfer latch. Revision 3
uses each distinct positive native throw time to recognize the new occurrence
without requiring that intermediate sample. Once release is committed, its
saved forecast stays frozen instead of advancing its sample clock each frame.

A full-handle first-discovery frame is recorded even before ownership is
readable. Neutral receipts cannot publish a path or invent an owner. After a
real local owner is known, saved-path handoff uses that first-discovery order,
kind and confirmed release. It runs before scene, spawn-age or velocity input
eligibility and has no arbitrary one-second expiration. A second merge after
capturing a newly released preview handles same-frame projectile discovery
without drawing duplicate forecasts. Existing live projectile stamps prevent
older grenades with delayed ownership from taking a later throw's path.

Native-clock rollback does not clear paths whose exact projectile and end
state remain valid. Genuine local-source changes, actual grenade endings,
removed/recycled handles, settings changes and cooperative stop still clear
state as appropriate. The pure flight/gravity/bounce/fuse calculation and the
uncached remote-age model are unchanged by this repair.


The revision-3 trace budget uses `QueryPerformanceCounter` converted to
microseconds. `GetTickCount64` remains suitable for publication freshness, but
its coarser increments must not measure a six-millisecond tracing window.
Functional fixtures inject a monotonic budget clock; the exhaustion fixture
advances it by eight milliseconds explicitly. This separates correctness from
PC scheduling load without changing production budget length, the bounded
sweep count or any trajectory physics.
