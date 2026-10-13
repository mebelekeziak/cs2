#pragma once
#include "movement_math.h"

enum class SteeringMode : unsigned char { Idle, Turning, Recovering, Aligned };

struct SteeringState {
    SteeringMode mode = SteeringMode::Idle;
    int turnSide = 0;
    float retainedSpeed = 0;
};

struct SteeringOutput {
    bool valid = false;
    float forward = 0, left = 0;
    float wishYaw = 0, accelerationStep = 0;
    float predictedVelocityX = 0, predictedVelocityY = 0;
    float headingError = 0, headingChange = 0;
    SteeringMode mode = SteeringMode::Idle;
};

// Input directions are physical intent, before movement synthesis. Positive left
// continuously turns counter-clockwise; releasing it recovers view-forward (or
// view-backward while S is held). The camera itself is never modified.
// physics.interval is the duration actually simulated by this command fragment.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;
