# Installed smoke and flash rendering evidence

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
