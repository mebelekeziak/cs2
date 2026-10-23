# Player chams native ownership and packet contracts

## Static recertification, October 21, 2026

Current installed Windows AMD64 client: patch `1.41.9.1`, client/server
`2000934`, Source revision `11150591`, Steam build `26087496`; version date
October 20, 2026, 11:41:22. The installation path below is unchanged.
`client.dll` SHA-256 is now
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
timestamp changed `0x6AD01314` -> `0x6AD7B41C`; image size stays `0x299D000`.
The full scene/material/tier0 identities and eight shader/texture assets remain
unchanged. The shared ESP verifier supplies the new strict client pin.

All three client scene-ownership instruction windows and all 20 scene windows
match exactly. Named descriptor lookup, RTTI-derived generation vtable/slot,
owner-handle reconstruction, skeleton-interface association, packet layout,
material ABI and complete sort-order evidence retain their existing values.
The material verifier also passes all frozen native windows and assets; see
[material recertification](CS2_PLAYER_CHAMS_MATERIAL_EVIDENCE.md).
`player_chams_offsets.h` requires no changes. The callback's four-argument ABI,
native-first generation, per-callback borrowed-pointer lifetime, and cooperative
resident-stop guarantees remain as documented below.

The full contract bodies passed in scratch research before changing pins:
`logs/research/26087496/render_grenade/initial-contract-checks.json`.
`python tools/verify_player_chams.py` subsequently passes normally. Four wrong
header copies (owner member, descriptor vtable, packet stride, entry bytes) and
a readable workspace client copy with one changed byte all reject. Details are
in `verification-and-negative.json` in that directory. Build/CTest results are
owned by the integrating update. No client/server was launched or opened;
actual player appearance, animation and occluded rendering were not observed
on this build.

## Historical October 18 baseline

Read-only installed-file investigation, October 18, 2026. Windows AMD64 CS2
client 1.41.9.1, Microsoft x64 ABI, existing D3D11 overlay. No game process or
server was started. Installed game files were only read. Run
`python tools/verify_player_chams.py`; its `--game` option selects the game root
and `--header` selects a source header to verify, never to regenerate.

## Binary identities and descriptor

The installed root is
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
`csgo/bin/win64/client.dll` SHA-256 is
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`.
`bin/win64/scenesystem.dll` SHA-256 is
`040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`.
These are full file identities. Runtime separately verifies the existing client
PE timestamp/image size, scene timestamp `0x6ACFCD4F` / image size `0x978000`,
callback entry bytes and exact vtable dispatch.

`CAnimatableSceneObjectDesc` primary RTTI vtable is scene RVA `0x5E5C90`.
Its primitive-generation virtual at byte slot `0x20` points to `0x7D7C0`.
Client `0xAC575C` supplies the exact `AnimatableSceneObjectDesc` string during
native creation. This descriptor is distinct from the existing static world
aggregate descriptor and does not classify all animated objects as players.

The callback has four register arguments: descriptor, scene object,
`GeneratePrimitivesArgs_t*`, and `DrawListVector_t*`. Register preservation at
`0x7D7F9`, `0x7D804`, `0x7D80F`, and object/view/list consumers establish this
ABI. Args `+0x10` supplies the actual scene view (`0x7D85F`). The original method
runs first. Only the newly appended range is considered. The existing native
fixed/overflow vector layout has `0x70` packet stride, independently confirmed
by this path's multiply at `0x7E474`. Original calls and material variant calls
run outside the mutation lock because the engine may dispatch work.

## Exact player association

The native animatable creator begins at client `0xAC5320`. RSI is the owning
`CSkeletonInstance*`; its model resource at `+0x1E0` agrees with the separately
verified model-swap and named-bone contracts. The scene object returned by the
scene-system create virtual at `0xAC578B` is retained in RBX.

At `0xAC5799..0xAC57A7`, the native creator writes skeleton `+0x130` into scene
object `+0x110`. This is an interface pointer within the exact skeleton. The
runtime requires that pointer to equal the current pawn's schema-associated
game-scene-node pointer `+0x130`, and verifies that node's installed skeleton
vtable. A weapon, attachment, stale scene object or another pawn's skeleton does
not satisfy this association.

At `0xAC5818..0xAC5859`, the creator follows skeleton `+0x30` to the entity, then
entity `+0x10` to its identity, reads identity handle `+0x10` and flags `+0x30`,
and constructs the scene owner handle. With raw identity handle `h`, the native
formula is `(((h >> 15) - (flags & 1)) << 15) | (h & 0x7FFF)`. The subtraction
and shifts are unsigned, including serial wrap. A missing entity or identity
takes a separate native `0xFFFFFFFF` branch. Runtime rejects a raw invalid
identity handle before serial reconstruction; it never resolves an invalid
identity even if correction would produce another bit pattern. The native
result is stored at scene object `+0xC0` by `0xAC5859`.
Scene `+0xB8` is not used as an owner field.

Runtime resolves that full serialized scene owner through the current entity
system's embedded bucket table. It requires the identity/entity backreference,
the same reconstructed full handle, a reciprocal serialized controller-to-pawn
link, the exact skeleton association, alive life state, non-dormancy and playing
team. Local filtering additionally validates the current global local
controller/pawn pair and reciprocal links. After the native material lookup,
it repeats source validation before writing. This packet adapter retains no
borrowed entity, scene object or packet pointers across callbacks. The material
component intentionally retains its immutable native material bindings and
resource ownership until normal process exit. The existing entity and skeleton
verifiers remain dependencies of the combined compatibility check.

## Material tuple and color ownership

Packet constructor stores at `0x7E5D7..0x7E5F9` establish RGBA `+0x50`, owning
object `+0x18`, material `+0x20`, and selected material variant `+0x28`.
`0x7E77B..0x7E7A8` separately establishes the two-argument material virtual at
byte slot `0x30`: RCX is material, RDX is this actual scene view `+0x30`, and RAX
is the returned selected variant. Native per-view material selection is
preserved. The **Through walls** option selects the style's immutable material
with native `F_DISABLE_Z_BUFFERING=1` and `F_DISABLE_Z_PREPASS=1`; the installed
shader feature expressions disable both depth testing and depth writes. Turning
the option off selects the corresponding material with native depth enabled.
See [material evidence](CS2_PLAYER_CHAMS_MATERIAL_EVIDENCE.md) for shader feature
and render-state evaluation. Exactly one material tuple replaces each qualifying
native draw. No duplicate pass is added, so visible pixels are not blended twice.

The packet helper changes only the material/variant tuple and packed RGBA.
It preserves the engine's mesh and skinned vertex streams, transforms, draw
flags, sorting data and packet neighbors. The UI's color channels are packed
as RGBA bytes, including opacity. Flat and iridescent styles have separate
saved colors. Material construction, shader coverage, actual shader color
consumers and their independent verification are documented by the material
component; the static-world aggregate packet color limitation does not prove
anything about this animatable path.

## Native ordering and through-wall limits

The existing layer renderer merges generated packets and sorts them before
dispatching native render batches. The layer policy at `0xED4FD..0xED560`
selects full sort when layer flags bit 0 is set, bypasses sorting with bit 1,
and otherwise uses batch sort. Full sort compares signed packet `+0x58` first
(`0x10F5E6`); native generation copies args `+0x40` to that field at
`0x7E73B..0x7E73E`. For equal render order, the next key is the scene object's
descriptor address, compared unsigned ascending (`0x10F5F2..0x10F600`). Batch
sort compares the descriptor address first (`0x1113CF`), with `+0x58` only a
later tiebreak (`0x11155B`). Both insertion and partition comparison paths
agree. Therefore changing only `+0x58` would not establish global late placement
in the batch-sort branch. The implementation preserves this field and all
native sorting flags.

The installed descriptors are fixed module objects. The aggregate descriptor at
scene `+0x674CB0` has its vtable `+0x5E4538` in initialized PE data and is
registered with `AggregateSceneObjectDesc` at `0xE672E..0xE673F`. The animatable
descriptor at scene `+0x675670` receives vtable `+0x5E5C90` in its CRT initializer
at `0x37A2..0x37A9` and is registered at `0xE622B`. Native comparisons load
packet `+0x18` -> scene object `+0x18` -> descriptor. These addresses establish
that same-layer aggregate world packets precede animatable player packets in
batch sort; the same is true in full sort when their native render order is
equal. This conclusion follows the actual static singleton addresses rather
than assuming that separately allocated objects follow a stable heap order.
The verifier freezes registration, both comparison paths and policy branches.

Depth-disabled materials can color submitted player geometry despite existing
wall depth. This does not bypass engine visibility, frustum/PVS, model or mesh
culling: if native generation emits no player packet, this adapter emits none.
Native no-sort layers, unequal full-sort render orders, geometry rendered in a
later layer and later compositing remain outside the ordering guarantee. No
late replay hook or priority-key mutation is introduced. Actual occluded-player
appearance and coverage remain unobserved until an authorized offline/private
render test.

## Lifecycle and evidence limits

Stop first disarms the callback, then drains packet writes through its mutation
lock. Native generation or material lookup already in progress may return;
post-call validation prevents further mutation. Subsequent engine generation
produces baseline packets. The original trampoline, mapped code and native
material resources remain resident until normal process exit. Stopped and
resident is not physically unloaded.

Standalone tests cover new/old/foreign packet ownership, current overflow
storage after reallocation, malformed counts, serialized serial correction,
identity backreferences, stale controllers/skeletons, team/local filters,
death/dormancy, missing materials, changes during native lookup, independent
style colors and stop while original generation is pending. Static verification
and these tests establish implementation contracts. They do not establish live
model appearance, alpha blending, GPU pass coverage or gameplay observations.
Raw disassembly is retained in `logs/research/chams-*.txt`.

The public and diagnostic Release `player_chams_checks` executables pass 139 checks each. The
installed-file verifier passes, and 15 deliberately changed header constants or
entry bytes are rejected (`logs/research/chams-negative-contracts.json`).

Material recipes, native KV3 lifetime, resource ownership, shader inputs and
consumer evidence are documented in
[material evidence](CS2_PLAYER_CHAMS_MATERIAL_EVIDENCE.md). The top-level player
verifier includes that material/asset verification; `--material-header` selects
an alternate material header for negative checks.

The integrating public and diagnostic Release builds each pass all 47 CTest
suites, including 139 player-adapter checks and 132 material-lifecycle checks.
The shared frame dispatcher is tested directly and through the model-swap
regressions. Both payloads have no static TLS/delay imports. This verification
did not launch CS2 and does not establish visible appearance or correct sorting
of translucent players: original packet pass/sort classification is preserved.

## October 23, 2026 recertification: Steam build 26144732

Installed Windows AMD64 client patch **1.41.9.2**, client/server **2000935**.
Read-only investigation used the documented Steam game installation; no game
process was opened or launched, and no installed files were changed.

| Module | PE timestamp / image size | SHA-256 |
| --- | --- | --- |
| client.dll | `0x6ADA7340` / `0x29A0000` | `ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5` |
| scenesystem.dll | `0x6ADA337D` / `0x978000` | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` |
| materialsystem2.dll | `0x6ADA32D5` / `0x186000` | `2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69` |
| tier0.dll | `0x6ADA3248` / `0x401000` | `f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d` |

Research scripts, candidate searches, decoded current instruction windows and
positive/negative JSON reports are retained under
`logs/research/26144732/render_grenade/`. Optional discovery used Capstone
**5.0.9**; the frozen verifiers remain standard-library Python and reject
unrecognized complete module hashes before checking contracts. Discovery
masked only instruction-decoded RIP-relative displacements and relative branch
immediates to locate candidates; the final frozen checks contain exact current
bytes/hashes and unmasked independently decoded operands.

Client scene creation was independently rediscovered from the complete skeleton
assignment/serialized-handle producer: descriptor-name LEA moves
`0xAC575C -> 0xAC659C`, skeleton interface assignment
`0xAC5799 -> 0xAC65D9`, and owner producer
`0xAC5818 -> 0xAC6658`. Exact new bytes preserve scene `+0x110`, skeleton
`+0x130`, scene owner `+0xC0`, and serial correction by `identity.flags & 1`.
The named descriptor target still reads `AnimatableSceneObjectDesc`.

Scene animated descriptor primary vtable `0x5E5C90`, generation slot `+0x20`
function `0x7D7C0`, four-argument ABI and packet members remain unchanged:
stride `0x70`, object `+0x18`, material `+0x20`, mode `+0x28`, RGBA `+0x50`,
view argument `+0x10`, material mode slot `+0x30`. All exact generator, packet,
view/mode and sorting windows were checked against the new image.

Static singleton registration RIP operands changed: aggregate
`0x674CB0 -> 0x674CA0`, animated `0x675670 -> 0x675660`; both initialization
and registration independently resolve the animated singleton. Aggregate
primary vtable is now `0x5E4518`. Existing descriptor-address sorting still
puts aggregate before animated within equal applicable native layer/order;
full-sort signed `packet+0x58`, descriptor tie-break, batch sorting and no-sort
policy windows are unchanged. No packet priority/order mutation was introduced.

`python tools/verify_player_chams.py` passes, including nested material/asset
checks. **15** individually mutated player header variants reject (14 numeric
constants plus GenerateBytes). `chams-verified.json` and `negative-results.json`
record this. Ownership checks, frame-owner integration and cooperative resident
stop behavior remain intact. Actual visible/occluded-player appearance remains
unobserved; build and fixture results are reported by the integrating pass.
