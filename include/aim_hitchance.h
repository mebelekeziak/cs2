#pragma once
#include "silent_aim_math.h"
#include <array>

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

// Pure copied spread model. Reuse within one decision across candidate angles
// and capsules; no engine pointers, native RNG, shared state or static TLS.
struct AimSpreadPattern {
    static constexpr unsigned Capacity = 1024;
    std::array<std::array<double, 2>, Capacity> offsets{};
    float inaccuracy = 0, spread = 0, recoilIndex = 0;
    unsigned samples = 0;
    bool negev = false, forceMaxRadius = false, fixedSpreadAngle = false;
    bool valid = false;
};
bool AimPrepareSpreadPattern(const AimHitchanceInput& input, AimSpreadPattern& pattern) noexcept;

// Finite shot segment, including tangencies and starts inside the capsule.
bool AimRayIntersectsCapsule(const AimVector& origin, const AimVector& direction,
                             float range, const AimCapsule& capsule) noexcept;
// Closest clearance of the finite segment from the capsule surface. Zero for
// intersection/tangency, -1 for invalid inputs.
float AimRayCapsuleMissDistance(const AimVector& origin, const AimVector& direction,
                               float range, const AimCapsule& capsule) noexcept;
// Fixed four-dimensional low-discrepancy samples; does not touch native RNG.
// Reports the full requested sample count. Zero spread has one identical ray
// for every sample and evaluates that ray once. Geometric capsule probability.
AimHitchanceResult AimEstimateHitchance(const AimHitchanceInput& input) noexcept;
// A mismatched/invalid pattern safely falls back to the standalone estimator.
AimHitchanceResult AimEstimateHitchance(const AimHitchanceInput& input,
                                      const AimSpreadPattern& pattern) noexcept;
