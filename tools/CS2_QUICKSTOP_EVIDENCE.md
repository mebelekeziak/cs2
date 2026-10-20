# Quickstop implementation evidence

Scope: Windows AMD64 CS2 client command pipeline, October 20, 2026.
No dedicated server, server plugin, new hook, signature or entity offset is
introduced. No installed game files are modified. All new verification uses
synthetic native command allocations; live movement feel remains unobserved.

## Existing native contracts

The installed client and executable match the previously verified installation:

- Game root: `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
- Client SHA-256: `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
- Executable SHA-256: `8614097aaa2761b0a7157c0045861713e769fb1e8235f50fc68f411294dd60cf`.
- AMD64 PE32+, MSVC project runtime and existing D3D11 overlay. The feature edits
  movement on the native command owner and does not use graphics interfaces.

`python tools/check_compatibility.py` passed all 22 independent components in
`logs/compatibility/20261020T121756416713Z/`. The recorded PE inventory, frozen
instruction checks and module hashes confirm existing movement, aim, weapon,
autofire and occlusion dependencies. No client process was launched or opened.

Sources of native evidence:

- `include/movement_offsets.h`, `tools/CS2_MOVEMENT_EVIDENCE.md`: local pawn
  velocity, ground/normal walking/water state, full entity identity, movement
  services, normalized command axes, buttons and subtick arena/container APIs.
- `tools/CS2_ANTI_AIM_EVIDENCE.md`: physical camera-space restoration, historical
  axes correction, native command finalization and time-zero boundaries.
- `tools/CS2_AIM_WEAPON_EVIDENCE.md`, `tools/CS2_AUTOFIRE_EVIDENCE.md`,
  `tools/CS2_AIM_OCCLUSION_EVIDENCE.md`: native accuracy/readiness, attack recorder
  and freshly validated direct-path tracing.

## Aim policy

`SilentAimSettings::quickstop` defaults false and occupies unused bit 45 of the
existing atomic snapshot. `aim.quickstop` persists independently of autofire;
missing fields in older presets restore false. The Aim checkbox is editable
when aim and autofire are enabled.

An automatic-fire candidate remains subject to the existing full geometry,
source, FOV, weapon and occlusion checks. If no candidate currently meets the
configured hitchance, selection may retain the nearest angular, clear, ready,
in-range candidate below threshold for braking. A candidate that can fire now
has priority. Missing geometry, stale state, blocked paths and unsupported
weapons never initiate a stop.

Each preparation samples current accuracy. Braking is a request to future
movement integration, not a fabricated improved cone. Below-threshold commands
request no attack. The next command fires only if its real sampled hitchance
meets the ordinary minimum. No hidden extra percentage margin is added. A
matching active cycle retains braking on the firing command so user acceleration
does not immediately undo the stop. Cycle identity includes local source,
serialized local/target handles, weapon pointer/handle and aim settings.

Any non-ready weapon ends the cycle, including primary or weapon-service
cooldown, reload, empty clip and semi-auto release waits. After cooldown, a
remaining valid target can start another stop. Losing focus/target/path,
changing identities or settings, and disabling the feature release movement.
An unavailable required movement transaction prevents requesting an automatic
shot. Manual fire still follows the existing manual history path.

## Braking and ownership

`QuickstopBrakeAxes` projects the opposite observed horizontal velocity into
the physical view basis: forward `(cos yaw, sin yaw)`, left `(-sin yaw, cos yaw)`.
Magnitude is full input at speed >=30 units/s and proportional below it;
speed <1 uses neutral axes. These are feedback tuning values, not native
movement constants or a claimed exact acceleration/friction simulation.
The feature cannot guarantee that a hitchance threshold is reachable at rest.

Finalizer order is anti-aim restoration, quickstop restoration, ordinary
movement replay, aim/quickstop preparation, anti-aim emission, native CRC.
Braking operates independently of the Space-gated bunnyhop/autostrafe planner.
It is limited to alive, grounded normal walking with current full pawn and
controller ownership, valid movement services and finite velocity. Held jump,
air movement, swimming, water-jump, frozen state and non-walk movement do not
receive braking.

Command transactions align native directional buttons with base analog axes,
sanitize only directional/analog portions of existing events while braking,
and insert an owned time-zero transition from effective historical axes.
Attack, jump, camera deltas, original action order and protobuf allocation
metadata remain native. Release handoffs preserve physical movement input.
Restoration validates active event payloads and identities before writing;
unresolved owned output prevents further command edits. Serialization may
refresh cached sizes. Engine arena allocations can grow while logical events
are removed, so restoration checks current logical ownership rather than
assuming the original pool representation remains fixed.

No initialization or teardown is added to DllMain. Cooperative stop disarms new
work and leaves stopped code resident until normal process exit. A rebuild
requires the existing restart-and-load workflow; resident is not physically
unloaded.

## Verification

Public and diagnostic x64 Release builds succeed with all 48/48 CTest suites
passing in each. `aim_pipeline_checks` runs 171 checks. `quickstop_checks` covers
camera-basis braking/taper, unrelated action preservation, serializer changes,
1,000 reapply/restore cycles, anti-aim pool growth, timed cooldown movement,
no-boundary restores, retained partial rebuild events/directions, invalid timing,
allocation/append rollback and pooled sequence reuse. Settings/config/menu suites
cover defaults, persistence, older presets, malformed fields and mouse controls.
Normal and narrow software-D3D11 previews were inspected in
`logs/quickstop-menu.bmp` and `logs/quickstop-menu-narrow.bmp`.

`logs/quickstop-payload-check.json` records final DLL SHA-256 values and AMD64
PE32+ payload directories: both static TLS and delay imports remain zero.
Full test outputs are retained in each build's `Testing/Temporary/LastTest.log`.
Standalone checks establish policy, command ownership and UI behavior; they do
not establish native server hit registration, live deceleration time or in-game
movement feel. No CS2 client or server was launched.
