# Installed environment and scene-light switch evidence

## Current recertification, October 23, 2026

Installed Windows AMD64 client **1.41.9.2 / 2000935**, Source **11162119**,
Steam build **26144732**. The unchanged installation's
`game/bin/win64/scenesystem.dll` SHA-256 is
`0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b`,
timestamp `0x6ADA337D`, image `0x978000`.

The named `lb_enable_lights` registration remains at `0xE840`; its RIP string
operand now resolves **`0x5FB290`** (formerly `0x5FB410`). The decoded wrapper
remains `0x923810`, and both native gate loads independently resolve value
slot `0x923818`. The point/spot and area entries, boolean `+0x58` test,
clean disabled returns, three-pointer void ABI, descriptor callers and
per-view `+0xE3D0` count clear all remain unchanged. All ten frozen windows
match after updating only the relocated registration-string displacement.

Full point/area gathers and frame-setup dispatch were re-disassembled.
The preserved area comparison covers 939 instructions; 11 changed operands
are RIP-relative constant/table/string references. The gates occur before
output work, and their early-return targets still execute only the native
epilogues. `include/world_lighting_switch_offsets.h` changes timestamp only;
all five byte arrays are unchanged. No runtime behavior or lifetime changes.

Frozen verification passes; temporary timestamp, entry, frame-clear and gate
fingerprint mutations fail, as does a separately modified scene-image copy.
See `logs/research/26144732/world/{probe,contracts,compare,validation}.json`,
`point_gather.txt`, `area_gather.txt`, `frame_setup.txt`, and
`verify_lighting_switches-verified.json`. Scratch decoding used Capstone 5.0.9;
the frozen verifier remains stdlib-only. No client/server was launched and no
fresh visual/graphics/runtime observation is claimed. The overall update
records builds/tests. The following recertification is historical.

## Historical recertification, October 15, 2026

Recertified October 15, 2026 for CS2 **1.41.9.1**, client **2000932**, Steam
build **25947352**, Source revision **11131657**. Current `scenesystem.dll`
SHA-256 is `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`,
AMD64 timestamp `0x6ACFCD4F`, image size `0x978000`.

Before metadata updates, all ten frozen registration/gate/return/clear/caller
checks and all five runtime byte arrays matched the new binary. Additionally,
every one of the 1,091 instruction records in the complete historical gather
disassembly matched byte-for-byte. Independently re-decoded the named wrapper
and value slot `0x923818`, and both native disabled-branch targets
`0x19F564` / `0x1A0F6F`. The three-pointer void ABI, clean no-output returns,
per-view clear, and original-call forwarding contracts below are unchanged.
The sunlight/buffer investigation adds 1,309 matching instruction records,
documented in `CS2_BAKED_LIGHT_EVIDENCE.md`. No live process was accessed.

The recertified frozen verifier passed, the targeted MSVC x64 Release switch
check executable rebuilt successfully, and its CTest suite passed. No live
game launch or full DLL build was performed in this subtask.

## Original investigation

October 14, 2026. This is static, read-only evidence from the installed AMD64
CS2 1.41.9.0 client using D3D11. No client was launched, opened or injected for
this investigation; live output remains unobserved.

Module: `game/bin/win64/scenesystem.dll`, SHA256
`bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831`, image
timestamp `0x6AC7FF95`, image size `0x978000`.

## The native light switches do not use a shared getter

Registration of `lb_enable_lights` occurs in function `0xE7C0`: at `0xE840`
the native LEA resolves to string `0x5FB410`, and at `0xE847` its RCX resolves
to wrapper `0x923810`. Consumer loads use the wrapper's pointer slot `+8`;
they read its native boolean value at `+0x58` directly. The implementation
does not write the wrapper, boolean, cvars, or source entities.

Other investigated names and value-pointer slots:

| Native name | Value pointer slot | Representative direct consumers |
| --- | --- | --- |
| `lb_enable_lights` | `0x923818` | `0x19F30D`, `0x19FE49` |
| `lb_enable_stationary_lights` | `0x923938` | `0x1A0B4F`, `0x1A147C` |
| `lb_enable_dynamic_lights` | `0x923948` | `0x1A0B5E`, `0x1A149C` |
| `lb_enable_sunlight` | `0x923B38` | `0x1A638D`, `0x1AF9E9`, `0x1AFA22` |
| `r_shadows` | `0x922DD8` | `0x1A81E3` |
| `sc_disable_baked_lighting` | `0x922D20` | `0x42101`, `0x5499B`, `0x59618`, `0x762F5`, `0x7A91F`, `0x80F23` |

This table records investigation, not implemented menu support. Sun/shadow
gates appear inside large routines after required output initialization;
returning early from those entire routines would skip unrelated state.

## Implemented local-light suppression mirrors an existing no-output path

The complete ordinary scene-light gather starts at `0x19F300`. Its original
arguments are `RCX=CLightBinnerGPU`, `RDX=scene light`, `R8=context`. It saves
those values before using them, reads no incoming R9, XMM or stack arguments,
and has no return value. Native boolean false branches from `0x19F321` to
`0x19F564` and returns at `0x19F56C` before reference counting, cache access,
light-list append, or shadow-light emission.

The complete barn/area-light gather starts at `0x19FE30` with the same three
pointer arguments and no return value. Its false branch at `0x19FE5A` jumps
to `0x1A0F6F` and returns at `0x1A0F7B`, preceding even the source-cache
lookup. Incoming R8 is saved in R14; RCX is saved in R12; RDX is saved in its
caller home slot. R9 and XMM registers are initialized before their use;
no incoming stack arguments are consumed.

Direct ordinary callers are `0x1A30A6`, `0x1A369C`, `0x1C0CC7`.
Direct area callers are `0x1A3937`, `0x1C1EAC`. The descriptor caller
`0x1C0CB0` obtains the scene light from its packet's source `+0x28`, calls
the ordinary gather, then independently returns AL=0. The area caller does
the same at `0x1C1E9F`–`0x1C1EB8`. Both ignore any output register from the
gather call; the other direct callers likewise consume no return.

Native binner setup beginning `0x1A8150` clears the ordinary gathered-light
count at `0x1A898B` (`[binner+0xE3D0]=0`) and clears supporting arrays before
the current view's gather dispatch. Suppression therefore does not depend
on editing previously gathered shared light state. It precisely skips the
current native gather as the engine does with its own light switch off.

The two hooks execute on the native dispatch thread, do not retain arguments,
and call the original outside the suppression decision lock. Cooperative
stop disarms and drains suppression decisions; originals continue forwarding.
Already running originals can finish normally. Hooks and code remain resident
until process exit; no physical DLL unload is attempted.

## Verification and practical limits

`python tools/verify_lighting_switches.py` verifies the frozen module hash,
native registration, both gates, both clean returns, the native clear, direct
callers, and the final source constants/byte checks. The runtime verifies
PE identity and those relevant entry/gate/clear bytes before installing hooks.
Standalone checks exercise master/option reset, exact pointer forwarding,
no source mutation, and stop during a running original call.

Disabling these gathers removes local point/spot and barn/area contributions
when those native paths are used. It does not remove already baked map lightmaps
or ambient probes, recolor sunlight, or override shadow state through shared
cvars. A map dominated by baked illumination may still look bright. Static
gate verification and callback telemetry do not prove live appearance.

Complete disassembly used to establish ABI and callers is retained in project
logs: `logs/research/environment-gather-complete.txt`,
`environment-cvar-registration.txt`, `environment-cvar-value-consumers.txt`,
and `environment-light-consumers-disasm.txt`.
