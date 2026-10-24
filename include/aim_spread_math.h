#pragma once
#include "silent_aim_math.h"

struct AimSpreadCompensation {
    AimAngles command{};
    // Basis angles after native float punch addition, without normalization;
    // roll remains native punch roll.
    AimAngles effective{};
    float residualDegrees = 0;
    bool valid = false;
    const char* reason = "not evaluated";
};

// Pure inverse of forward + right*x + up*y in the native AngleVectors basis.
// Offsets must describe the actual shot; this function neither predicts nor
// changes random seeds. Desired roll must be zero (a world direction), while
// punch includes native roll. Command roll is zero and pitch stays [-89, 89].
// Unreachable directions and nonfinite inputs fail without pitch clamping.
AimSpreadCompensation AimSpreadCompensate(const AimAngles& desired,
    const AimAngles& punch, double offsetX, double offsetY) noexcept;
