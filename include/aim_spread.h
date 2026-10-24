#pragma once
#include "aim_spread_search.h"
#include "aim_weapon.h"
#include "silent_aim_command.h"

struct AimSpreadTime { int tick = 0; float fraction = 0; };
using AimSpreadTickFn = int* (*)(int*, int, int);
using AimSpreadTimeFn = AimSpreadTime* (*)(AimSpreadTime*, int);
using AimSpreadPunchFn = AimAngles* (*)(void*, AimAngles*, const AimSpreadTime*, bool);
using AimSpreadSeedFn = std::uint32_t (*)(void*, const AimAngles*, int);
using AimSpreadRandomCtor = void* (*)(void*, int);
using AimSpreadRandomFloat = float (*)(void*, float, float);
struct AimSpreadContext {
    std::uintptr_t client = 0, tier0 = 0;
    AimSpreadTickFn convert = nullptr;
    AimSpreadTimeFn time = nullptr;
    AimSpreadPunchFn punch = nullptr;
    AimSpreadSeedFn seed = nullptr;
    AimSpreadRandomCtor randomCtor = nullptr;
    AimSpreadRandomFloat randomFloat = nullptr;
};
struct AimSpreadSnapshot {
    AimSpreadTime time{}, now{}, converted{};
    AimSpreadTime alternateTime{};
    AimAngles punch{};
    AimAngles alternatePunch{};
    AimVector velocity{};
    std::uint32_t movementFlags = 0;
    bool movementStateKnown = false;
    AimWeaponSnapshot weapon{};
    std::uintptr_t entry = 0;
    int historyTick = 0, scene = -1;
    int commandSequence = 0, historyIndex = -1, controllerTick = -1;
    unsigned seedVariants = 1;
    bool rawClockAlternative = false;
    float historyFraction = 0;
    bool pattern = false, valid = false;
    const char* reason = "not evaluated";
};
// Initialization outside DllMain. Calls below belong to the native command owner.
// Only indexed single-projectile primary shots are modeled. Native SHA-1 and a
// private null-mutex random stream never seed or advance the engine's RNG.
// Scout sampling covers two copied clock/recoil models. A raw indexed timestamp
// more than one tick ahead uses current predicted recoil for the alternative;
// this does not determine the eventual authoritative shot time or server seed.
bool AimSpreadPrepare(std::uintptr_t client, AimSpreadContext& context) noexcept;
bool AimSpreadRead(const AimSpreadContext& context, const AimSource& source,
    const AimWeaponSnapshot& weapon, const AimShot& shot, AimSpreadSnapshot& out) noexcept;
bool AimSpreadStillCurrent(const AimSpreadContext& context, const AimSource& source,
    const AimSpreadSnapshot& snapshot) noexcept;
AimSpreadSearchResult AimSpreadSolve(const AimSpreadContext& context,
    const AimSpreadSnapshot& snapshot, const AimAngles& desired) noexcept;
#ifdef CS2_AIM_SPREAD_CHECKS
void AimSpreadTestRuntime(bool enabled) noexcept;
#endif
