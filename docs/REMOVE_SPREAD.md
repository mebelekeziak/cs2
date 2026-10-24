# Remove spread

Implemented October 23, 2026 for the Windows AMD64 client 2000935, patch
1.41.9.2, Steam build 26144732. Runtime rendering uses the existing verified
D3D11 callback; this change does not launch the client or establish a new live
graphics/runtime observation.

The current [revision 3](REMOVE_SPREAD_REVISION3.md) accounts for the later raw
history/current-recoil mismatch and documents the separately verified independent
server-seed mode. General non-local support is unverified; active independent
server seeds are not predicted by this feature.

## Controls and testing

Use **Aim → Remove spread**. The toggle starts off, works independently of
Silent aim and Autofire, and saves as `aim.remove_spread`. Old presets restore
off. The diagnostic panel reports the setting. With Silent aim off, aim manually
and tap individual primary shots; airborne/unscoped SSG 08 shots use the same
compensation path. Manual recoil remains part of the intended direction.
With Silent aim on, the selected target direction is compensated using native
shot-time punch. [Revision 2](REMOVE_SPREAD_REVISION2.md) accounts for the
conditional current/next-tick ambiguity found in the user's Scout log. Scout
autofire requires both predicted rays to intersect; manual Scout compensation
minimizes their worst angular error. The shared captured accuracy/punch can
still differ from eventual firing state.

Restart CS2 normally before loading a rebuilt DLL if an older one is resident,
then use **Load Overlay.cmd**. Stopped and resident is not physically unloaded.
Development/testing remains at the disconnected menu, offline, or in the user's
private session. No server configuration or installed game files are changed.

Successful corrections emit `AIM_SPREAD` with the client seed, shot tick/fraction,
residual angle, evaluation count and predicted ray. `AIM_SPREAD_DIAGNOSTICS`
reports availability, corrections/skips and the latest reason. This client seed
is not an observed server seed. Capture logs from actual offline attempts to
evaluate whether the predicted ray matches impacts; standalone tests cannot
establish that agreement.

## Implementation

The existing native history callback remains the sole write boundary. The
feature reads the indexed entry's player time, converts its scene domain,
applies the native four-tick clamp and queries punch for that exact time. It
checks source, weapon, timing, punch, config and history before publication.
Native getter memoization stays on the command owner. No new hook, shared RNG
mutation, timestamp rewrite, weapon accuracy write or camera edit is added.

The seed hashes quantized effective pitch/yaw plus the integer shot tick.
Candidate angles therefore use their own native seed. An analytic inverse of
the complete native basis handles punch roll. A search evaluates at most 8192
random-stream samples total, including bin-interior approximations, and keeps a result at least
as accurate as the sampled original command in the copied client model's worst
considered clock variant. It
runs for primary-shot publication, not every target scan or render frame.
An exact fixed point is not always found. Legal pitch limits and unavailable
models can prevent compensation.

The random stream lives in owned aligned stack storage. It uses the installed
native null-mutex constructor and float generator, never global RandomSeed or
RandomFloat. The native pattern cvar affects even a single projectile's draw
order; both orders, Negev shaping and debug overrides are represented. See
[native proof](../tools/CS2_AIM_SPREAD_EVIDENCE.md) for all ABIs and provenance.

Single-projectile primary weapons supported by the existing weapon adapter are
eligible. Revolvers, shotguns, burst sequences and held-fire histories without
a native attack index are not modeled. Unsupported manual shots retain normal
input/ordinary silent aim where its own model is available. Missing spread
prediction cannot authorize an automatic shot.

Automatic selection retains damage/path readiness and ranking. With the toggle
on, the statistical cone chance is provisional: final publication must validate
the corrected client ray against the selected capsule. Shot metadata labels a
single conditional client ray instead of presenting 1/1 as statistical 100%
hitchance. Neither its seed nor ray proves the server's eventual firing state.
Path/damage checks still use the selected center ray; residual spread can alter
edge-cover traversal or hitgroup. Current pose/origin, accuracy changes during
subsequent prediction, and server timing remain limitations.

## Initial verification, before the user trial

Public and diagnostic x64 Release builds each pass 61/61 CTest suites without
project compiler warnings. Both payloads remain AMD64 with zero static TLS and
delay imports. Build/test logs are `logs/remove-spread-build-{public,diagnostic}.log`
and `logs/remove-spread-tests-{public,diagnostic}.log`; the payload check is
`logs/remove-spread-payload-contract.json`. The new inverse suite
passes 5068 checks, search 228 and native adapter 102. They cover forward
reconstruction, angle-dependent seeds, large spread, native roll, yaw seams,
bounded work, malformed/stale state, exact timing ABI, both RNG draw orders and
private RNG isolation. Integrated tests cover manual independence, one-time
correction, corrected autofire below ordinary cone chance, and ownership-safe
cancellation. Config/menu tests cover on/off and old presets.

All 30/30 installed-file compatibility checks pass in
`logs/compatibility/20261023T161703894876Z/`. The new verifier rejects
23 deliberately altered source contracts/fingerprints. The public menu preview
was rendered and inspected. No native gameplay calls were executed in CS2, no
client/server was launched, and live no-scope accuracy remains unobserved.
