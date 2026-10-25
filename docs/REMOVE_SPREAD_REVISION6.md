# Completed-command rebuild regression — revision 6

October 24, 2026. This fixes a regression introduced by revision 5's completed
spread-publication replay. The user reports that enabling Remove spread stops
Autofire and target-point visualization, and disabling it does not recover.

## Evidence and cause

The two subsequent workspace logs reproduce the stopped state:

| Capture | Corrected publication before failure | First sampled stopped diagnostic |
| --- | --- | --- |
| `overlay-19784-134373202283434583.log` | sequence 2235, line 2275 | line 2287, `autofire ownership changed; stopped` |
| `overlay-11208-134373211601731066.log` | sequence 2461, line 1580 | line 1596, same status |

Both diagnostics show empty history, no primary index and native buttons 0x8.
The completed-publication rejection counter becomes one; retained/replayed
remain zero. Callbacks continue, while aim decisions and fire counters freeze.
Turning Remove spread off leaves `active=0`. Both failing publications later
settle without an observed native fire event. One earlier corrected Deagle
publication in capture 19784 does fire and hit, so this is a lifecycle failure
after publication rather than evidence that every corrected trajectory fails.
The logs sample diagnostics periodically and do not identify the exact native
instruction or ownership field that first changed.

Source tracing explains the failure: replay runs before ordinary command-rebuild
retirement. Its rejection calls the strict pending-request cancellation helper
using ownership copied from a completed publication. Native code can already
have replaced those primary fields. Strict cancellation then reports changed
ownership and permanently stops the whole aim component. The normal rebuild
helper already distinguishes native replacement and physical takeover, but
the new replay path did not use it.

A separate defect in the completed-command Prepare branch skips the point scan.
Its previous points expire after 100 ms even when aim remains active.

## Fix

Completed replay uses the existing rebuild ownership classification before
attempting an owned undo. Native button replacement and physical takeover
retire copied ownership without editing current input or stopping aim. An
unchanged completed synthetic pulse may still replay the original indexed
angles. An unverified indexed history with a still-owned pulse removes only
that pulse.

Retired completed sequences retain a marker, preventing a second automatic
request using post-shot accuracy on the same sequence. This marker survives
temporary weapon-model unavailability. A fresh command can use the ordinary
selection/fire path. Source, settings and actual immutable weapon-key changes
continue to invalidate the relevant ownership scope.

Checksum repair follows the native phase: Prepare precedes the original
finalizer and lets it regenerate the checksum; post-History retirement
refinalizes immediately only when it changed command bits. Genuine unresolved
restoration or checksum failures still stop the component. Pending requests
retain their existing strict cancellation behavior.

Completed and retired Prepare paths now refresh visualization with the normal
base-view, preview-only scan. That scan does not request fire, solve spread,
capture shot metadata, rewrite history or acquire braking. Missing/unavailable
preview data clears points through the existing path.

`AIM_SPREAD_REBUILD revision=6` records retirement phase, reason, command
sequence, actual mutation and checksum handling. `AIM_SPREAD_DIAGNOSTICS` adds
a retired-publication count. Diagnostics remain queued on the command owner.
The timing hypotheses and revision 5 search-performance changes are preserved.

## Regression verification

The new integration fixture copies the failed sequence 2461's empty-history,
index -1 and native button-reset transition into both Prepare and post-History
phases. Before the fix, it reproduces the global stop and failed off/on
recovery; eight assertions fail. The fixed pipeline passes those assertions
and additionally checks temporary model loss, fresh subsequent shots, physical
takeover, preview refresh and phase-specific checksum repair. It also preserves
the stop on a real checksum failure. Focused suites pass 452 pipeline checks
and 89 command-ownership checks.

Both final public and diagnostic x64 Release builds succeed without project
compiler warnings and pass **67/67 CTest suites each**. All **33/33 installed-file
compatibility checks** pass in `logs/compatibility/20261024T133137220121Z/`.
Both DLLs remain AMD64 with zero static TLS and delay imports. Build/test logs
are `logs/remove-spread-r6-{build,tests}-{public,diagnostic}.log`; exact payload
hashes and PE inspection are in `logs/remove-spread-r6-payload-contract.json`.

Reproduction output is preserved as `logs/remove-spread-r6-repro-*.log`, focused
verification as `logs/remove-spread-r6-focused-*.log`. Exact log hashes, line
windows, parser and audit are under
`logs/research/26144732/spread_revision6/log_audit/`.

This pass uses the existing Windows AMD64 client 2000935 / Steam build 26144732
and D3D11 workflow. No installed game file, setting, native address, loader or
hook was changed. No game/server was launched, joined or injected during this
repair. The new DLL must be loaded into a fresh normal CS2 process; a revision 5
DLL that has already stopped remains resident and cannot be repaired by a menu
toggle. Stopped and resident is not physically unloaded. Live gameplay recovery
still requires a permitted private/offline trial.
