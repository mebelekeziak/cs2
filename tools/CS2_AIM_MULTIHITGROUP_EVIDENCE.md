# Authored landmarks for multi-hitgroup targeting

October 23, 2026: client 2000935 / Steam build 26144732 recertification is
recorded in [capsule geometry evidence](CS2_AIM_GEOMETRY_EVIDENCE.md) and
[named landmark evidence](CS2_SILENT_AIM_HITBOX_EVIDENCE.md). The authored
SAS asset hash and all selected bone/group/shape combinations remain unchanged.
The following implementation evidence describes the historical build.

Inspected October 20, 2026, from installed files and synthetic fixtures only.
No CS2 client/server was launched, attached to or injected into for this work.
The installed client remains native Windows AMD64 (`Machine=0x8664`), with
`game/csgo/steam.inf` reporting patch 1.41.9.1, client/server 2000933 and source
revision 11133601. The existing observed renderer is D3D11; this change adds no
graphics/native call ABI. The installation is
`C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game`.

`python tools/inspect_aim_geometry.py` passed the current full client hash
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
animationsystem hash
`fb2b4cd71468e880c3b3845e8e7a18c9d550dd3c4504e4a257a2110fb91c1235`
and the existing schema/native capsule contracts. The same installed-file
extraction verifies `agents/models/ctm_sas/ctm_sas.vmdl_c` hash
`7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22`.

The retained MIT-licensed ValveResourceFormat 20.0 CLI was rerun with `--all`
against that hash-matched extracted asset. Its five `cstrike` hitbox sets have
the same 19 authored bone/shape/group combinations. Eighteen are selectable:

| User selection | Concrete bone names | Authored damage group |
| --- | --- | --- |
| Head | `head_0` | 1 |
| Chest | `spine_3`, `spine_1`, `spine_2` | 2 |
| Stomach | `spine_0` | 3 |
| Pelvis | `pelvis` | 3 |
| Arms | `arm_upper_l`, `arm_lower_l`; `arm_upper_r`, `arm_lower_r` | 4; 5 |
| Hands | `hand_l`; `hand_r` | 4; 5 |
| Legs | `leg_upper_l`, `leg_lower_l`; `leg_upper_r`, `leg_lower_r` | 6; 7 |
| Feet | `ankle_l`; `ankle_r` | 6; 7 |

The remaining `neck_0` record belongs to native group 8 and is not selected.
All these records use shape 2 and have `m_bTranslationOnly=false`. The existing
reader continues to require ordinary bone-space transforms, unit scale and a
valid current pose. It copies each current model's endpoints/radius and does
not embed the SAS example dimensions or numerical bone indices.

This corrects the old Stomach landmark from `spine_2` (authored chest group 2)
to `spine_0` (authored stomach group 3). Old presets retain their user-facing
Stomach choice but target the corrected region. Hands and feet have separate
selection toggles while their native damage groups remain arms and legs.
Damage prediction continues using the actual first collision's verified group;
the selection table never overrides that trace result.

Additional exact client string RVAs, checked by the expanded
`inspect_silent_aim_hitboxes.py`:

| Name | RVA |
| --- | --- |
| `spine_0`, `spine_1`, `spine_2` | `0x1BA2620`, `0x1BA2628`, `0x1BA2630` |
| `arm_upper_l`, `arm_upper_r` | `0x1C8CD10`, `0x1C8CD20` |
| `arm_lower_l`, `arm_lower_r` | `0x1C8CD30`, `0x1C8CD40` |
| `hand_l`, `hand_r` | `0x1C8CD4C`, `0x1C8CD54` |
| `leg_lower_l`, `leg_lower_r` | `0x1C8CD70`, `0x1C8CD80` |
| `ankle_l`, `ankle_r` | `0x1C8CD90`, `0x1C8CD98` |

Upper-leg names are established by the hash-matched authored resource, rather
than a standalone native string. The verifier now checks that resource hash
as well as all 18 runtime name literals and the unchanged native ownership,
lookup and pose layout contracts.

Reproduce the metadata read with the existing extracted asset and tool:

```powershell
.\logs\research\vrf20\Source2Viewer-CLI.exe -i logs/research/ctm_sas_agent.vmdl_c --all
python tools/inspect_silent_aim_hitboxes.py
python tools/inspect_aim_geometry.py
```

The mask selector enumerates actors once and independently resolves every
selected concrete landmark. An empty or unknown-only mask produces no target.
With a callback, every in-FOV valid landmark reaches ranking, including farther
landmarks after a closer accepted candidate. Returning true replaces the best;
the callback owns damage/probability ranking. Without a callback selection is
angular. Existing serialized identity and pose revalidation remains in place.

`silent_aim_checks` covers empty/unknown masks, both hands, independent FOV
rejection, every named landmark, three chest levels and callback ownership.
`aim_hitchance_checks` covers copied and revalidated authored limb/torso
capsules, mirrored endpoints and in-place bone-name invalidation. These are
synthetic tests; no observed live hit registration is claimed.

The command runtime first prefers qualifying lethal shots over nonlethal shots.
Lethality compares the conservative whole-HP direct-ray estimate to copied enemy
health; it never bypasses automatic eligibility gates. Among lethal shots,
higher geometric hitchance wins, with no extra priority for overkill. Remaining
candidates rank by geometric hitchance multiplied by predicted direct-ray
damage capped to copied enemy health. Equal scores
prefer higher hitchance, smaller centered angle, then pawn handle and landmark
enum order. All automatic thresholds remain eligibility gates. Immediately
shootable candidates outrank quickstop fallbacks; the latter use the same score
among clear, ready, minimum-damage-qualified candidates. Manual aiming ranks
modeled shots without automatic thresholds and retains angular fallback when
none can be modeled. The weapon snapshot is shared across the complete scan.

Captured actor health is part of target revalidation, including after native
trace callbacks and before publication. Changed health invalidates the prior
effective-damage cap rather than publishing stale ranking. Mask changes use
the existing atomic snapshot comparison and owned-attack cancellation path.

Final verification: public and diagnostic x64 Release builds each pass 49/49
CTest suites, including 202 integrated pipeline checks and 173 hitchance checks.
The pipeline covers enumeration order, expected damage versus reliability,
overkill, disabled groups, blocked/unsupported regions, independent minimums,
immediate fire versus braking, all-below-chance braking, manual ranking, and
mask/health changes. Both DLLs retain zero static TLS and delay imports. All 23
installed-file components pass in
`logs/compatibility/20261020T132158578600Z/`. All 18 wrong name mutations are
rejected; normal and narrow software-D3D11 menu previews were inspected.

## Full-health lethal-shot correction

The reported body preference exposed a policy limitation: the original score
could rank a 90-damage shot at 100% above a lethal headshot at 70–89% against
100 HP. A 70-HP minimum alone only makes shots eligible. The new comparator
gives qualifying one-bullet kills priority before expected nonlethal damage.
It still prefers the more reliable body region when both shots are lethal,
or when the head fails the configured minimum hitchance.

The runtime regression models a 100-HP enemy, head damage 300, body damage 90,
head hitchance between 70% and 90%, body hitchance 100%, and both minimums at
70. Both candidate orders and both quickstop settings choose head. Raising
minimum hitchance to 100% excludes head and retains body. No native contract
or client lifecycle changes are part of this correction.

Both Release payloads rebuild successfully; public and diagnostic CTest runs
each pass 53/53 suites, including 213 integrated pipeline checks. Logs are
`logs/lethal-ranking-ctest-public.txt` and
`logs/lethal-ranking-ctest-diagnostic.txt`. Both payloads retain zero static
TLS and delay imports. No game was launched for this correction, and no live
behavior of the new comparator is claimed.

## Enemy-count performance correction

The original path regenerated four Halton dimensions and four trigonometric
values per spread sample per capsule. Five enemies with Head/Chest/Stomach/
Pelvis means 30 capsules; at 1,024 samples this is 122,880 per-sample trig calls
per scan, before native lookup/tracing. It also scanned while automatic fire
was blocked by weapon readiness and while manual mode had no shot history.
Guarded scalar reads already used SEH memcpy, not VirtualQuery.

`AimSpreadPattern` now copies at most 1,024 spread offsets once per decision,
and shares them across capsule angles. No native pointers, native RNG or
cross-command state are cached. A bit-exact spread-key comparison falls back
to scalar generation on mismatch; standalone counts through 65,536 remain
supported. Capsule constants are hoisted and duplicate normalization square
roots removed. Exactly zero spread has identical rays; one intersection reports
the same full hit/sample count. Frozen reference comparisons cover 160 variants,
Negev/recoil/debug modes, invalid data, mismatches and large-count fallback.

Automatic readiness is read once before actor enumeration; unavailable or
not-ready weapons skip the scan and release quickstop. Manual preparation
restores command ownership/releases movement without acquisition; real shot
history still ranks fresh targets. A centered candidate's optimistic lethal
100%-chance result is compared with the current winner before sampling. If it
cannot win, sampling/tracing is skipped. A second bound after sampling assumes
lethal damage at measured chance to avoid unnecessary traces. Both bounds use
the complete comparator, including cross-target health, angle, identity and
landmark ties. They do not cut candidate counts or lower sample budgets.
Final live validity and path checks remain before request and publication.

Redundant explicit TargetValid calls immediately before VolumeValid were
removed in Current and damage Sample; real VolumeValid already invokes them.
Mocks were corrected to match that contract, with stale-state regressions kept.

Synthetic benchmark output is `logs/aim-hitchance-performance.csv`, generated
by `build/Release/aim_hitchance_benchmark.exe`. For five enemies/six capsules
each, nonzero spread at 1,024 samples changes median copied-geometry time from
5.1826 ms to 1.8014 ms (2.88x); p95 changes 6.4153 to 2.0068 ms. At 256 samples,
median changes 1.1472 to 0.4451 ms. Preparation is included. Native acquisition,
traces, runtime ranking bounds and cooldown skips are outside this benchmark;
it establishes neither live FPS nor complete command time.

`AIM_PERFORMANCE` is emitted through the existing throttled diagnostic flush.
Its timing covers Evaluate/selection, while cumulative trace counts also
include request/publication rechecks. It logs copied counters only, with no
per-candidate sink I/O. The integrated five-enemy fixture verifies 30 candidates
share one pattern and weapon read; for a perfect lethal winner only one needs
sampling and eligibility tracing (29 exact bounds), with two fresh firing
traces retained. Cooldown, immediate readiness recovery, manual history and
equal-chance ties have operation-count regressions.

Both public and diagnostic Release builds pass 53/53 CTest suites, including
226 pipeline checks and 3,389 hitchance/geometry checks. Logs are
`logs/aim-performance-ctest-public.txt` and
`logs/aim-performance-ctest-diagnostic.txt`. All 25 installed-file components
pass in `logs/compatibility/20261020T141705084077Z/`. Both payloads retain zero
static TLS and delay imports. No game was launched; live FPS remains unobserved.

The user subsequently reported substantially improved performance after testing
the rebuilt payload. This is qualitative user observation; no numerical FPS
comparison or independent live timing capture was supplied.
