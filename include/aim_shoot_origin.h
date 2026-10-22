#pragma once
#include "silent_aim_target.h"
#include <cstdint>

using AimShootOriginFn = AimVector* (*)(void*, AimVector*);
struct AimShootOriginContext {
    std::uintptr_t client = 0;
    AimShootOriginFn getter = nullptr;
};
struct AimShootOriginSnapshot {
    AimVector position{};
    std::uintptr_t services = 0, identity = 0, scene = 0;
    std::uintptr_t camera = 0, observer = 0;
    std::uint32_t handle = UINT32_MAX, controllerHandle = UINT32_MAX;
    bool valid = false;
    const char* reason = "native shooting origin unavailable";
};
// Prepare outside DllMain. Read/revalidate only on the native command owner.
// The current native getter may refresh engine-owned scene/interpolation caches.
// This copies the current native origin; it does not select a historical shot
// timestamp, edit input-history timing or predict the future firing position.
bool AimShootOriginPrepare(std::uintptr_t client, AimShootOriginContext& context) noexcept;
bool AimShootOriginRead(const AimShootOriginContext& context, const AimSource& source,
    AimShootOriginSnapshot& out) noexcept;
// Re-queries the current native origin and rejects any source/position change.
bool AimShootOriginStillCurrent(const AimShootOriginContext& context, const AimSource& source,
    const AimShootOriginSnapshot& snapshot) noexcept;
#ifdef CS2_AIM_SHOOT_ORIGIN_CHECKS
void AimShootOriginTestRuntime(bool enabled) noexcept;
#endif
