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
float HeadingError(float vx, float vy, float camera, float forward, float left) {
    return std::remainder(std::atan2(vy, vx) - camera * radians - std::atan2(left, forward),
                          2 * std::numbers::pi_v<float>) / radians;
}
struct Trajectory {
    SteeringState state{};
    float vx = 0, vy = 0;
    bool valid = true, predictionMatches = true;
    std::uint32_t sequence = 0;
    SteeringOutput Step(float forward, float left, const AirPhysics& physics, float camera = 0) {
        const auto out = PlanAirSteering(state, {vx, vy, camera, forward, left, ++sequence}, physics);
        valid &= out.valid && std::hypot(out.forward, out.left) <= 1.00001f;
        // Independent engine-style capped acceleration, using analog command
        // fields instead of the planner's advertised next velocity/wish yaw.
        const double amount = std::hypot(out.forward, out.left);
        const double angle = camera * static_cast<double>(radians) + std::atan2(out.left, out.forward);
        const double dx = std::cos(angle), dy = std::sin(angle);
        const double wish = physics.wishSpeed * amount;
        const double projection = vx * dx + vy * dy;
        const double added = std::min(physics.acceleration * wish * physics.surfaceFriction * physics.interval,
                                      std::max(0.0, std::min<double>(wish, physics.wishSpeedCap) - projection));
        vx = static_cast<float>(vx + added * dx); vy = static_cast<float>(vy + added * dy);
        predictionMatches &= std::abs(vx - out.predictedVelocityX) < 0.004f &&
                             std::abs(vy - out.predictedVelocityY) < 0.004f;
        return out;
    }
    float Speed() const { return std::hypot(vx, vy); }
};
}

int main() {
    const AirPhysics normal{250, 30, 12, 1, 1.0f / 64};
    bool allGain = true, allHeadings = true, allPredictions = true;
    // Standstill and lost-speed starts for all eight directions, including
    // diagonals under a rotated camera. No direction has a special speed goal.
    for (float initial : {0.0f, 10.0f, 30.0f}) {
        for (const float camera : {0.0f, 73.0f, -180.0f}) {
            for (int forward = -1; forward <= 1; ++forward) {
                for (int left = -1; left <= 1; ++left) {
                    if (!forward && !left) continue;
                    Trajectory path{}; path.vx = initial;
                    for (int i = 0; i < 128; ++i) path.Step(static_cast<float>(forward), static_cast<float>(left), normal, camera);
                    allGain &= path.Speed() > 280;
                    allHeadings &= std::abs(HeadingError(path.vx, path.vy, camera,
                        static_cast<float>(forward), static_cast<float>(left))) <= 15.01f;
                    allPredictions &= path.valid && path.predictionMatches;
                }
            }
        }
    }
    Expect(allGain, "all eight WASD directions rebuild speed from standstill and below the air cap");
    Expect(allHeadings, "all eight directions follow camera-relative intent within the gain envelope");
    Expect(allPredictions, "independent analog simulation agrees with every low-speed predicted velocity");

    Trajectory sharp{}; sharp.vx = 250;
    bool sharpRetained = true;
    for (int i = 0; i < 8; ++i) {
        sharp.Step(0, -1, normal);
        sharpRetained &= sharp.Speed() >= 249.99f;
    }
    Expect(sharpRetained && std::abs(HeadingError(sharp.vx, sharp.vy, 0, 0, -1)) <= 15.01f,
           "ordinary supplied physics turn forward travel into D within eight commands without speed loss");

    Trajectory left{}, right{}; left.vx = right.vx = 250;
    for (int i = 0; i < 128; ++i) { left.Step(0, 1, normal); right.Step(0, -1, normal); }
    Expect(std::abs(HeadingError(left.vx, left.vy, 0, 0, 1)) <= 15.01f &&
           std::abs(HeadingError(right.vx, right.vy, 0, 0, -1)) <= 15.01f,
           "A and D aim at fixed side headings rather than continuous circles");
    Expect(std::abs(left.vx - right.vx) < 0.005f && std::abs(left.vy + right.vy) < 0.005f,
           "A and D produce mirrored velocity trajectories");
    Expect(left.Speed() > 380 && right.Speed() > 380,
           "side inputs continue accelerating after the requested turn");

    Trajectory forward{}; forward.vx = 250;
    for (int i = 0; i < 128; ++i) forward.Step(1, 0, normal);
    Expect(forward.Speed() > 410, "W builds speed above the initial speed and air cap");
    forward.vx *= 0.08f; forward.vy *= 0.08f;
    for (int i = 0; i < 128; ++i) forward.Step(1, 0, normal);
    Expect(forward.Speed() > 280 && std::abs(HeadingError(forward.vx, forward.vy, 0, 1, 0)) <= 15.01f,
           "W rebuilds observed lost velocity without a retained entry-speed ceiling");
    const float beforeTurn = forward.Speed();
    bool retained = true;
    for (int i = 0; i < 128; ++i) {
        forward.Step(-1, 0, normal);
        retained &= forward.Speed() >= beforeTurn - 0.02f;
    }
    Expect(retained && std::abs(HeadingError(forward.vx, forward.vy, 0, -1, 0)) <= 15.01f,
           "reversing WASD steers toward the new heading while retaining speed in collision-free air");
    for (int i = 0; i < 128; ++i) forward.Step(1, 1, normal, 95);
    Expect(std::abs(HeadingError(forward.vx, forward.vy, 95, 1, 1)) <= 15.01f,
           "diagonal intent immediately follows a changed camera heading");

    bool matrixValid = true, matrixGain = true, matrixDirection = true;
    for (float speed : {30.0f, 250.0f, 800.0f}) {
        for (float friction : {0.2f, 1.0f}) {
            for (float interval : {1.0f / 128, 1.0f / 64, 1.0f / 32, 0.004f}) {
                const AirPhysics p{250, 30, 12, friction, interval};
                Trajectory path{}; path.vx = speed;
                for (int step = 0; step < static_cast<int>(8 / interval); ++step) path.Step(0, -1, p);
                matrixGain &= path.Speed() > speed + 10;
                matrixDirection &= std::abs(HeadingError(path.vx, path.vy, 0, 0, -1)) <= 15.01f;
                matrixValid &= path.valid && path.predictionMatches;
            }
        }
    }
    Expect(matrixGain, "normal and weak acceleration grow speed without a retained-speed ceiling");
    Expect(matrixDirection, "camera-relative headings converge across speed, friction and fractional duration");
    Expect(matrixValid, "varied trajectories remain normalized and match independent acceleration");

    bool boundaryValid = true, boundaryGain = true, boundaryEnvelope = true;
    for (const AirPhysics p : {AirPhysics{250, 3, 100, 1, 0.1f},
                              AirPhysics{250, 30, 100, 1, 0.1f},
                              AirPhysics{250, 250, 100, 1, 0.1f},
                              AirPhysics{250, 30, 12, 1, 0.0001f},
                              AirPhysics{20, 30, 12, 1, 1.0f / 64}}) {
        for (float speed : {0.0f, 1.0f, 30.0f, 250.0f, 800.0f}) {
            Trajectory path{}; path.vx = speed;
            for (int i = 0; i < 64; ++i) {
                const float previous = path.Speed();
                path.Step(1, 0, p);
                boundaryGain &= path.Speed() >= previous - 0.004f;
                boundaryEnvelope &= std::abs(HeadingError(path.vx, path.vy, 0, 1, 0)) <= 15.01f;
                boundaryValid &= path.valid && path.predictionMatches;
            }
        }
    }
    Expect(boundaryValid, "large impulses, tiny caps and fragments preserve independent normalized physics");
    Expect(boundaryGain, "extreme valid physics never impose an obsolete speed ceiling");
    Expect(boundaryEnvelope, "bounded gain impulses respect the requested heading under extreme physics");

    SteeringState state{};
    auto noInput = PlanAirSteering(state, {250, 80, 0, 0, 0, 1}, normal);
    Expect(!noInput.valid && state.mode == SteeringMode::Idle,
           "no net WASD input leaves ordinary release behavior alone without autostop");
    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{}; constrained.vx = 250;
    for (int i = 0; i < 64; ++i) constrained.Step(0, 1, weak);
    Expect(constrained.vy > 0 && constrained.vy < 3,
           "weak air acceleration cannot manufacture an instant requested turn");
    weak.acceleration = 0;
    const auto frozen = PlanAirSteering(state, {250, 0, 0, 0, -1, 2}, weak);
    Expect(frozen.valid && frozen.predictedVelocityX == 250 && frozen.predictedVelocityY == 0,
           "zero acceleration cannot manufacture direction or speed");
    weak = normal; weak.surfaceFriction = 0;
    Expect(PlanAirSteering(state, {250, 0, 0, 1, 0, 3}, weak).predictedVelocityX == 250,
           "zero surface friction leaves observed velocity unchanged");
    auto bad = normal; bad.interval = 0;
    Expect(!PlanAirSteering(state, {250, 0, 0, 1, 0, 4}, bad).valid && state.mode == SteeringMode::Idle,
           "invalid physics invalidates planner state");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Expect(!PlanAirSteering(state, {nan, 0, 0, 1, 0, 5}, normal).valid,
           "nonfinite observed velocity rejects steering synthesis");
    bad = normal; bad.acceleration = std::numeric_limits<float>::infinity();
    Expect(!PlanAirSteering(state, {250, 0, 0, 1, 0, 6}, bad).valid,
           "nonfinite acceleration rejects steering synthesis");
    ResetAirSteering(state);
    Expect(state.mode == SteeringMode::Idle && !state.turnSide,
           "explicit lifecycle reset clears retained direction");
    return failures ? 1 : 0;
}
