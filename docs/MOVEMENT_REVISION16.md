# Anti aim with quantized movement — October 24, 2026

Revision 16 composes the existing anti-aim settings with the working quantized
autostrafer. The previous version, preserved in **f8f26f8**, chose its exact
movement representation near the physical camera and therefore replaced
configured anti-aim angles during assisted movement.

## Combined command

When both features are enabled and eligible, the command owner first generates
the configured anti-aim target from the original physical view and command
sequence. Static, Jitter, Spin and Random yaw use their existing View/World
base and sequence phase. Every pitch mode retains its configured result.

The owner then chooses the exact digital movement representation nearest that
anti-aim target yaw. The ideal world-space strafe wish and modeled movement
impulse remain unchanged. This uses the same eight equivalent representations,
native input-time event and raw predecessor handoff as revision 15. The chosen
absolute yaw is assigned directly after pattern generation; it is not fed back
through the View offset or sequence pattern a second time.

Quantized movement constrains the available yaw representations. The outgoing
yaw is within **22.5 degrees** of the requested anti-aim yaw, rather than being
an exact arbitrary yaw. Small requested yaw changes can select the same
available representation; sector changes can still create 45-degree steps.
Pitch follows the configured pattern exactly within existing supported bounds.
The UI states the yaw limit beside the anti-aim controls.

With anti aim disabled, quantized movement retains revision 15's camera-based
representation and physical pitch. With quantization disabled, the ordinary
anti-aim/analog-movement path is unchanged. No settings or config keys were
added. Existing primary/secondary attack, Use, indexed-shot and timed-action
checks retain the physical-view digital fallback for those commands.

The combined path retains strict movement payload validation, including for
the default Static/View/180/Down anti-aim preset. The older relaxed legacy
payload path remains exclusive to ordinary anti aim. Copied history publishes
the original physical view, not the anti-aim target. Raw diagonal axes, exact
restore/replay, fixed outgoing subtick view and existing allocation-free
handoff remain in use. Jumpbug code and timing are unchanged.

## Use and limits

Enable Autostrafe and Anti aim using their existing controls, then choose the
usual yaw and pitch settings. Keep the same authorized offline/private test
and quantization rule. Restart CS2 normally before **Load Overlay.cmd** when
an older DLL is resident. Stopped code remains resident until normal exit.

`MOVE_IMPLEMENTATION revision=16` identifies the integration, and anti-aim
diagnostics use `revision=8`. `movement_anti_aim_edits` counts successfully
composed commands; state identifies `anti aim + quantized steering`. These
diagnostics describe command edits, not independent server or model-facing
observation.

No client/server was launched or injected by the agent. Combined runtime
behavior remains unobserved; the earlier user feedback confirms working
movement/jumpbug, not this new composition. No native offsets, dependencies,
hooks, game files, server rules or loader/shutdown workflow changed.

## Verification

Both x64 Release builds complete without project warnings and pass **66/66
CTest suites** each. All **6/6** focused anti-aim suites pass. Added coverage
includes **1,728** yaw/base/pitch/phase/view/wish combinations, exact configured
pitch and normalized wish, the 22.5-degree target-yaw bound, raw native-history
handoff, fixed subtick basis, physical view restore/replay, disabled-AA baseline
equivalence and 64 consecutive AA on/off transitions. Rejection fixtures also
exercise enabled AA with actions, unsupported payloads and stale ownership.

The installed dependencies are unchanged from the prior **33/33** static
compatibility bundle. Build/test outputs use
`logs/movement-r16-{build,tests}-{public,diagnostic}.txt`; final hashes and zero
static TLS/delay-import directories are in `logs/movement-r16-artifacts.json`.
