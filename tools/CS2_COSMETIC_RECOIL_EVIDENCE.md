# Cosmetic camera recoil - October 25, 2026



The user confirms that camera recoil removal now works. Their latest request

removes the separate weapon-recoil feature entirely. The final implementation

keeps **Remove camera recoil** and the existing viewmodel X/Y/Z/FOV controls;

there is no weapon-recoil setting, selected weapon getter, cached HUD-basis

correction API, or weapon-basis publication in this component.



The confirming operator run is

`logs/overlay-37228-134373926904598470.log`: native camera diagnostics record

30,081 additional view-punch edits, including 27,091 nonzero impulses. These

counters support execution of the added path; the user's separate visual

observation establishes the reported camera result. Authored weapon fire/reload

animation remains native. Removing the abandoned weapon feature does not change

the working camera suppression path.



## Installed target and reproduction



Investigated the installed Windows AMD64 **client**, patch **1.41.9.2**,

client/server **2000935**, Source revision **11162119**, Steam build **26144732**.

Installation:

`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`;

executable `bin/win64/cs2.exe`, inspected module `csgo/bin/win64/client.dll`.

Client SHA-256:

`ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5`;

PE timestamp `0x6ADA7340`, SizeOfImage `0x29A0000`. The existing project uses

its D3D11 overlay. Agent investigation was file-only; the operator performed

live observations. No installed game file was changed.



Runtime header:

[cosmetic_recoil_offsets.h](../include/cosmetic_recoil_offsets.h).

Run the frozen standard-library verifier with

`python tools/inspect_cosmetic_recoil.py`, or use the combined compatibility

command. It accepts `--client` and `--header`, rejects unfamiliar binary hashes

and never launches/opens a game process. Reports and disassembly are under

`logs/research/cosmetic_recoil_20261025/`.



## Two separate native camera contributions



The first implementation suppressed only the native visual aim-punch getter.

An operator run in `overlay-18276-134373922380155709.log` proved thousands of

successful callback edits and nonzero cleared vectors, yet the user still saw

camera motion. The missing contribution was the independently decayed camera

view-punch vector applied before that getter. Synthetic output-clear tests had

not established complete visible camera behavior.



| Contribution | Base camera core `0x883090` | CS player camera core `0x883360` |

| --- | --- | --- |

| Decayed camera-service view punch | Native add `0x883228`, return **`0x88322D`** | Native add `0x883590`, return **`0x883595`** |

| Native visual aim punch, scaled by **0.45** | Getter call `0x88323E`, return **`0x883243`** | Getter call `0x8835B3`, return **`0x8835B8`** |



Named `CPlayer_CameraServices` schema registration `0x222D3E8`, array

`0x222D150`, count 20 independently identifies `m_vecCsViewPunchAngle=+0x48`,

`m_nCsViewPunchAngleTick=+0x54` and `m_flCsViewPunchAngleTickRatio=+0x58`.

Both native cores copy the vector into their own stack, apply the engine's

native time decay, then call leaf `0x8A7000` with that temporary. Runtime uses

the completed native operand rather than reconstructing the decay formula.



The complete leaf ABI is **void(const float[3]*left, const float[3]*right,

float[3]*out)**: RCX/RDX/R8 are the three pointers. It performs three additions

into R8 and returns without assigning RAX. The callers ignore return registers.

Runtime preserves this void ABI, always executes the native leaf and substitutes

a zero right-hand operand only for the two exact view-punch add callers above.

Native persistent camera state, every other addition, velocity roll and shake

remain unchanged. The shared visual view-punch setting also hides damage or

landing impulses represented in that same vector.



The visual aim-punch getter at **`0x853C10`** has a different ABI:

`float[3]*(AimPunchServices*, float[3]*out)` (RCX/RDX inputs, original RAX output

pointer preserved). It executes native interpolation and memoization, then

returns its caller-owned twelve-byte temporary. Runtime clears that temporary

only for the two exact camera getter returns above. Unrelated callers,

including the native viewmodel's separate 0.325 contribution, retain their

original output.



## Native class, constructor and ABI proof



Named `C_CSPlayerPawn::m_pAimPunchServices=+0x1598` comes from schema registration

`0x223BAC8`, array `0x223AD40`, count 105. Native pawn initialization calls

constructor `0x84B480` at `0xC716E0`; that constructor installs actual primary

`CCSPlayer_AimPunchServices` vtable **`0x1C0BFE8`** at `0x84B490..0x84B497`.

The pawn stores it at `+0x1598` at `0xC716ED`, then calls owner initializer

`0xA85420`. Instruction `0xA8543A` stores the owning pawn at service **`+0x38`**.

Source eligibility is therefore supported by native construction and ownership,

not only by fake callback fixtures matching a header.



RTTI independently identifies the primary camera tables:

`CCSPlayerBase_CameraServices=0x1C0ECE0` and

`CCSPlayer_CameraServices=0x1C0F4A8`. Both have slot `+0x108 -> 0x883830`;

that wrapper calls `0xA653A0`, which tail-dispatches slot `+0x110` to the two

camera cores above. The cores have ABI

`void(CameraServices*, float[3]*origin, float[3]*angles, float*fov)`. Native

`0xA653A0` moves its sixth stack argument (FOV output) into R9 before that

four-argument tail dispatch, independently proving the core argument routing.

Named base-pawn schema identifies `m_pCameraServices=+0x1328`.



The physical eye-angle getter chain was also checked. Actual pawn primary table

`0x1C8C068` has slot `+0x570 -> 0xC76160`; its local path calls `0x93D320`,

which uses slot `+0x578 -> 0x952870`. That complete leaf copies the named

`C_BasePlayerPawn::v_angle` at `+0x13A8`. Runtime does not replace physical input

angles or reset the completed camera wholesale.



## Gameplay state and callback ownership



The separate current-prediction punch getter **`0x8534E0`**, called by pawn

wrapper `0x8CD850`, and shot-time punch getter **`0x853410`**, called by the

shot builder at `0x80F17F`, remain native. The verifier checks those independent

call targets and complete wrapper/shot-add instruction windows. No command,

history, recoil index, spread, shot, camera-service or punch-service field is

directly overwritten by cosmetic camera removal.



Each selected camera core establishes a stack-owned scope with the current

native output-angle pointer and a copied local source. It requires the alive

local pawn/controller, both identity backreferences and complete serialized

handles through the entity-system bucket table, the controller's pawn link,

the exact current punch service and owner, and the current pawn's exact camera

service/table/owner. The add helper requires the matching scoped angle buffer

as both left input and output, plus a freshly identical source. The visual

getter likewise snapshots and revalidates its source around native execution.

Source changes conservatively retain native output.



The expected internal visual getter uses the camera core's existing SRW shared

barrier rather than acquiring it recursively. Other nested camera views hide

that edit scope for their entire native execution and retain both contributions.

Dynamic Win32 TLS provides these scopes; no static TLS is introduced.

Cooperative stop disarms new edits and drains the outer camera/getter barrier.

Later callbacks continue native forwarding. Code/trampolines remain resident

until normal process exit: **stopped and resident is not physically unloaded**.



## Verification and diagnostics



The frozen verifier checks **15** instruction windows, named schema and RTTI,

actual constructors/owner initialization, physical-angle and camera dispatch,

independently decoded call/member operands, **18 scalar source constants** and

**eight runtime fingerprint arrays**. The final installed real-header report is

`verified_camera_only.json`. All **26/26** individual scalar/array header

mutations reject in `negative_checks_camera_only.json`. An earlier one-byte

modified workspace client copy also rejected by the unchanged hash gate;

installed files were never modified.



Memory-backed tests cover both camera classes, camera toggle independence from

XYZ/FOV enable, preservation of native weapon/other getters, both view-punch

and 0.45 aim-punch contributions, exact pointer/void ABIs, unchanged origin/FOV

and source bytes, invalid/stale/dead sources, native source mutation, invalid

outputs, expected nested getters, unrelated nested views, and cooperative stop

during a blocked native core. Project integration reports current build/CTest

results separately. Both final x64 Release variants build without project
warnings and pass **70/70 CTest suites** each; the camera recoil suite passes
**201 checks**. All **35/35** installed compatibility components pass in
`logs/compatibility/20261025T091815604629Z/`.
These tests exercise supplied native substitutes; the user's

subsequent visual observation is recorded at the top of this document.



`COSMETIC_RECOIL` records camera-base/camera/other caller counts, requested

settings, edits/nonzero vectors, source/return/output rejections and latest

independent samples. `COSMETIC_RECOIL_SOURCE` separates each identity/link/

service rejection stage. `COSMETIC_CAMERA` records actual native core calls and

additional view-punch edits/nonzero impulses. Flush runs at most every two

seconds on the existing diagnostic owner; callbacks perform no file I/O.

Counters establish execution and rejection behavior, not appearance by themselves.
