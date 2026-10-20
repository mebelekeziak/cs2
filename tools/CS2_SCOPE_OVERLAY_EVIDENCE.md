# Native first-person scope overlay

Investigated October 20, 2026 from installed Windows AMD64 client.dll at
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`.
SHA-256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
timestamp `0x6AD01314`, image size `0x299D000`. Addresses below are client RVAs.
Investigation used only files; no client/server was started or accessed.

`python tools/inspect_scope_overlay.py` independently decodes the native
producer/call edges, temporary ownership and separate healthshot/scope branches,
checks thirteen complete instruction windows and compares the runtime header.
Decoded scratch evidence is under `logs/research/scope-*.txt`.

## Producer and ownership

`0x8E15E0` has the used ABI `void(pawn*, visual_output*)`. RCX is the selected
pawn and RDX is a temporary output. The prologue copies RDX to RDI and reads
the pawn's fields from RCX/RBX. The complete function ends at `0x8E18C4`.
The render caller at `0x11DA30C` loads RCX from native pawn selection
`0xC7BDB0` and RDX with `lea rdx,[rbp+0x980]`. Return `0x11DA311` ignores RAX,
then copies output byte `+4` from `[rbp+0x984]` to `[rbp+0x176]`, which is view
member `+0x1B6` as established below.
The constructor `0x11CDED0` initializes the 28-byte temporary before acquisition.

The other direct caller is `0xCEFCaa`, return `0xCEFCaf`, inside the float/FOV
path `0xCEFC10`. Its byte-4 test affects native weapon FOV behavior. It is
deliberately left untouched by requiring the exact render return address.

Relevant output fields are:

| Offset | Native meaning and evidence |
| --- | --- |
| `+0`, float | Independent healthshot boost: source pawn schema `m_flHealthShotBoostExpirationTime` at `+0x15A8`, written `0x8E1650` |
| `+4`, byte | Sniper scope enabled: weapon virtual type result must equal 5 at `0x8E16CB`, scoped state checked at `0x8E16EC`, written at `0x8E1712`/`0x8E171A` |
| `+8`, float | Scope visual alpha/inaccuracy, written `0x8E17D8` and `0x8E1897` |
| `+0xC`, `+0x10`, floats | Native scope inaccuracy components, written `0x8E173F`/`0x8E1754` |
| `+0x14`, integer | Sniper line width, native cvar value loaded `0x8E189D` and written `0x8E18A7` |
| `+0x18`, integer | Scope blur parameter, written `0x8E1846` |

The hook always invokes the original first, on the native owner, preserving
bookkeeping and the original argument addresses. Only the one-byte scope flag
in that render caller's current temporary is cleared. It verifies readability
and writability before asking the shared crosshair state to mark suppression.
It requires a fresh replacement for this exact currently validated local pawn.
No pawn/weapon/FOV/cvar state or installed files are changed.

## Separate render branches

Graph consumer `0x11EC870` receives the temporary as argument 5, loaded into
RSI at `0x11EC889`. It tests float `+0` and byte `+4` independently. If neither
is active it returns the existing input resource (R8, saved to RBX) unchanged.
The float branch creates the explicitly named `Health Boost Effect` pass
(`lea rdx` at `0x11EC98B`); the hook preserves that float and all its data.

The byte branch at `0x11ECA98` jumps directly to `0x11ECC91` when byte `+4` is
zero. The entire `Legacy Sniper Scope` graph pass (name LEA `0x11ECB0F`), including
native scope alpha, spread, line width and blur parameters, sits inside that
branch. There is no need to skip a whole renderer or alter individual materials.
The caller `0x11EAF1A` uses the returned graph resource normally.

The native view starts at `[rbp-0x40]`, making the earlier `[rbp+0x176]` store
view member `+0x1B6`. Native view copying at `0x11CD6F6`/`0x11CD6FD` propagates
that byte. Its modern sniper consumers are also checked:

- `0x11FCAC5` tests view `+0x1B6`, jumping to `0x11FCB48` when false, before
  allocating and registering the explicitly named `SniperZoomStencil` pass
  (name LEA `0x11FCB15`, renderer type `CSniperZoomStencilRenderer`).
- `0x11EE94B` tests the same view member and jumps to normal extent setup at
  `0x11EE97A` when false, bypassing the sniper-specific centered crop geometry.

The producer replacement therefore propagates through both legacy and modern
sniper scope setup. The verifier independently decodes all four member operands,
the bypass branches and stencil pass name; it freezes each instruction window.

## AUG/SG iron-sight rendering

Static `C_CSWeaponBase` schema registration `0x221AC98`, field array
`0x221AF80` (57 records), independently identifies embedded
`m_IronSightController=0x1BD0` and `m_iIronSightMode=0x1C80`.
The native gate `0x82FE70` computes weapon `controller-0x1BD0` and returns a
Boolean in AL: explicit true at `0x82FF63`, false at `0x82FF7A`.
Its used ABI is `bool(controller*)`. The complete function ends `0x82FF87`.

Two exact consumers are intercepted:

| Native call / return | False-result behavior |
| --- | --- |
| `0x8321DD` / `0x8321E2` | Geometry draw `0x832170` branches at `0x8321E4` to epilogue `0x8335CD`, before draw buffer/resource allocation |
| `0x11F9971` / `0x11F9976` | Graph builder `0x11F9940` branches at `0x11F9978` to epilogue `0x11FAA2D`, before graph texture allocation and pass registration |

The first path comes from `CIronSightSceneObjectDescriptor` vtable `0x1BFE798`,
slot 1 (`0x82A260`). Its packet loop loads the controller from scene object
`+0xD0` and invokes the geometry draw. The draw also independently subtracts
`0x1BD0` from the controller at `0x8321BB`, agreeing with schema ownership.
The graph builder is called at `0x11DEC18` by the existing client render owner.
It constructs `IronSightResolve`, `IronSight: Blur X`, `IronSight: Blur Y`
and `IronSightToScreen` passes. It has no resource-handle return used by its
caller: the call is followed by another view-state check, with RAX ignored.
Native false-result exits therefore leave the existing graph/resources in place
and require no fabricated return value or cleanup.

The hook always calls the gate first, preserving its work and result for all
other users. Only when that native result is true, the return site is one of
those two consumers, and the derived weapon matches the freshly validated local
active weapon does it return false. The shared crosshair predicate also checks
current first person, scope, full serialized source and accuracy freshness.
There are no persistent controller, weapon, pawn, FOV or mode writes.

Other callers include `0x11DEBF7` (front render selection), `0x11F073D`
(viewmodel scope stencil), `0xCEFCEE` (FOV), `0x82C9F9`, `0x82EE09`, `0x9017F9`
and other weapon paths. All receive the native result unchanged. Mask/state
consumers need not be globally falsified to skip the two visible overlay paths.
The gate function, complete graph body, draw prefix and both native early-exit
epilogues are frozen; the verifier separately decodes both call targets/branches
and checks embedded ownership against schema. Live appearance remains unobserved.

## Lifecycle and validation limits

Preparation verifies AMD64 PE identity, timestamp, image size, producer prologue,
render caller, consumer gate, iron-sight gate and its exact callers before queuing
the two MinHook callbacks.
Activation occurs after the overlay owner's queued activation. A stopped or
partially prepared callback forwards native behavior. Preparation arms neither
feature unless both hooks were created and queued successfully; a partial
preparation retains any installed/queued callback as a native forwarder.
Cooperative stop disarms
suppression and retains callbacks/trampolines until normal process exit:
stopped and resident is not physically unloaded.

The adapter fixture covers native side effects and argument forwarding, byte-only
replacement, independent healthshot preservation, rejected replacement, other
pawns/callers/FOV consumer, invalid native flag, null/unreadable/read-only output,
reentrant stop and missing module identity. Shared crosshair tests cover the
actual source freshness, scope and camera validation. Static checks and synthetic
fixtures do not establish live first-person disappearance or appearance.
Iron-sight fixtures additionally cover both target callers, unchanged FOV/state/
stencil users, exact embedded ownership, original result/bookkeeping, native
false, invalid pointers, reentrant stop and stale module identity.

Final verification: public and diagnostic x64 Release builds each pass all 54
CTest suites, including 32 adapter checks and 403 shared crosshair state checks.
All 26 installed-file compatibility components pass in
`logs/compatibility/20261020T144039132356Z/`. All 33 deliberate scope header/window
mutations are rejected (`logs/research/scope-negative.json`). Both final DLLs
retain zero static TLS and delay imports (`logs/scope-overlay-payload-checks.json`).
The software D3D11 menu preview was inspected at `logs/scope-overlay-menu.bmp`.
Build and test logs are `logs/scope-overlay-{build,tests}-{public,diagnostic}.log`.
