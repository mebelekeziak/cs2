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
    // Quantized commands publish a safe digital fallback first. The command
    // orientation owner can represent the continuous wish as full Forward in
    // this world basis, preserving the local camera through its normal replay.
    bool commandBasis = false;
    float basisWishYaw = 0;
};

// Physical WASD selects camera-relative travel heading, including diagonals.
// No net input returns an invalid plan, leaving ordinary release behavior alone.
// The velocity-relative wish uses the installed accelerator's maximum-gain
// projection, widened toward intent when needed for low-speed control. Every
// wish has nonnegative velocity projection, avoiding intentional air braking.
// There are no fixed speed thresholds. Opposite intent turns through an arc;
// acceleration limits the attainable heading change. W-only input participates.
// Camera is unchanged. Prediction uses the supplied engine physics and does
// not account for collisions, ground friction or subsequent native modifiers.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;

// Recalculate the wish after each requested fraction of predicted acceleration.
// sliceCount must be 1..AirSteeringSliceCount; fewer events can avoid additional
// ground integration boundaries when landing during this command is possible.
// Each slice's prediction uses that fraction of the supplied interval; the
// summary uses the final axes/velocity and whole-command impulse/heading change.
// Sequence and slice parity are deterministic across retained-command rebuilds.
// Quantized input selects one whole-interval wish, a digital fallback, and its
// ideal command basis. The orientation owner reports whether it was applied;
// fallback summary prediction does not claim the upgraded native outcome.
SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& input,
                                      const AirPhysics& physics,
                                      std::size_t sliceCount = AirSteeringSliceCount) noexcept;
