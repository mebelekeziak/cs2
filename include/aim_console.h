#pragma once

// Call only from initialization/render telemetry consumers, outside DllMain and
// engine input hooks. The game's own logging policy controls console visibility.
bool AimConsoleInitialize() noexcept;
void AimConsoleWrite(const char* message) noexcept;

// Waits for an in-progress console call, then rejects subsequent writes. This
// does not unload this DLL or retain/pin the game's tier0 module.
void AimConsoleStop() noexcept;

#ifdef CS2_AIM_CONSOLE_CHECKS
using AimConsoleTestPrint = void (*)(const char*, ...);
// Replace only the external module boundary; formatting and shutdown remain the
// production path. Changing the target first drains and stops the current sink.
void AimConsoleTestSetTarget(AimConsoleTestPrint print, bool (*valid)() noexcept) noexcept;
#endif
