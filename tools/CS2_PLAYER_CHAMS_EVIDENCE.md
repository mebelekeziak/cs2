# Player chams native ownership and packet contracts

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
preserved; depth behavior is not disabled and no extra occluded pass is added.

The packet helper changes only the material/variant tuple and packed RGBA.
It preserves the engine's mesh and skinned vertex streams, transforms, draw
flags, sorting data and packet neighbors. The UI's color channels are packed
as RGBA bytes, including opacity. Flat and iridescent styles have separate
saved colors. Material construction, shader coverage, actual shader color
consumers and their independent verification are documented by the material
component; the static-world aggregate packet color limitation does not prove
anything about this animatable path.

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

The public Release `player_chams_checks` executable passes 136 checks. The
installed-file verifier passes, and 15 deliberately changed header constants or
entry bytes are rejected (`logs/research/chams-negative-contracts.json`).

Material recipes, native KV3 lifetime, resource ownership, shader inputs and
consumer evidence are documented in
[material evidence](CS2_PLAYER_CHAMS_MATERIAL_EVIDENCE.md). The top-level player
verifier includes that material/asset verification; `--material-header` selects
an alternate material header for negative checks.

The integrating public and diagnostic Release builds each pass all 47 CTest
suites, including 136 player-adapter checks and 77 material-lifecycle checks.
The shared frame dispatcher is tested directly and through the model-swap
regressions. Both payloads have no static TLS/delay imports. This verification
did not launch CS2 and does not establish visible appearance or correct sorting
of translucent players: original packet pass/sort classification is preserved.
