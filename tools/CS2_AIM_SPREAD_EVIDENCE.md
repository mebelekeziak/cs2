# Indexed primary-shot spread compensation contract

Investigated October 23, 2026 against installed Windows AMD64 client 2000935,
patch 1.41.9.2, Steam build 26144732. Installation is
`C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game`.
Client DLL is `csgo/bin/win64/client.dll`, SHA-256
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`;
`bin/win64/tier0.dll` SHA-256 is
`f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d`,
PE timestamp `0x6ADA3248`, image size `0x401000`.

`python tools/inspect_aim_spread.py` verifies both complete module hashes,
complete native windows, independent producer/consumer operands, named
imports/exports, the named pattern cvar, all source constants and runtime entry
fingerprints. It accepts `--game`, `--client`, `--tier0` and `--header`.
Research scripts and frozen output are in
`logs/research/26144732/remove_spread/`. Installed files were read only: no
CS2/client/server process was launched, no installed file was changed and no
native gameplay function was invoked during this investigation.

## Exact seed inputs and angle dependence

Ordinary shot caller `0x804B8B..0x804B99` passes the selected shot's integer tick
and its effective (history plus scaled punch) angles into `0xD24D70`.
Microsoft x64 ABI is:

```cpp
uint32_t Seed(void* unusedPawn, const AimAngles* effective, int shotTick);
```

RCX is not consumed by this body. RDX points to the copied angle structure and
R8D contains the integer tick. Seed generation uses only local stack state and
named `tier0!CSHA1::Reset`, `Update`, `Final` imports. It does not reseed the RNG.
Pitch and yaw each pass through `0xD1DF80`: normalize via `0x35B270`, multiply by
two, floor via `0x1A0B258`, multiply by 0.5. Normalization preserves the closed
interval [-180,180], including both endpoints. The helper hashes exactly twelve
bytes: native float32 quantized pitch, native float32 quantized yaw and native
little-endian int32 tick. `CSHA1::Final` writes ordinary big-endian digest bytes
at context+0x64 (`tier0+0x59F69..0x59F8C`), and the client reads the first four
bytes as a little-endian uint32.

The outer shooter path increments the hash result by one at `0xD25178` before
passing R9D at `0xD2538D` into distribution `0xD25630`, which calls named
`tier0!RandomSeed`. Thus the stream seed is the native Seed return **plus one**,
wrapping at 32 bits. Changing an effective angle across a half-degree bin changes
the seed. A solver must check the seed for its final command-plus-punch angles;
compensating an initially sampled offset without checking this dependence is
incorrect.

## Indexed history time and native punch

The protobuf descriptor at `0x1BCE648` names `player_tick_count` (field 6) and
`player_tick_fraction` (field 7). Their producer `0xCFBD6A..0xCFBD80` sets
presence bits 11 and 12 in history+0x10, stores integer tick at +0x68 and float
fraction at +0x6C. The native indexed shot consumer independently reads those
same fields at `0x80E79C..0x80E7A6`. Require both presence bits (0x1800), a valid
native primary index and a finite fraction in [0,1) for this copied adapter.

```cpp
struct TimePair { int tick; float fraction; }; // 8 bytes
int* TickConvert(int* out, int globalHistoryTick, int scene); // 0x75C350
TimePair* CurrentTick(TimePair* out, int scene);              // 0xAAEA70
AimAngles* ShotPunch(void* service, AimAngles* out,
                    const TimePair* shot, bool scaled);    // 0x853410
```

The weapon identity's already-verified +0x38 scene selects the time domain.
`TickConvert` preserves nonzero scenes; scene zero converts through the native
global/scene tick offset and correction policy (`0xBA8690`). `CurrentTick`
combines the exact native scene tick query `0x75DFF0` with current predicted
fraction at the native globals+0x50, then normalizes the pair. This is the
tick/fraction domain corresponding to the existing float `AimWeapon` time
query; converting that float back to a tick would lose information.

At `0x80E7BD..0x80E84C`, the indexed consumer clamps its normalized converted
history pair lexicographically to `[CurrentTick - 4 ticks, CurrentTick]`.
The lower bound retains the current fraction. The clamped integer tick is the
seed input. This is not a raw command sequence or a render tick.

The shot builder supplies a **pointer** to that pair in R8 at `0x80F17C`, and
calls `ShotPunch` at `0x80F17F`, with R9B=true. The service preserves the pointer
in RSI and forwards it to both native punch evaluators. The returned scaled
punch is added component-wise to history angles by `0x7FEB20` at `0x80F18C`.
Use this shot-time punch when deriving the seed and bullet basis. Calls remain
on the command/prediction owner, with the existing pawn/service ownership and
lifetime checks; the evaluator can refresh its own engine memoization caches.
The weapon shot-builder itself is not called by this adapter, because it writes
weapon state and consumes more historical state than this feature needs.

Held primary fire without a native attack index uses retained/interpolated
history and a separately chosen timestamp. That path is not represented by this
indexed contract; guessing its tick from a command number would not establish
the same seed. Neither player nor render timestamps need to be rewritten.

## Isolated native random stream and draw order

These exact named tier0 exports bind the independent stream:

```text
??0?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAA@H@Z
?RandomFloat@?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAAMMM@Z
```

They resolve to `0x15E7A0` and `0x15E880`. Call with these x64 signatures:

```cpp
void* RandomCtor(void* ownedState, int seed);
float RandomFloat(void* ownedState, float low, float high);
```

State is 0x88 bytes: int idum at +0, int iy at +4 and shuffle[32] at +8.
The constructor initializes idum/iy. The shared generator at `0x15E1B0` initializes
the shuffle before use. Own and zero the full aligned state; do not call global
RandomSeed/RandomFloat. The global and local float functions both call this same
generator; both cast its returned integer to float, multiply by 2^-31, cap at
`0.9999998807907104`, then perform range multiply/add. No native global random
state is read, advanced or reseeded by calls using the independent object.

Distribution `0xD25630` draws radius A in [0,1], then angle A in [0,2*pi]. Its
second component has two possible draw orders. The named cvar
`weapon_accuracy_shotgun_spread_patterns` is registered at `0xF59AC`, wrapper
`0x25C0C78`. Read its live value through wrapper+8 then storage+0x58. When false,
the direct fallback draws radius B then angle B. When true, the verified
single-projectile helper `0x7CEA70` draws **angle B then radius B**. The supported
one-projectile policy makes its cached shotgun branch unavailable. This cvar
therefore matters even for the SSG 08's exact sequence.

Each radius is shaped for the Negev before multiplication by the corresponding
native inaccuracy/spread. Debug max-radius overrides both shaped radii with one;
debug fixed-angle overrides both angles with pi/2. **All four random draws still
occur with either override enabled.** First-component shaping precedes the
second-component draws but consumes no RNG. The existing verified formula is:

```text
x = cos(angleA) * inaccuracy * radiusA + cos(angleB) * spread * radiusB
y = sin(angleA) * inaccuracy * radiusA + sin(angleB) * spread * radiusB
direction = forward + right*x + up*y
```

No square-root radial transform is present. Revolver alternate mode, multiple
projectiles and burst continuation retain the existing unsupported policy.

## Limits

This establishes a deterministic **client indexed-shot model** from installed
files. Server receipt, authoritative weapon state, event timing and hit
registration have not been observed. Native prediction can advance between the
history edit and actual firing. Finite bounded bin search may find an improving
candidate rather than exact compensation; the final angle/seed must be checked
together and failure must leave the original manual shot available. Do not
describe this as proof of a server seed or guaranteed airborne hits.

## Static verification result

Revision 7 selects one of the retained copied clock/recoil models for production
solves, using indexed-versus-clamped tick lead. Both native snapshots remain
available and the original clamp still participates in freshness checks. This
policy is a client prediction choice, not new proof of the authoritative firing
clock. Native functions, addresses, timing conversion and seed contracts below
are unchanged. See [revision 7](../docs/REMOVE_SPREAD_REVISION7.md) for capture
evidence, latency changes and offline comparison limits.

Revision 5 (October 24) independently retraces the installed indexed branch and
ordinary firing caller. The selected protobuf angle is copied verbatim before
punch; nonzero history fraction does not interpolate its angle. Read-only
decoded evidence is in `logs/research/26144732/spread_revision5/native/`.
Pistol log replay establishes the same timing uncertainty previously observed
for Scout, so all supported indexed firearms now use two copied hypotheses.
The native normalization/clamp and seed contracts above are unchanged. The
per-solve sample cache applies only to exact canonical float-bin/tick keys;
noncanonical angles still call the native helper. See
[revision 5](../docs/REMOVE_SPREAD_REVISION5.md) for implementation, regression
coverage, performance tradeoffs and observation limits.

The frozen verifier passes the installed client and tier0. Twenty-three
independent deliberate mutations covering every header constant and runtime
fingerprint are rejected; results are preserved in `negative.json`. Runtime
integration, solver tests, builds and any user observation are separate evidence
and are reported by the implementation task.
