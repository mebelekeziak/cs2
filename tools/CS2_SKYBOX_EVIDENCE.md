# Workspace custom skybox evidence — October 25, 2026

The user subsequently confirms custom skyboxes work after the ownership repair
described below. This is the user's offline/private observation; the agent did
not launch a new client/server trial.

## Fresh material ownership failure — October 25

The user's new session `logs/overlay-24548-134374148332806425.log` establishes
that custom textures reach native loaded state **3**, KV3 parsing succeeds,
and created material names exactly match the requested unique names. Solitude
Night and three unrelated LynX textures then fail at
`material_binding_validation`; built-in Overpass and Train materials succeed.
Those receipts rule out the previously suspected texture format/mount/recipe
failures for these attempts. They do not establish visible custom rendering.

The original `ValidBinding` required a positive strong-reference count before
the caller acquired its first reference. That is appropriate for already
retained drawing inputs, but rejects legitimate newly created materials.
The installed native ownership chain independently proves why:

1. Primary `CMaterialSystem2` RTTI identifies the creation method through slot
   29: `materialsystem2+0x3ADE0`. At `+0x3AE29`, the creator loads
   `+0x129AE0` and stores it into the new material. It is exactly the primary
   `CMaterial2` table; there is no distinct runtime-created material subclass
   requiring a relaxed type check.
2. The creator calls `+0x44F20` at `+0x3AEF9`. That wrapper forwards the new
   material through the resource-system method at byte slot `+0xE0`, storing
   its returned binding in the caller's output. Primary `CResourceSystem`
   RTTI identifies table `resourcesystem+0x65348`, whose `+0xE0` method is
   `resourcesystem+0x17630`.
3. With the creator's registration argument 1, that method follows its named
   registration branch and calls `resourcesystem+0x173C0` at `+0x17898` for a
   newly inserted binding. The initializer stores the material value at
   binding `+0`, zeroes `R15D` at `+0x173EC`, copies zero to `EAX` at
   `+0x17411`, then atomically initializes **binding `+0x20` to zero** with
   `xchg [rdx+0x20], eax` at `+0x17428`. Registration returns the binding
   without acquiring the caller's first strong reference.
4. The native material ownership exchange independently acquires a reference
   with `lock inc [rbx+0x20]` at `materialsystem2+0x3AD0C`. The sky renderer
   reads this same member and requires it positive at submission time.

The ownership correction therefore validates material identity and expected
name before retaining a fresh binding, allows zero only at that new-material
boundary, acquires the reference atomically, and requires a positive count for
publication and every later draw. Existing loaded-material and cached/draw
validation must continue rejecting zero or negative counts. Type/name checks,
bounded retention, changed-selection checks and stopped/resident lifetime
remain necessary; accepting every zero-count borrowed binding would be wrong.

`verify_skybox.py` now pins the complete installed `resourcesystem.dll` hash
`29d503822278305b197c5d4c299b77961f6c5b9c0ab989dae81723c8546fa75f`, in addition
to the existing module pins. It derives both RTTI tables, the creation/registration
call chain and the common reference member, verifies the creator's exact table
store, initializer zero-value instructions and native retain instruction, and
freezes these complete instruction scopes:

| Module / scope | SHA-256 |
| --- | --- |
| material `0x3ADE0..0x3AF48` creator | `ae6d100c6686911ef89ab4abd54ace44f9fc0c769dc48072fcd30f58941b3a2a` |
| material `0x44F20..0x44F99` registration wrapper | `047550e1f61a7f0e8f2e33e598040cc7f0a238f8e4c0ae330c3ad11fd9d790e7` |
| resource `0x17630..0x178D4` registration | `814431543877c7df3d5590a0ae42b1e3aa80ed05d95db33a2f5cae4c2ff73a5e` |
| resource `0x173C0..0x1762F` binding initialization | `017cca0924394e11a078d0c23fb9bd1ed4a54b550a4bfd293a8d11d092526f55` |

Addresses are RVAs of the named installed AMD64 module. Source/disassembly and
verification reports are retained under
`logs/research/sky-retention-20261025/`. This is static ownership evidence and
observed loading diagnostics, not a claim of post-repair in-game appearance.

## User comparison and diagnostic follow-up

The user confirms built-in swapping works and imported selections display
**Skybox unavailable**. Session `logs/overlay-33912-134374134281605132.log`
records failed owner-thread preparation attempts interspersed with successful
material loads. The original generic status does not identify whether these
failures concern the workspace texture, loaded state or runtime material.
It cannot establish that the LynX asset contents are defective.

The follow-up records each requested resource before loading and one completed
`WORLD_SKYBOX_LOAD` record containing stage, type, native loaded state,
binding/value, texture readiness, parse result and expected/observed material
names. Public failure text identifies the same stage; failed selections still
avoid per-frame retries. Source-style `shader` is independently confirmed in
the installed material converter; `m_shaderName` belongs compiled material DATA.
The native loader ABI and rendering behavior are unchanged. Two alternate
single-cube textures are installed for comparison, with provenance in
`skyboxes/PACKS.md`; no custom native load or visible success is claimed.

## Initial implementation

This is a client-side Windows AMD64 extension to the existing seven-argument
sky draw hook. Installed baseline: CS2 1.41.9.2 / client 2000935 / Steam build
26144732. Installation:
`C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game`.
The implementation targets the project's existing D3D11 client. This work read
installed binaries and resources; it did not launch CS2 or observe live output.

Run `python tools/verify_skybox.py` for the new independently decoded contracts.
The existing compatibility bundle also verifies the sky draw, model/resource
loading and native KV3/material construction dependencies. Build and standalone
test results are recorded in the README alongside the complete change.

| Module | SHA-256 | Timestamp / image size |
| --- | --- | --- |
| filesystem_stdio.dll | `465be859c2cc231666301328939da3ed4f7b087ebdbaad325631b481717da183` | `0x6ADA32A6` / `0x22C000` |
| engine2.dll | `e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29` | Existing model/resource verifier identity |
| scenesystem.dll | `0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b` | `0x6ADA337D` / `0x978000` |
| materialsystem2.dll | `2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69` | `0x6ADA32D5` / `0x186000` |

## Workspace content mount

Primary MSVC RTTI `.?AVCFileSystem_Stdio@@` independently resolves table
`filesystem_stdio+0x1B0CA0`. The installed `VFileSystem017` interface string is
at `+0x1B1110`. Runtime obtains that interface through the module's exported
`CreateInterface`, and requires the exact primary table before use.

Table byte slot `+0xF8` (index 31) selects `+0x53BF0`. Its entry preserves RCX
receiver, RDX path, R8 path ID, R9D insertion mode, and reads both stack DWORD
arguments 5 and 6. Its call at `+0x53C30` forwards these to `+0x52FF0`, adding an
internal boolean argument. Omitting those stack arguments would be an ABI error.

Independent native caller `engine2+0x20E0D2..0x20E0FB` zeroes EDX, writes zero to
stack argument 5 and argument 6, supplies an absolute local-directory path,
`USRLOCAL` as path ID, R9D=1, and calls byte slot `+0xF8`. Another native caller
at `+0x19798B..0x1979B2` supplies `DEFAULT_WRITE_PATH`, insertion mode 0,
argument 5=2 and argument 6=0. The wrapper is therefore called with **six**
arguments. We reuse the first caller's insertion/default arguments `(1,0,0)`,
with the `GAME` content path ID and the compile-time workspace's `skyboxes/`.

The native preparation routine canonicalizes the supplied directory, interns
both directory and path ID, and takes its own filesystem synchronization.
`+0x533F7..0x5344D` establishes insertion: mode 0 selects the earlier matching
group position; mode 1 retains the group tail. Existing mounted game assets
therefore keep priority. The native engine owns its copies of the strings.
The mount stays registered until process exit because resources and pending
native/GPU users may depend on it. No gameinfo file or installed file changes.

## Native resources and runtime material recipe

Names, blocking JIT-manifest loading, loaded-state query and temporary name
cleanup reuse the exact contracts in
[model evidence](CS2_MODEL_SWAP_EVIDENCE.md). Material arena construction,
generic KV3 parsing, material-system interface, creation slot and retained
binding reference `+0x20` reuse
[material evidence](CS2_PLAYER_CHAMS_MATERIAL_EVIDENCE.md).

The native name's eight-byte type at `+0xD8` must equal `vmat` (`0x74616D76`)
or `vtex` (`0x78657476`). Loaded state must equal 3; an error placeholder or
unready binding is rejected. For ready materials, `binding -> CMaterial2`
must have the current primary material table, a positive reference count, and
the exact expected material-name pointer at material `+0x10`. One native
reference is retained for process lifetime, matching existing material code.

The installed VPK material `materials/skybox/sky_de_dust2.vmat_c` was decoded
with the existing local Source 2 Viewer CLI. DATA establishes shader `sky.vfx`,
texture parameter `g_tSkyTexture`, and float parameters
`g_flBrightnessExposureBias` / `g_flRenderOnlyExposureBias`. The custom texture
recipe uses those exact names, both exposure values 0, and the selected native
`.vtex` resource. It is parsed and created in memory; no compiled material is
written into the game directory. Native materials have unique retained names
`materials/codex_custom_sky_<id>.vmat`.

All loading, mounting and material construction run on the shared original-first
stage-8 frame owner. Native blocking operations run outside publication locks.
The initial catalog scan runs once during preparation, so saved custom configs
work after restart; explicit Refresh supports later imports. Imported textures
must be catalog-validated single cubemaps. Imported arbitrary materials are not
accepted: built-in `.vmat` selections use known shipped sky materials, while
community `.vtex_c` selections use our verified recipe. See
[pack/catalog details](../skyboxes/README.md).

The immutable cache holds at most 64 successfully retained selections; at most
64 custom material-construction attempts are made. Earlier selections can be
reused after reaching the limit. Each failed selected path is attempted once
until selection changes or a disabled owner frame clears it. Existing engine
JIT manifests also retain native resources; stop cannot safely reclaim them.

## Draw replacement without shared scene mutation

The complete sky renderer `scenesystem+0x154FB0..0x1556D9` has SHA-256
`56a35244d265ea3cf476debf6ef55e995993e277184f43b9ca6087db47c3faf5`.
Disassembly establishes that packet count is used only to reject an empty call
and select the last packet's source at `packet+(count-1)*0x70+0x18`.
The packet-array argument is not otherwise read, saved or forwarded.

The renderer selects the native view override when available, or reads the
scene's material binding at `+0xD0` (`+0x155029`). It checks binding `+0x20 > 0`,
loads `[binding]`, then calls material byte slot `+0x28` with the view `+0x30`.
Current `CMaterial2` RTTI table `+0x129AE0` independently resolves that slot to
`materialsystem2+0xB8D0`. This is the native selected-variant lookup, distinct
from slot `+0x30` used for aggregate/player material replacement.

The scene allocator explicitly requests `0x110` bytes at `+0x1557EE`. For the
visible kind only, the hook copies exactly that many bytes and the final
`0x70` packet onto its callback stack. Only the local scene's `+0xD0` binding
and local packet's `+0x18` source are changed. The original renderer receives
that local packet and count 1; all other arguments and its RAX are preserved.

The original's only source-address helper call is `+0x155087 -> +0x49B6D0`,
which copies the source transform's twelve floats into its caller's local
matrix synchronously and retains no pointer. Other scene fields are read as
values; the copied `+0xA8` pointer still refers to the same native-owned
parameter object. Rendering submits values from its own local parameter
collection. The original source/packet bytes are never written, and no borrowed
source pointer is cached across callbacks. Nested draw scopes restore prior TLS.

Lighting sky kind `0x0881EE9E` keeps its native material. Existing visible and
lighting tint controls retain their independent behavior. Replacing the visible
sky does not promise regenerated environment probes, baked lighting or a new
3D sky map. Native view-material overrides retain priority where the engine
already uses them.

## Stop and limitations

Stop disarms future publication and draw selection, clears the published binding,
and drains the existing short tint-writer barrier. It does not wait while native
loading or rendering may dispatch or wait for engine jobs. Already admitted
callbacks and queued draws may finish using retained immutable resources. A
load completing after stop or a selection change cannot publish obsolete intent.
Forwarding hooks, dynamic TLS, mount and native resources remain resident until
normal process exit. Stopped and resident is not physically unloaded.

Standalone sky tests cover tint independence, missing resources, explicit retry,
stable cached reuse, bounded retention, changed intent during blocking load,
stop during loading, invalidated bindings, nested lighting draws, batch final
source selection, and untouched shared packet/scene bytes. These checks and the
frozen binary verifier establish contracts, not observed in-game appearance.

Research disassembly lives under
`logs/research/skybox-native-20261025/`; the pre-change compatibility bundle is
`logs/compatibility/20261025T144042831703Z/` (36/36 passed).
