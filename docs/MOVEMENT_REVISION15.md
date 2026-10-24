# Movement facing correction — October 24, 2026

The user reports revision 14 movement works substantially better and that
jumpbug works well. They also report extreme character-facing twitching and
subtle viewmodel twitching. The working version is preserved in **961bd64**.

## Cause and change

Revision 14 encoded every ideal strafe wish as full Forward plus a matching
command yaw. Alternating perpendicular wishes therefore rotated outgoing
facing by about 180 degrees even when the physical view remained steady.

Revision 15 selects among eight equivalent full digital directions: forward,
backward, left, right and their diagonals. For each direction it computes
`command yaw = ideal world wish - atan2(left, forward)` and chooses the yaw
nearest the physical camera. All axes remain exact -1, 0 or 1, so native
quantization leaves them unchanged. Outgoing yaw stays within 22.5 degrees
of the sampled physical view. Opposite side wishes commonly retain the same
yaw and change only the side input. Exact sector ties are deterministic.

The installed AirMove normalizes its combined horizontal wish at
`0x8E691E..0x8E6940` and caps its speed at the effective movement limit at
`0x8E6971..0x8E697D`. Consequently full cardinal and diagonal inputs have the
same final wish speed. This change preserves revision 14's ideal world wish
and modeled air impulse. It does not smooth or change the physics solution.

Both transition paths use the selected digital axes: raw native predecessor
handoff and the ordinary rotated predecessor. Final axes, protobuf presence
and directional buttons agree. Raw diagonal history retains its magnitude;
normalizing stored history would break the next command's handoff. Existing
restore/replay ownership, fixed outgoing subtick basis and action fallback
remain in use. Jumpbug logic and timing are unchanged from 961bd64.

## Limits and use

Choosing another digital sector can still create a 45-degree yaw step. Native
animation and viewmodel response may retain some visible movement. This is
an exact alternate input representation with bounded facing deviation, not
a claim of completely stationary model angles or observed visual smoothness.
No native model-angle writes or new hooks were added.

Restart CS2 normally before **Load Overlay.cmd** if an older DLL is resident.
Keep the same Movement settings and quantization enabled for an offline/private
comparison. The startup log identifies `revision=15` and
`facing=nearest_digital`. Stopped code stays resident until normal process exit.

No client/server was launched or injected by the agent. User feedback above
describes revision 14, not an observed revision 15 result.

## Verification

Both x64 Release variants build without project warnings and pass **66/66
CTest suites** each. The focused anti-aim suites pass **6/6**. New checks cover
20,167 camera/wish combinations, independent normalized wish and native-style
capped acceleration, the 22.5-degree facing bound, opposite-wish sector ties,
raw diagonal predecessor publication, allocation-free handoff and repeated
restore/reapply. Independent read-only review found no integration blocker.

Build/test outputs use `logs/movement-r15-{build,tests}-{public,diagnostic}.txt`.
Final DLL hashes and zero static TLS/delay-import directories are recorded in
`logs/movement-r15-artifacts.json`. Native dependencies are unchanged from the
revision 14 **33/33** static compatibility bundle.
