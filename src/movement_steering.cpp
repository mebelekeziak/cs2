#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double pi = std::numbers::pi;
constexpr double radians = pi / 180.0;
// Air acceleration above its projection cap requires a lateral component.
// Bound its transient deviation from the requested travel direction.
constexpr double headingEnvelope = 15 * radians;
double Wrap(double angle) noexcept { return std::remainder(angle, 2 * pi); }
#ifdef CS2_STEERING_BENCHMARK
thread_local unsigned int candidateEvaluations = 0;
#endif
struct Candidate { double angle = 0, amount = 0, step = 0, x = 0, y = 0; };

Candidate Simulate(const StrafeInput& in, const AirPhysics& p,
                   double angle, double amount) noexcept {
#ifdef CS2_STEERING_BENCHMARK
    ++candidateEvaluations;
#endif
    Candidate c{};
    c.angle = angle; c.amount = std::clamp(amount, 0.0, 1.0);
    const double dx = std::cos(angle), dy = std::sin(angle);
    const double wish = p.wishSpeed * c.amount;
    const double available = p.acceleration * wish * p.surfaceFriction * p.interval;
    c.step = std::min(available, std::max(0.0,
        std::min<double>(p.wishSpeedCap, wish) - in.velocityX * dx - in.velocityY * dy));
    c.x = in.velocityX + c.step * dx;
    c.y = in.velocityY + c.step * dy;
    return c;
}

Candidate Turn(const StrafeInput& in, const AirPhysics& p, double speed,
               double velocityYaw, double error, double available, double cap) noexcept {
    // The difference between two equal-length velocity vectors supplies the
    // exact impulse for a turn. Its final projection is half its length, so
    // both the full-impulse and capped branches have an analytical turn bound.
    const double angle = std::min(std::abs(error), 2 * std::asin(std::clamp(
        std::min(available, 2 * cap) / (2 * speed), 0.0, 1.0)));
    const double step = 2 * speed * std::sin(angle / 2);
    const double amount = std::max(step / available, step / (2 * p.wishSpeed));
    const int side = error < 0 ? -1 : 1;
    return Simulate(in, p, velocityYaw + side * (pi / 2 + angle / 2), amount);
}
}

#ifdef CS2_STEERING_BENCHMARK
unsigned int SteeringCandidateEvaluationsForBenchmark() noexcept { return candidateEvaluations; }
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
    const double speed = std::hypot(in.velocityX, in.velocityY);
    const double available = static_cast<double>(p.acceleration) * p.wishSpeed *
                             p.surfaceFriction * p.interval;
    if (!std::isfinite(speed) || speed > 100000 || !std::isfinite(available)) {
        ResetAirSteering(state); return out;
    }
    const bool intent = std::abs(in.forward) > 0.01f || std::abs(in.left) > 0.01f;
    if (!intent) {
        // No autostop or retained release behavior: do not synthesize movement.
        ResetAirSteering(state); return out;
    }
    const double camera = Wrap(in.viewYaw * radians);
    const double desired = Wrap(camera + std::atan2(in.left, in.forward));
    const double velocityYaw = speed > 0.000001 ? std::atan2(in.velocityY, in.velocityX) : desired;
    const double error = Wrap(desired - velocityYaw);
    const double cap = std::min(p.wishSpeedCap, p.wishSpeed);
    Candidate chosen{};
    if (available <= 0) {
        state.mode = SteeringMode::Turning;
        chosen = Simulate(in, p, desired, 1);
    } else if (speed > 0.000001 && std::abs(error) > headingEnvelope + 0.000001) {
        state.mode = SteeringMode::Turning;
        state.turnSide = error < 0 ? -1 : 1;
        chosen = Turn(in, p, speed, velocityYaw, error, available, cap);
    } else {
        state.mode = SteeringMode::Accelerating;
        // Maximal speed gain uses projection max(0, cap-available), limited
        // by the observed speed. Choose its mirror toward physical intent.
        // This continues building speed above the cap, including after a
        // collision or weapon-speed change; no entry-speed ceiling is kept.
        const double projection = std::min(speed, std::max(0.0, cap - available));
        const double relative = speed > 0.000001 ?
            std::acos(std::clamp(projection / speed, 0.0, 1.0)) : 0;
        const int side = std::abs(error) > 0.000001 ? (error < 0 ? -1 : 1) :
                         (state.turnSide ? state.turnSide : 1);
        state.turnSide = side;
        chosen = Simulate(in, p, velocityYaw + side * relative, 1);
        const double nextError = Wrap(std::atan2(chosen.y, chosen.x) - desired);
        if (speed > 0.000001 && std::abs(nextError) > headingEnvelope + 0.000001) {
            // Intersect the impulse ray with the near edge of the permitted
            // heading envelope. Scaling wish speed changes both acceleration
            // and the cap; satisfy both instead of scaling acceleration only.
            const double targetTurn = side * Wrap(desired + side * headingEnvelope - velocityYaw);
            const double denominator = std::sin(relative - targetTurn);
            const double step = denominator > 0 ?
                std::clamp(speed * std::sin(targetTurn) / denominator, 0.0, chosen.step) : 0;
            chosen = Simulate(in, p, chosen.angle,
                std::max(step / available, (projection + step) / p.wishSpeed));
        }
    }
    const double relative = Wrap(chosen.angle - camera);
    out.forward = static_cast<float>(chosen.amount * std::cos(relative));
    out.left = static_cast<float>(chosen.amount * std::sin(relative));
    out.wishYaw = static_cast<float>(Wrap(chosen.angle) / radians);
    out.accelerationStep = static_cast<float>(chosen.step);
    out.predictedVelocityX = static_cast<float>(chosen.x);
    out.predictedVelocityY = static_cast<float>(chosen.y);
    out.headingError = static_cast<float>(Wrap(desired - std::atan2(chosen.y, chosen.x)) / radians);
    out.headingChange = static_cast<float>(Wrap(std::atan2(chosen.y, chosen.x) - velocityYaw) / radians);
    out.mode = state.mode; out.valid = true;
    return out;
}
