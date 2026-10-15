#pragma once
#include "silent_aim_math.h"
#include "silent_aim_settings.h"
#include <cstdint>

struct AimSource { std::uintptr_t system = 0, controller = 0, pawn = 0; };
struct AimActor {
    std::uintptr_t controller = 0, pawn = 0, scene = 0, collision = 0;
    std::uintptr_t modelHandle = 0, model = 0, boneArray = 0;
    std::uintptr_t skeletonVtable = 0, skeletonQuery = 0;
    std::uint32_t controllerHandle = UINT32_MAX, pawnHandle = UINT32_MAX;
    std::uint8_t team = 0;
};
struct AimTarget { AimActor local{}, enemy{}; AimSolution solution{}; };
using AimBoneLookup = int (*)(void*, const char*);
struct AimBoneContext { std::uintptr_t client = 0; AimBoneLookup lookup = nullptr; };

// Command-owner only; no shared ESP snapshots or retained engine pointers.
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, AimHitbox hitbox,
                           const AimBoneContext& bones, AimTarget& target) noexcept;
bool SilentAimTargetValid(const AimSource& source, const AimTarget& target) noexcept;
