#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double pi = std::numbers::pi;
constexpr double radians = pi / 180.0;
// Keep useful acceleration even while following a continuously changing
// heading. This is a speed-squared gain fraction, not an engine cvar or a
// guessed strafe angle. Large turns retain half the best gain for this slice.
constexpr double turnGainFraction = 0.5;
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

double HeadingChange(double projection, double speed, double available, double cap) noexcept {
    const double cosine = std::clamp(projection / speed, -1.0, 1.0);
    const double sine = std::sqrt(std::max(0.0, 1 - cosine * cosine));
    const double step = std::min(available, std::max(0.0, cap - projection));
    return std::atan2(step * sine, speed + step * cosine);
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
    } else if (speed <= 0.000001) {
        state.mode = SteeringMode::Accelerating;
        chosen = Simulate(in, p, desired, 1);
    } else {
        const int side = std::abs(error) > 0.000001 ? (error < 0 ? -1 : 1) :
                         (state.turnSide ? -state.turnSide : 1);
        state.turnSide = side;
        // Full-input maximum gain: a=min(A,C-q), gain=2*a*q+a*a.
        // Following a fixed heading alternates its two mirrors. No fixed
        // angle envelope or reduced analog amplitude suppresses acceleration.
        const double gainProjection = std::min(speed, std::max(0.0, cap - available));
        double projection = gainProjection;
        const double gainTurn = HeadingChange(gainProjection, speed, available, cap);
        state.mode = SteeringMode::Accelerating;
        if (std::abs(error) > gainTurn + 0.000001) {
            // The most backward projection satisfying our gain floor is
            // analytical in both branches. It turns almost as hard as the
            // former equal-speed impulse, but accelerates throughout circles.
            const double gainStep = std::min(available, std::max(0.0, cap - gainProjection));
            const double requiredGain = turnGainFraction * gainStep * (2 * gainProjection + gainStep);
            const double fullProjection = (requiredGain - available * available) / (2 * available);
            const double turnProjection = std::clamp(fullProjection <= cap - available ? fullProjection :
                -std::sqrt(std::max(0.0, cap * cap - requiredGain)), -speed, gainProjection);
            projection = turnProjection;
            state.mode = SteeringMode::Turning;
            if (std::abs(error) < HeadingChange(turnProjection, speed, available, cap)) {
                // Between gain and turn directions, heading is monotone.
                // Solve only this narrow interval, keeping full input and
                // the gain floor. Sixteen bounded scalar iterations avoid
                // an angle scan and land small corrections without a delay.
                double low = turnProjection, high = gainProjection;
                for (int iteration = 0; iteration < 16; ++iteration) {
                    const double middle = (low + high) / 2;
                    if (HeadingChange(middle, speed, available, cap) > std::abs(error)) low = middle;
                    else high = middle;
                }
                projection = (low + high) / 2;
            }
        }
        chosen = Simulate(in, p, velocityYaw + side *
            std::acos(std::clamp(projection / speed, -1.0, 1.0)), 1);
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
