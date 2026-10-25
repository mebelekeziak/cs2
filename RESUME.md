# Shot misses and native hitchance experiment — October 25, 2026

Latest request: investigate consecutive misses diagnosed as saved-capsule
crossing/outside cone, plus separate native spread-source hitchance experiment.
Work preserved all pre-existing uncommitted R5/R6/R7 and movement changes.

Six recent logs:951 native shots=438 hit/245 miss/268 unconfirmed;48 crossed
capsule(25corrected/23normal),8 outsidecone(all normal Scout;none latest3).
Four outsidecone examples have changed saved aim vs nearby earlier pub;
9276#44 earlier yaw110.110 saved109.0704 impact within earlier0.1851deg cone.
Old throttled pub logs omit seq so association is not proven. Reproduced/fixed
normal indexed history being reevaluated/recaptured after publication: it now
shares completed publication retention/replay/consumption/retry with corrected
shots. Scratch build disabling only this repair fails7 regressions;fixed488/488.

New monotonic publicationId distinguishes duplicate captured model from genuine
same-sequence replacement. Tracker freezes same/older nonzeroID, newerID resets
timeout/withdrawn/reported state preserving existing native ack semantics.
Unidentified preview refresh remains. AIM_PUBLICATION revision8 unthrottled per
capture records seq/ID/time/index/origin/aim/native sampling;AIM_SHOT_DETAIL adds
publication_id. Impact-angle cross/dot atan2 fixes old float-unit acos error
of~0.00965deg on identical rays. Tracker202/202 checks.

Corrected evidence:13964#138 seq2332 impact matches prediction~0.000050deg,
crosses saved head but no damage,stationary. Latest3 independent SHA/ran1 replay
118/118 seeds and predicted rays within0.00000693deg.43unambiguousScout misses:
selected clock/punch best42/43;clamp rollback worse39/43. Current-vs-historical
origin lead:back velocity/64 reduces median .682568->.330636deg but helps26/43,
worsens17;no blind rewind shipped. Native firing historical/controller-adjusted
time ownership and target/server pose timing remain unestablished. Not all
crossed-capsule misses fixed. Reports/scripts/hashes in logs/research/
misses_20261025/ and corrected_misses_20261025/.

Separate bool nativeHitchance(defaultfalse,bit63,config aim.native_hitchance),
public Native RNG experiment control disabled while Remove spread on but keeps
preference. With Remove spread off experiment uses real private native RNG
seeds1..N plus verified copied single-projectile distribution;native result
drives ranking/gating,baseline/native threshold diagnostics retained. Prepared
once per decision. Failure invalidates experiment(no hidden baseline autofire).
Halton default and indexed solver unchanged. Does NOT call full client spread
helper(shared RNG/cache/TLS). Native samples not intrinsically superior:
analytic50.0025% case at1024 Halton50.0977%,native52.9297%.

Final both public/diagnostic x64 Release build no project compiler warnings,
CTest68/68 each;native18546 synthetic,37098 installed-tier0 checks. Baseline
compatibility33/33 logs/compatibility/misses-20261025-baseline/. Both DLLs zero
staticTLS/delayimports;hashes logs/misses-20261025-payload-contract.json.
Logs misses-20261025-{build,tests}-{public,diagnostic}.log. git diff--check clean.
No client/server launched/injected, no installed files/settings changed.
Restart normally then Load Overlay.cmd;baseline first,compare native with
Remove spread off. Subsequent user report: substantially better behavior and
essentially no random misses so far;later confirms clean behavior with native
RNG both ON and OFF,also briefly tried Remove spread on/off. Native sampling
not required for improvement;shared publication repair stronger explanation.
No exact configuration matrix/counts/rates or independently audited post-fix
capture. Native option stays experimental/defaultOFF;no advantage established.
Remove spread still bypasses statistical sampling. Details tracked in
docs/SHOT_MISSES_20261025.md and docs/NATIVE_HITCHANCE_EXPERIMENT.md.

# Autofire delay, attempt spam and firing movement, revision 7 — October 24, 2026

Latest user requests: console spam (`AIM_SHOT cmd #15481`..`#15515`), excessive
shooting delay, and remove firing-related autostrafe/Jumpbug restriction.
Immutable R6 capture audit: 287 corrected pubs,236 native_primary_rebuilt
retirements0-16ms later;0 retired pubs match native fire vs44/51 nonretired.
Continuous123 accepted-pub burst lasts1.984s before native fire. Pasted35 lines
are command attempts flushed at lifecycle transition, plausibly normal round
end; not35 fired bullets. 2360/2647 searches rejected by old dual-clock gate.
Audit/snapshot/hash: logs/research/26144732/spread_revision7/log_audit/.

R7 implemented: record clip/recoil at publication, latch consumption on every
valid weapon sample including RefreshShot and ModelUnchanged. Native-primary
reset with no observed consumption may retry immediately at Prepare from
current base view/native recorder. Post-History waits for Prepare. Retry
tombstone persists until replacement acquisition succeeds, protecting live old
indexed correction through missing-target/cooldown/manual-index failure.
Observed consumption survives rollback. Physical takeover, consumed shots and
real restore/CRC failures retain ownership guards. Exact serialized actor/seq/
weapon withdrawal suppresses obsolete attempt settlements, preserves context
for late native events; fresh publication can reuse seq. Remaining cmd-only
console output follows existing Verbose; actual native shot/hit stays visible.

Production spread now picks one retained clock/punch: converted tick ahead of
clamp selects alternative; >1tick uses raw/current punch. Original clamp still
freshness-checked. Seed1 routes selectedVariant0/1 to corresponding time/punch;
generic dual models remain test/benchmark only. Telemetry revision7 reports
chosen clock/punch+native clamp; automatic4096/manual8192 and cache preserved.
Tradeoff: choosing wrong client clock can miss; no authoritative seed claim.
4096 offline70cases: saved capsules22->64,callbacks median4096->626,native SHA/
RNG2683->273,solver median2.198->.353ms,p953.717->2.479ms(11reps).
34olderScoutcases explicitly assume nexttick; timestamped36cases15->33;
pistols6cases1->3. Not live FPS/hit rate.

Firing movement: anti_aim.cpp MovementReady ignores attacks but retains Use;
anti_aim_command allows held/indexed/timed primary/secondary through quantized
orientation. Primary yields AA pattern, preserves physical pitch/nearest
physical-facing movement basis. Attack fields/history and Jumpbug timed Duck/
Jump edges preserved. Jumpbug had no direct attack gate; Quickstop already
yields airborne/jump. No movement.cpp/timing changes.20 regressions fail before
fix, focused5/5 pass including435Jumpbug checks.

Final public+diagnostic x64 Release build no project warnings,CTest67/67 each;
pipeline474/native467/search738/pistol44/tracker198/adapter128/console67 checks.
Compatibility33/33 logs/compatibility/20261024T135615263957Z/; both DLLs zero
staticTLS/delayimports. Build/test logs remove-spread-r7-{build,tests}-
{public,diagnostic}.log; payload hashes logs/remove-spread-r7-payload-contract.json.
Root reviewer found/fixed History-only consumption gap and early-tombstone
removal; rereview no additional actionable issues. No native offsets/installed
files/settings/hooks/loader changes, no game/server launched/injected.
Restart CS2 normally then Load Overlay.cmd; stopped DLL remains resident until
normal exit. Live delay/FPS/accuracy/movement coexistence remains unobserved.
Detailed tracked docs: docs/REMOVE_SPREAD_REVISION7.md. All R5/R6/R7 work is
still uncommitted; preserve it and unrelated current edits.

# Remove spread regression, revision 6 — October 24, 2026

User reports revision 5 stops the entire ragebot and visualization after
enabling Remove spread; toggling it off does not recover. Logs 19784 and 11208
confirm active=0, "autofire ownership changed; stopped" immediately after a
completed-publication rejection with empty history, index -1 and buttons 0x8.
Callbacks continue but aim/fire counters freeze. Failing corrected sequences
2235/2461 have no observed native fire; earlier corrected sequence 1998 fires
and hits. Exact audit is logs/research/26144732/spread_revision6/log_audit/.

Root cause: the revision 5 replay rejection strict-cancels a copied completed
request before normal native rebuild ownership classification can retire it.
Native primary replacement then becomes a permanent component stop. Also its
early Prepare return skips refreshing the 100 ms point preview.

Fix implemented: optional retainCompleted in existing rebuild helper keeps
only unchanged, fully released owned pulses; normal button replacement and
physical takeover retire without writes. Completed replay uses that classifier
and normal safe retirement, with CRC deferred to the original finalizer during
Prepare and repaired immediately after History only when command bits changed.
Retired sequence markers prevent repeat auto requests, including through a
temporary unavailable weapon model. Prepare retains preview-only scans for
completed/retired shots. Pending cancellation and real failure stops remain.
New AIM_SPREAD_REBUILD revision=6 and retired diagnostic counter identify paths.

New fixture reproduces eight revision 5 failures before the fix. Focused checks
now pass 452 pipeline and 89 command-ownership assertions. Includes native empty
reset in both phases, subsequent shots, off/on recovery, physical takeover,
preview refresh, transient model loss and genuine checksum failure behavior.
Full verification results follow in docs/REMOVE_SPREAD_REVISION6.md.
Final public and diagnostic x64 Release builds pass 67/67 CTest suites each,
without project compiler warnings. Compatibility passes 33/33 in
logs/compatibility/20261024T133137220121Z/. Both DLLs have zero static TLS/delay
imports. Logs: remove-spread-r6-{build,tests}-{public,diagnostic}.log; hashes/PE
inspection: logs/remove-spread-r6-payload-contract.json.
No native offsets, hooks, installed files/settings, loader or launch changes;
no game/server was launched, joined or injected. Restart CS2 normally before
Load Overlay.cmd; revision 5's already stopped resident DLL cannot be replaced
with a menu toggle. New live private/offline recovery remains unobserved.

# Remove spread, revision 5 — October 24, 2026

User reports Deagle/Autofire worst misses, inconsistent Scout results during
movement, and major FPS drops with Remove spread on. Initial worktree clean.
Implemented shared two-clock coverage for supported indexed firearms (formerly
Scout only), copied successful-publication replay to avoid later shot penalties
and compounding corrected manual angles, plus owned automatic cancellation for
unverified rebuilds. Exact original indexed history rebuilds restore the copied
correction; original shot metadata remains. Source, settings, weapon, scene and
lifecycle changes invalidate reuse.

Performance: skip unused statistical cone preparation/estimation in this mode;
analytic yaw intervals preserve original search ordering/results; per-solve
canonical float-bin/tick cache reuses native samples. Automatic search stops
at a conservative interior tolerance and uses a 4096-sample maximum; manual
retains 8192 and precise tolerance. Both final capsule checks remain required.
Smaller search may defer Autofire. Actual-adapter offline benchmark, 70 dual
states/19 repetitions: p95 5.986 ms at 8192 -> 3.080 ms at 4096; saved
both-capsule intersections 26 -> 22. Synthetic native callbacks, not live FPS.

Three unambiguous Deagle misses replay from 5.925/11.063/4.278 degrees of
mismatch to .089/.748/.011 degrees at raw ticks. Glock sequence 1848 was
rewritten after clip 8 -> 7, recoil 0 -> 1, accuracy .14672 -> .20352. The new
six-shot pistol fixture retains hashes/source lines and reproduces every old
seed/ray first. Native indexed angles are copied verbatim: no fraction-angle
interpolation or invented future-accuracy interval was introduced.

Both Release builds pass 67/67 CTest suites without project compiler warnings.
Focused counts: 433 pipeline, 738 search, 169 adapter, 37 pistol assertions.
Both DLLs are AMD64 with zero static TLS and delay imports. Initial and final
33/33 compatibility bundles: logs/compatibility/20261024T124041948447Z/ and
20261024T130324406480Z/. Logs: remove-spread-r5-{build,tests}-{public,diagnostic}.log;
payload hashes: logs/remove-spread-r5-payload-contract.json. Full notes:
docs/REMOVE_SPREAD_REVISION5.md. Research: logs/research/26144732/spread_revision5/.

No game/server was launched, joined or injected; no installed file/config was
changed. Independent server seeds and later motion/origin/pose changes remain
model limits. Actual accuracy, cadence and FPS need a fresh permitted private
or offline trial. Restart normally before Load Overlay.cmd; stopped code stays
resident until normal process exit.

# Anti aim + quantized movement, revision 16 — October 24, 2026

User requests both working features together instead of movement replacing AA.
Clean starting checkpoint f8f26f8. Shared command owner now optionally passes
enabled AA settings into the quantized movement transaction. Generate target
once from original physical view + command sequence, retain its exact pitch,
then select the equivalent digital wish yaw nearest that target (<=22.5 degrees).
Small yaw changes may share a representation. Do not feed selected absolute yaw
back through configured View/Jitter/Spin/Random offsets; that applies them twice.

AA off retains r15 physical-reference representation and pitch. Strict movement
checks remain in force with default Static/View/180/Down; legacyPayload excludes
all composed movement. Actions retain digital physical-view fallback. Raw native
N-1 axes, diagonal history, publication physical view, restore/replay and fixed
subtick basis unchanged. Jumpbug source/timing and config schema unchanged.

Focused 6/6 AA suites pass, including 1,728 combined yaw/base/pitch/phase/view/wish
cases, exact pitch/wish, bounded target yaw, replay/restore, AA on/off histories
and strict action/ownership rejection. Both x64 Release builds are warning-free
and pass 66/66 CTests each. Final DLLs have zero static TLS/delay imports; hashes
in logs/movement-r16-artifacts.json. Native dependencies unchanged from prior
33/33 static bundle. Details: docs/MOVEMENT_REVISION16.md. No launch/injection or combined
gameplay observed. Startup movement revision=16, AA revision=8; diagnostic
movement_anti_aim_edits counts successful compositions.

# Movement facing correction, revision 15 — October 24, 2026

User reports revision 14 movement and jumpbug work very well, but character
facing twitches extremely and viewmodel subtly. Working revision 14 committed
as 961bd64 before this change. User authorized commits.

Revision 15 preserves the ideal steering solution and jumpbug timing. Instead
of always Forward + wish yaw, the shared command owner chooses the nearest
of eight equivalent full digital direction/yaw pairs. Facing stays within
22.5 degrees of physical view. Common opposite side wishes keep one facing
and switch buttons; digital-sector boundaries can still make 45-degree steps.
Native normalization + effective wish-speed cap make full diagonals equivalent
to full cardinal speed. Preserve raw diagonal predecessor/history magnitude.
Both retarget branches, final axes, presence and masks use the chosen pair.
No new native fields/calls/hooks or camera/model-angle writes.

Both x64 Release builds pass 66/66 CTests warning-free, 6/6 focused AA suites.
New coverage independently checks 20,167 view/wish combinations, capped impulse,
facing bounds/ties and history/restore. Independent review found no blocker.
Logs movement-r15-build/tests-public/diagnostic; final metadata in
logs/movement-r15-artifacts.json, both zero static TLS/delay imports.
Native compatibility dependencies unchanged from prior 33/33 bundle.
No revision 15 gameplay observed or agent launch/injection. Restart normally,
Load Overlay.cmd, same quantization 1/settings; stopped code stays resident.
Full details: docs/MOVEMENT_REVISION15.md.

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
