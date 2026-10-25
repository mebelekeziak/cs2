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
    // Single-model solves route callback variant zero to this retained clock
    // and its matching recoil. Explicit two-model copied solves use both.
    unsigned selectedVariant = 0;
    bool rawClockAlternative = false;
    float historyFraction = 0;
    bool pattern = false, valid = false;
    const char* reason = "not evaluated";
};
// Initialization outside DllMain. Calls below belong to the native command owner.
// Only indexed single-projectile primary shots are modeled. Native SHA-1 and a
// private null-mutex random stream never seed or advance the engine's RNG.
// Supported firearms select one copied indexed clock/recoil model. Indexed time
// ahead of the clamped native time selects the retained alternative; a lead of
// more than one tick uses raw indexed time. Its scaled punch is queried through
// native ShotPunch at that exact selected scene time and freshness-checked.
// Future engine state can still change; this is a client model choice, not an
// authoritative server shot time, future recoil state or seed.
bool AimSpreadPrepare(std::uintptr_t client, AimSpreadContext& context) noexcept;
// Copy the verified distribution-mode cvar on the command owner, without RNG.
bool AimSpreadReadPattern(const AimSpreadContext& context, bool& pattern) noexcept;
bool AimSpreadRead(const AimSpreadContext& context, const AimSource& source,
    const AimWeaponSnapshot& weapon, const AimShot& shot, AimSpreadSnapshot& out) noexcept;
bool AimSpreadStillCurrent(const AimSpreadContext& context, const AimSource& source,
    const AimSpreadSnapshot& snapshot) noexcept;
// Optional copied-target tolerance and per-attempt work bound. A reduced budget
// can defer automatic fire; callers must still verify each returned ray.
AimSpreadSearchResult AimSpreadSolve(const AimSpreadContext& context,
    const AimSpreadSnapshot& snapshot, const AimAngles& desired,
    double stopResidualDegrees = .0002, unsigned maxEvaluations = 8192) noexcept;
#ifdef CS2_AIM_SPREAD_CHECKS
void AimSpreadTestRuntime(bool enabled) noexcept;
#endif
