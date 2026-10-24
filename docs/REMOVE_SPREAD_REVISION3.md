# Raw history clock and server-seed limits — revision 3

Investigated October 24, 2026 after the user reported that compensation worked
better in bot matches than non-local sessions. Only installed files and existing
workspace logs were read. No client/server was launched, no session was joined,
and no server configuration was changed.

## Two distinct issues

The supplied session `logs/overlay-10152-134372468703772196.log` keeps the
compensation adapter available in every sampled diagnostic. Among 30 native
Scout fires with matched correction metadata, nine hit and 21 miss. Inferred
clock segments split into 8/12 hits early and 1/18 later. The log does not name
the maps or reliably establish session type, so those segments are not treated
as proven local/remote boundaries. Full audit and parser are under
`logs/research/26144732/spread_remote_audit/`.

In the later records, raw indexed history is 2–5 ticks ahead of the locally
clamped model. Revision 2 sampled only the clamped tick and its successor.
Independent seed/RNG replay matches all 30 old pairs, then shows that raw history
with the copied current-prediction recoil improves the observed-ray comparison
in 16 of 18 later cases: median mismatch falls from 5.19 degrees to 0.62 degrees.
This is a concrete deficiency in the considered client models. It does not
identify an authoritative server seed or prove an exact future firing state.

For shot 66, publication and the native current-recoil sample share the exact
recorded uptime. The current getter reports zero punch, while the historical
clamped query contributes about -1.2456 degrees pitch. Raw-tick/current-recoil
replay nearly reproduces the observed ray. Other fixtures use current-recoil
records 0–47 ms before publication; the captured text rounds those values.
These provenance limits remain explicit in the regression data.

Separately, the installed server contains a seed selector that can bypass the
client angle/tick hash. Under the verified conditional command path,
`sv_usercmd_custom_random_seed` supplies a SHA-1 value derived from server time
and server RNG. Its registered default is true; that is not a measurement of
the user's server value. No authoritative independent seed value was found in
the supplied client logs. A seed-prediction label does not establish recovery
of this value. See [native server evidence](../tools/CS2_SERVER_SPREAD_SEED_EVIDENCE.md)
and `python tools/inspect_server_spread_seed.py`.

## Implementation

Scout prediction retains the original clamped-time/punch model. When converted
raw history is more than one integer tick ahead, the second hypothesis uses
that raw integer tick and `AimWeaponSnapshot::aimPunch`, the existing copied
native current-prediction recoil. Otherwise the previous current/next-tick pair
and shared punch remain unchanged. Other weapons retain their prior policy.
Neither alternative is claimed to be the actual server shot state.

The pure solver composes each candidate command with each hypothesis's own
pitch, yaw and roll before seeding and reconstructing its ray. Different recoil
bases cannot use the old shared-basis separation/bin pruning; that path uses
both inverse candidates and a seam-aware command midpoint, and resamples both
actual rounded angles. Both models retain the shared 8192-call budget and the
worst-case baseline guarantee in the copied model.

Automatic fire still requires both predicted rays to intersect the selected
capsule, including at minimum hitchance zero. Manual fire remains available.
For standalone raw-clock Scout shots, intended recoil comes from the copied
current prediction rather than reintroducing the decayed historical punch.
No future native punch/time query, timestamp rewrite, clock write, RNG mutation,
additional hook or loader/session gate is introduced. The existing owner-thread
revalidation and cooperative stopped/resident lifecycle remain in force.

`AIM_SPREAD` revision 3 records the selected alternate tick, raw-clock selection,
historical/current/alternate recoil and both predicted rays. The menu now states
that independent server randomness is unsupported. The on/off toggle and preset
key remain unchanged.

## Verification and limits

The new 30-shot fixture independently reconstructs both old seeds/rays before
comparing the timing/recoil hypotheses. It retains all source lines and timing
provenance without labeling the captured session a particular server type.
On those copied states, worst angular error under the new hypotheses improves
from median 1.59 degrees for the prior solver output to 0.70 degrees for the
adapted search. Simultaneous saved-capsule intersections increase from 13 to 18
of 30. These are conditional offline geometry comparisons, not observed new
hits or a remote success rate. Twelve fixtures retain the old pair unchanged.

The independent server path remains unsupported when active. Its runtime mode
and authoritative firing state were not observed, and later state/origin/pose
changes remain possible even when both client hypotheses pass. Revision 3 is a
limited evidence-based model improvement, not validated general non-local
no-spread support.

Both public and diagnostic x64 Release builds pass **63/63 CTest suites** without
project compiler warnings. They include 5068 inverse, 685 search, 117 native
adapter, 275 original replay, 363 clock replay and 373 integrated pipeline
checks. Both DLLs remain AMD64 with zero static TLS and delay imports. The
updated menu preview was inspected. Build/test logs and payload inspection are
`logs/remove-spread-r3-build-{public,diagnostic}.log`,
`logs/remove-spread-r3-tests-{public,diagnostic}.log` and
`logs/remove-spread-r3-payload-contract.json`.

All **30/30 installed-file compatibility checks** pass in
`logs/compatibility/20261024T074423098183Z/`. The standalone server audit passes
and rejects a wrong-module input; outputs are `logs/remove-spread-r3-server-audit.json`
and `logs/remove-spread-r3-server-audit-negative.log`. No server setting was read
at runtime or changed. These checks do not establish live revision 3 behavior.

Restart CS2 normally before **Load Overlay.cmd** if an older DLL is resident.
Stopped and resident is not physically unloaded. A fresh private/offline user
trial is needed to assess the revised behavior.
