#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
constexpr float pi = std::numbers::pi_v<float>;
constexpr float radians = pi / 180.0f;
float Wrap(float angle) noexcept { return std::remainder(angle, 2 * pi); }
bool Finite(float value) noexcept { return std::isfinite(value); }
#ifdef CS2_STEERING_BENCHMARK
thread_local unsigned int candidateEvaluations = 0;
#endif

struct Candidate {
    float angle = 0, amount = 1, step = 0, x = 0, y = 0, speed = 0, turn = 0;
};
Candidate Simulate(const StrafeInput& in, const AirPhysics& p, float angle, float amount,
                   float velocityYaw, int side) noexcept {
#ifdef CS2_STEERING_BENCHMARK
    ++candidateEvaluations;
#endif
    Candidate c{};
    c.angle = angle; c.amount = std::clamp(amount, 0.0f, 1.0f);
    const float dx = std::cos(angle), dy = std::sin(angle);
    const float wish = p.wishSpeed * c.amount;
    const float cap = std::min(p.wishSpeedCap, wish);
    const float available = p.acceleration * wish * p.surfaceFriction * p.interval;
    c.step = std::min(available, std::max(0.0f, cap - in.velocityX * dx - in.velocityY * dy));
    c.x = in.velocityX + c.step * dx; c.y = in.velocityY + c.step * dy;
    c.speed = std::hypot(c.x, c.y);
    c.turn = side * Wrap(std::atan2(c.y, c.x) - velocityYaw);
    return c;
}

// Maximize signed heading change under the engine's projection cap, with a
// retained-speed floor and no extra speed growth once that floor is reached.
Candidate Turn(const StrafeInput& in, const AirPhysics& p, float velocityYaw,
               int side, float retained, float maximumTurn) noexcept {
    const float speed = std::hypot(in.velocityX, in.velocityY);
    const float available = p.acceleration * p.wishSpeed * p.surfaceFriction * p.interval;
    const float floor = retained <= speed + 0.01f ? retained : speed;
    // Excess entry speed widens the orbit. Spend at most half the available
    // impulse reducing that excess; preserve the remainder for turning.
    const float ceiling = speed > retained ? std::max(retained, speed - available * 0.5f) : retained;
    Candidate best = Simulate(in, p, velocityYaw, 0, velocityYaw, side);
    if (available <= 0 || speed <= 0 || maximumTurn <= 0) return best;
    auto consider = [&](float relative, float amount = 1.0f) {
        Candidate c = Simulate(in, p, velocityYaw + side * relative, amount, velocityYaw, side);
        if (c.speed + 0.001f < floor || c.speed > ceiling + 0.001f ||
            c.turn < -0.00001f || c.turn > maximumTurn + 0.000001f) return;
        if (c.turn > best.turn + 0.0000001f ||
            (std::abs(c.turn - best.turn) <= 0.0000001f && c.speed > best.speed)) best = c;
    };
    // Full-impulse heading extrema, speed boundaries, and the projection-cap
    // transition are analytical. In the cap-clipped branch heading increases
    // monotonically over its feasible half-circle, so its speed boundaries
    // suffice. This bounded set avoids scanning/refining angles per fragment.
    if (available > 0 && speed > 0) {
        const float constantTurn = std::min(maximumTurn, 2 * std::asin(std::clamp(
            std::min(available, 2 * std::min(p.wishSpeedCap, p.wishSpeed)) / (2 * speed), 0.0f, 1.0f)));
        const float constantImpulse = 2 * speed * std::sin(constantTurn / 2);
        // Reducing analog amount also reduces wish speed. Keep it high enough
        // that the engine's wish-speed cap does not clip the intended impulse.
        consider(pi / 2 + constantTurn / 2, std::max(constantImpulse / available,
                                                  constantImpulse / (2 * p.wishSpeed)));
        consider(std::acos(std::clamp(-available / speed, -1.0f, 1.0f)));
        consider(std::acos(std::clamp((floor * floor - speed * speed - available * available) /
                                     (2 * speed * available), -1.0f, 1.0f)));
        consider(std::acos(std::clamp((ceiling * ceiling - speed * speed - available * available) /
                                     (2 * speed * available), -1.0f, 1.0f)));
        consider(std::acos(std::clamp((std::min(p.wishSpeedCap, p.wishSpeed) - available) /
                                     speed, -1.0f, 1.0f)));
        // With a large impulse the engine projection cap clips acceleration.
        // projection=-cap gives a capped impulse of 2*cap and no speed loss.
        consider(std::acos(std::clamp(-std::min(p.wishSpeedCap, p.wishSpeed) / speed, -1.0f, 1.0f)));
        const double cap = std::min(p.wishSpeedCap, p.wishSpeed);
        for (float boundary : {floor, ceiling}) {
            const double squaredProjection = static_cast<double>(speed) * speed + cap * cap -
                                             static_cast<double>(boundary) * boundary;
            consider(std::acos(-static_cast<float>(std::sqrt(std::clamp(
                squaredProjection / (static_cast<double>(speed) * speed), 0.0, 1.0)))));
        }
        // While shedding excess speed, the constant-speed candidate above is
        // outside the ceiling. Include the finite-angle limit on both retained
        // speed boundaries using a smaller impulse when the cap permits it.
        if (floor != speed || ceiling != speed) {
            const float sine = std::sin(maximumTurn), cosine = std::cos(maximumTurn);
            for (float boundary : {floor, ceiling}) {
                const float dx = boundary * cosine - speed, dy = boundary * sine;
                const float length = std::hypot(dx, dy);
                if (length > 0 && length <= available) {
                    const float finalProjection = (boundary * boundary - speed * boundary * cosine) / length;
                    consider(std::atan2(dy, dx), std::max(length / available,
                                                        finalProjection / p.wishSpeed));
                }
            }
        }
    }
    return best;
}
}

#ifdef CS2_STEERING_BENCHMARK
// Benchmark-only observability; no counter or API is present in normal builds.
unsigned int SteeringCandidateEvaluationsForBenchmark() noexcept { return candidateEvaluations; }
#endif

void ResetAirSteering(SteeringState& state) noexcept { state = {}; }

SteeringOutput PlanAirSteering(SteeringState& state, const StrafeInput& in,
                              const AirPhysics& p) noexcept {
#ifdef CS2_STEERING_BENCHMARK
    candidateEvaluations = 0;
#endif
    SteeringOutput out{};
    if (!Finite(in.velocityX) || !Finite(in.velocityY) || !Finite(in.viewYaw) ||
        !Finite(in.forward) || !Finite(in.left) || !Finite(p.wishSpeed) ||
        !Finite(p.wishSpeedCap) || !Finite(p.acceleration) || !Finite(p.surfaceFriction) ||
        !Finite(p.interval) || p.wishSpeed <= 0 || p.wishSpeedCap <= 0 ||
        p.acceleration < 0 || p.surfaceFriction < 0 || p.interval <= 0 || p.interval > 0.1f) {
        ResetAirSteering(state); return out;
    }
    const float speed = std::hypot(in.velocityX, in.velocityY);
    const float available = p.acceleration * p.wishSpeed * p.surfaceFriction * p.interval;
    if (!Finite(speed) || speed > 100000 || !Finite(available)) {
        ResetAirSteering(state); return out;
    }
    const float camera = in.viewYaw * radians;
    const float desired = Wrap(camera + (in.forward < -0.01f ? pi : 0));
    const int side = in.left > 0.01f ? 1 : (in.left < -0.01f ? -1 : 0);
    const float velocityYaw = speed > 0.001f ? std::atan2(in.velocityY, in.velocityX) : desired;
    const float error = Wrap(desired - velocityYaw);
    Candidate chosen{};
    if (side) {
        if (state.mode != SteeringMode::Turning || state.turnSide != side)
            state.retainedSpeed = std::min(p.wishSpeed, std::max(speed, std::min(p.wishSpeedCap, p.wishSpeed)));
        state.mode = SteeringMode::Turning; state.turnSide = side;
        state.retainedSpeed = std::min(state.retainedSpeed, p.wishSpeed);
        if (speed < 0.001f)
            chosen = Simulate(in, p, camera + side * pi / 2, 1, velocityYaw, side);
        else
            chosen = Turn(in, p, velocityYaw, side, state.retainedSpeed, pi * 0.95f);
    } else {
        if (state.mode == SteeringMode::Turning || state.mode == SteeringMode::Idle)
            state.retainedSpeed = std::min(speed, p.wishSpeed);
        state.retainedSpeed = std::min(state.retainedSpeed, std::min(speed, p.wishSpeed));
        const int recoverySide = error > 0 ? 1 : (error < 0 ? -1 : (state.turnSide ? state.turnSide : 1));
        state.mode = SteeringMode::Recovering;
        chosen = Turn(in, p, velocityYaw, recoverySide, state.retainedSpeed, std::abs(error));
        // A small variable-amplitude correction lands exactly on the desired
        // heading. Unlike alternating full-strength strafes it cannot keep
        // oscillating sideways after the user releases A/D.
        const float target = std::min(speed, std::max(state.retainedSpeed, 0.0f));
        const float dx = target * std::cos(desired) - in.velocityX;
        const float dy = target * std::sin(desired) - in.velocityY;
        const float length = std::hypot(dx, dy);
        if (available > 0 && length > 0.00001f && length <= available + 0.00001f) {
            const float finalProjection = (target * std::cos(desired) * dx +
                                           target * std::sin(desired) * dy) / length;
            const Candidate exact = Simulate(in, p, std::atan2(dy, dx),
                                             std::max(length / available, finalProjection / p.wishSpeed),
                                             velocityYaw, recoverySide);
            if (std::hypot(exact.x - target * std::cos(desired), exact.y - target * std::sin(desired)) < 0.001f)
                chosen = exact;
        }
        if (speed < 0.001f || std::abs(error) < 0.00001f) {
            chosen = Simulate(in, p, desired, 1, velocityYaw, recoverySide);
            state.mode = SteeringMode::Aligned;
        } else if (chosen.speed > 0.001f &&
                   std::abs(Wrap(desired - std::atan2(chosen.y, chosen.x))) < 0.00001f) {
            state.mode = SteeringMode::Aligned;
        }
    }
    const float relative = Wrap(chosen.angle - camera);
    out.forward = chosen.amount * std::cos(relative);
    out.left = chosen.amount * std::sin(relative);
    out.wishYaw = Wrap(chosen.angle) / radians; out.accelerationStep = chosen.step;
    out.predictedVelocityX = chosen.x; out.predictedVelocityY = chosen.y;
    out.headingError = Wrap(desired - std::atan2(chosen.y, chosen.x)) / radians;
    out.headingChange = Wrap(std::atan2(chosen.y, chosen.x) - velocityYaw) / radians;
    out.mode = state.mode; out.valid = true;
    return out;
}
