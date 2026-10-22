#include "movement_steering.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

namespace {
int failures = 0;
constexpr double pi = std::numbers::pi, radians = pi / 180.0;
void Expect(bool result, const char* message) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", message); failures += !result;
}
double Wrap(double angle) { return std::remainder(angle, 2 * pi); }
bool Near(double a, double b, double tolerance = 0.00001) { return std::abs(a - b) <= tolerance; }
bool Axes(const SteeringOutput& o, double forward, double left) {
    return o.valid && Near(o.forward, forward) && Near(o.left, left);
}
bool Equal(const SteeringOutput& a, const SteeringOutput& b) {
    return a.valid == b.valid && a.forward == b.forward && a.left == b.left && a.wishYaw == b.wishYaw &&
        a.accelerationStep == b.accelerationStep && a.predictedVelocityX == b.predictedVelocityX &&
        a.predictedVelocityY == b.predictedVelocityY && a.headingError == b.headingError &&
        a.headingChange == b.headingChange && a.mode == b.mode;
}
struct Applied { float x = 0, y = 0; double impulse = 0; };
Applied Apply(float vx, float vy, float camera, const SteeringOutput& o, const AirPhysics& p) {
    // Independent integration from emitted analog axes over one real command.
    // Neither wishYaw nor predicted velocity participates in this simulation.
    const double amount = std::hypot(static_cast<double>(o.forward), static_cast<double>(o.left));
    const double cosine = std::cos(camera * radians), sine = std::sin(camera * radians);
    const double dx = (o.forward * cosine - o.left * sine) / amount;
    const double dy = (o.forward * sine + o.left * cosine) / amount;
    const double wish = p.wishSpeed * amount;
    const double added = std::min(p.acceleration * wish * p.surfaceFriction * p.interval,
        std::max(0.0, std::min<double>(wish, p.wishSpeedCap) - vx * dx - vy * dy));
    return {static_cast<float>(vx + added * dx), static_cast<float>(vy + added * dy), added};
}
struct Trajectory {
    SteeringState state{};
    float vx = 0, vy = 0;
    bool consistent = true;
    std::uint32_t sequence = 0;
    SteeringCommand Step(float forward, float left, const AirPhysics& p, float camera = 0) {
        const auto command = PlanAirSteeringCommand(state, {vx, vy, camera, forward, left, ++sequence}, p);
        consistent &= command.valid && command.count == 1;
        if (!command.valid || command.count != 1) return command;
        const auto& o = command.slices[0];
        consistent &= o.valid && Near(std::hypot(o.forward, o.left), 1, 0.000003) && Equal(command.summary, o);
        const auto actual = Apply(vx, vy, camera, o, p);
        const double budget = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
        consistent &= Near(actual.x, o.predictedVelocityX, 0.004) && Near(actual.y, o.predictedVelocityY, 0.004) &&
            Near(actual.impulse, o.accelerationStep, 0.004) && actual.impulse >= 0 && actual.impulse <= budget + 0.004;
        vx = actual.x; vy = actual.y;
        return command;
    }
    double Speed() const { return std::hypot(vx, vy); }
    double Error(float camera, float forward, float left) const {
        return Wrap(camera * radians + std::atan2(left, forward) - std::atan2(vy, vx)) / radians;
    }
};
constexpr std::array<std::array<float, 2>, 8> directions{{
    {{1, 0}}, {{1, 1}}, {{0, 1}}, {{-1, 1}}, {{-1, 0}}, {{-1, -1}}, {{0, -1}}, {{1, -1}}
}};
}

int main() {
    const AirPhysics normal{250, 30, 12, 1, 1.0f / 64};
    SteeringState state{};
    // Fixed fixtures for Axion's 15/speed ideal angle and configured smoothing=50.
    // These numbers are independent of planner calculations.
    const auto even = PlanAirSteering(state, {250, 0, 0, 1, 0, 2}, normal);
    const auto odd = PlanAirSteering(state, {250, 0, 0, 1, 0, 3}, normal);
    Expect(Axes(even, 0.0598922907, 0.9982048455) && Axes(odd, 0.0598922907, -0.9982048455),
        "W alone emits alternating full-input axes instead of Axion's zero-offset early return");
    Expect(Axes(PlanAirSteering(state, {800, 0, 0, 1, 0, 2}, normal), 0.0187467050, 0.9998242651),
        "ideal strafe angle narrows with higher observed speed");
    const auto positive = PlanAirSteering(state, {250, 0, 0, 0, 1, 2}, normal);
    const auto negative = PlanAirSteering(state, {250, 0, 0, 0, -1, 2}, normal);
    Expect(Axes(positive, -0.1195695496, 0.9928258270) && Axes(negative, -0.1195695496, -0.9928258270),
        "velocity-error corrections preserve Axion smoothing and our positive-left convention");
    Expect(Equal(positive, PlanAirSteering(state, {250, 0, 0, 0, 1, 3}, normal)),
        "large heading correction takes priority over command parity");
    Expect(Axes(PlanAirSteering(state, {250, 0, 0, -1, 0, 2}, normal), -0.1195695496, 0.9928258270) &&
        Axes(PlanAirSteering(state, {250, 0, 0, -1, -0.001f, 3}, normal), -0.1195695496, 0.9928258270),
        "both errors beyond 170 degrees use Axion's positive-side recovery branch");
    Expect(Axes(PlanAirSteering(state, {80, 0, 0, 0, 1, 2}, normal), -0.9828721869, 0.1842885351),
        "speed exactly 80 uses parity even for large directional error");
    const auto above = PlanAirSteering(state, {80.01f, 0, 0, 0, 1, 2}, normal);
    Expect(above.valid && above.forward < 0 && above.forward > -0.37f && above.left > 0.93f,
        "speed above 80 enters directional correction");

    constexpr double d = 0.7071067811865475;
    constexpr std::array<std::array<double, 2>, 8> evenGolden{{
        {{d, d}}, {{0, 1}}, {{-d, d}}, {{-1, 0}}, {{-d, -d}}, {{0, -1}}, {{d, -d}}, {{1, 0}}
    }};
    constexpr std::array<std::array<double, 2>, 8> oddGolden{{
        {{d, -d}}, {{1, 0}}, {{d, d}}, {{0, 1}}, {{-d, d}}, {{-1, 0}}, {{-d, -d}}, {{0, -1}}
    }};
    bool allDirections = true;
    for (std::size_t i = 0; i < directions.size(); ++i) {
        const auto& direction = directions[i];
        allDirections &= Axes(PlanAirSteering(state, {0, 0, 0, direction[0], direction[1], 2}, normal),
            evenGolden[i][0], evenGolden[i][1]);
        allDirections &= Axes(PlanAirSteering(state, {0, 0, 0, direction[0], direction[1], 3}, normal),
            oddGolden[i][0], oddGolden[i][1]);
    }
    Expect(allDirections, "all eight intents have fixed golden outputs at the 45-degree low-speed clamp");
    SteeringState other{};
    const auto wrapA = PlanAirSteering(state, {250, 80, 179.9f, 1, 1, 9}, normal);
    const auto wrapB = PlanAirSteering(other, {250, 80, -180.1f, 1, 1, 9}, normal);
    Expect(wrapA.valid && wrapB.valid && Near(wrapA.forward, wrapB.forward, 0.00003) &&
        Near(wrapA.left, wrapB.left, 0.00003) && Near(wrapA.predictedVelocityX, wrapB.predictedVelocityX, 0.004) &&
        Near(wrapA.predictedVelocityY, wrapB.predictedVelocityY, 0.004), "wrapped camera angles produce equivalent steering");
    const auto mirrorA = PlanAirSteering(state, {200, 70, 15, 0, 1, 2}, normal);
    const auto mirrorB = PlanAirSteering(other, {200, -70, -15, 0, -1, 3}, normal);
    Expect(mirrorA.valid && mirrorB.valid && Near(mirrorA.forward, mirrorB.forward) && Near(mirrorA.left, -mirrorB.left) &&
        Near(mirrorA.predictedVelocityX, mirrorB.predictedVelocityX, 0.004) &&
        Near(mirrorA.predictedVelocityY, -mirrorB.predictedVelocityY, 0.004),
        "ordinary left/right corrections mirror across the world X axis");

    bool sameCommand = true, independentPhysics = true;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 0.2f, 0.004f}, AirPhysics{250, 30, 12, 2, 1.0f / 32},
        AirPhysics{20, 30, 100, 1, 0.1f}, AirPhysics{250, 3, 100, 1, 0.1f}, AirPhysics{250, 30, 12, 1, 0.0001f}}) {
        for (float speed : {0.0f, 10.0f, 80.0f, 250.0f, 800.0f}) {
            for (const auto& direction : directions) {
                for (std::uint32_t sequence : {2u, 3u}) {
                    SteeringState singleState{}, commandState{};
                    const StrafeInput input{speed, 0, 73, direction[0], direction[1], sequence};
                    const auto single = PlanAirSteering(singleState, input, p);
                    const auto command = PlanAirSteeringCommand(commandState, input, p);
                    sameCommand &= command.valid && command.count == 1 && Equal(single, command.summary) &&
                        Equal(single, command.slices[0]) && singleState.mode == commandState.mode &&
                        singleState.turnSide == commandState.turnSide;
                    for (std::size_t i = 1; i < command.slices.size(); ++i) sameCommand &= !command.slices[i].valid;
                    if (!single.valid) { independentPhysics = false; continue; }
                    const auto actual = Apply(speed, 0, 73, single, p);
                    independentPhysics &= Near(actual.x, single.predictedVelocityX, 0.004) &&
                        Near(actual.y, single.predictedVelocityY, 0.004) && Near(actual.impulse, single.accelerationStep, 0.004);
                }
            }
        }
    }
    Expect(sameCommand, "whole command equals one planner call with inactive retained-capacity slots");
    Expect(independentPhysics, "prediction matches independent full-command analog integration across varied physics");
    Expect(Equal(even, PlanAirSteering(state, {250, 0, 0, 1, 0, 2}, normal)),
        "sequence parity makes replay deterministic regardless of intervening calls");
    bool trajectories = true;
    for (const auto& direction : directions) {
        Trajectory path{}; path.vx = 250;
        for (int i = 0; i < 256; ++i) path.Step(direction[0], direction[1], normal);
        trajectories &= path.consistent && path.Speed() > 500 && std::abs(path.Error(0, direction[0], direction[1])) < 3;
        std::printf("intent=(%.0f,%.0f) commands=256 speed=%.6f heading_error=%.6f\n",
            direction[0], direction[1], path.Speed(), path.Error(0, direction[0], direction[1]));
    }
    Expect(trajectories, "normal-physics trajectories build speed and settle toward all eight requested directions");
    Trajectory sides{}; sides.vx = 250;
    const auto towardLeft = sides.Step(0, 1, normal);
    const auto towardRight = sides.Step(0, -1, normal);
    Expect(sides.consistent && towardLeft.summary.headingChange > 0 && towardRight.summary.headingChange < 0,
        "switching A to D changes turn direction on the next command");
    Trajectory reversal{}; reversal.vx = 250;
    const auto firstReverse = reversal.Step(-1, 0, normal);
    Expect(reversal.consistent && firstReverse.summary.headingChange > 0 && reversal.vx > 244 && reversal.vx < 245,
        "Axion reversal starts an orbit rather than direct braking");
    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{}; constrained.vx = 250;
    for (int i = 0; i < 64; ++i) constrained.Step(0, 1, weak);
    Expect(constrained.consistent && constrained.vy > 0 && constrained.vy < 3,
        "weak acceleration cannot manufacture instant capture or extra command impulse");
    weak.acceleration = 0;
    Trajectory frozen{}; frozen.vx = 250; frozen.Step(0, -1, weak);
    weak = normal; weak.surfaceFriction = 0;
    Trajectory frictionless{}; frictionless.vx = 250; frictionless.Step(-1, 0, weak);
    Expect(frozen.consistent && frictionless.consistent && frozen.vx == 250 && frozen.vy == 0 &&
        frictionless.vx == 250 && frictionless.vy == 0, "zero acceleration or friction preserves observed velocity");

    PlanAirSteeringCommand(state, {250, 80, 0, 0, 1, 1}, normal);
    const auto release = PlanAirSteeringCommand(state, {250, 80, 0, 0, 0, 2}, normal);
    Expect(!release.valid && release.count == 0 && !release.summary.valid && state.mode == SteeringMode::Idle && !state.turnSide,
        "physical-input release clears synthesized movement and steering state");
    bool rejected = true;
    const float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
    for (float duration : {0.0f, -0.01f, 0.2f, infinity, nan}) {
        auto bad = normal; bad.interval = duration;
        const auto out = PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 3}, bad);
        rejected &= !out.valid && !out.count && state.mode == SteeringMode::Idle;
    }
    for (const StrafeInput bad : {StrafeInput{nan, 0, 0, 1, 0, 1}, StrafeInput{250, infinity, 0, 1, 0, 1},
        StrafeInput{250, 0, nan, 1, 0, 1}, StrafeInput{250, 0, 0, nan, 0, 1},
        StrafeInput{250, 0, 0, 1, infinity, 1}, StrafeInput{100001, 0, 0, 1, 0, 1}}) {
        const auto out = PlanAirSteeringCommand(state, bad, normal);
        rejected &= !out.valid && !out.count && state.mode == SteeringMode::Idle;
    }
    for (int field = 0; field < 4; ++field) {
        auto bad = normal;
        if (field == 0) bad.wishSpeed = nan;
        if (field == 1) bad.wishSpeedCap = -1;
        if (field == 2) bad.acceleration = infinity;
        if (field == 3) bad.surfaceFriction = -1;
        const auto out = PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 1}, bad);
        rejected &= !out.valid && !out.count && state.mode == SteeringMode::Idle;
    }
    Expect(rejected, "invalid durations, inputs and physics reject the entire command and reset state");
    PlanAirSteering(state, {250, 0, 0, 0, 1, 2}, normal);
    ResetAirSteering(state);
    Expect(state.mode == SteeringMode::Idle && !state.turnSide, "lifecycle reset clears retained steering state");
    return failures ? 1 : 0;
}
