# Native local-light GPU radiance evidence

## Current recertification, October 23, 2026

Installed client **1.41.9.2 / 2000935**, Source revision **11162119**, Steam
build **26144732**. Read-only AMD64
`game/bin/win64/scenesystem.dll` SHA-256:
`0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b`;
timestamp **`0x6ADA337D`**, size **`0x978000`**. Installation is unchanged.

Independent `CLightBinnerGPU` primary RTTI locates table **`0x5FE678`**,
formerly `0x5FE688`. Slot `+0x70` still resolves thunk `0x1B14A0`, whose decoded
branch reaches pack `0x1B14B0`. The upload caller still returns to `0x1B2537`,
with RCX binner, RDX native source, R8 fresh output; full RAX remains forwarded.
GPU stride `0x1E0`, radiance `+0xA0`, contiguous RGB stores and native sun removal
remain unchanged. All 29 frozen machine windows pass.

Complete function scopes were re-disassembled before pinning. Comparison of
775 preserved pack instructions and 939 preserved area-gather instructions
found 26 and 11 changed instructions respectively; every changed byte belongs
to a decoded RIP displacement, preserving opcode, registers, sizes, members,
calls and control flow. Targets were examined independently, including the
native one load now pointing at `0x5FFF80` (float 1). These include relocated
constant vectors, tables and static synchronization data; this is not blanket
byte masking. Exact whole-scope hashes were renewed for pack and area gather.
The full upload and selected-sun-removal hashes still match their prior values.

`include/world_lighting_offsets.h` changes only timestamp and binner table.
`verify_world_lighting.py` additionally resolves RTTI independently and retains
all four exact function-scope checks. Positive verification passes. Temporary
timestamp, table, `GpuRadiance` and `PackBytes` changes all fail; an unknown
scene image fails. Evidence is in `logs/research/26144732/world/`: `compare.json`,
`contracts.json`, full pack/upload/sun/gather disassemblies,
`verify_world_lighting-verified.json`, and `validation.json`.
Scratch disassembly uses Capstone 5.0.9; frozen verification uses stdlib only.
No client/server launch or fresh runtime/visual observation was performed.
Build/CTest results are reported by the overall update; earlier observations
and shader coverage limits below remain historical.

## Historical 1.41.9.1 investigation

Read-only installed-binary and shader research, corrected October 15, 2026.
Canonical verifier: `python tools/verify_world_lighting.py`. Module RVAs
throughout. Research did not launch or inject into a process, access a public
session, or change installed game files. No live visual result is claimed.

`scenesystem.dll` SHA256:
`040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`;
AMD64 PE timestamp `0x6ACFCD4F`, image size `0x978000`.

## Two actual defects in the earlier local-light modifier

The old hook modified native generator `0x4A3CE0` output RGB `+0xB4` at
caller return `0x1A2061`. That handles generated point/spot/orthographic
sublights but misses the ordinary registered-light path `0x19FE30`.
Registered lights copy source `+0xF0` into a fresh per-view record `+0x18`
through `0x8D010` at `0x1A002E`. Source radiance `+0x1A4/+0x1A8/+0x1AC`
(`0xF0 + 0xB4`) is tested at `0x19FF0D..0x19FF4B`. The completed record
is enqueued through `0x1C9A40` at `0x1A0F1C`, without calling the generator.
Both paths later reach the same final GPU packing callback.

The second hook, previously called an area-light extension, was incorrectly
classified. Function `0x1B0FB0` actually packs **environment cubemap volumes**.
Its `0x90`-byte records begin at per-view constant buffer `+0x190` (cb3[25]);
their RGB `+0x70` corresponds to cb3[9*index+32]. The generic shader's
`S_SPECULAR=1` branch samples `texturecubearray t10`, selects its layer using
cb3[9*index+28].w, multiplies reflection RGB by cb3[9*index+32].xyz and blends
probe volumes. It does not produce diffuse area/barn-light radiance.

That output really is uploaded: the base `0x7DE0`-byte packet is copied at
`0x1B0CCC` and optionally again at `0x1B0D83`, then uploaded at `0x1B0F91`.
The defect was feature classification, not a later overwrite. The inspected
generic static-1 shader consumes it in eight dynamic variants; generic
static-0 and lightmapped static-0/4/6 in 24 inspected dynamic variants do not.

**Both old hooks have been removed.** Local-light tint no longer changes
cubemap reflection-volume constants, and no generator-stage multiplication
remains to apply the same scale twice.

## Final common GPU record

`CLightBinnerGPU` primary vtable `0x5FE688`, slot `+0x70`, points to thunk
`0x1B14A0` (`e9 0b 00 00 00`), which jumps to `0x1B14B0`. The complete
contiguous function is `0x1B14B0..0x1B244F` (exclusive end, sole return
`0x1B244E`). Its Windows x64 inputs are exactly:

- RCX: native binner/context.
- RDX: current per-view CPU light record.
- R8: one caller-owned GPU output record.

It consumes no fourth register or stack argument. The caller ignores RAX;
the forwarding wrapper preserves the complete register as `uintptr_t` without
assigning a boolean/count meaning to it.

The native virtual call is `0x1B2533`, bytes `41 ff 51 70`, returning to
`0x1B2537`. The caller computes input as `[binner+0xE3B0] + index*0x640`
and output as its private array plus `index*0x1E0`. The array initially has
64 records on the caller's stack and may grow through native allocation.
After packing, graphics upload vtable `+0x380` at `0x1B2572` receives that
same array and `count*0x1E0` bytes. Native cleanup follows upload. The modifier
retains neither input nor output pointers across callbacks.

Native transformation at `0x1B1674` copies the light's RGB into temporary
data. Instructions `0x1B17BB/0x1B17C3` unconditionally write output RGB
`+0xA0/+0xA4/+0xA8`. Additional native factors can scale these values at
`0x1B18C6/0x1B18CE/0x1B18D9`. The new hook therefore applies only **after
the complete native function returns**, and only for exact return
`0x1B2537`. It multiplies those twelve output bytes by local tint/brightness.
All source records, geometry, flags and unrelated output bytes remain native.

This common output includes lights from both generator and registered-light
gathering, without a source-type restriction that would omit area/barn-shaped
native lights. It changes contributions represented in this local GPU list;
it does not recolor baked irradiance or manufacture map lights.

## Selected sun independence

Native selection `0x1A7FD0` removes the selected sun from the ordinary
`+0xE3B0/+0xE3A8` list. For an existing sun it removes the matching source
identifier; for fallback selection it copies the directional record and
removes it with native memmove. Count decrements occur at `0x1A8064` and
`0x1A812B`. Selection runs at `0x1A9B59`, before ordinary final packing
through `0x1B4569 -> 0x1B2450`. The selected sun remains under the separate
native sun control. This does not establish behavior for every hypothetical
additional directional source that the engine might place in its local list.

## Actual shader consumers

Installed shader metadata identifies `g_BarnLights` as generic resource t14
and lightmapped resource t17 in the inspected variants. Both are structured
buffers with stride 480 (`0x1E0`). Their shaders load RGB at byte offset 160
(`0xA0`), multiply it by native per-light attenuation and accumulate lighting.
This directly connects the final GPU float3 to world shading, independently
of sky tint, cubemap-volume coefficients and screen grading.

Retained research under `logs/research/`:

- `local-light-final-gpu-findings-20261015.md`: consolidated correction.
- `final-local-1b14a0-complete-20261015.txt`: full final pack and caller/upload.
- `final-local-sun-selection-20261015.txt`: native sun selection and callers.
- `environment-gather-complete.txt`: registered-light path bypassing generator.
- `area-shader-bindings-20261015.py`: actual g_BarnLights binding metadata.
- `area-shader-consumers-20261015.py/.json`: inspected shader variants.
- `area-generic-s1-d0-20261015.asm`: explicit cubemap-volume consumer
  (search `cb3[r15.x + 32]`).

## Verification, lifecycle and limits

The standalone verifier pins the complete installed module and four complete
native function scopes: final pack, GPU-array builder/upload, selected-sun
removal and registered-light gather. It checks 29 frozen instruction ranges,
decodes the thunk target, virtual slot, GPU stride and RGB displacement
independently, and compares all runtime header constants and fingerprints.
`--header` mutation checks rejected an altered RGB offset, caller return and
prolog fingerprint. Verification never updates offsets automatically.

Public and diagnostic x64 Release builds both succeeded after this correction,
and all 16 CTest suites passed in each configuration. All seven component
compatibility verifiers passed against the installed build.

`tests/world_lighting_checks.cpp` exercises three-argument forwarding, complete
RAX preservation, all output bytes outside RGB, source preservation, fresh
native baselines, exact caller selection, disabled/neutral behavior, invalid
and overflowing radiance, zero brightness, independent settings, and stop
during a busy native original. These synthetic cases cannot demonstrate a
live material's final appearance or engine scheduling.

Native packing always runs outside the short RGB-write barrier. Stop disarms
first and drains only pending output writes; a native call that returns after
stop receives no modification. Hooks remain forwarding and resident until
normal process exit. Stopped and resident does not mean physically unloaded;
queued GPU work can finish. Neutral controls perform no RGB read or write.
Telemetry distinguishes callback visits, nonzero source radiance and actual
changed GPU RGB records, and includes current tint/brightness. These counters
are diagnostic evidence, not proof of visible pixels.
