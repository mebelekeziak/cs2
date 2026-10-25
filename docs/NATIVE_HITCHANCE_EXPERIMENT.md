# Native random-stream hitchance experiment — October 25, 2026

The experiment samples CS2's verified private native random stream and sends
those copied offsets through the existing capsule geometry estimator. The
original Halton sampler and indexed Remove spread solver remain available.
Changing a statistical sample source does not establish or repair the shot's
actual timestamp, recoil, firing accuracy, target pose or event association.

## Native contract

This targets the installed Windows AMD64 client **1.41.9.2 / 2000935 / Steam
26144732**, with `client.dll` SHA-256
`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`
and `tier0.dll` SHA-256
`f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d`.
The relevant native source is already frozen by
`tools/inspect_aim_weapon.py` and `tools/inspect_aim_spread.py`.

`AimPrepareNativeSpreadPattern` borrows the context prepared by
`AimSpreadPrepare`, whose native `tier0` constructor and float method are:

```cpp
void* RandomCtor(void* ownedState, int seed);       // tier0 + 0x15E7A0
float RandomFloat(void* ownedState, float low, float high); // + 0x15E880
```

Each sample constructs a separate aligned, zeroed **0x88-byte** state. The
samples use native stream seeds **1 through N**, for N from 1 through 1024.
These enumerate a statistical ensemble; they do not predict the next server
seed. Zero spread evaluates one identical sample while preserving the full
requested count. There is no engine-global RNG access or static TLS.

The caller supplies the copied live value of
`weapon_accuracy_shotgun_spread_patterns` on the existing native command
owner. The verified wrapper is `client + 0x25C0C78`, pointer `+8`, value `+0x58`.
The first draws are radius A then angle A. The second draws are angle B then
radius B with patterns enabled, or radius B then angle B otherwise. Native
Negev shaping and debug radius/angle overrides preserve the installed
float32 order. All four draws occur even with the overrides enabled.

The function rejects invalid distribution values, unsupported sample counts,
missing native methods, unexpected returns, structured native exceptions and
nonfinite/out-of-range draws. It invalidates its output on failure and returns
an explicit reason. Integration must check this return before calling the
existing prepared-pattern estimator, whose general invalid-pattern overload
otherwise falls back to Halton. Experimental failure must remain visible.

## Difference from the full native generator

The linked [Velocity reference](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/combat/impl/shared.cpp)
uses the full native spread distribution function for consecutive seed
samples. Its fetched content blob was
`bbf3678aa6a2c0246a313c5d72eb05d5d1d99fd3`. No third-party implementation
code or signature was copied into this experiment.

The installed equivalent is `client + 0xD25630`; its actual caller/callee
establishes the Microsoft x64 signature:

```cpp
void Distribution(uint16_t itemDefinition, int numBullets, int mode,
                  uint32_t streamSeed, float inaccuracy, float spread,
                  float recoilIndex, float* xs, float* ys);
```

That full function reseeds/advances the shared native RNG, initializes item
and shotgun caches and uses engine-owned TLS. Its global RNG locks apply to
individual seed/draw calls. A command-owner snapshot/restore does not exclude
RNG activity by other native threads and could overwrite their changes.

The experiment therefore calls the **actual private native RNG methods** and
retains the locally verified single-projectile primary distribution formula.
It does **not** call the complete client distribution entry. Statistical
hitchance, deterministic indexed compensation and authoritative server shot
prediction are separate questions.

## Verification and comparison

The standalone test target requires only
`tests/aim_native_hitchance_checks.cpp`, `src/aim_native_hitchance.cpp`,
`src/aim_hitchance.cpp` and project include paths. Default tests use synthetic
native methods and an independent RNG reference; no game installation is
required. The test covers state isolation/alignment, every draw/order, native
failures, parameter rejection, zero spread, sample limits, copied model
changes, Negev recoil stages and debug combinations.

An optional isolated test process can load the already verified installed
`tier0.dll` and invoke its named private methods, without loading the client
or launching a game/server:

```powershell
build/Release/aim_native_hitchance_checks.exe --native-tier0 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\tier0.dll'
```

The actual source and executable paths depend on the configured build tree.
Run the normal compatibility bundle first: the optional test checks loaded
PE identity, export RVAs and entry fingerprints; the bundle verifies complete
installed-file hashes and native windows.

Observed on October 25: focused MSVC x64 Release compilation succeeds without
warnings. Synthetic tests pass **18,546 checks**. The optional installed-tier0
run passes **37,098 checks**, including bit-exact offset parity across eighteen
distribution/order combinations and 1,024 seeds per combination. Outputs are
under `logs/research/native_hitchance_20261025/`.

Sequential native seeds are not inherently more accurate than Halton. For a
sphere at distance 1,000, radius 10, inaccuracy 0.02 and zero weapon spread,
the analytic geometric probability is **50.0025%**:

| Samples | Existing Halton | Native seeded experiment |
| --- | ---: | ---: |
| 256 | 50.3906% | 51.1719% |
| 1024 | 50.0977% | 52.9297% |

The harness also prints ordinary, Negev and debug-law comparisons with a
65,536-sample Halton reference. That dense reference is an approximation, not
an independent native truth. Different finite seed ensembles can produce
threshold disagreements even with the same underlying distribution law;
increasing sample count need not improve every deterministic ensemble.

The user subsequently reports substantially better behavior and essentially
no random misses so far, then confirms clean behavior with the native option
both enabled and disabled. They also briefly toggled Remove spread. Native
sampling is not required for the reported improvement; the shared publication
repair is the stronger explanation. These qualitative on/off observations
have no measured shot counts or rates and establish no native-sampler
advantage or authoritative server parity. The default remains disabled.
The existing Remove spread path bypasses ordinary statistical hitchance and
uses its indexed clock/recoil/ray model. A new hitchance source alone cannot
establish that model's correctness.
