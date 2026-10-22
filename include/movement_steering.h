#pragma once
#include "movement_math.h"
#include <array>
#include <cstddef>

enum class SteeringMode : unsigned char { Idle, Turning, Accelerating, Reversing };

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

// Retain replay capacity so previously owned events can be retired safely.
inline constexpr std::size_t AirSteeringSliceCount = 8;
struct SteeringCommand {
    bool valid = false;
    std::size_t count = 0;
    std::array<SteeringOutput, AirSteeringSliceCount> slices{};
    SteeringOutput summary{};
};

// Physical WASD selects camera-relative travel heading, including diagonals.
// No net input returns an invalid plan, leaving ordinary release behavior alone.
// Axion's speed-based ideal angle, velocity correction and command-parity
// alternation select full-input movement. Smoothing is tuned to 50 (Axion default: 99).
// W-only input participates; no reference command-ring or engine layout is used.
// Camera is unchanged. Prediction uses the supplied engine physics.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;

// One chosen wish per command, published at time zero through the existing
// analog transition/replay path. Sequence parity does not advance on rebuilds.
SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& input,
                                      const AirPhysics& physics) noexcept;
