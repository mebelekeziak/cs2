# Installed smoke and flash rendering evidence

## Static recertification, October 21, 2026

The unchanged client path below now contains Windows AMD64 patch `1.41.9.1`,
client/server `2000934`, Source revision `11150591`, Steam build `26087496`.
Version date is October 20, 2026, 11:41:22. SHA-256 changed to
`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`;
PE timestamp changed `0x6AD01314` -> `0x6AD7B41C`, with the same `0x299D000`
image size. Runtime uses the shared `cs2_offsets.h` build identity. The removal
header needs no address or byte-array changes; the verifier's strict client
hash pin was updated only after its full contract body passed on the new image.

All five complete frozen rendering windows retain their exact hashes. RTTI
still derives smoke descriptor `0x1CA82E8`, slot 1 still targets `0xD20410`, and
both mask/normal dispatches match. The opacity call, return ABI and native
publication match; both flash keys, alpha operands, writer calls and return
sites match. Every consumed RVA, key and entry byte remains unchanged. The
seven-argument smoke ABI, float opacity return, synchronous float4-copy
ownership and cooperative resident-stop behavior documented below are retained.

The pre-edit research result is
`logs/research/26087496/render_grenade/initial-contract-checks.json`.
`python tools/inspect_grenade_effects.py` passes with the new pin. Four altered
headers (draw RVA, flash key, opacity return and draw fingerprint) reject, and
a workspace client copy with one changed byte rejects at the full-hash check.
See `verification-and-negative.json` in that directory. Builds/CTest are
reported by the integrating update. No process was launched or opened; live
smoke/flash appearance on this build remains unobserved.

## Historical October 20 baseline

Investigated October 20, 2026 from the local AMD64 PE32+ client:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
timestamp `0x6AD01314`, image size `0x299D000`. All addresses are client RVAs.
No client/server was started or accessed. `python tools/inspect_grenade_effects.py`
rechecks RTTI, dispatch, instructions and the runtime header from installed files.

## Smoke volume draw

MSVC primary RTTI `.?AVCSmokeVolumeSceneObjectDesc@@` resolves the vtable at
`0x1CA82E8`. Slot 1 points to `0xD20410`. Its seven arguments have the existing
scene descriptor ABI: descriptor, graphics context, packet list, integer count,
argument 5, view, draw statistics. Prologue saves RCX/RDX/R8/R9d; native stack
loads consume arguments 5 at original RSP+0x28, 6 at +0x30 and 7 at +0x38.
The function dispatches all volume draw modes, including `0xD21940` DrawMask
(its diagnostic text explicitly names `CSmokeVolumeSceneObjectDesc::DrawMask`),
normal `0xD20500`, and two alternate passes `0xD323B0`/`0xD2F910`.

Skipping this descriptor's draw avoids smoke volume/mask/depth submissions.
It does not change the grenade's detonation state, received voxel buffer,
simulation allocation, native scene object, timers or server state. That leaves
the native occupancy data available to the aura collector. The hook forwards
all original arguments when disabled, stopped, on stale module identity, or
when its descriptor object does not carry the verified vtable.

## Inside-smoke opacity

`0xCA06E0` is the pawn smoke opacity calculation, ABI `float(pawn*)`. Its sole
direct call is `0xC689C6` (return `0xC689CB`), preceded by `mov rcx,rdi`.
The caller transfers XMM0 to XMM7 at `0xC689DC`, performs native decay against
the prior alpha, then publishes XMM7 to pawn `+0x1530` at `0xC68A0D`.
The opacity function itself updates native sampled color and smoke age and
returns an alpha incorporating previous opacity. Its final old-alpha read is
`0xCA0C15`; its epilogue returns XMM0 without integer conversion.

The hook always executes the original, then replaces only its returned visual
alpha with zero when enabled and called by the verified native callsite. It
does not overwrite pawn memory. Native color/age bookkeeping continues.
Existing inside haze may decay briefly through the native caller's interpolation;
there is deliberately no forced entity-state restoration or field clobbering.
This hook covers the installed volumetric calculation; the caller also has an
older alternate particle calculation at `0xC9BA00`, which is not hooked.

## Flash shader alpha parameters

The producer `0x11D98B0` selects the current local/observed pawn and prepares
`FlashbangOverlay`. At `0x11D9BFF` it scales `+0x1504` (white overlay alpha);
at `0x11D9C1B` it scales `+0x1500` (screenshot alpha). Those become separate
float4 shader parameters through `0x16AE20`: key `0x47DC283C` with return site
`0x11D9C1B`, and key `0xF0372CC4` with return site `0x11D9C4B`.

The hooked writer has ABI `uintptr_t(collection*, uint32 key, const float4*)`.
It reads 16 input bytes at `0x16B017` and copies them to collection-owned
storage at `0x16B024`. The copied input address is not retained. The hook
forwards the collection and key unchanged, supplying a temporary zero float4
only for either verified caller/key pair while flash removal is enabled.
The original writer always executes; the native screenshot capture, render
pass scheduling, duration/state and audio/DSP bookkeeping continue normally.
No lock is held across native calls. Wrong keys or caller sites pass through.

The separate `CFlashbangResolveLayerRenderer` vtable at `0x1D2AD98`, slot 0
`0x1211C80`, is created inside screenshot capture `0x121BEB0` at `0x121C153`.
That capture is invoked at `0x11D9A3E`, before building `FlashbangOverlay`.
Consequently skipping that resolver alone would not establish suppression of
the actual white overlay; it is deliberately not hooked. Its static window
is retained in the verifier as supporting path evidence only.

The static schema registration for `C_CSPlayerPawnBase` at `0x22384B8`,
field array `0x2239AF0` with 26 entries, independently identifies screenshot
alpha `0x1500`, overlay alpha `0x1504`, max alpha `0x150C`, duration `0x1510`,
last smoke alpha `0x1530`, smoke age `0x1534`, and smoke overlay color `0x1538`.
The removal runtime does not write or depend on these offsets directly.

## Lifecycle and limits

Preparation verifies AMD64 identity, timestamp/image size, smoke descriptor method
slot, three complete instruction-boundary prolog windows, both flash alpha
producer callsites and the opacity caller. Activation occurs after the owner's MinHook queued activation.
Partial preparation stays disarmed; any installed callbacks forward. Stop
disarms future suppression and retains all code/trampolines until normal exit.
There are no locks held across engine calls and no persistent removal writes.

Synthetic checks exercise argument/return forwarding, independent toggles,
foreign/unreadable smoke descriptors, opacity bookkeeping, unrelated shader
keys and callers, both flash alpha replacements,
reentrant cooperative stop, stale client identity and unchanged caller bytes.
These checks and installed-file evidence do not establish actual appearance.
Live volume/inside-haze/flash disappearance still needs observation in the
authorized offline/private client environment.

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

Smoke descriptor RTTI moved `0x1CA82E8 -> 0x1CAA228`; render slot 1 now
resolves `0xD20410 -> 0xD21A60`. The complete `0xEE` byte render window retains
all seven Microsoft-x64 arguments, early native pass dispatch and forwarding.
DrawMask dispatch at `0xD21AE7` reaches `0xD22F90`; normal dispatch at
`0xD21B30` reaches `0xD21B50`. Supporting screenshot resolver RTTI now resolves
`0x1213EE0`, and its full `0x77` byte window is pinned. The runtime still hooks
the actual volume descriptor and transient alpha inputs.

The only direct call to independently found opacity getter
`0xCA06E0 -> 0xCA1FE0` is at `0xC69936`; caller window moves
`0xC689C3 -> 0xC69933`, return site `0xC689CB -> 0xC6993B`.
Its full caller and `0x557` byte getter remain pinned. XMM0 return and native
previous/current smoke opacity publication `+0x1530` remain unchanged.

Flash key `0x47DC283C` locates the new producer
`0x11D9BE0 -> 0x11DBE10`. Its complete `0x6B` byte window still broadcasts
the pawn `+0x1504` white-overlay alpha and `+0x1500` screenshot alpha into
stack float4 inputs. Both calls now reach writer `0x16AE20 -> 0x16AE00`;
returns are `0x11DBE4B` and `0x11DBE7B`, with unchanged screenshot key
`0xF0372CC4`. Runtime entry and entire producer fingerprints were refreshed.
No persistent entity/simulation writes or new lifecycle behavior were added.

`python tools/inspect_grenade_effects.py` passes. **17** individual bad-header
variants reject: all 12 numeric constants and five entry/producer/caller arrays.
Reports are `effects-verified.json` and `negative-results.json` in the research
directory above. Actual smoke/haze/flash appearance remains unobserved;
static contracts, builds and live observations remain separate claims.
