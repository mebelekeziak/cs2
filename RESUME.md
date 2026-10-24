# Quantized movement and jumpbug, revision 14 — October 24, 2026

User confirms prior analog autostrafe works well with quantization off and
requests smooth movement with the normal server rule enabled. New path reads
`sv_quantize_movement_input` (verified wrapper 0x2534510), computes one ideal
whole-interval wish and publishes a digital fallback first. Existing anti-aim
command/replay owner upgrades eligible commands to matching world yaw plus
exact Forward 1 / Left 0, retaining physical camera. No server cvar override.
Action commands keep physical view/mouse/actions and digital fallback; its
nonbraking model assumes sampled yaw, so rapid action mouse turns can differ.
Quantization-off steering retains 32 ascent wishes and 1 descent/jump wish.

Both quant representations coalesce actual native sequence-1 axes in the
existing owned time-zero event. No extra boundary allocation; exact restore
precedes movement replay. Scoped requests, action recheck after autofire,
stale sources, fallback publication and physical camera protection use the
shared owner. Independent review caught/fixed missing startup action fallback
handoff gate. Quant mode prioritizes movement yaw over anti aim; AA UI can be off.

Jumpbug now copies native posture bounds/provider, models opposite half-hull
origin changes, chooses an interior native 1/64-tick capture event, and verifies duck
approach, CanUnduck-style full standing sweep/headroom and categorize ground.
Same-time unduck/jump avoids an extra friction interval. Separate cooldown
survives ordinary airborne hop resets; rules read with bhop off too. Native
observer logs new capture/posture data. Quickstop refuses raw subtick Jump taps.

Both x64 Release builds succeed warning-free; each passes 66/66 CTests. All 33/33
static components pass: logs/compatibility/20261024T115852087340Z/. Jumpbug's 435
assertions pass; 19 native windows verified; 33 altered headers and bad quant
wrapper reject. Final DLL metadata: logs/movement-r14-artifacts.json, both
zero static TLS/delay imports. Logs movement-r14-build/tests-public/diagnostic.
No agent client/server launch/injection; gameplay smoothness and damage avoidance
not measured. User subsequently reports substantially improved working movement,
but extreme character-facing twitching and subtle viewmodel twitching. Jumpbug
was not separately confirmed. Restart normally, Load Overlay.cmd; test quantization 1 offline/private.
Stopped code remains resident. Full research/behavior: docs/MOVEMENT_REVISION14.md.

The previous movement continuation follows for historical context.

# Jumpbug investigation, revision 13 — October 23, 2026

Revision 12 is committed as `c885645`. User reports air steering works well at
airaccelerate 32 and hopping improves with Jumpbug off. Do not alter steering
to compensate for missed jumpbug attempts. Tentative predictive ordinary
bunnyhop changes were removed; saved only in
`logs/movement-r13-deferred-bunnyhop.patch`.

New run `overlay-33312-134372389997829026.log`: 82 jumpbug attempt ticks with
native observations, zero same-tick takeoffs. Sample 1645 timed press misses,
1646 release, 1647 ground speed 330.492, fallback 1648 speed 156.210. Ordinary
no-jumpbug run `overlay-27888-134372393324387841.log` also has losses; aggregate
runs are not controlled comparisons. Do not promise all loss is fixed.

Revision 13 removes an extra recovery tick only for an uncorrected timed
request: its full-interval uncertainty margin already covers a press strictly
inside the request tick, so equality with penalty + interval suffices after
observed release and a full released tick. Rewind discards this bound. Builds
and both 58-test suites pass. Native jump telemetry now includes move-data
origin and airborne duck edges; probe logs include copied source bounds/origin.
The installed MoveOrigin +0xC8 operand is independently decoded and crouch
bodies frozen in `tools/jumpbug_contract.json`. No new hook or native call.

Contact timing is STILL unresolved. Native finish-duck/unduck adjusts origin
and categorizes before jumping; the current standing-hull probe alone does
not prove these transitions. New diagnostics support the next user-run test.
No agent client/server launch or injection. Details: `docs/MOVEMENT_REVISION13.md`.

# Movement revision 12 — October 23, 2026

User reported air/landing speed loss, low-speed A/D kicks and uncertain jumpbug.
Reviewed the actual revision 11 run in
`logs/overlay-33704-134372367408887983.log`. Turning math applied negative
velocity projections. Jumpbug reset its planner while publishing Jump down;
the next command omitted release, and 32 analog slices repeatedly restarted
the native jump cooldown. Tick 563 records fresh input at every 1/32 fraction.

Revision 12 uses a non-braking projection solver with no 10/80 speed thresholds,
32 ascent wishes and one full-interval wish on descent or with jump input.
Jumpbug now releases historical input at zero and explicitly releases its
owned press on the next command, independently of bunnyhop. Replay keeps the
chosen event count/timing; ground handoff uses physical keys. Observer outcomes
separate actual takeoff from mere recorded presses. No native offsets changed.

Both Release builds pass 58/58 CTest suites; 29/29 static components pass in
`logs/compatibility/20261023T140008355306Z/`. Build/test logs:
`logs/movement-r12-*`. Full findings: `docs/MOVEMENT_REVISION12.md`.
No new gameplay observation or client/server launch. Jumpbug posture/grounding
prediction is still unverified; do not describe it as reliable or claim damage
avoidance. Restart normally before Load Overlay.cmd; stopped code stays resident.

The previous continuation follows for historical context.

# CS2 compatibility update — October 23, 2026

Installed Windows AMD64 client **1.41.9.2 / 2000935 / Steam 26144732**, Source
revision **11162119**. Steam current-version API reports up to date. Start next
update with `docs/CS2_UPDATE_GUIDE.md` and `docs/RE_ADDRESS_INDEX.md`; full current
record: `docs/CS2_UPDATE_26144732.md`.

All **28/28** frozen checks pass after independent schema/RTTI/export/native
caller rediscovery. Globals/functions/vtables/fingerprints moved nonuniformly;
armor now `0x1ED0`, scoped `0x1EA0`, aim readiness members moved +8. Exact hashes
and bad-input rejection remain enforced. No lifecycle/loader behavior changed.
Initial bundle `logs/compatibility/20261023T131613316408Z/`; final source bundle
`logs/compatibility/26144732-final-source/`; research `logs/research/26144732/`.

Both x64 Release variants build without project warnings; sequential full CTest
runs pass **57/57** each. An obsolete skeleton literal in a synthetic test was
fixed; parallel config-fixture collision cleared on isolated/sequential reruns.
Run the two CTest variants sequentially. Both payloads retain zero static TLS/
delay imports. Final source hashes and whitespace checks pass.

No game/server process was launched, opened or injected; installed files were
unchanged. Live rendering/movement/shot outcomes remain unobserved. Existing
**Load Overlay.cmd** workflow applies; restart client normally if an older DLL
is resident. Cooperative stop does not physically unload resident code.

The previous continuation follows for historical context.

# CS2 compatibility update — October 21, 2026

Installed Windows AMD64 client **1.41.9.1 / 2000934 / Steam 26087496**, Source
revision **11150591**. Steam's current-version API confirms this client is
up to date. Full record: `docs/CS2_UPDATE_26087496.md`; next update still starts
with `docs/CS2_UPDATE_GUIDE.md` and `docs/RE_ADDRESS_INDEX.md`.

All **27/27** frozen installed-file compatibility checks pass. Shared/scoped PE
timestamps and exact client/server hashes were updated only after independent
scratch rechecks. No consumed runtime addresses, schema members, hook bytes or
ABIs changed. Three scope-consumer windows changed only in nineteen native
data-reference displacements; the frozen verifier now checks their new windows
and decoded data identities. Unknown builds and wrong headers still reject.
Initial bundle: `logs/compatibility/20261021T052237287897Z/`; final bundle:
`logs/compatibility/26087496-final/`; research: `logs/research/26087496/`.

Public and diagnostic x64 Release builds succeed without project warnings,
each passing **56/56 CTest suites**. Both DLLs retain AMD64 PE32+, an exception
directory and zero static TLS/delay imports. Build/test logs and payload hashes
are under the research directory. Final bundle source hashes match current files.

User's existing visualization/menu/config edits were preserved. No game/server
process was opened, launched or injected and no installed files changed. Live
behavior on this build remains unobserved. Use existing **Load Overlay.cmd**;
restart the client normally if an older DLL remains resident. Cooperative stop
does not physically unload resident code.

## Historical auto-bhop correction — October 16, 2026

Revision 9 fixes observed contradictory jump decisions during prediction
corrections in private online play. Same-sequence command tick/TickBase changes
now retain the selected native/subtick edge. New-command simulation rollback
preserves pending release and restarts waits, discarding old processing proof.
Latest revision 8 log has 18 sequences with opposite edges; the exact
4728/4729/4730 trace and limitations are recorded at the top of
`tools/CS2_MOVEMENT_EVIDENCE.md`. User reports ordinary settings, roughly 10 ms.

Both x64 Release variants build and pass 18/18 CTest suites. All ten static
compatibility checks pass. Installed metadata is now client/server 2000933,
Steam build 25966348; consumed client DLL hash and offsets remain unchanged.
No client/server run was performed. Restart normally and use Load Overlay.cmd;
isolate with F9 off first. Logs identify revision 9 and correction counters.
Repeated corrections can postpone retries; arbitrary historical command replay
is outside the existing single-entry cache. Work is implemented and tested,
with private-session gameplay confirmation still pending.

## Previous compatibility continuation — October 15, 2026

For subsequent updates, start with `docs/CS2_UPDATE_GUIDE.md` and
`docs/RE_ADDRESS_INDEX.md`. `python tools/check_compatibility.py` runs all seven
frozen checks independently, recording installation/module identities, source
hashes and per-component diagnostics in a dated `logs/compatibility/` bundle.
It reads installed files without opening a game process or changing offsets.

User requested current-build ESP/autobhop compatibility and a commit before
changes. Checkpoint **`d69477f`** preserves the clean project at `d69e168`.
The installed client is 1.41.9.1 / 2000932 / Steam build 25947352; Steam's
UpToDateCheck API reports it current. The user's latest run
`logs/overlay-26684-134365408103402107.log` confirms the old ESP/movement identity
checks rejected the new client. The user confirms world and lighting work.

Current changes recertify ESP globals/schema/entity layout and movement input,
helpers, Cvar wrappers, constants and consumed layouts from local installed PE
files. Armor changed from 0x1ECC to 0x1EC8. Other consumed player/schema field
offsets remain unchanged. Both x64 Release variants build and pass 16/16 CTest
suites. `tools/inspect_cs2.py` now verifies static evidence and source headers;
its `--header` option verifies an alternate header, rather than generating one
from the historical dump. `tools/inspect_movement.py` verifies current movement
constants against frozen machine evidence. See their evidence documents.

No client launch, injection or server interaction was performed for this update.
ESP output and actual hops on 1.41.9.1 still need an offline/private observation.
Use **Load Overlay.cmd** with the rebuilt public DLL, with F9 off first to isolate
autobhop. Keep stopped code resident until normal exit as documented in README.

The world follow-up below is historical; its ESP/movement warning is superseded
by the compatibility work above.

## Earlier world visuals continuation

The bounded implementation and standalone verification are complete. No game
was launched or injected during this follow-up. See README for current controls,
compatibility and limitations.

## Saved state

- `70c0544`: baseline committed before this follow-up, as requested.
- `6ef58ef`: paused work from October 14, before full integration verification.
- User previously confirmed sky color/brightness; flat materials had no visible
  effect. Old counters logged 9,160,911 callbacks and no changed packets, but
  omitted early exits and cannot identify the precise blocking guard.

## Completed

- Removed the unjustified flat-material view-bit filter and added bounded
  stage/failure telemetry. One Flat world textures checkbox controls the feature.
- Removed unsupported surface tint UI/settings: aggregate draws never consume
  its old packet RGB writes. A real material-constant implementation remains
  future work, documented in tools/CS2_SURFACE_TINT_EVIDENCE.md.
- Lighting tab exposes local-light color/brightness and removal, sun
  color/brightness, and native directional-lightmap mode. Defaults are neutral
  and disabled. Lighting reset preserves other world settings.
- Native modules are integrated into preparation, activation, stop and logging.
  Partial activation and resident cooperative stop were independently reviewed.
- Menu checks now use real mouse input at submitted item bounds. World/Lighting
  previews, including narrow tabs, were inspected.
- Full public and diagnostic x64 Release builds succeed; all 16 CTest suites
  pass in each. Five combined installed-binary verifiers pass; report is
  logs/world-verification-20261015.log.

## Version and next observation

Installed CS2 updated to patch 1.41.9.1, client/server 2000932, source revision
11131657, Steam build 25947352. World/sky/lighting code fingerprints, operands and
metadata were recertified from installed binaries. Shared ESP/movement offsets
remain at 1.41.9.0 and will reject the new client; update these separately with
fresh evidence before expecting those features to run.

Repaired flat materials and new lighting still need an authorized offline/private
visual observation. Inspect the new material-stage counters if flat mode remains
unchanged. Native controls affect their corresponding light contributions;
baked illumination/shadows are not recomputed. Stopped code remains resident
until process exit.

## Optional later work

Surface tint requires shader-specific authored-material constant handling and
verified GPU resource/cache lifetime. Generic artist g_vColorTint is CB0 byte32;
lightmapped shaders use different layered fields. A generic frame parameter or
packet RGB write does not implement it. Reproducible shader investigation is
in tools/inspect_surface_tint.py; official ValveResourceFormat20.0 tooling,
license and extracted evidence remain in ignored logs/research paths.
