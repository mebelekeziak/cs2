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

inline constexpr std::size_t AirSteeringSliceCount = 8;
struct SteeringCommand {
    bool valid = false;
    std::size_t count = 0;
    std::array<SteeringOutput, AirSteeringSliceCount> slices{};
    SteeringOutput summary{};
};

// Physical WASD selects camera-relative travel heading, including diagonals.
// No net input returns an invalid plan, leaving ordinary release behavior alone.
// Full-input gain impulses alternate near intent. Direction capture uses the
// maximum available heading change, allowing speed loss. Behind-velocity intent
// brakes directly toward the requested axis instead of taking a wide orbit.
// Turning obeys supplied acceleration. Camera is unchanged.
// physics.interval is the duration actually simulated by this command fragment.
// Reset state at pawn/airborne lifecycle changes and whenever steering is disabled.
SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& input,
                              const AirPhysics& physics) noexcept;
void ResetAirSteering(SteeringState& state) noexcept;

// Eight real command fragments at when=i/count. Their engine-time deltas sum
// to physics.interval. Each plan uses the previous fragment's predicted velocity,
// rather than allocating a new tick's acceleration. View yaw remains unchanged.
// Callers must emit the corresponding timed analog transitions; using only the
// summary/base axes does not implement this integration plan.
SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& input,
                                      const AirPhysics& physics) noexcept;
