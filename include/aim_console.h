#pragma once
#include <cstdint>

// Four copied RGBA bytes used by the verified native color-print boundary.
struct AimConsoleColor { std::uint8_t r, g, b, a; };
static_assert(sizeof(AimConsoleColor) == 4);

// Call only from initialization/render telemetry consumers, outside DllMain and
// engine input hooks. The game's own logging policy controls console visibility.
bool AimConsoleInitialize() noexcept;
void AimConsoleWrite(const char* message) noexcept;
// Reprint the last 256 emitted project lines, preserving their colors. Called
// after native clear on the console command owner; does not duplicate history.
void AimConsoleReplay() noexcept;

// Waits for an in-progress console call, then rejects subsequent writes. This
// does not unload this DLL or retain/pin the game's tier0 module.
void AimConsoleStop() noexcept;

#ifdef CS2_AIM_CONSOLE_CHECKS
using AimConsoleTestPrint = void (*)(const char*, ...);
using AimConsoleTestColorPrint = void (*)(const AimConsoleColor&, const char*, ...);
// Replace only the external module boundary; formatting and shutdown remain the
// production path. Changing the target first drains and stops the current sink.
void AimConsoleTestSetTarget(AimConsoleTestPrint print, bool (*valid)() noexcept,
    AimConsoleTestColorPrint colorPrint = nullptr, bool (*colorValid)() noexcept = nullptr) noexcept;
#endif
