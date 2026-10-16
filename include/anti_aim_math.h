#pragma once
#include "silent_aim_math.h"

struct AntiAimMovement {
    float forward = 0, left = 0, up = 0;
    bool valid = false;
};

// Copied values only; no local camera writes. Output yaw is in [-180,180).
// Failure leaves command unchanged.
bool AntiAimBackwards(const AimAngles& local, AimAngles& command) noexcept;

// WALK uses individually flattened, normalized forward/positive-left vectors.
// Preserves world wish vector and analog length without per-axis clipping.
// Arbitrary wrapped yaw/pitch is supported except exactly vertical forward;
// nonzero roll is rejected because its flattened basis is not orthonormal.
// Failure returns valid=false and zero axes. Runtime must preserve its input.
AntiAimMovement AntiAimCorrectWalkMovement(const AimAngles& from, const AimAngles& to,
                                           float forward, float left) noexcept;

// Complete orthonormal Source angle-basis transform, including vertical views
// and roll. This is pure 3D math, NOT the installed WALK movement model, and is
// not proof of noclip/ladder/swimming semantics. No game memory is accessed.
AntiAimMovement AntiAimCorrectMovement3D(const AimAngles& from, const AimAngles& to,
                                         float forward, float left, float up) noexcept;
