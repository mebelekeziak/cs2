// Steering adapted from Axion-CS2-RAGE-CHEAT, Copyright (c) 2024 T1GXR.
// MIT license and source provenance: third_party/Axion-CS2-RAGE-CHEAT-*.txt.
#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double radians = std::numbers::pi / 180.0;
// User tuning; lower values increase correction angle and its deadband.
constexpr double smoothing = 50.0;
double Wrap(double degrees) noexcept { return std::remainder(degrees, 360.0); }
#ifdef CS2_STEERING_BENCHMARK
thread_local unsigned int candidateEvaluations = 0;
thread_local unsigned int commandEvaluations = 0;
#endif
}

#ifdef CS2_STEERING_BENCHMARK
unsigned int SteeringCandidateEvaluationsForBenchmark() noexcept { return candidateEvaluations; }
unsigned int SteeringCommandEvaluationsForBenchmark() noexcept { return commandEvaluations; }
#endif

void ResetAirSteering(SteeringState& state) noexcept { state = {}; }

SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& in,
                              const AirPhysics& p) noexcept {
#ifdef CS2_STEERING_BENCHMARK
    candidateEvaluations = 0;
#endif
    SteeringOutput out{};
    if (!std::isfinite(in.velocityX) || !std::isfinite(in.velocityY) ||
        !std::isfinite(in.viewYaw) || !std::isfinite(in.forward) || !std::isfinite(in.left) ||
        !std::isfinite(p.wishSpeed) || !std::isfinite(p.wishSpeedCap) ||
        !std::isfinite(p.acceleration) || !std::isfinite(p.surfaceFriction) ||
        !std::isfinite(p.interval) || p.wishSpeed <= 0 || p.wishSpeedCap <= 0 ||
        p.acceleration < 0 || p.surfaceFriction < 0 || p.interval <= 0 || p.interval > 0.1f) {
        ResetAirSteering(state); return out;
    }
    const double speed = std::hypot(static_cast<double>(in.velocityX), static_cast<double>(in.velocityY));
    const double available = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
    if (!std::isfinite(speed) || speed > 100000 || !std::isfinite(available) ||
        (std::abs(in.forward) <= 0.01f && std::abs(in.left) <= 0.01f)) {
        ResetAirSteering(state); return out;
    }

    const double camera = Wrap(in.viewYaw);
    const double desired = Wrap(camera + std::atan2(static_cast<double>(in.left), static_cast<double>(in.forward)) / radians);
    const double velocityYaw = std::atan2(static_cast<double>(in.velocityY), static_cast<double>(in.velocityX)) / radians;
    const double ideal = std::clamp(std::atan2(15.0, speed) / radians, 0.0, 45.0);
    const double correct = (100.0 - smoothing) * 0.02 * (ideal + ideal);
    const double delta = Wrap(desired - velocityYaw);
    double targetYaw = desired;
    int side = 0;
    state.mode = SteeringMode::Accelerating;
    // Physical input is resolved before this function, so synthesized buttons
    // cannot feed back. These are Axion's correction and parity branches.
    if (speed > 80 && (std::abs(delta) > 170 || delta > correct)) {
        targetYaw = velocityYaw + correct;
        side = -1;
        state.mode = SteeringMode::Turning;
    } else if (delta >= -correct || speed <= 80) {
        const bool even = (in.sequence & 1u) == 0;
        targetYaw += even ? -ideal : ideal;
        side = even ? -1 : 1;
    } else {
        targetYaw = velocityYaw - correct;
        side = 1;
        state.mode = SteeringMode::Turning;
    }
    state.turnSide = -side;
    // Axion rotates a pure side input, then negates its side axis. Our native
    // analog convention is positive LEFT; derive world wish from that rotation.
    const double wish = Wrap(targetYaw - side * 90.0);
    const double relative = Wrap(wish - camera) * radians;
    out.forward = static_cast<float>(std::cos(relative));
    out.left = static_cast<float>(std::sin(relative));
    out.wishYaw = static_cast<float>(wish);

    // Prediction uses live engine physics, not Axion's heuristic constants.
    // One chosen wish is applied for exactly one command interval.
    const double dx = std::cos(wish * radians), dy = std::sin(wish * radians);
    const double projection = in.velocityX * dx + in.velocityY * dy;
    const double step = std::min(available, std::max(0.0,
        std::min<double>(p.wishSpeedCap, p.wishSpeed) - projection));
    const double x = in.velocityX + step * dx, y = in.velocityY + step * dy;
    out.accelerationStep = static_cast<float>(step);
    out.predictedVelocityX = static_cast<float>(x);
    out.predictedVelocityY = static_cast<float>(y);
    const bool headingValid = std::hypot(x, y) > 0.000001;
    const double heading = headingValid ? std::atan2(y, x) / radians : velocityYaw;
    out.headingError = static_cast<float>(Wrap(desired - heading));
    out.headingChange = headingValid && speed > 0.000001 ? static_cast<float>(Wrap(heading - velocityYaw)) : 0;
    out.mode = state.mode; out.valid = true;
#ifdef CS2_STEERING_BENCHMARK
    candidateEvaluations = 1;
#endif
    return out;
}

SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& input,
                                      const AirPhysics& physics) noexcept {
    SteeringCommand command{};
    command.summary = PlanAirSteering(state, input, physics);
#ifdef CS2_STEERING_BENCHMARK
    commandEvaluations = candidateEvaluations;
#endif
    if (!command.summary.valid) return command;
    command.valid = true; command.count = 1;
    command.slices[0] = command.summary;
    return command;
}
