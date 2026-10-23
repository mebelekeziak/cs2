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
bool Equal(const SteeringOutput& a, const SteeringOutput& b) {
    return a.valid == b.valid && a.forward == b.forward && a.left == b.left && a.wishYaw == b.wishYaw &&
        a.accelerationStep == b.accelerationStep && a.predictedVelocityX == b.predictedVelocityX &&
        a.predictedVelocityY == b.predictedVelocityY && a.headingError == b.headingError &&
        a.headingChange == b.headingChange && a.mode == b.mode;
}
struct Applied { float x = 0, y = 0; double impulse = 0; };
Applied Apply(float vx, float vy, float camera, const SteeringOutput& o, const AirPhysics& p) {
    // Independently decode analog axes, without using wishYaw or prediction.
    const double amount = std::hypot(static_cast<double>(o.forward), static_cast<double>(o.left));
    const double cosine = std::cos(camera * radians), sine = std::sin(camera * radians);
    const double dx = (o.forward * cosine - o.left * sine) / amount;
    const double dy = (o.forward * sine + o.left * cosine) / amount;
    const double wish = p.wishSpeed * std::min(amount, 1.0);
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
        consistent &= command.valid && command.count == AirSteeringSliceCount;
        if (!command.valid || command.count != AirSteeringSliceCount) return command;
        auto fragment = p; fragment.interval /= static_cast<float>(command.count);
        double impulse = 0;
        for (const auto& o : command.slices) {
            consistent &= o.valid && Near(std::hypot(o.forward, o.left), 1, 0.000003);
            const auto actual = Apply(vx, vy, camera, o, fragment);
            consistent &= Near(actual.x, o.predictedVelocityX, 0.004) && Near(actual.y, o.predictedVelocityY, 0.004) &&
                Near(actual.impulse, o.accelerationStep, 0.004);
            impulse += actual.impulse; vx = actual.x; vy = actual.y;
        }
        const double budget = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
        consistent &= Near(vx, command.summary.predictedVelocityX, 0.004) &&
            Near(vy, command.summary.predictedVelocityY, 0.004) &&
            Near(impulse, command.summary.accelerationStep, 0.004) && impulse >= 0 && impulse <= budget + 0.004 &&
            command.summary.forward == command.slices.back().forward && command.summary.left == command.slices.back().left;
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
    auto fragment = normal; fragment.interval /= static_cast<float>(AirSteeringSliceCount);
    const auto even = PlanAirSteering(state, {250, 0, 0, 1, 0, 2}, fragment);
    const auto odd = PlanAirSteering(state, {250, 0, 0, 1, 0, 3}, fragment);
    Expect(even.valid && odd.valid && even.forward > 0 && Near(even.forward, odd.forward) &&
        even.left > 0 && Near(even.left, -odd.left), "aligned W input alternates full left/right wishes deterministically");
    // Projection is cap - half of a 1/32-command acceleration budget.
    // These numeric fixtures independently catch a return to the fixed 15/speed angle.
    Expect(Near(even.forward, 0.116276215, 0.00001) && Near(even.left, 0.993216916, 0.00001),
        "slice angle uses measured acceleration, wish speed, friction and duration");
    const auto high = PlanAirSteering(state, {800, 0, 0, 1, 0, 2}, fragment);
    Expect(high.valid && high.forward < even.forward && high.left > even.left,
        "ideal angle narrows as observed horizontal speed grows");
    const auto positive = PlanAirSteering(state, {250, 0, 0, 0, 1, 2}, fragment);
    const auto negative = PlanAirSteering(state, {250, 0, 0, 0, -1, 3}, fragment);
    Expect(positive.valid && negative.valid && positive.forward < 0 && positive.left > 0 &&
        Near(positive.forward, negative.forward) && Near(positive.left, -negative.left),
        "directional velocity corrections honor positive-left input and mirror right turns");
    Expect(Equal(positive, PlanAirSteering(state, {250, 0, 0, 0, 1, 3}, fragment)),
        "large heading correction takes priority over sequence alternation");
    const auto reverse = PlanAirSteering(state, {250, 0, 0, -1, 0, 2}, fragment);
    const auto reverseOther = PlanAirSteering(state, {250, 0, 0, -1, -0.001f, 3}, fragment);
    Expect(reverse.valid && reverse.forward < 0 && reverse.left > 0 && Near(reverse.forward, reverseOther.forward),
        "errors beyond 170 degrees select consistent orbital recovery");
    const auto low = PlanAirSteering(state, {80, 0, 0, 0, 1, 2}, fragment);
    const auto above = PlanAirSteering(state, {80.01f, 0, 0, 0, 1, 2}, fragment);
    Expect(low.valid && above.valid && low.forward < -0.9f && above.forward > -0.4f && above.left > 0.9f,
        "directional correction begins above the reference 80-unit threshold");
    bool starts = true;
    for (const auto& direction : directions) {
        const auto start = PlanAirSteering(state, {0, 0, 45, direction[0], direction[1], 2}, fragment);
        const double amount = std::hypot(direction[0], direction[1]);
        starts &= start.valid && Near(start.forward, direction[0] / amount) && Near(start.left, direction[1] / amount);
    }
    Expect(starts, "stationary input accelerates directly toward all eight physical WASD headings");
    const auto slow = PlanAirSteering(state, {9.99f, 0, 0, 1, 0, 2}, fragment);
    const auto threshold = PlanAirSteering(state, {10, 0, 0, 1, 0, 2}, fragment);
    Expect(slow.valid && slow.forward == 1 && slow.left == 0 && threshold.valid && threshold.left > 0.7f,
        "input follows the desired heading below 10 units and begins steering at the speed threshold");
    SteeringState other{};
    const auto wrapA = PlanAirSteeringCommand(state, {250, 80, 179.9f, 1, 1, 9}, normal);
    const auto wrapB = PlanAirSteeringCommand(other, {250, 80, -180.1f, 1, 1, 9}, normal);
    bool wrapping = wrapA.valid && wrapB.valid;
    for (std::size_t i = 0; i < AirSteeringSliceCount; ++i) wrapping &=
        Near(wrapA.slices[i].forward, wrapB.slices[i].forward, 0.00003) &&
        Near(wrapA.slices[i].left, wrapB.slices[i].left, 0.00003) &&
        Near(wrapA.slices[i].predictedVelocityX, wrapB.slices[i].predictedVelocityX, 0.004) &&
        Near(wrapA.slices[i].predictedVelocityY, wrapB.slices[i].predictedVelocityY, 0.004);
    Expect(wrapping, "all timed slices are equivalent across the wrapped camera seam");
    bool independentPhysics = true, progressing = false;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 0.2f, 0.004f}, AirPhysics{250, 30, 12, 2, 1.0f / 32},
        AirPhysics{20, 30, 100, 1, 0.1f}, AirPhysics{250, 3, 100, 1, 0.1f}, AirPhysics{250, 30, 12, 1, 0.0001f}}) {
        for (float speed : {0.0f, 10.0f, 80.0f, 250.0f, 800.0f}) {
            for (const auto& direction : directions) {
                Trajectory path{}; path.vx = speed;
                const auto command = path.Step(direction[0], direction[1], p, 73);
                independentPhysics &= path.consistent;
                progressing |= command.slices.front().forward != command.slices.back().forward ||
                    command.slices.front().left != command.slices.back().left;
            }
        }
    }
    Expect(independentPhysics, "independent timed analog integration matches every slice and whole-command budget across varied physics");
    Expect(progressing, "command planning recalculates wishes instead of repeating one full-command solution");
    const auto original = PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 2}, normal);
    PlanAirSteeringCommand(state, {250, 70, 90, -1, 0, 9}, normal);
    const auto replayed = PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 2}, normal);
    bool deterministic = original.count == replayed.count && Equal(original.summary, replayed.summary);
    for (std::size_t i = 0; i < AirSteeringSliceCount; ++i) deterministic &= Equal(original.slices[i], replayed.slices[i]);
    Expect(deterministic, "retained commands reproduce all slices regardless of intervening calls");
    bool trajectories = true;
    for (const auto& direction : directions) {
        Trajectory path{}; path.vx = 250;
        for (int i = 0; i < 256; ++i) path.Step(direction[0], direction[1], normal);
        trajectories &= path.consistent && path.Speed() > 250 && std::abs(path.Error(0, direction[0], direction[1])) < 8;
        std::printf("intent=(%.0f,%.0f) commands=256 speed=%.6f heading_error=%.6f\n",
            direction[0], direction[1], path.Speed(), path.Error(0, direction[0], direction[1]));
    }
    Expect(trajectories, "synthetic trajectories build speed and settle toward all eight requested directions");
    Trajectory sides{}; sides.vx = 250;
    const auto towardLeft = sides.Step(0, 1, normal);
    const auto towardRight = sides.Step(0, -1, normal);
    Expect(sides.consistent && towardLeft.summary.headingChange > 0 && towardRight.summary.headingChange < 0,
        "switching A to D changes turn direction on the next command");
    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{}; constrained.vx = 250;
    for (int i = 0; i < 64; ++i) constrained.Step(0, 1, weak);
    Expect(constrained.consistent && constrained.vy > 0 && constrained.vy < 3,
        "weak acceleration cannot manufacture instant turns or extra impulse");
    weak.acceleration = 0;
    Trajectory frozen{}; frozen.vx = 250; frozen.Step(0, -1, weak);
    weak = normal; weak.surfaceFriction = 0;
    Trajectory frictionless{}; frictionless.vx = 250; frictionless.Step(-1, 0, weak);
    Expect(frozen.consistent && frictionless.consistent && frozen.vx == 250 && frozen.vy == 0 &&
        frictionless.vx == 250 && frictionless.vy == 0, "zero acceleration or friction preserves observed velocity");
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
    const float maximum = std::numeric_limits<float>::max();
    const AirPhysics overflowing{maximum, maximum, maximum, maximum, 0.1f};
    const auto overflowDirect = PlanAirSteering(state, {250, 0, 0, -1, 0, 1}, overflowing);
    const auto overflowCommand = PlanAirSteeringCommand(state, {250, 0, 0, -1, 0, 1}, overflowing);
    Expect(overflowDirect.valid && std::isfinite(overflowDirect.predictedVelocityX) &&
        std::isfinite(overflowDirect.predictedVelocityY) && !overflowCommand.valid && state.mode == SteeringMode::Idle,
        "finite physics that overflow serialized prediction reject without publishing partial slices");
    PlanAirSteering(state, {250, 0, 0, 0, 1, 2}, normal);
    ResetAirSteering(state);
    Expect(state.mode == SteeringMode::Idle && !state.turnSide, "lifecycle reset clears retained steering state");
    return failures ? 1 : 0;
}
