# Model catalog startup crash, October 18, 2026

Checkpoint `bbc4d6b` preserves the reported state, whose implementation was
`f0d588f`. This is a client overlay startup failure. No native model assignment
had run.

## Evidence

The matching logs are `logs/overlay-25584-134368014958016928.log` and
`logs/mapper-25584-134368014958016928.log`. Overlay initialization ends after
`ZOOM_BUILD_VERIFIED`, before `MODEL_SWAP_BUILD_VERIFIED` or hook activation.
The mapper reports thread exit **3765269347 (`0xE06D7363`)**, with unwind
registration successful and bootstrap completion still unset.

Steam saved `crash_cs2.exe_20261018145219_1.dmp`. The exception is on startup
thread 34408. Its ThrowInfo decodes `std::runtime_error` and `std::exception`.
The matching payload loads the message **Model does not reference the installed
AG2 world graph** and calls `_CxxThrowException` at overlay RVA `0x3D273`;
return address `0x3D278` is present on the exception thread's stack. This maps
to the ordinary unsupported-model rejection in the original validator.

The payload's `_CxxThrowException` calls `RtlPcToFileHeader` to obtain its image
base. The result is zero for this manually mapped image, and the raised
exception contains image-base parameter zero and magic `0x1994000`. Successful
`RtlAddFunctionTable` registration did not provide that missing image lookup.
Consequently the intended local C++ catch did not handle this routine rejection.
The original payload SHA-256 was
`8277000f58fd486a31afe501eecefc27d2f79a062798ccb2780c10018a8a59bc`.

Generated dump inspection, exact-payload disassembly and a preserved payload
are under `logs/research/model_crash/`. No dump or game asset is redistributed.

## Correction

Metadata readers now return explicit success/error values for malformed,
unsupported or missing resources. Expected filesystem failures use error-code
overloads, and UTF-8/native path conversion checks Win32 results. Index-build
failure still invalidates its temporary index; catalog failure still retains
the last completely published immutable snapshot. Validation rules and the
739-entry installed catalog are preserved.

`MODEL_SWAP_CATALOG_BEGIN` now identifies disk discovery before native model
loading or hook activation. A catalog failure reports model-swap unavailability
and allows the remaining overlay startup to continue. No session gates,
installed-file changes, or changes to the production mapper were introduced.

## Regression and scope

The old normally linked tests exercised throw/catch successfully and missed
the mapped-image failure. The new `model_catalog_mapping_checks` builds a
catalog-only fixture DLL and uses the **unchanged production mapper**, its
import resolver, CRT bootstrap and dynamic unwind registration to map it into
an isolated test child. The fixture scans a valid model alongside a malformed
model, an unsupported graph and a missing dependency, then checks invalid-VPK
and missing-root failures with snapshot retention. It installs no game hooks,
calls no engine APIs and uses no CS2 process. No mapped code is physically
unloaded while executing.

Using the `f0d588f` catalog/validator in this probe reproduced child exit
`0xE06D7363`; the corrected sources complete successfully. The test also rejects
any first-chance C++ exception. The direct parser/catalog tests now count
first-chance exceptions across invalid metadata and filesystem cases too.

Reproduce the mapped regression after building:

```powershell
ctest --test-dir build -C Release -R model_catalog_mapping_checks --output-on-failure
.\build\Release\model_catalog_mapping_checks.exe "$PWD\build\Release\model_catalog_mapping_probe.dll" --scan 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo'
```

The installed-pack scan is read-only. This confirms catalog execution through
the mapper; it does not certify live character loading, animation or appearance.
No CS2 client/server was started during this correction.

Both x64 Release variants build without compiler warnings and pass 41/41 CTest
suites. The direct resource parser passes 75 checks; the installed catalog scan
still reports 739 entries and zero first-chance C++ exceptions. Build/test and
installed-scan diagnostics are `logs/model-crash-*`. Both rebuilt payloads keep
zero static TLS and delay imports.
