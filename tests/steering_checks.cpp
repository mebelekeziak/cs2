#include "movement_steering.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

namespace {
int failures = 0;
constexpr double pi = std::numbers::pi;
constexpr double radians = pi / 180.0;
void Expect(bool result, const char* message) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", message); failures += !result;
}
double Wrap(double angle) { return std::remainder(angle, 2 * pi); }
double HeadingError(float vx, float vy, float camera, float forward, float left) {
    return Wrap(std::atan2(vy, vx) - camera * radians - std::atan2(left, forward)) / radians;
}
struct Gain { double squared = 0, turn = 0; };
Gain OptimalGain(double speed, const AirPhysics& p) {
    const double impulse = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
    const double cap = std::min(p.wishSpeedCap, p.wishSpeed);
    const double projection = std::min(speed, std::max(0.0, cap - impulse));
    const double added = std::min(impulse, std::max(0.0, cap - projection));
    const double lateral = speed > 0 ? std::sqrt(std::max(0.0, 1 - projection * projection / (speed * speed))) : 0;
    return {2 * projection * added + added * added,
        speed > 0 ? std::atan2(added * lateral, speed + added * projection / speed) / radians : 180};
}
struct Trajectory {
    SteeringState state{};
    float vx = 0, vy = 0;
    bool valid = true, fullAnalog = true, predictionMatches = true, usefulGain = true;
    std::uint32_t sequence = 0;
    SteeringOutput Step(float forward, float left, const AirPhysics& physics, float camera = 0) {
        const double previousSquared = static_cast<double>(vx) * vx + static_cast<double>(vy) * vy;
        const auto optimum = OptimalGain(std::sqrt(previousSquared), physics);
        const auto out = PlanAirSteering(state, {vx, vy, camera, forward, left, ++sequence}, physics);
        const double amount = std::hypot(out.forward, out.left);
        valid &= out.valid && std::isfinite(amount) && amount <= 1.00001;
        fullAnalog &= std::abs(amount - 1) < 0.000003;
        // Independent engine-style capped acceleration, using the analog command
        // fields instead of the advertised next velocity or wish yaw.
        const double angle = camera * radians + std::atan2(out.left, out.forward);
        const double dx = std::cos(angle), dy = std::sin(angle);
        const double wish = physics.wishSpeed * amount;
        const double projection = vx * dx + vy * dy;
        const double added = std::min(physics.acceleration * wish * physics.surfaceFriction * physics.interval,
                                      std::max(0.0, std::min<double>(wish, physics.wishSpeedCap) - projection));
        vx = static_cast<float>(vx + added * dx); vy = static_cast<float>(vy + added * dy);
        predictionMatches &= std::abs(vx - out.predictedVelocityX) < 0.004f &&
                             std::abs(vy - out.predictedVelocityY) < 0.004f &&
                             std::abs(added - out.accelerationStep) < 0.004;
        const double squaredGain = static_cast<double>(vx) * vx + static_cast<double>(vy) * vy - previousSquared;
        // Float command/velocity roundoff dominates extremely small fragments.
        usefulGain &= squaredGain + 0.12 >= 0.499 * optimum.squared;
        return out;
    }
    float Speed() const { return std::hypot(vx, vy); }
};
// Numerical oracle for a large turn: search all full-unit wish projections,
// retaining commands with at least half the globally optimal squared-speed gain.
double StrongestUsefulTurn(double speed, const AirPhysics& p) {
    const double impulse = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
    const double cap = std::min(p.wishSpeedCap, p.wishSpeed);
    const double minimumGain = 0.5 * OptimalGain(speed, p).squared;
    double strongest = 0;
    for (int sample = 0; sample <= 32768; ++sample) {
        const double projection = speed * (2.0 * sample / 32768 - 1);
        const double added = std::min(impulse, std::max(0.0, cap - projection));
        if (2 * projection * added + added * added + 1e-8 < minimumGain) continue;
        const double x = speed + added * projection / speed;
        const double y = added * std::sqrt(std::max(0.0, 1 - projection * projection / (speed * speed)));
        strongest = std::max(strongest, std::atan2(y, x) / radians);
    }
    return strongest;
}
}

int main() {
    const AirPhysics normal{250, 30, 12, 1, 1.0f / 64};
    constexpr std::array<std::array<float, 2>, 8> directions{{
        {{1, 0}}, {{1, 1}}, {{0, 1}}, {{-1, 1}},
        {{-1, 0}}, {{-1, -1}}, {{0, -1}}, {{1, -1}}
    }};
    bool allGain = true, allHeadings = true, allPredictions = true, allFull = true;
    for (float initial : {0.0f, 10.0f, 30.0f}) {
        for (float camera : {0.0f, 73.0f, -180.0f}) {
            for (const auto& direction : directions) {
                Trajectory path{}; path.vx = initial;
                for (int i = 0; i < 128; ++i) path.Step(direction[0], direction[1], normal, camera);
                allGain &= path.Speed() > 330 && path.usefulGain;
                allHeadings &= std::abs(HeadingError(path.vx, path.vy, camera, direction[0], direction[1])) <=
                               2 * OptimalGain(path.Speed(), normal).turn + 0.08;
                allPredictions &= path.valid && path.predictionMatches;
                allFull &= path.fullAnalog;
            }
        }
    }
    Expect(allGain, "all eight WASD directions recover low speed while preserving useful acceleration every command");
    Expect(allHeadings, "low-speed directions converge within a physics-scaled gain step");
    Expect(allPredictions && allFull, "low-speed full analog commands agree with independent capped acceleration");

    bool strongTurn = true, strongGain = true, strongFull = true;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 2, 1.0f / 64},
                              AirPhysics{250, 30, 12, 0.2f, 0.004f},
                              AirPhysics{250, 3, 100, 1, 0.1f}}) {
        for (float speed : {250.0f, 800.0f}) {
            for (float side : {-1.0f, 1.0f}) {
                Trajectory path{}; path.vx = speed;
                const auto out = path.Step(0, side, p);
                const double strongest = StrongestUsefulTurn(speed, p);
                const double natural = OptimalGain(speed, p).turn;
                const bool nearStrongest = side * out.headingChange >= strongest - std::max(0.00005, strongest * 0.001);
                // Weak impulses already point nearly sideways at maximum gain;
                // require extra snap only when the supplied physics permits it.
                const bool beatsGainTurn = strongest > 1.05 * natural ?
                    side * out.headingChange > 1.05 * natural : side * out.headingChange >= natural - 0.00005;
                if (!nearStrongest || !beatsGainTurn)
                    std::printf("turn_oracle speed=%.0f side=%.0f friction=%.3f interval=%.6f cap=%.0f turn=%.6f strongest=%.6f natural=%.6f\n",
                                speed, side, p.surfaceFriction, p.interval, p.wishSpeedCap,
                                out.headingChange, strongest, OptimalGain(speed, p).turn);
                strongTurn &= nearStrongest && beatsGainTurn;
                strongGain &= path.usefulGain;
                strongFull &= path.valid && path.fullAnalog && path.predictionMatches;
                if (speed == 250 && side == 1 && p.surfaceFriction == 1 && p.interval == normal.interval) {
                    const double gain = static_cast<double>(path.vx) * path.vx + static_cast<double>(path.vy) * path.vy - speed * speed;
                    std::printf("strong_turn_degrees=%.6f squared_speed_gain=%.6f optimum_squared_gain=%.6f\n",
                                out.headingChange, gain, OptimalGain(speed, p).squared);
                }
            }
        }
    }
    Expect(strongTurn, "large turns approach the strongest half-gain command, including clipped high-friction impulses");
    Expect(strongGain && strongFull, "aggressive turns preserve half the available squared-speed gain at full analog input");

    bool refinement = true;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 2, 1.0f / 64},
                              AirPhysics{250, 30, 12, 0.2f, 0.004f}}) {
        for (float side : {-1.0f, 1.0f}) {
            const double target = side * (OptimalGain(250, p).turn + StrongestUsefulTurn(250, p)) / 2;
            Trajectory path{}; path.vx = 250;
            const auto out = path.Step(1, 0, p, static_cast<float>(target));
            refinement &= std::abs(out.headingError) < 0.001 && path.valid && path.fullAnalog && path.usefulGain && path.predictionMatches;
        }
    }
    Expect(refinement, "intermediate turns land on the requested heading without reducing analog input or useful gain");

    Trajectory left{}, right{}; left.vx = right.vx = 250;
    for (int i = 0; i < 128; ++i) { left.Step(0, 1, normal); right.Step(0, -1, normal); }
    Expect(std::abs(HeadingError(left.vx, left.vy, 0, 0, 1)) <= 2 * OptimalGain(left.Speed(), normal).turn + 0.08 &&
           std::abs(HeadingError(right.vx, right.vy, 0, 0, -1)) <= 2 * OptimalGain(right.Speed(), normal).turn + 0.08,
           "fixed side intent settles near its heading while continuing to gain speed");
    Expect(std::abs(left.vx - right.vx) < 0.01f && std::abs(left.vy + right.vy) < 0.01f,
           "opposite side intents produce mirrored velocity trajectories");
    Expect(left.Speed() > 400 && right.Speed() > 400 && left.usefulGain && right.usefulGain,
           "side steering gains useful speed during and after the turn");

    Trajectory forward{}; forward.vx = 250;
    bool maximumGain = true, closeHeading = true;
    for (int i = 0; i < 128; ++i) {
        const double previous = static_cast<double>(forward.vx) * forward.vx + static_cast<double>(forward.vy) * forward.vy;
        const auto optimum = OptimalGain(forward.Speed(), normal);
        forward.Step(1, 0, normal);
        const double gain = static_cast<double>(forward.vx) * forward.vx + static_cast<double>(forward.vy) * forward.vy - previous;
        maximumGain &= std::abs(gain - optimum.squared) < 0.15;
        closeHeading &= std::abs(HeadingError(forward.vx, forward.vy, 0, 1, 0)) <= optimum.turn + 0.08;
    }
    Expect(maximumGain && forward.Speed() > 410, "constant heading uses maximum available gain without a retained speed ceiling");
    Expect(closeHeading, "constant heading error shrinks with the natural gain impulse as speed rises");
    forward.vx *= 0.08f; forward.vy *= 0.08f;
    for (int i = 0; i < 128; ++i) forward.Step(1, 0, normal);
    Expect(forward.Speed() > 330 && forward.usefulGain && forward.fullAnalog,
           "observed lost velocity immediately restores full-gain speed recovery");

    bool allSwitches = true, immediateSide = true;
    Trajectory switching{}; switching.vx = 250;
    for (const auto& direction : directions) {
        const double previousError = -HeadingError(switching.vx, switching.vy, 0, direction[0], direction[1]);
        const auto out = switching.Step(direction[0], direction[1], normal);
        if (std::abs(previousError) > OptimalGain(switching.Speed(), normal).turn + 0.1 && std::abs(previousError) < 179.9)
            immediateSide &= previousError * out.headingChange > 0;
        for (int i = 0; i < 31; ++i) switching.Step(direction[0], direction[1], normal);
        allSwitches &= std::abs(HeadingError(switching.vx, switching.vy, 0, direction[0], direction[1])) <=
                      2 * OptimalGain(switching.Speed(), normal).turn + 0.08;
    }
    Expect(allSwitches && immediateSide && switching.usefulGain && switching.fullAnalog,
           "all eight intent changes steer immediately toward the new direction with useful gain");
    Trajectory reversing{}; reversing.vx = 250;
    const auto towardLeft = reversing.Step(0, 1, normal);
    const auto towardRight = reversing.Step(0, -1, normal);
    Expect(towardLeft.headingChange > 0 && towardRight.headingChange < 0 && reversing.state.turnSide == -1,
           "reversing A to D changes the steering side on the next command");

    bool circles = true, circleGain = true, circleAnalog = true;
    for (float side : {-1.0f, 1.0f}) {
        Trajectory circle{}; circle.vx = circle.vy = 250 / std::sqrt(2.0f);
        double greatestLag = 0;
        for (int i = 1; i <= 384; ++i) {
            const float camera = static_cast<float>(std::remainder(side * i * 90.0 / 64, 360.0));
            circle.Step(1, 1, normal, camera);
            const double lag = std::abs(HeadingError(circle.vx, circle.vy, camera, 1, 1));
            greatestLag = std::max(greatestLag, lag);
            const double natural = OptimalGain(circle.Speed(), normal).turn;
            circles &= lag <= 2 * natural + 90.0 / 64 + 0.1;
            if (i == 128) std::printf("circle_side=%.0f commands=128 start_speed=250 final_speed=%.6f maximum_heading_lag_degrees=%.6f\n",
                                     side, circle.Speed(), greatestLag);
        }
        circleGain &= circle.usefulGain && circle.Speed() > 450;
        circleAnalog &= circle.valid && circle.fullAnalog && circle.predictionMatches;
    }
    Expect(circles, "continuously rotating diagonal intent follows both circle directions within two gain steps");
    Expect(circleGain && circleAnalog, "circling retains useful speed gain with independent full analog physics");

    SteeringState wrapStateA{}, wrapStateB{};
    const auto wrapA = PlanAirSteering(wrapStateA, {250, 80, 179.9f, 1, 1, 1}, normal);
    const auto wrapB = PlanAirSteering(wrapStateB, {250, 80, -180.1f, 1, 1, 1}, normal);
    Expect(wrapA.valid && wrapB.valid && std::abs(wrapA.predictedVelocityX - wrapB.predictedVelocityX) < 0.001f &&
           std::abs(wrapA.predictedVelocityY - wrapB.predictedVelocityY) < 0.001f &&
           std::abs(wrapA.forward - wrapB.forward) < 0.00001f && std::abs(wrapA.left - wrapB.left) < 0.00001f,
           "equivalent camera headings across the wrap boundary produce equivalent steering");

    bool matrixValid = true, matrixGain = true, matrixDirection = true;
    for (float speed : {30.0f, 250.0f, 800.0f}) {
        for (float friction : {0.2f, 1.0f, 2.0f}) {
            for (float interval : {1.0f / 128, 1.0f / 64, 1.0f / 32, 0.004f}) {
                const AirPhysics p{250, 30, 12, friction, interval};
                Trajectory path{}; path.vx = speed;
                for (int step = 0; step < static_cast<int>(8 / interval); ++step) path.Step(0, -1, p);
                matrixGain &= path.Speed() > speed + 10 && path.usefulGain;
                matrixDirection &= std::abs(HeadingError(path.vx, path.vy, 0, 0, -1)) <=
                                   2 * OptimalGain(path.Speed(), p).turn + 0.08;
                matrixValid &= path.valid && path.fullAnalog && path.predictionMatches;
            }
        }
    }
    Expect(matrixGain, "normal, weak and high-friction acceleration preserve useful speed gain");
    Expect(matrixDirection, "camera-relative headings converge across speed, friction and fractional duration");
    Expect(matrixValid, "varied trajectories use full analog and match independent acceleration");

    bool boundaryValid = true, boundaryGain = true, boundaryHeading = true;
    for (const AirPhysics p : {AirPhysics{250, 3, 100, 1, 0.1f},
                              AirPhysics{250, 30, 100, 1, 0.1f},
                              AirPhysics{250, 250, 100, 1, 0.1f},
                              AirPhysics{250, 30, 12, 1, 0.0001f},
                              AirPhysics{20, 30, 12, 1, 1.0f / 64}}) {
        for (float speed : {0.0f, 1.0f, 30.0f, 250.0f, 800.0f}) {
            Trajectory path{}; path.vx = speed;
            for (int i = 0; i < 64; ++i) {
                const auto optimum = OptimalGain(path.Speed(), p);
                path.Step(1, 0, p);
                boundaryHeading &= std::abs(HeadingError(path.vx, path.vy, 0, 1, 0)) <= 2 * optimum.turn + 0.08;
            }
            boundaryGain &= path.usefulGain;
            boundaryValid &= path.valid && path.fullAnalog && path.predictionMatches;
        }
    }
    Expect(boundaryValid, "large impulses, tiny caps and fragments preserve independent full analog physics");
    Expect(boundaryGain && boundaryHeading, "extreme physics retains useful gain within its natural heading step");

    SteeringState state{};
    PlanAirSteering(state, {250, 80, 0, 0, 1, 1}, normal);
    auto noInput = PlanAirSteering(state, {250, 80, 0, 0, 0, 1}, normal);
    Expect(!noInput.valid && state.mode == SteeringMode::Idle && !state.turnSide,
           "no net WASD input leaves ordinary release behavior alone without autostop");
    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{}; constrained.vx = 250;
    for (int i = 0; i < 64; ++i) constrained.Step(0, 1, weak);
    Expect(constrained.vy > 0 && constrained.vy < 3 && constrained.usefulGain,
           "weak air acceleration advances a requested turn within its physical limit");
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
