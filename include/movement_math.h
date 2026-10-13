#pragma once
#include <cstdint>

struct AirPhysics {
    float wishSpeed = 0;
    float wishSpeedCap = 0;
    float acceleration = 0;
    float surfaceFriction = 0;
    float interval = 0;
};
struct StrafeInput {
    float velocityX = 0, velocityY = 0;
    float viewYaw = 0;
    float forward = 0, left = 0;
    std::uint32_t sequence = 0;
};
struct StrafeOutput {
    bool valid = false;
    float forward = 0, left = 0;
    float speed = 0, wishYaw = 0, accelerationStep = 0;
};

// Computes normalized camera-relative analog movement. Does not rotate the
// camera. Physics values must come from this engine's current movement rules.
StrafeOutput OptimizeAirStrafe(const StrafeInput& input, const AirPhysics& physics) noexcept;
