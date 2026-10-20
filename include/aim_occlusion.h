#pragma once
#include "silent_aim_math.h"
#include <cstdint>

struct AimSource;
struct AimTarget;
struct AimTargetVolume;
struct AimOcclusionContext {
    std::uintptr_t client = 0;
    bool prepared = false;
};
struct AimOcclusionResult {
    bool valid = false, clear = false;
    float fraction = 0;
    const char* reason = "occlusion not evaluated";
    // First native pawn collision, copied before the stack trace expires.
    // A clear segment without a known hitbox cannot predict health damage.
    bool hitgroupKnown = false;
    int hitgroup = 0;
    float distance = 0;
    // Native autowall copies pre-hitgroup/armor damage, including range loss.
    float penetrationDamage = -1, bulletRangeModifier = 0, bulletPenetration = 0;
    int bulletBaseDamage = 0, penetrations = 0;
};
// Native command owner only. Fresh direct line of fire to the selected capsule;
// no retained trace/manager/entity pointers and no penetration simulation.
bool AimOcclusionPrepare(std::uintptr_t client, AimOcclusionContext& context) noexcept;
AimOcclusionResult AimOcclusionCheck(const AimOcclusionContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume) noexcept;

#ifdef CS2_AIM_OCCLUSION_CHECKS
using AimOcclusionShapeFn = void (*)(void*, const float*);
using AimOcclusionResultFn = void (*)(void*);
using AimOcclusionFilterFn = void* (*)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
using AimOcclusionTraceFn = bool (*)(void*, void*, const void*, const void*, void*, void*);
void AimOcclusionTestConfigure(std::uintptr_t client, AimOcclusionShapeFn shape,
    AimOcclusionResultFn result, AimOcclusionFilterFn filter, AimOcclusionTraceFn trace) noexcept;
#endif
