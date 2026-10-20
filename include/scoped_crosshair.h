#pragma once
#include "scoped_crosshair_draw.h"

// Setup outside DllMain; native accuracy getters run only in CaptureCommand on
// the existing command owner. Render reads copied values and entity bytes only.
bool ScopedCrosshairPrepare() noexcept;
void ScopedCrosshairCaptureCommand() noexcept;
ScopedCrosshairDrawInput ScopedCrosshairCaptureForDraw() noexcept;
// Native scope output may disable its graphic only for this matching local pawn
// when a fresh replacement is ready.
bool ScopedCrosshairSuppressNativeScope(std::uintptr_t pawn) noexcept;
// Iron-sight rendering carries its controller's owning weapon instead of pawn.
bool ScopedCrosshairSuppressNativeIronSight(std::uintptr_t weapon) noexcept;
void ScopedCrosshairStop() noexcept;

#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
#include "aim_weapon.h"
using ScopedCrosshairClock = unsigned long long (*)();
void ScopedCrosshairTestConfigure(const AimWeaponContext& context, ScopedCrosshairClock clock) noexcept;
#endif
