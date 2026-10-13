#pragma once
#include "movement_math.h"

enum class SteeringMode : unsigned char { Idle, Turning, Accelerating };

struct SteeringState {
    SteeringMode mode = SteeringMode::Idle;
    int turnSide = 0;
};

struct SteeringOutput {
    bool valid = false;
    float forward = 0, left = 0;
    float wishYaw = 0, accelerationStep = 0;
    float predictedVelocityX = 0, predictedVelocityY = 0;
    float headingError = 0, headingChange = 0;
    SteeringMode mode = SteeringMode::Idle;
};

// Physical WASD selects camera-relative travel heading, including diagonals.
// No net input returns an invalid plan, leaving ordinary release behavior alone.
// Speed gain above the air projection cap uses bounded lateral impulses near
// that heading. Turning still obeys supplied acceleration. Camera is unchanged.
// physics.interval is the duration actually simulated by this command fragment.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;
