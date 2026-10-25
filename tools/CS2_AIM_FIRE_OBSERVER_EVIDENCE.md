# Native client shot-construction observer

Revision 2 also observes the actual ordinary-fire spread and inaccuracy
getter returns after native construction. It never calls a getter to create
a diagnostic, and neither construction nor these values establish server fire.

Investigated October 25, 2026 against installed Windows AMD64 client
**1.41.9.2 / 2000935 / Steam 26144732**. Client is
`C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/bin/win64/client.dll`,
SHA-256 `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`,
PE timestamp `0x6ADA7340`, image size `0x29A0000`.

Research decodes installed files only. No process is launched or attached,
no installed file is changed, and no native gameplay method is manually
invoked. The implementation observes an already invoked native method.

## Builder and caller ABI

The ordinary firing function at `0x804660` calls wrapper `0x804EA0` at
`0x80485A`. Wrapper passes RCX=weapon, RDX=caller-owned shot output,
R8D=attack selector into `0x80E530` at `0x804EDA`, returning to `0x804EDF`.
The builder returns that same output pointer in RAX at `0x80F200`.

```cpp
void* Builder(void* weapon, void* callerOwned64ByteOutput, int attackSelector);
```

The observer accepts only the exact ordinary wrapper return address and
selector zero, corresponding to the native primary attack index at command
`+0x4C`. Selector one selects secondary index `+0x50` and is forwarded without
being reported as a primary shot.

**The attack selector is distinct from scoped weapon mode.** Native primary
producer `0x8235D4` loads weapon mode from weapon `+0x1A00` into R8D, then
clears R9D at `0x8235DB` before calling `0x804660` at `0x8235E4`. That function
retains weapon mode in R12D and attack selector in R13D. At `0x80484D` it passes
the latter through wrapper R9D to builder R8D. Thus a scoped weapon with
weapon mode one still uses attack selector zero for its primary shot.
The observation structure's `mode` field and corresponding telemetry label
refer to this **attack selector**, not scope/weapon mode.

## Native command provenance

The builder obtains weapon owner pawn through full-handle helper `0x942DD0`,
which loads weapon `+0x520`, and then pawn controller through `0x75DDA0`,
which loads pawn `+0x14BC`. Both resolve the complete serialized handle through
the entity table, compare the identity's full handle and reject stale entries.
Named `C_BaseEntity::m_hOwnerEntity` schema independently corroborates `0x520`.

At `0x80E66B`, the builder loads controller `+0x780` as its current command.
The setter `0x964490` writes that field at `0x964493`; prediction dispatcher
`0xBEFC60` calls the setter with its command at `0xBEFE33` and clears it with
null at `0xBF0256`. This pointer describes the native command currently being
consumed, which need not be the newest publication at event receipt.
The existing independently frozen native command sequence is at command `+8`.

The observer validates current local pawn/controller and reciprocal serialized
handles, alive state, local-controller flag, exact supported weapon table,
weapon owner, weapon-services table/owner and active weapon handle. It copies
the native current-command pointer and sequence before original execution,
then requires the same complete source and command identity afterward.
All borrowed addresses remain local to the callback. Only copied handles,
sequence, times and geometry enter the bounded diagnostic queue.

## Caller-owned output

The ordinary caller copies four 16-byte blocks from the native result at
`0x804A68`, `0x804A70`, `0x804A86`, and `0x804A8E`. The complete extent is
therefore 64 bytes. Native builder operands establish these fields:

| Offset | Copied field |
| --- | --- |
| `+0/+4` | Native selected shot tick/fraction |
| `+8` | Native scalar retained as opaque, unused by observer validation |
| `+0xC/+0x10` | Native render tick/fraction; initialized to zero together before branch selection |
| `+0x14..+0x1C` | Historical native shooting origin used by this construction |
| `+0x20..+0x28` | Effective angles after the native shot-time punch addition |
| `+0x2C..+0x34` | Scaled shot-time punch copied by native code |
| `+0x38/+0x39` | Opaque native flags, logged without claiming alternate/indexed semantics |
| `+0x3C` | Native origin-selection kind |

The indexed branch selects native history through command `+0x38` and the
primary index at `+0x4C`, applies its native converted/clamped shot time,
queries the historical origin wrapper at `0x80E8A1`, copies selected angles
verbatim, and adds native scaled shot-time punch at `0x80F17F/0x80F18C`.
The observer executes no getter/builder to produce a speculative value;
it copies the actual returned output after original native execution.

Input-history diagnostics require sane size/capacity/allocated representation,
all relevant presence bits and finite scalar fields. Sparse/malformed history
is reported as `history_known=0`; it does not fabricate defaults or prevent
otherwise valid native output observation. The unused native scalar at `+8`
is not required to be finite. Native output flag bytes are left opaque.

## Limits and lifecycle

`AIM_NATIVE_BUILT_SHOT` is explicitly marked `built_client_shot=1 server_fire=0
observer_only=1`. The outer ordinary function still has a virtual gate at
`+0xD38` after construction; a built shot is not proof that the later native
bullet generator ran. Prediction can also rebuild/replay client shots.
Neither construction nor matching a published sequence proves a server
shot ID, authoritative origin/seed, damage pose, hit or server acknowledgement.
The ordinary caller samples final native spread and inaccuracy later at
`0x804B71/0x804B85`, after the observed builder returns. These final getter
results are not present in the builder output. Revision 2 separately observes
those actual invocations as described below; published model logs remain
earlier copied-model evidence.
The observer therefore does not select shot-log metadata, approve aiming,
change firing, or turn missing observation into a gameplay gate.

Native callbacks always forward original arguments and return values exactly
once, including rejected source, disabled logging and stopped operation.
Each callback uses its own stack snapshots, so native recursion holds no
observer lock and needs no static or dynamic TLS. A short queue lock covers
only copied-value storage. A bounded 64-record queue exposes dropped records.
Detailed capture/drain honors the existing shot-logging enabled setting.

## Actual native accuracy getter returns

The complete ordinary accuracy-input window `0x804B6B`, length 223, is pinned
to SHA-256 `12326217c2839b9ce3e2c2fac780b95b999126d59fe94f54b133d53acbfc2059`.
It is independently frozen by `inspect_aim_weapon.py` as well. The observer
verifier decodes the six-byte `FF 90 disp32` calls and resolves their targets
through the independently named `C_AK47` primary RTTI table at `0x1BDFBB0`:

| Component | Virtual slot | Actual call / return | Resolved getter | ABI |
| --- | --- | --- | --- | --- |
| Spread | `+0xBD0` | `0x804B71 / 0x804B77` | `0x80E2C0` | `float(void* weapon)` |
| Inaccuracy | `+0xCF8` | `0x804B85 / 0x804B8B` | `0x80CFD0` | `float(void* weapon, float* optional1, float* optional2)` |

Before the spread call, native loads the weapon table from `[RSI]` and moves
RSI into RCX. After it returns, native reloads that table, clears R8D and EDX,
moves RSI into RCX, retains the spread float in XMM7 and invokes inaccuracy.
The actual ordinary inaccuracy call therefore supplies null optional pointers.
The observer forwards all supplied pointers unchanged and records only their
presence; it never invents their meaning or dereferences them itself.

Getter return values use XMM0. Runtime keeps the exact `float` return and
copies its `uint32_t` bit representation, including signed zero and nonfinite
values, without arithmetic or validation that could alter gameplay behavior.
The full getter windows and both callsites are independently pinned by the
file verifier. Runtime checks complete spread bytes, the inaccuracy entry
prefix and each virtual-call fingerprint before installing hooks.

Each callback accepts only its exact ordinary-fire return address, enabled
shot logging and the same verified local full source/current command before
and after the actual original call. Project hitchance/model getter calls and
other native consumers forward original execution but produce no accuracy
record. Reentrant calls retain independent stack provenance with no callback
lock held during native execution.

`AIM_NATIVE_ACCURACY` records receipt time, full local controller/pawn/weapon
handles, the actual current native command sequence, component, decimal value,
exact `value_bits`, bitwise finiteness and optional-pointer presence. It is
marked `native_client_input=1 server_fire=0 observer_only=1`.
These getter arguments contain **no attack selector**. Telemetry explicitly
uses `attack_selector_known=0 attack_selector=-1`, rather than inferring one
from scope, weapon mode or the separate builder observation. The same sequence
can be rebuilt/replayed; receipt order and the builder record remain separate
evidence. Missing/mismatched observations never select shot-log metadata or
change aim/fire decisions.

Accuracy has a separate bounded 64-record queue, sharing the short copied-value
queue lock. Overflow increments its own counter without consuming builder
capacity. Logging off suppresses capture and discards drained details. All
three hook callbacks share the original-plus-post-copy in-flight stop barrier.
If installation fails after an earlier hook was queued, activation remains
disabled; installed resident callbacks forward their own original bodies.
No hook or stopped code is physically unloaded and no static TLS is introduced.

Cooperative Stop prevents further queue publication and waits for the callback
barrier, including original execution and post-native copying. It is called
outside the observer's own callback. Hooks and stopped code stay resident
until normal process exit; this is not physical unloading.

## Verification

`python tools/inspect_aim_fire_observer.py` pins the complete client hash,
eleven independently identified native windows, owner schema, native
producer/consumer/caller operands, attack-selector producer, every observer
constant, named weapon RTTI/virtual slot targets, null optional argument
setup and all six runtime entry/call fingerprints. `--client` and `--header`
allow exact file and deliberately mutated temporary-header checks.

Disassembly, full-window hashes, verified output and negative-test results
are under `logs/research/misses_followup_20261025/native_provenance/`.
The implementation fixtures cover exact native forwarding/output preservation,
serialized source rejection and changes inside original, missing/sparse
history, actual output copies, native recursion, bounded overflow, logging-off
behavior and a native call blocked while cooperative Stop waits.
No live callback observation is claimed by these static/fixture checks.

The real installed-file verifier passes (`verified_accuracy.json`); all
**18/18** independent deliberate header-constant/fingerprint mutations reject
(`negative_headers_accuracy.json`). This extends the original eight negative
tests to all twelve scalar contracts and six fingerprint arrays. The focused
public Release build succeeds without observer compiler warnings and its
native observer fixture passes **121 checks**, preserving the original 53
builder checks. Added cases cover exact getter arguments and float payloads,
project/unrelated callers, source changes, disabled logging, nested builder
and accuracy calls, independent queue overflow, and both native getters
blocked while cooperative Stop waits. Build/test outputs are `accuracy_build.log`
and `accuracy_tests.log` in the same provenance directory. Full integrated
build/CTest results are owned by the primary integration pass.
