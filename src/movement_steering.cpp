// Independent implementation informed by Velocity's physics-aware timed steering.
// https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/movement/impl/airstrafe.cpp
// Inspected blob d39767b2e52c82fd088c7678fbdbb02fad167723. No upstream code copied.
#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double radians = std::numbers::pi / 180.0;
double Wrap(double degrees) noexcept { return std::remainder(degrees, 360.0); }
#ifdef CS2_STEERING_BENCHMARK
thread_local unsigned int candidateEvaluations = 0;
thread_local unsigned int commandEvaluations = 0;
#endif
bool Valid(const StrafeInput& in, const AirPhysics& p) noexcept {
    return std::isfinite(in.velocityX) && std::isfinite(in.velocityY) &&
        std::isfinite(in.viewYaw) && std::isfinite(in.forward) && std::isfinite(in.left) &&
        std::isfinite(p.wishSpeed) && std::isfinite(p.wishSpeedCap) &&
        std::isfinite(p.acceleration) && std::isfinite(p.surfaceFriction) &&
        std::isfinite(p.interval) && p.wishSpeed > 0 && p.wishSpeedCap > 0 &&
        p.acceleration >= 0 && p.surfaceFriction >= 0 && p.interval > 0 && p.interval <= 0.1f &&
        std::hypot(static_cast<double>(in.velocityX), static_cast<double>(in.velocityY)) <= 100000 &&
        (std::abs(in.forward) > 0.01f || std::abs(in.left) > 0.01f);
}
SteeringOutput Slice(SteeringState& state, double x, double y, double camera, double desired,
                     const AirPhysics& p, double duration, bool positiveSide) noexcept {
    SteeringOutput out{};
    const double speed = std::hypot(x, y);
    const double available = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * duration;
    const double velocityYaw = std::atan2(y, x) / radians;
    const double cap = std::min<double>(p.wishSpeedCap, p.wishSpeed);
    if (!std::isfinite(speed) || !std::isfinite(available)) return out;
    double wish = desired;
    state.mode = SteeringMode::Accelerating;
    state.turnSide = 0;
    if (speed >= 10) {
        // Half-budget selects the steering angle. Prediction still spends the
        // full native acceleration budget for this fraction of the command.
        const double halfBudget = available * 0.5;
        const double projection = std::max(halfBudget, cap - halfBudget);
        const double ideal = std::clamp(std::atan2(projection, speed) / radians, 0.0, 45.0);
        const double error = Wrap(desired - velocityYaw);
        const bool turning = speed > 80 && (std::abs(error) > 170 || std::abs(error) > ideal);
        const int side = turning ? ((std::abs(error) > 170 || error > 0) ? 1 : -1) :
            (positiveSide ? 1 : -1);
        const double heading = turning ? velocityYaw : desired;
        wish = Wrap(heading + side * (turning ? 90.0 + ideal : 90.0 - ideal));
        state.turnSide = side;
        if (turning) state.mode = SteeringMode::Turning;
    }
    const double relative = Wrap(wish - camera) * radians;
    out.forward = static_cast<float>(std::cos(relative));
    out.left = static_cast<float>(std::sin(relative));
    out.wishYaw = static_cast<float>(Wrap(wish));
    const double dx = std::cos(wish * radians), dy = std::sin(wish * radians);
    const double step = std::min(available, std::max(0.0, cap - x * dx - y * dy));
    const double finalX = x + step * dx, finalY = y + step * dy;
    const double heading = std::hypot(finalX, finalY) > 0.000001 ? std::atan2(finalY, finalX) / radians : velocityYaw;
    out.accelerationStep = static_cast<float>(step);
    out.predictedVelocityX = static_cast<float>(finalX);
    out.predictedVelocityY = static_cast<float>(finalY);
    out.headingError = static_cast<float>(Wrap(desired - heading));
    out.headingChange = speed > 0.000001 ? static_cast<float>(Wrap(heading - velocityYaw)) : 0;
    out.mode = state.mode;
    out.valid = std::isfinite(out.predictedVelocityX) && std::isfinite(out.predictedVelocityY) &&
        std::isfinite(out.accelerationStep);
    return out;
}
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
    if (!Valid(in, p)) { ResetAirSteering(state); return {}; }
    const double camera = Wrap(in.viewYaw);
    const double desired = Wrap(camera + std::atan2(static_cast<double>(in.left), static_cast<double>(in.forward)) / radians);
    const auto out = Slice(state, in.velocityX, in.velocityY, camera, desired, p, p.interval, (in.sequence & 1u) == 0);
    if (!out.valid) { ResetAirSteering(state); return {}; }
#ifdef CS2_STEERING_BENCHMARK
    candidateEvaluations = 1;
#endif
    return out;
}
SteeringCommand PlanAirSteeringCommand(SteeringState& state, const StrafeInput& in,
                                      const AirPhysics& p) noexcept {
#ifdef CS2_STEERING_BENCHMARK
    commandEvaluations = 0;
#endif
    if (!Valid(in, p)) { ResetAirSteering(state); return {}; }
    SteeringCommand command{};
    const double camera = Wrap(in.viewYaw);
    const double desired = Wrap(camera + std::atan2(static_cast<double>(in.left), static_cast<double>(in.forward)) / radians);
    const double duration = static_cast<double>(p.interval) / AirSteeringSliceCount;
    const double originalHeading = std::atan2(static_cast<double>(in.velocityY), static_cast<double>(in.velocityX)) / radians;
    double x = in.velocityX, y = in.velocityY, impulse = 0;
    for (std::size_t i = 0; i < AirSteeringSliceCount; ++i) {
        auto& output = command.slices[i];
        output = Slice(state, x, y, camera, desired, p, duration, ((in.sequence + i) & 1u) == 0);
        if (!output.valid) { ResetAirSteering(state); return {}; }
        x = output.predictedVelocityX; y = output.predictedVelocityY;
        impulse += output.accelerationStep;
    }
    command.count = AirSteeringSliceCount;
    command.valid = true;
    command.summary = command.slices.back();
    command.summary.accelerationStep = static_cast<float>(impulse);
    if (!std::isfinite(command.summary.accelerationStep)) { ResetAirSteering(state); return {}; }
    command.summary.headingChange = std::hypot(static_cast<double>(in.velocityX), static_cast<double>(in.velocityY)) > 0.000001 ?
        static_cast<float>(Wrap(std::atan2(y, x) / radians - originalHeading)) : 0;
#ifdef CS2_STEERING_BENCHMARK
    commandEvaluations = static_cast<unsigned int>(command.count);
#endif
    return command;
}
