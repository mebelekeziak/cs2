#pragma once
#include "silent_aim_target.h"
#include <cstdint>

using AimWeaponSpreadFn = float (*)(void*);
using AimWeaponInaccuracyFn = float (*)(void*, float*, float*);
using AimWeaponCooldownFn = int (*)(void*);
using AimWeaponTimeFn = float* (*)(float*, int);
using AimWeaponPunchFn = AimAngles* (*)(void*, AimAngles*, bool);
struct AimWeaponContext {
    std::uintptr_t client = 0;
    AimWeaponSpreadFn spread = nullptr;
    AimWeaponInaccuracyFn inaccuracy = nullptr;
    AimWeaponCooldownFn cooldown = nullptr;
    AimWeaponTimeFn time = nullptr;
    AimWeaponPunchFn punch = nullptr;
};
struct AimWeaponSnapshot {
    std::uintptr_t weapon = 0, identity = 0, services = 0, data = 0, punchServices = 0;
    std::uint32_t handle = UINT32_MAX;
    float spread = 0, inaccuracy = 0, range = 0, recoilIndex = 0;
    float currentTime = 0, nextAttack = 0;
    AimAngles aimPunch{};
    int mode = 0, clip = 0;
    bool fullAuto = false, negev = false, forceMaxRadius = false, fixedSpreadAngle = false;
    bool ready = false, reloading = false;
    const char* name = "unavailable";
    const char* reason = "weapon unavailable";
};
// Prepare outside DllMain. Read only on the native command/prediction owner.
// No accuracy update, RNG seeding, direct entity writes or shot construction.
// The native punch getter may refresh its own engine-owned memoization caches.
bool AimWeaponPrepare(std::uintptr_t client, AimWeaponContext& context) noexcept;
bool AimWeaponRead(const AimWeaponContext& context, const AimSource& source,
    AimWeaponSnapshot& out) noexcept;
// Read only the visual accuracy cone on the native command owner. Does not
// query firing readiness, recoil punch, time or console variables.
bool AimWeaponReadSpread(const AimWeaponContext& context, const AimSource& source,
    AimWeaponSnapshot& out) noexcept;
// Revalidate identity/data/mode/ammo immediately before committing an aim/fire edit.
bool AimWeaponStillCurrent(const AimWeaponContext& context, const AimSource& source,
    const AimWeaponSnapshot& snapshot) noexcept;
#ifdef CS2_AIM_WEAPON_CHECKS
void AimWeaponTestRuntime(bool enabled) noexcept;
#endif
