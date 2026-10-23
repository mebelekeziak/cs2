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

// Timed analog wishes are consumed over one command interval.
inline constexpr std::size_t AirSteeringSliceCount = 32;
struct SteeringCommand {
    bool valid = false;
    std::size_t count = 0;
    std::array<SteeringOutput, AirSteeringSliceCount> slices{};
    SteeringOutput summary{};
};

// Physical WASD selects camera-relative travel heading, including diagonals.
// No net input returns an invalid plan, leaving ordinary release behavior alone.
// Physics-based angle selection and velocity correction follow the algorithmic
// approach inspected in wubly/velocity's airstrafe.cpp. Independent implementation;
// no upstream command structures, engine offsets or prediction interfaces are used.
// W-only input participates. Below 10 units/s input follows the desired heading.
// Camera is unchanged. Prediction uses the supplied engine physics.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;

// Recalculate the wish after every 1/32 interval of predicted acceleration.
// Each slice's prediction uses that fraction of the supplied interval; the
// summary uses the final axes/velocity and whole-command impulse/heading change.
// Sequence and slice parity are deterministic across retained-command rebuilds.
SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& input,
                                      const AirPhysics& physics) noexcept;
