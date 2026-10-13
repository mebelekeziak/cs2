#include "movement_steering.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <numbers>

namespace {
int failures = 0;
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
void Expect(bool result, const char* message) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", message); failures += !result;
}
struct Trajectory {
    SteeringState state{};
    float vx = 250, vy = 0, x = 0, y = 0, turn = 0, maxDistance = 0;
    bool valid = true, predictionMatches = true;
    std::uint32_t sequence = 0;
    SteeringOutput Step(float left, const AirPhysics& physics, float camera = 0, float forward = 0) {
        const auto out = PlanAirSteering(state, {vx, vy, camera, forward, left, ++sequence}, physics);
        valid &= out.valid && std::hypot(out.forward, out.left) <= 1.00001f;
        // Independent engine-style capped acceleration, derived from analog
        // command fields rather than the planner's predicted velocity or yaw.
        const float amount = std::hypot(out.forward, out.left);
        const float angle = camera * radians + std::atan2(out.left, out.forward);
        const float dx = std::cos(angle), dy = std::sin(angle);
        const float wish = physics.wishSpeed * amount;
        const float projection = vx * dx + vy * dy;
        const float limit = std::min(wish, physics.wishSpeedCap);
        const float added = std::min(physics.acceleration * wish * physics.surfaceFriction * physics.interval,
                                     std::max(0.0f, limit - projection));
        const float previousYaw = std::atan2(vy, vx);
        vx += added * dx; vy += added * dy;
        predictionMatches &= std::abs(vx - out.predictedVelocityX) < 0.002f &&
                             std::abs(vy - out.predictedVelocityY) < 0.002f;
        turn += std::remainder(std::atan2(vy, vx) - previousYaw, 2 * std::numbers::pi_v<float>);
        x += vx * physics.interval; y += vy * physics.interval;
        maxDistance = std::max(maxDistance, std::hypot(x, y));
        return out;
    }
};
}

int main() {
    const AirPhysics normal{250, 30, 12, 1, 1.0f / 64};
    Trajectory right{}, left{};
    for (int step = 0; step < 64; ++step) {
        right.Step(-1, normal); left.Step(1, normal);
    }
    Expect(right.valid && right.turn < -std::numbers::pi_v<float> / 2,
           "one second holding D continues turning beyond a fixed side heading");
    Expect(std::abs(left.vx - right.vx) < 0.01f && std::abs(left.vy + right.vy) < 0.01f &&
           std::abs(left.turn + right.turn) < 0.001f,
           "holding A mirrors D across repeated turns");
    for (int step = 0; step < 7 * 64; ++step) right.Step(-1, normal);
    Expect(std::abs(right.turn) > 8 * std::numbers::pi_v<float> && right.maxDistance < 60 &&
           std::abs(std::hypot(right.vx, right.vy) - 250) < 0.2f,
           "repeated circles remain bounded and retain entry speed");
    for (int step = 0; step < 128; ++step) right.Step(0, normal);
    Expect(std::abs(right.vy) < 0.003f && right.vx > 249 && right.state.mode == SteeringMode::Aligned,
           "releasing A/D actively recovers forward travel without residual sideways velocity");
    const float initialY = right.y;
    for (int step = 0; step < 128; ++step) right.Step(0, normal);
    Expect(std::abs(right.y - initialY) < 0.01f,
           "aligned recovery does not resume alternating lateral drift");
    Expect(right.predictionMatches && left.predictionMatches,
           "observed command simulation agrees with advertised next velocity");

    bool matrixValid = true, matrixRecovery = true, matrixCircles = true;
    for (float speed : {30.0f, 250.0f, 800.0f}) {
        for (float friction : {0.2f, 1.0f}) {
            for (float interval : {1.0f / 128, 1.0f / 64, 1.0f / 32, 0.004f}) {
                const AirPhysics p{250, 30, 12, friction, interval};
                Trajectory path{}; path.vx = speed;
                const int holdSteps = static_cast<int>(8.0f / interval);
                for (int step = 0; step < holdSteps; ++step) path.Step(-1, p);
                const float retained = std::min(speed, p.wishSpeed);
                const float orbitEnvelope = 3 * speed * speed /
                    (p.acceleration * p.wishSpeed * p.surfaceFriction) + 4 * speed * interval;
                const bool circle = path.turn < -std::numbers::pi_v<float> &&
                                 path.maxDistance < orbitEnvelope &&
                                 std::hypot(path.vx, path.vy) >= retained - 0.3f &&
                                 std::hypot(path.vx, path.vy) <= retained + 0.5f;
                if (!circle) std::printf("circle detail speed=%g friction=%g dt=%g turn=%g distance=%g bound=%g finalSpeed=%g\n",
                    speed, friction, interval, path.turn, path.maxDistance, orbitEnvelope, std::hypot(path.vx, path.vy));
                matrixCircles &= circle;
                for (int step = 0; step < static_cast<int>(4.0f / interval); ++step) path.Step(0, p);
                const bool recovery = std::abs(path.vy) < 0.01f && path.vx > retained - 0.5f;
                if (!recovery) std::printf("release detail speed=%g friction=%g dt=%g vx=%g vy=%g mode=%u\n",
                    speed, friction, interval, path.vx, path.vy, static_cast<unsigned>(path.state.mode));
                matrixRecovery &= recovery;
                matrixValid &= path.valid && path.predictionMatches;
            }
        }
    }
    Expect(matrixCircles, "varied speed, friction and fractional durations produce bounded continuous turns");
    Expect(matrixRecovery, "release converges with low/high speed, friction and 128 FPS durations");
    Expect(matrixValid, "all varied trajectories remain finite and match independent acceleration");

    Trajectory back{}; back.Step(-1, normal);
    for (int step = 0; step < 128; ++step) back.Step(0, normal, 90, -1);
    Expect(std::abs(back.vx) < 0.01f && back.vy < -249,
           "recovery follows rotated camera and explicit backward intent");
    SteeringState state{};
    auto still = PlanAirSteering(state, {0, 0, 0, 0, -1, 1}, normal);
    Expect(still.valid && still.left < 0, "stationary turn begins in physical input direction");
    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{};
    for (int i = 0; i < 64; ++i) constrained.Step(-1, weak);
    Expect(constrained.turn < 0 && constrained.turn > -0.02f,
           "tiny available acceleration is reported honestly rather than instant turning");
    weak.acceleration = 0;
    const auto frozen = PlanAirSteering(state, {250, 0, 0, 0, -1, 2}, weak);
    Expect(frozen.valid && frozen.predictedVelocityX == 250 && frozen.predictedVelocityY == 0,
           "zero acceleration cannot manufacture direction change");
    auto bad = normal; bad.interval = 0;
    Expect(!PlanAirSteering(state, {}, bad).valid && state.mode == SteeringMode::Idle,
           "invalid physics invalidates planner state");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Expect(!PlanAirSteering(state, {nan, 0, 0, 0, -1, 1}, normal).valid,
           "nonfinite observed velocity rejects steering synthesis");
    bad = normal; bad.acceleration = std::numeric_limits<float>::infinity();
    Expect(!PlanAirSteering(state, {}, bad).valid, "nonfinite acceleration rejects steering synthesis");
    ResetAirSteering(state);
    Expect(state.mode == SteeringMode::Idle && !state.turnSide && !state.retainedSpeed,
           "explicit lifecycle reset clears retained intent");
    return failures ? 1 : 0;
}
