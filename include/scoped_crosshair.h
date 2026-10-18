#pragma once
#include "scoped_crosshair_draw.h"

// Setup outside DllMain; native accuracy getters run only in CaptureCommand on
// the existing command owner. Render reads copied values and entity bytes only.
bool ScopedCrosshairPrepare() noexcept;
void ScopedCrosshairCaptureCommand() noexcept;
ScopedCrosshairDrawInput ScopedCrosshairCaptureForDraw() noexcept;
void ScopedCrosshairStop() noexcept;

#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
#include "aim_weapon.h"
using ScopedCrosshairClock = unsigned long long (*)();
void ScopedCrosshairTestConfigure(const AimWeaponContext& context, ScopedCrosshairClock clock) noexcept;
#endif
