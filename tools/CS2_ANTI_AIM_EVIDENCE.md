# Anti aim command and local view evidence

Read-only installed-client investigation, October 16, 2026. Target is the Windows
AMD64 client in the documented Steam installation. `client.dll` timestamp is
`0x6AD01314`, image size `0x299D000`, SHA256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
No game launch, process access or server interaction was performed. Existing
movement/history evidence supplies the command layouts and callback ordering.

## Command production and independent physical view

`CCSGOInput` is the embedded object at `0x257B640`, vtable `0x1C9DB20`, verified
by constructor and exact RTTI. The physical input view is three contiguous floats
at input `+0x688/+0x68C/+0x690`, corresponding to input per-slot state `+0x228`
plus `+0x460`. At `0xB663DC..0xB66450`, the command producer copies those values
to stack storage, optionally dispatches input slot 32, then passes the values to
`0xB85D70`, which owns copying them to the base command QAngle message. Slot 32
is `0xB78350`, an immediate false return on this exact class/build.

The movement finalizer slot 6 is then dispatched at `0xB66973`. Full function
`0xCECF90..0xCED2BD` constructs and serializes a temporary protobuf from the
base command angles and native button masks, writing the result to base move_crc.
It never consumes its input argument or writes input camera state. A base-angle
change must precede that original finalizer (or run the verified trampoline
again afterward). Existing movement assistance must see the physical command
before anti aim changes its basis.

Input history is constructed later through slot 25. The ordinary builder loop
at `0xD027B0` reads input `+0xBE0` records with `0x60` stride, passing each record
to `0xCDE100`, then `0xCFA6B0`. The latter copies record angle floats at
`+0x10/+0x14/+0x18` into history QAngle `+0x18/+0x1C/+0x20`. It does not derive
those angles from the command's base angle message. Leaving physical input and
history alone therefore keeps native shot history independent of the spoofed
movement/base angles. Existing silent aim can continue to edit shot history.
The retained native history caveats in `CS2_SILENT_AIM_EVIDENCE.md` still apply.

Base upmove is `+0x60`, presence mask `0x100`, independently established by
`0xB663D0..0xB663DB`; ingestion copies it to services `+0x1A8` at `0xA77658`.
Named native input enum records independently establish `IN_ATTACK=1`
(`0x1B28D10`), `IN_USE=0x20` (`0x1B28DB0`), and `IN_ATTACK2=0x800`
(`0x1B28E50`). Primary-attack/use passthrough also accounts for intra-command
subtick presses instead of checking only final held buttons. Secondary attack
intentionally keeps anti aim active across held buttons, attack indices and
subtick presses.

## Historical movement and command boundaries

The current native base SetupMove is `0xA74890`, with ABI
`void(services*, command*, moveData*)`. The CS-specific override `0x8E47E0`
calls it at `0x8E47FA` while preserving those three arguments. At
`0xA74A24..0xA74A75`, SetupMove gates the historical services `+0x1A0/+0x1A4`
axes using historical buttons `+0x58` (masks `0x18` and `0x600`), sanitizes the
floats, and seeds both move-data `+0x20/+0x24/+0x28` and `+0x2C/+0x30/+0x34`.
These are the previous ingested effective axes; current base forward/left fields
do not replace this initial state. Current command angles establish the view
basis independently.

`0xA74B30` decodes command subticks: nonzero raw button uses the digital path,
and zero button copies the analog deltas. It preserves the list order and
accumulates angle deltas. At `0xA65C4D`, the integrator compares slice time with
the next event time. An event at time zero jumps past empty-slice integration
to `0xA65DC8`, still processes the event, and reaches `0xA65F56/0xA65F5D`, which
add forward/left deltas to the historical axes. Before a subsequent nonempty
slice, `0xA65C6B/0xA65C71` write the resulting axes to move-data `+0x2C/+0x30`.
At `0xA711CA`, the command dispatcher invokes ingestion slot `+0xF0` after
integration; its verified thunk copies the final command axes/buttons for the
next command.

Therefore each command-basis transition needs a serialized time-zero analog
adjustment from the historical effective axes to those same physical axes
expressed in the current command basis. This includes enabling, disabling and
attack/use passthrough. Transformed native deltas alone cannot fix that initial
boundary mismatch. The generated step must use the verified native arena and
append ABI, sort before other equal-time events, carry no digital button, and
be tracked/restored across native command rebuilds. A post-SetupMove local
correction would not serialize the boundary adjustment. These decoded consumer
sites are frozen by `inspect_anti_aim.py`; the allocation/append contracts are
frozen by the included movement verifier. This establishes the installed
**client** consumer. It does not separately verify a server DLL's consumer or
claim observed server movement.

## Walking/airborne basis correction

The installed AirMove uses the command view at move-data `+0x14`:
`0x8E580D..0x8E5820` passes it to native AngleVectors `0x1742AF0`, with forward
and side output vectors in stack storage. `0x8E58CE` zeros the forward Z,
and `0x8E58DE` zeros the side Z. The following paths normalize those horizontal
vectors independently (`0x8E5923..0x8E5945` and `0x8E59E1..0x8E5A06`), with
separate exceptional-length normalization branches. At `0x8E5A5B..0x8E5A90`,
the native code multiplies those normalized vectors by forward and side axes
and adds their components to form horizontal wish movement.

Correction for ordinary walk/air movement must use that flattened, independently
normalized basis rather than a full 3D pitch rotation: vertical forward from
pitch does not become an upmove request. Finite pitch beyond a vertical boundary
can reverse horizontal forward. Exactly vertical pitch has a singular
horizontal forward and requires an explicit no-op/rejection policy instead of
inventing a direction. General roll can also make the horizontal pair singular;
the correction must reject a singular or nonfinite basis and preserve the
original command. These native consumer windows are frozen in the anti aim
verifier. Noclip, swimming and ladder motion have separate movement contracts
and are outside the supported walk/air correction path.

## Camera reset would otherwise expose spoofed angles

Keeping input untouched during command mutation is insufficient. Verified input
slot 8 is `0xCEA280`, with ABI `void(input*, int splitScreenSlot)`. It preserves
RCX/EDX and calls base input reset `0xB6C980` at `0xCEA289`. The native caller
ignores return values. The base method immediately returns for nonzero slots.

For slot zero it obtains the local controller using `0x96A8B0`, obtains its
current sequence with `0x941690`, and finds its sequence-checked command using
`0x9444A0`. At `0xB6CA17..0xB6CA48` it follows command `+0x40` -> base `+0x40`,
loads pitch/yaw/roll and calls native angle normalization `0x180160`. At
`0xB6CA5C/0xB6CA64`, it writes those command angles directly to input
`+0x688/+0x690`. This can expose a spoofed command as the local camera view.

After these stores the only native call is `0xB601A0`. The complete function
shifts the timestamps of input events and clears per-slot time/delta fields;
it does not consume input angles. The remainder of `0xCEA280` clears history
flags, attack indices and counters and does not consume the angles either.
Consequently a wrapper can copy the current physical input float3 before
calling native slot 8, allow all original lifecycle work, and restore that same
float3 afterward. No command pointers or historical command angle mutations
are needed for this compensation. It must preserve all native arguments and
run on the native owner. Source changes invalidate feature state.

The direct-displacement store scan also found physical input angle updates in
the mouse/input path at `0xB7C7BB/0xB7C7C4`; those read the existing input view
and add input yaw. They do not load command base angles. The older-slot input
buffer path separately stores its view at record `+0x430/+0x438`; its values
come from the input view, not this spoofed command. This is static evidence of
the traced command/reset and physical-input paths, not a universal proof of
every camera mode, replay source, observer mode or future code path.

Compensation is still needed for retained spoofed commands after disabling or
stopping command mutations. A forwarding reset hook that has stopped correcting
the camera would permit a later native reset to expose those angles. Keep the
necessary compensation code resident through normal process exit; stopped and
resident does not mean physically unloaded. Shared locks must not be held while
calling code that can synchronously acquire the same lock again.

## Reproduction and verification

### October 16 failure: sparse base QAngle fields

The offline bot-match log `logs/overlay-21024-134366360999930010.log`
records `edited=0`, increasing rejection counts, and `state=command_layout`.
The pre-finalizer snapshot incorrectly required all three QAngle presence bits.
Its test fixture used presence `0x8007` with a zero roll; that does not model the
ordinary native base producer's output. This establishes a concrete rejection
cause; the repaired behavior still requires a fresh offline observation.

The installed producer `0xB85D70`, called at `0xB6644B` before finalizer dispatch
at `0xB66973`, sets base `+0x10` bit `4` at `0xB85DF6` and obtains its QAngle
pointer from base `+0x40`. It then treats each float separately:

| Field | Native zero branch | Native nonzero branch |
| --- | --- | --- |
| Pitch `+0x18`, bit `1` | `0xB85E2E` writes zero; `0xB85E31` clears bit 1 | `0xB85E3A` sets bit 1; `0xB85E41` writes float |
| Yaw `+0x1C`, bit `2` | `0xB85E52` clears bit 2; `0xB85E55` writes zero | `0xB85E5D` sets bit 2; `0xB85E63` writes float |
| Roll `+0x20`, bit `4` | `0xB85E74` clears bit 4; `0xB85E77` writes zero | `0xB85E7F` sets bit 4; `0xB85E85` writes float |

These branches compare to floating-point zero, including negative zero, and
canonicalize omitted payloads to positive zero. Unordered comparisons take the
nonzero branch; finite-value validation remains necessary. With ordinary zero
roll the presence low bits are at most `3`; looking level can leave only yaw
bit `2`, and an all-zero view can have no scalar bits. Native reset reads all
three floats directly at `0xB6CA35..0xB6CA46`, without checking scalar presence.
Accepting absent scalar fields therefore also requires their native zero payload
invariant. Editing a zero yaw to nonzero must set its presence bit; changing
pitch to zero must update its bit, with original presence restored alongside
original values when reverting or exposing the physical replay view.

Command `+0x20` bit `1` is set by `0xB6612A`, before this producer and finalizer.
Base bit `4` is also set before finalization as above. Neither requirement is a
post-finalizer-only assumption. History is built later at `0xB66A43`; its
converter `0xCFA751..0xCFA77D` unconditionally sets all three angle bits, which is
a distinct producer contract. The verifier now freezes the independently decoded
base sparse-field branches and command-base presence timing in addition to the
existing physical input and history producers. No offsets changed.

The CRC finalizer does not require the original angle message's cached size.
At `0xCECFDC..0xCECFF0` it reads the three raw floats, normalizes them to a
stack value, constructs a fresh temporary base at `0xCECFFF`, and obtains a
fresh temporary angle message at `0xCED03D`. Stores `0xCED047..0xCED06B`
set all three bits and copy all three normalized floats into that temporary.
It leaves the original base-angle presence unchanged. The call at `0xCED150`
computes the temporary base size using `0x4F57B0` before serialization at
`0xCED182`. The base size function calls the QAngle size function `0x6449A0`
at `0x4F599F`; this recomputes five bytes per present scalar from presence
bits, then passes angle `+0x14` to `0x1238820`, which writes the cached size
at `0x123884A` or `0x1238857`. Thus the decoded base size path refreshes nested
angle sizes instead of trusting their previous values. The complete later
network-send caller chain was not separately traced in this repair; no cache
invalidation writes or new cache-layout dependencies were introduced.

## Revision 3: copied ingestion history and downward pitch

Checkpoint `b6c6137` preserves revision 2. The user confirms normal local view
but reports very slow/reversed movement. The supplied revision-2 run
`logs/overlay-39816-134366368765959678.log` shows edits and identical angle
values at ingestion while both `edited_ingests` and `overwritten_ingests`
remain zero. Both counters were behind exact command-address matching, with
further base/angle-address checks afterward. Restoration entries were also
discarded on native same-sequence rebuilds.

Native clone `0xB5E310` calls generated protobuf copy `0x7B4DE0` for command
`+0x10`, then explicitly copies native sequence `+8` at `0xB5E33F..0xB5E345`.
Producer call `0xB65F5F` exercises this clone for a stack backup. This proves
native command copying preserves consumed values and sequence; that one call
alone does not establish the complete later prediction-copy chain. Dispatcher
`0xA7102C` and `0xA711BE` pass its current command to SetupMove and ingestion.
These sites and the full clone body are frozen by the verifier.

Historical axes are raw final base values: ingestion copies base forward/left
at `0xA77638/0xA7763B` and `0xA7764F/0xA77652`, irrespective of scalar presence.
The native producer stores zero before clearing absent axis bits. Digital
subtick events add signed unit values at `0xA65EBA..0xA65F20`; analog events
add their deltas directly. Parameter preprocessing at `0x8C4AEB..0x8C4B12`
multiplies normalized slice axes by effective maximum speed. No independent
extra scaling factor was found.

The old held-W failure follows directly: base +1 becomes corrected -1; native
history stores -1; unrecognized ingestion treats that as physical input and
appends +2, yielding +1 under backwards yaw, hence backwards world movement.
Recognized history instead converts -1 to physical +1, then to outgoing -1,
requiring no additional delta. Revision 3 matches copied source/sequence,
scalar angle presence and values, raw axes and direction-down bits in bounded
publication history. It retains same-sequence variants independently of pointer
restoration. A cache miss is explicit, rather than guessed to be edited.

For this fixed yaw offset, correction is an exact signed-axis permutation.
Ordinary pitch uses (-forward, -left); upside-down pitch uses
(forward, -left). The same physical view defines the inverse. This preserves
small inputs and avoids tiny trigonometric residues generating boundary steps.
The standalone pipeline test copies actual command/base/angle buffers to new
addresses and runs 1,000 corrected/physical command transitions through the
historical-plus-delta model, including held WASD, diagonals and action handoffs.
It does not simulate authoritative server physics or establish observed speed.

The clarified look uses pitch +89. AngleVectors `0x1742AF0` obtains pitch sine
through `tier0!V_sincosf`, negates it at `0x1742BA3` and stores forward Z at
`0x1742BAF`: positive pitch points down. Constructors `0xCE9E0` and `0xCEB20`
register `cl_pitchdown`/`cl_pitchup` with default 89 (`0x1C66258`). Native clamp
`0xB63B70` reads those live cvars, negates the upward limit and clamps pitch
inclusively. These are ordinary defaults; custom cvars can differ. No cvars or
installed game files are changed by this implementation.

`python tools/inspect_anti_aim.py` reads installed files only. It freezes the
input RTTI/vtable, native reset call graph, base physical-view producer, history
record producer/converter, upmove and named input enums. Full native reset,
post-reset timestamp helper, CRC finalizer and history builder bodies are also
frozen by independent hashes. `--header PATH` checks temporary source mutations
without changing installed game files or rewriting offsets. Existing movement
and silent aim instruction checks are included.

Research scripts under `logs/research/anti_aim_*.py` use the existing MIT
Capstone/pefile research tooling; dependency licenses remain in their original
workspace locations. Successful static checks establish binary/layout
compatibility. They do not establish observed local camera smoothness, movement
response, model facing, server receipt or shooting behavior.

## Configurable yaw and pitch: raw subtick movement contracts

Read-only inspection on October 18, 2026 used the same installed client identity
listed above. No client launch, process access, server interaction or additional
engine API was needed. Configurable yaw uses the existing independently
normalized horizontal walk basis; movement remains limited to the supported
walk/air path. Keeping each native yaw delta on both physical and outgoing
views preserves the selected relative yaw throughout one command. Pitch Keep
retains native pitch deltas; other pitch modes suppress their outgoing raw
payloads. A physical forward-basis sign crossing still requires rejection.

The complete decoder window `0xA74CF3..0xA74D62` establishes these raw-field
rules independently of protobuf scalar presence:

- `0xA74CF3` loads the raw button at step `+0x18`; `0xA74CFB` tests it.
  A zero button copies raw forward/left deltas at `+0x28/+0x2C` into the
  decoded event. Any nonzero button instead copies the pressed bool.
- `0xA74D18` tests nonzero raw masks against `0x801`; matching attack events
  enter a separate action list, after which the movement event's button and
  analog storage are zeroed. Adding analog payload to a digital event does
  not combine both forms of input.
- `0xA74D49` and `0xA74D58` add raw pitch/yaw deltas at `+0x30/+0x34` for
  every event, including digital events. Clearing their presence alone cannot
  suppress their effect in this installed consumer.

The exact digital-axis window `0xA65EBA..0xA65F23` compares raw button by
equality with forward `8`, back `0x10`, left `0x200`, and right `0x400`.
It adds respectively `(+1,0)`, `(-1,0)`, `(0,+1)`, or `(0,-1)`, negating the
delta on release. Loads `0xA65B49/0xA65BCE` obtain the independently checked
float constants `+1/-1` at `0x1AC6074/0x1AC60AC`. Other masks do not receive
one of these movement-axis additions, even when they contain direction bits.
The updated verifier freezes both complete windows and these unit loads/data.

Consequently the generic configurable command path converts an exact
single-direction digital event into a button-zero analog event containing the
transformed signed unit pair. Ordinary analog events transform both components
together, updating both presence bits when rotation introduces a previously
zero component. Event time, order, unrelated actions and native yaw deltas are
preserved. Mixed masks involving direction bits are rejected instead of being
split into invented transitions. A nonzero raw button selects its native branch
even when the button presence bit is absent.

The generic path rejects nonzero payloads whose corresponding scalar presence
is absent, rather than allowing local raw-field consumption to disagree with
serialized presence. The existing default fixed backwards/down branch retains
its exact signed-axis transformation and inactive payload bytes; it is a
separate compatibility path, not evidence that arbitrary rotation can preserve
inactive analog bytes. Existing ownership, finite-value and layout checks
continue to apply.

Final base axes are rotated together and their presence/direction-down bits
must represent the resulting pair. This is necessary for the next command:
`0xA77638..0xA77652` ingests raw final base axes without scalar-presence gates,
while `0xA74A24..0xA74A75` gates historical forward/left using direction pairs
`0x18/0x600`. The serialized time-zero historical boundary correction remains
necessary when yaw or pitch settings change and during existing passthrough
transitions. These are installed-client static contracts; configurable settings
and successful compatibility checks do not establish observed movement,
visible model facing, server receipt or authoritative server physics.

## Revision 5: prediction rebuilds and physical direction feedback

Read-only inspection on October 18, 2026 confirms the same installed AMD64
client hash and patch 1.41.9.1 / client-server 2000933. The supplied revision-4
log `logs/overlay-32200-134368034829666506.log` contains 41,637 finalizer calls
with last edited sequence 17,240 and recognized edited ingestion. Generation and
ingestion are not a one-to-one timeline. Static rotation tests that immediately
ingest each generated command do not model rebuilding the same sequence after
its prediction already ran.

The previous implementation used the most recently ingested movement-service
axes/basis for a serialized boundary. If that ingestion was N itself, rebuilding
N could calculate a zero boundary even though prediction restarts N from N-1.
A static relative heading hides this difference. Changing headings expose it.
The added regression reproduces incorrect world wish in over 1,000 changing
commands when that zero boundary is applied to the predecessor's axes.

The native controller lookup at `0x9444A0`, called by replay at `0xB6C9BC`, has
ABI `command*(controller*, int sequence)`. It calls `0x944530` for the controller's
command ring, rejects nonpositive sequences, indexes `sequence % 150` with stride
`0x98`, and returns null unless the slot's native sequence at `+8` matches. The
ring helper resolves the controller index through `0x15DF9F0`; its existing-ring
branch returns directly. Its missing-ring branch allocates/initializes a ring;
normal finalization already owns a command in that ring. The complete lookup
and ring helper bodies, native replay call target and runtime entry bytes are
verified. No ring pointers are retained across callbacks.

Revision 5 reads sequence N-1 through this lookup and matches its copied wire
values to publication history, recovering the physical axes before expressing
them in N's outgoing basis. The movement planner uses the same source. Unknown
edited predecessors reject correction instead of borrowing a different
prediction sequence. The initial pre-edit fallback can still use physical
native services when no readable predecessor exists. Stopping/disabling retains
the predecessor handoff and replay compensation until source change or exit.

Replay has another relevant side effect. At `0xB6C9F7/0xB6C9FE`, it clears input
`+0x260/+0x268`; at `0xB6CA05..0xB6CA10`, it loads command native down `+0x60`
and stores it to input **previous-down `+0x250`**. Producer
`0xB668F0..0xB6694B` combines previous-down, **current physical `+0x258`**,
changed and tap fields to encode command buttons. Thus restoring camera angles
alone leaves rotating command directions feeding physical button transitions.
The wrapper now captures current physical directions before native replay and
replaces only direction bits in previous-down afterwards. It advances to the
current input rather than retaining a stale previous mask. Native action bits,
changed/tap resets and timestamp processing remain native. Both destinations
are preflighted and the source lifecycle is revalidated before restoration.

Regression coverage adds 9,216 actual pattern-command/replay transactions,
4,608 rebuild/predecessor lookups and 10,000 fresh view/key transitions, including
presses, releases, diagonals, reversals, native action changes and protected
storage. The unchanged original view-only wrapper fails the new direction and
held-edge checks. Missing/reused/wrong-sequence ring slots reject without
changing the caller output. Existing 6,144 copied-command/subtick handoffs
continue to run. These checks establish copied-value behavior and the installed
client contracts, not observed live movement or authoritative server physics.

## Round-start freeze period, October 18, 2026

New angle edits now wait while `C_CSGameRules::m_bFreezePeriod` is true.
Warmup remains permitted; there is no match-start or warmup prerequisite.
The native RIP-relative load at `client+0x817FA0` (`48 8B 05 29 D2 D4 01`)
resolves the rules pointer at `client+0x25651D0`. Named schema registration
`0x2212A78` identifies `C_CSGameRules` and its 98-entry field array at
`0x2211E20`; `m_bFreezePeriod` supplies byte offset `0x40`. These are the same
installed client and existing weapon-readiness contracts. The anti-aim verifier
now independently checks the registration, field and global-load operand
against the anti-aim header.

The command owner rereads this state each command, so every later round pauses
again and edits resume immediately after freeze ends. Null/unreadable rules or
non-boolean freeze bytes pause new edits. Retained command history, physical
movement handoffs and replay compensation remain available during the pause;
the user's enable setting is unchanged. No new hook or loading requirement is
introduced.

Standalone checks cover warmup, entering/leaving freeze, later-round reentry,
rules replacement, invalid and inaccessible state, and recovery. Public and
diagnostic Release builds each pass all 42 CTest suites. Installed-file anti-aim
verification passes. No CS2 client/server was launched for this change; live
round behavior remains unobserved.
