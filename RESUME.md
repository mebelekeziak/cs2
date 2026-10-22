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
