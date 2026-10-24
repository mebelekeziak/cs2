# Quantized movement and posture-aware jumpbug — October 24, 2026

Targets the installed Windows AMD64 CS2 client **1.41.9.2 / 2000935 /
Steam build 26144732**, through the existing D3D11 overlay. The user reports
that the prior autostrafer works well with quantization disabled and asks for
similar steering under the normal enabled rule. Sources were initially clean.

## Quantization-aware steering

The installed native path rounds movement axes before converting them to a
world wish. Thus a continuous camera-relative vector cannot simply be sent
unchanged when `sv_quantize_movement_input` is enabled. The feature now reads
that replicated rule; it does not change its value or other server settings.

For quantized movement, the existing nonbraking projection solver selects one
ideal wish for the whole command interval. The command owner represents that
world wish with a matching command yaw and exactly full Forward input. Rounding
leaves that digital input unchanged. This is a different representation of the
same wish, using the normal movement rules. It does not turn server
quantization off. The existing replay owner preserves the physical camera.

Commands first receive a digital fallback selected from directions that do not
project behind velocity. If an action or failed ownership check prevents the
command-basis upgrade, that fallback remains available. It has only eight
possible directions and can coast when no useful safe direction is available;
its control is consequently more limited. That projection check uses the
sampled base view; preserved rapid mouse movement during an action can still
rotate an intermediate wish and change speed. Firing and Use retain their original
view/action inputs. During an eligible quantized strafe, the movement basis
takes priority over the configured anti-aim yaw. The anti-aim UI toggle does
not need to be enabled for movement to use the shared owner.

An upgraded command suppresses outgoing raw subtick mouse deltas so the
selected wish remains fixed during its interval. Native decoding reads these
raw floats even without presence bits. Original camera input, actions and
mouse payloads remain owned for exact restoration before a command rebuild.
The owned time-zero event also carries the actual native predecessor's input
directly, including on the first action-blocked fallback. No extra boundary
allocation is needed for either quantized representation. Repeated commands
retain their jump edge; changed keys, view, or replicated
quantization refresh steering from the frozen command-start state.

Enabled quantization uses one whole-interval wish on both ascent and descent.
With quantization disabled, the previous 32-ascent/one-descent policy remains.
The enabled path therefore does not promise identical speed gain to 32 timed
analog wishes. Collision, slopes, ground friction and prediction differences
can still change speed; the air-step model does not remove those mechanics.

## Jumpbug

The previous probe timed its attempt at standing-hull contact, then emitted
crouch/unduck inputs that changed the native origin and hull before jumping.
Saved revision-12 observations already demonstrated missed attempts and
delayed fallback hops. Exact standing contact is the wrong target for an
unduck capture maneuver.

The replacement copies standing/crouched bounds from the verified native
vector provider and uses the installed crouch/unduck origin factors. It finds
an interior event in the native two-unit ground-categorization window, on
the verified 1/64-of-tick input-time grid. Duck release and Jump down share
that time. It checks the crouched approach, native standing-hull unduck sweep
and the native-style ground probe. Ceiling obstruction, partial crouch,
inconsistent posture, steep surfaces and changing sources reject the attempt.
Production code does not hardcode player posture dimensions.

The controller reads jump rules even when ordinary bunnyhop is disabled.
A separate attempt clock survives ordinary airborne hop-phase resets, keeps
the native cooldown plus request-interval uncertainty, and rebases on a
prediction rewind. Attempts are recorded only after command publication.
An owned timed press is still explicitly released on the next command.
The probe remains a copied client prediction, not a complete native movement
simulation. Reliable jumpbugs and fall-damage avoidance require observation.

## Other movement handoff correction

Quickstop now detects Jump in all final masks and raw timed events, including
a wheel press/release with final Jump down clear. It refuses braking on such
a takeoff command and uses its existing input-preserving history release.
Quickstop's near-rest analog settling model remains separate: quantization
can round its tiny inputs to zero, leaving normal ground friction to finish.

## Evidence and references

Installed contracts and disassembly are authoritative for this build:
[movement evidence](../tools/CS2_MOVEMENT_EVIDENCE.md) and
[jumpbug evidence](../tools/CS2_JUMPBUG_EVIDENCE.md). Investigation artifacts
are under `logs/research/movement-r14/` and the existing jumpbug research path.
The initial compatibility bundle is `logs/compatibility/20261024T114558670476Z/`.

Research used the author's
[CS2 movement issue collection](https://github.com/zer0k-z/cs2-movement-issues)
and [bunnyhop analysis](https://github.com/zer0k-z/cs2-movement-issue-bhop),
especially same-slice unduck/jump, input subdivision and press cooldown. These
are research references, not a source of installed offsets. UnknownCheats
could not be accessed through the available web path. No upstream
implementation text or unverified offsets were imported.

## Use and observation limits

Restart CS2 normally if an older DLL is resident, then use **Load Overlay.cmd**.
Enable Autostrafe, hold Space and use physical WASD as before. Leave
`sv_quantize_movement_input` at **1** for the intended vanilla test. Jumpbug
remains a separate option. Compare the same route with it off/on in the
authorized offline/private environment.

`MOVE_IMPLEMENTATION revision=14` identifies this build. `MOVE_QUANTIZED`
records the rule, requested ideal yaw and digital fallback; the shared
orientation telemetry reports applied bases and fallbacks. `MOVE_JUMPBUG`
adds capture bounds, predicted clearance, posture shift and event origin.
These are bounded diagnostics, not independent proof of a successful maneuver.

No game files, server settings, loader workflow or shutdown policy changed.
Stopped code remains resident until normal process exit. No client/server
was launched or injected by the agent. The user subsequently reports that
movement works much better, with extreme character-facing twitching and subtle
viewmodel twitching. This is user observation; no measured speed comparison
or separate jumpbug reliability result is available.

## Verification

Both public and diagnostic x64 Release builds succeed without project
warnings; each passes **66/66 CTest suites**. Focused coverage includes digital
rounding fixed points, independent ideal-basis acceleration, camera/mouse
restore, attack/Use fallback, rotated predecessor histories, allocation-free
handoff, replay/replicated-rule refresh, missed-attempt cooldown and raw jump
tap brake refusal. The jumpbug fixture passes **435 assertions**, including
independent plane geometry, posture/ceiling checks, timestamp rounding and
source changes. An independent integration review identified and resolved the
first action-blocked command's missing native-history handoff.

All **33/33** installed-file components pass in
`logs/compatibility/20261024T115852087340Z/`. The jumpbug verifier checks
**19 frozen native windows**, independently decoded posture members, provider
operands, scalar constants and timing/call ordering. All **33 altered jumpbug
headers** and a QuantizeInput wrapper/pointer-slot mutation reject.

Build/test logs are `logs/movement-r14-{build,tests}-{public,diagnostic}.txt`.
`logs/movement-r14-artifacts.json` records final DLL paths, SHA-256 and PE
directories. Both DLLs remain AMD64 with zero static TLS and delay imports.
