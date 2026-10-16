# Updating CS2 compatibility

Start here after a game update. This project targets the Windows AMD64 **client**
and its D3D11 overlay. Current verified baseline: **1.41.9.1 / client 2000932 /
Steam build 25947352**, investigated October 15, 2026.

Use the [address index](RE_ADDRESS_INDEX.md) to find a symbol's module, meaning,
rediscovery anchor, owning header and verifier. Headers are the executable source
of truth; index values describe this baseline only. Detailed evidence is tracked
in `tools/*EVIDENCE.md`; generated reports/disassembly live under ignored `logs/`.

## First pass: one command

From the repository root, using standard-library Python 3.10 or newer:

```powershell
python tools/check_compatibility.py
```

For another installation, point at the **game** directory containing `bin/` and
`csgo/`, rather than `cs2.exe` or the Steam root:

```powershell
python tools/check_compatibility.py --game 'D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game'
```

This does not start/open CS2, inject, contact a server, change installed files or
rewrite offsets. It runs all thirteen frozen verifiers independently, so
an ESP failure cannot hide a passing world component. Default output is a new
UTC-dated directory under `logs/compatibility/`. `--output PATH` selects a report
directory; reusing one replaces its report files.

The bundle contains:

- `summary.json`: per-component results, executable/module hashes and PE
  identities, `steam.inf` version, Steam build ID when available, and hashes of
  the source offset headers/verifier entry points used for the run.
- Thirteen component JSON files: exact command, exit code, stdout and stderr.
  Successful verifier stdout contains the detailed decoded evidence.

Exit code **0** means all thirteen static checks passed; **1** means one or more
failed or could not run. Missing installations also produce a failure bundle.
This command does not build, run CTest, establish the active graphics API, or
observe gameplay. Inventory of a renderer DLL establishes its presence only.

## Decide what needs investigation

| Result | Next step |
| --- | --- |
| All components pass | The frozen files/source checks still match. Build any changed project sources, then do the relevant offline observation. |
| Only ESP/movement fail | Start with `client.dll`; world modules may be unchanged. |
| Sky fails while other scene checks pass | Check both scene sky callback and client lighting-kind producer. |
| Several world components fail | Check `scenesystem.dll`; material mode also depends on `worldrenderer.dll` and `materialsystem2.dll`. |
| Missing file, Python exception or source-header error | Resolve the path/tool/source issue before treating the report as evidence of an engine change. |
| Hash changed | Investigate that module. Hash rejection alone does not say which address or contract moved. |

Every frozen verifier deliberately rejects an unfamiliar binary hash before
trusting its old sites. Keep that behavior. During investigation, use a separate
scratch script to locate candidates and decode them; update the frozen verifier
only after confirming the new evidence. A relocation delta or matching entry
prefix is a candidate, not sufficient ABI/layout proof.

## Repeatable investigation pass

1. **Preserve the starting state.** Inspect `git status` and existing work. Save a
   checkpoint when requested; preserve unrelated edits. Record the current
   reports before changing headers or expected hashes.
2. **Identify the build.** Read `game/csgo/steam.inf`, appmanifest build ID, module
   SHA-256, AMD64 PE timestamp and image size. Confirm installation/executable.
   For live observation, establish the actual loaded runtime/graphics API.
3. **Find the feature in the index.** Read its evidence and source consumers.
   Start with schema names, RTTI, named Cvar registration, caller relationships
   and parameter producers. Search the *new* image rather than assuming the old
   RVA or a uniform shift. Keep research scripts and decoded windows in
   `logs/research/<build>/` or another documented workspace log directory.
4. **Re-establish the contract.** Verify the complete relevant caller/callee,
   argument/return ABI, vtable slot, member widths/offsets, container ownership
   and lifecycle. Metadata strings alone do not establish C++ protobuf layouts.
   Compare preserved old disassembly where useful, accounting for decoded
   relative addresses rather than masking arbitrary changed bytes.
5. **Freeze the evidence.** Add independently decoded operands/schema records
   and checked instruction windows to the verifier. Then update the source
   header, entry fingerprints and exact build pins together. Record old/new
   values and the reason for each in the relevant evidence document. Never
   make a source constant and a second copied literal the only cross-check.
6. **Reject bad inputs.** Verify the real header, then deliberately change
   representative globals, members and fingerprints in temporary header copies
   under `logs/`. They must fail. ESP and movement accept `--header PATH` to
   verify an alternate header; this option does not generate one. Do not modify
   installed files for these checks. Some world verifiers also accept `--header`;
   check that tool's `--help` rather than assuming a common option.
7. **Run the combined check again.** Keep the fresh bundle separate from the
   initial failure bundle. Run the build/test commands below for implementation
   changes, and finish with the relevant offline observation.
8. **Leave a useful handoff.** Update README compatibility, the address index,
   evidence and RESUME. Separate static verification, builds, tests and actual
   observed behavior. Keep historical addresses clearly labeled by build.

## Local tools and evidence

All twelve frozen verifiers use standard-library Python. Their entry points are
listed below; the combined runner supplies the correct installation arguments.

| Component | Frozen verifier | Detailed evidence |
| --- | --- | --- |
| ESP/shared entities | [inspect_cs2.py](../tools/inspect_cs2.py) | [entity evidence](../tools/CS2_ENTITY_EVIDENCE.md) |
| Jump/steering/input | [inspect_movement.py](../tools/inspect_movement.py) | [movement evidence](../tools/CS2_MOVEMENT_EVIDENCE.md) |
| Silent aim history | [inspect_silent_aim.py](../tools/inspect_silent_aim.py) | [history evidence](../tools/CS2_SILENT_AIM_EVIDENCE.md) |
| Third-person view/model/collision | [inspect_third_person.py](../tools/inspect_third_person.py) | [camera evidence](../tools/CS2_THIRD_PERSON_EVIDENCE.md) |
| Aim eye/entity source | [inspect_silent_aim_targets.py](../tools/inspect_silent_aim_targets.py) | [eye evidence](../tools/CS2_SILENT_AIM_TARGET_EVIDENCE.md) |
| Aim named bone points | [inspect_silent_aim_hitboxes.py](../tools/inspect_silent_aim_hitboxes.py) | [bone evidence](../tools/CS2_SILENT_AIM_HITBOX_EVIDENCE.md) |
| Sky | [verify_sky_draw.py](../tools/verify_sky_draw.py) | [sky evidence](../tools/CS2_SKY_DRAW_EVIDENCE.md) |
| Flat world material | [verify_world_materials.py](../tools/verify_world_materials.py) | [material evidence](../tools/CS2_MATERIAL_EVIDENCE.md) |
| Local-light radiance | [verify_world_lighting.py](../tools/verify_world_lighting.py) | [local-light evidence](../tools/CS2_LIGHTING_EVIDENCE.md) |
| Light removal | [verify_lighting_switches.py](../tools/verify_lighting_switches.py) | [gather evidence](../tools/CS2_ENV_LIGHT_EVIDENCE.md) |
| Sun/directional lightmaps | [verify_environment_lighting.py](../tools/verify_environment_lighting.py) | [baked-light evidence](../tools/CS2_BAKED_LIGHT_EVIDENCE.md) |

[inspect_world.py](../tools/inspect_world.py) runs the five world checks but stops
on the first failure. The new combined runner is preferable for update triage.
Its `--legacy-update` mode belongs to old sky research, not the current feature.
Other `inspect_*` scripts can contain historical/frozen research; read their
docstrings/build requirements before using them against a new image.

The former a2x/cs2-dumper JSON remains historical 1.41.9.0 evidence. Current
entity verification reads the installed PE's static schema templates. If a new
live dump is needed, establish its association with that exact client build and
retain tool version/hash/license provenance. Do not substitute online dumps.

Existing optional disassembly dependencies are under
`logs/research/python-deps/`; this ignored directory is a local convenience,
not a portable project dependency. Discovery scripts needing Capstone or other
tools should record their versions. The final frozen verifiers remain portable.

## Build and offline observation

Existing configured builds:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build-diagnostic --config Release --parallel
ctest --test-dir build-diagnostic -C Release --output-on-failure
```

For first-time configuration, see the [README build instructions](../README.md).
The current baseline has 16 CTest suites per variant. Passing these tests does
not certify a new binary or a successful hop/rendered player.

Use a fresh normal client process and the documented launcher. Loading at the
disconnected menu is authorized; gameplay observations stay offline or in a
user-controlled private session. Follow [AGENTS.md](../AGENTS.md) before any
server interaction, including verifying the designated private server and its
loopback-only configuration. Do not add session-detection gates to the loader.

| Feature | Logs and observation |
| --- | --- |
| ESP | `ESP_BUILD_VERIFIED`, `ESP_SCAN`, `ESP_PLAYER`, `ESP_DRAW`; confirm visible names/boxes, health, distances and changing pawn identities. An empty menu scene is not a player-ESP test. |
| Autobhop | `MOVE_BUILD_VERIFIED`, `MOVE_OWNER`, `MOVE_JUMP`, `MOVE_CONSUMED`, `MOVE_JUMP_EXEC`; F9 off to isolate jump, hold Space and observe actual takeoffs/grounded gaps. |
| Steering | F9 on, WASD/diagonals/reversal/camera changes and release; inspect `MOVE_PERF`/`MOVE_STAGE`. Predicted speed is distinct from observed movement. |
| World/lighting | `WORLD_*_BUILD_VERIFIED` and component counters; compare each enabled control visually. Packet/callback counts alone do not prove the effect. |
| Stop | End/explicit cooperative stop; look for `STOPPED_RESIDENT`. Stopped code remains resident until normal process exit; restart before mapping another copy. |

Keep initialization outside DllMain and mutations on their verified engine
callbacks. Refreshing an address must not weaken existing ownership, restoration
or cooperative-shutdown guarantees.

## Evidence entry template

Copy this into the feature evidence document for the next build:

```text
Date/build: patch / client-server / Steam build / source revision
Installation + executable + module:
SHA-256 / machine / PE timestamp / image size:
Source symbol + owning header:
Previous value -> current value (RVA, member offset, key or mask):
Rediscovery anchor (schema, RTTI, Cvar name, caller, string):
Decoded instruction windows + operand derivation:
Argument/return ABI + ownership/lifecycle confirmation:
Frozen verifier changes + command/report:
Rejected temporary header mutations:
Build/CTest results:
Live observation or explicitly unobserved behavior:
Remaining uncertainty / required next observation:
```
