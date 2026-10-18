#pragma once
#include "silent_aim_math.h"

// Copied geometry only. Capsule endpoints are centers of its spherical ends.
struct AimCapsule { AimVector start{}, end{}; float radius = 0; };
struct AimHitchanceInput {
    AimVector eye{};
    AimAngles angles{};
    AimCapsule capsule{};
    float inaccuracy = 0, spread = 0, range = 0, recoilIndex = 0;
    unsigned samples = 256;
    bool negev = false, forceMaxRadius = false, fixedSpreadAngle = false;
};
struct AimHitchanceResult {
    unsigned hits = 0, samples = 0;
    float percent = 0;
    bool valid = false;
    const char* reason = "not evaluated";
};

// Finite shot segment, including tangencies and starts inside the capsule.
bool AimRayIntersectsCapsule(const AimVector& origin, const AimVector& direction,
                             float range, const AimCapsule& capsule) noexcept;
// Closest clearance of the finite segment from the capsule surface. Zero for
// intersection/tangency, -1 for invalid inputs.
float AimRayCapsuleMissDistance(const AimVector& origin, const AimVector& direction,
                               float range, const AimCapsule& capsule) noexcept;
// Fixed four-dimensional low-discrepancy samples; does not touch native RNG.
// Evaluates every requested sample. Geometric selected-capsule probability only.
AimHitchanceResult AimEstimateHitchance(const AimHitchanceInput& input) noexcept;
