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
struct Applied { float x = 0, y = 0; double impulse = 0; };
Applied Apply(float vx, float vy, float camera, const SteeringOutput& output, const AirPhysics& physics, double duration) {
    // Use emitted analog fields and elapsed ENGINE TIME. Wish yaw and the
    // planner's prediction are not inputs to this independent simulation.
    const double amount = std::hypot(static_cast<double>(output.forward), static_cast<double>(output.left));
    const double cosine = std::cos(camera * radians), sine = std::sin(camera * radians);
    const double dx = (output.forward * cosine - output.left * sine) / amount;
    const double dy = (output.forward * sine + output.left * cosine) / amount;
    const double wish = physics.wishSpeed * amount;
    const double projection = vx * dx + vy * dy;
    const double added = std::min(physics.acceleration * wish * physics.surfaceFriction * duration,
                                  std::max(0.0, std::min<double>(wish, physics.wishSpeedCap) - projection));
    return {static_cast<float>(vx + added * dx), static_cast<float>(vy + added * dy), added};
}
struct Trajectory {
    SteeringState state{};
    float vx = 0, vy = 0;
    bool valid = true, fullAnalog = true, predictionMatches = true, timeBudget = true;
    std::uint32_t sequence = 0;
    SteeringCommand Step(float forward, float left, const AirPhysics& physics, float camera = 0) {
        const double oldYaw = std::hypot(vx, vy) > 0.000001 ? std::atan2(vy, vx) :
            Wrap(camera * radians + std::atan2(left, forward));
        const auto command = PlanAirSteeringCommand(state, {vx, vy, camera, forward, left, ++sequence}, physics);
        valid &= command.valid && command.count == AirSteeringSliceCount;
        if (!command.valid || !command.count || command.count > command.slices.size()) return command;
        double totalImpulse = 0, elapsed = 0;
        for (std::size_t i = 0; i < command.count; ++i) {
            const auto& out = command.slices[i];
            const double amount = std::hypot(out.forward, out.left);
            valid &= out.valid && std::isfinite(amount) && amount <= 1.00001;
            fullAnalog &= std::abs(amount - 1) < 0.000003;
            const double when = static_cast<double>(i) / command.count;
            const double nextWhen = static_cast<double>(i + 1) / command.count;
            const double duration = (nextWhen - when) * physics.interval;
            const auto actual = Apply(vx, vy, camera, out, physics, duration);
            vx = actual.x; vy = actual.y;
            totalImpulse += actual.impulse; elapsed += duration;
            predictionMatches &= std::abs(vx - out.predictedVelocityX) < 0.004f &&
                                 std::abs(vy - out.predictedVelocityY) < 0.004f &&
                                 std::abs(actual.impulse - out.accelerationStep) < 0.004;
        }
        const double available = static_cast<double>(physics.acceleration) * physics.wishSpeed * physics.surfaceFriction * physics.interval;
        timeBudget &= std::abs(elapsed - physics.interval) < 1e-8 && totalImpulse <= available + 0.004;
        const auto& summary = command.summary;
        const auto& last = command.slices[command.count - 1];
        const double netHeadingChange = std::hypot(vx, vy) > 0.000001 ? Wrap(std::atan2(vy, vx) - oldYaw) : 0;
        predictionMatches &= summary.valid && summary.forward == last.forward && summary.left == last.left &&
            std::abs(summary.predictedVelocityX - vx) < 0.004f && std::abs(summary.predictedVelocityY - vy) < 0.004f &&
            std::abs(summary.accelerationStep - totalImpulse) < 0.004 &&
            std::abs(Wrap(summary.headingChange * radians - netHeadingChange)) < 0.0001;
        return command;
    }
    float Speed() const { return std::hypot(vx, vy); }
    bool Consistent() const { return valid && fullAnalog && predictionMatches && timeBudget; }
};
// Independent dense search of full-unit wish projections. No speed-preservation
// constraint: the oracle intentionally permits the tangent impulse to lose speed.
double FastestTurn(double speed, const AirPhysics& p) {
    const double impulse = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
    const double cap = std::min(p.wishSpeedCap, p.wishSpeed);
    double fastest = 0;
    for (int sample = 0; sample <= 32768; ++sample) {
        const double projection = speed * (2.0 * sample / 32768 - 1);
        const double added = std::min(impulse, std::max(0.0, cap - projection));
        const double x = speed + added * projection / speed;
        const double y = added * std::sqrt(std::max(0.0, 1 - projection * projection / (speed * speed)));
        fastest = std::max(fastest, std::atan2(y, x) / radians);
    }
    return fastest;
}
}

int main() {
    const AirPhysics normal{250, 30, 12, 1, 1.0f / 64};
    auto fragment = normal; fragment.interval /= static_cast<float>(AirSteeringSliceCount);
    constexpr std::array<std::array<float, 2>, 8> directions{{
        {{1, 0}}, {{1, 1}}, {{0, 1}}, {{-1, 1}},
        {{-1, 0}}, {{-1, -1}}, {{0, -1}}, {{1, -1}}
    }};

    bool fastest = true, turnLoss = true, singularPhysics = true;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 2, 1.0f / 64},
                              AirPhysics{250, 30, 12, 0.2f, 0.004f}}) {
        const double available = static_cast<double>(p.acceleration) * p.wishSpeed * p.surfaceFriction * p.interval;
        for (float speed : {50.0f, 80.0f, 250.0f, 800.0f}) {
            if (available >= speed) continue;
            for (float side : {-1.0f, 1.0f}) {
                SteeringState state{};
                const auto out = PlanAirSteering(state, {speed, 0, 0, 0, side, 1}, p);
                const auto actual = Apply(speed, 0, 0, out, p, p.interval);
                const double squared = static_cast<double>(actual.x) * actual.x + static_cast<double>(actual.y) * actual.y;
                fastest &= out.valid && out.mode == SteeringMode::Turning &&
                    side * out.headingChange >= FastestTurn(speed, p) - 0.002;
                turnLoss &= squared < speed * speed && std::abs(squared - (speed * speed - available * available)) < 0.3;
                singularPhysics &= std::abs(actual.x - out.predictedVelocityX) < 0.004f &&
                    std::abs(actual.y - out.predictedVelocityY) < 0.004f &&
                    std::abs(std::hypot(out.forward, out.left) - 1) < 0.000003;
            }
        }
    }
    Expect(fastest, "direction capture uses the fastest physically available turn without a gain-preservation floor");
    Expect(turnLoss && singularPhysics, "maximum-heading impulses intentionally shed momentum and match independent full-input physics");

    bool capture = true;
    for (const AirPhysics p : {normal, AirPhysics{250, 30, 12, 2, 1.0f / 64},
                              AirPhysics{250, 30, 12, 0.2f, 0.004f}}) {
        for (float side : {-1.0f, 1.0f}) {
            const double target = side * (OptimalGain(250, p).turn + FastestTurn(250, p)) / 2;
            SteeringState state{};
            const auto out = PlanAirSteering(state, {250, 0, static_cast<float>(target), 1, 0, 1}, p);
            const auto actual = Apply(250, 0, static_cast<float>(target), out, p, p.interval);
            capture &= out.valid && std::abs(out.headingError) < 0.001 &&
                std::abs(actual.x - out.predictedVelocityX) < 0.004f && std::abs(actual.y - out.predictedVelocityY) < 0.004f;
        }
    }
    Expect(capture, "reachable intermediate directions are captured in one fragment at full analog input");

    Trajectory reversing{}; reversing.vx = 250;
    bool directBrake = true;
    int reversalCommand = 0;
    for (int i = 1; i <= 6; ++i) {
        const float previous = reversing.vx;
        const auto out = reversing.Step(-1, 0, normal);
        if (previous > 0) directBrake &= out.summary.mode == SteeringMode::Reversing && reversing.vx < previous;
        if (!reversalCommand && reversing.vx < 0) reversalCommand = i;
        if (reversing.vx > 0) directBrake &= std::abs(reversing.vy) < 0.001f;
        else directBrake &= std::abs(HeadingError(reversing.vx, reversing.vy, 0, -1, 0)) <=
            2 * OptimalGain(reversing.Speed(), fragment).turn + 0.1;
    }
    Expect(directBrake && reversalCommand > 0 && reversalCommand <= 6 && reversing.Consistent(),
           "W-to-S cancels unwanted momentum directly and reverses within six real command intervals");
    std::printf("reversal_commands=%d final_velocity_x=%.6f final_speed=%.6f\n", reversalCommand, reversing.vx, reversing.Speed());
    bool stableAntipode = true;
    for (float left : {0.0f, -0.0f}) {
        Trajectory antipode{}; antipode.vx = 250;
        for (int i = 0; i < 5; ++i) {
            const auto out = antipode.Step(-1, left, normal);
            for (std::size_t j = 0; j < out.count; ++j)
                stableAntipode &= out.slices[j].mode == SteeringMode::Reversing && out.slices[j].forward < -0.99999f &&
                    std::abs(out.slices[j].left) < 0.00001f;
        }
        stableAntipode &= antipode.Consistent() && antipode.vx > 0 && antipode.vx < 16 && std::abs(antipode.vy) < 0.001f;
    }
    Expect(stableAntipode, "both signed antipodal headings keep one stable direct braking axis instead of alternating an orbit");

    SteeringState exactStopState{};
    const float availableImpulse = normal.acceleration * normal.wishSpeed * normal.surfaceFriction * normal.interval;
    const auto exactStop = PlanAirSteering(exactStopState, {availableImpulse, 0, 0, -1, 0, 1}, normal);
    Expect(exactStop.valid && exactStop.mode == SteeringMode::Reversing &&
           std::hypot(exactStop.predictedVelocityX, exactStop.predictedVelocityY) < 0.000001f &&
           exactStop.headingChange == 0 && std::abs(std::abs(exactStop.headingError) - 180) < 0.001f,
           "exact braking to zero velocity reports no invented heading and retains the requested reversal error");
    const auto afterStop = PlanAirSteering(exactStopState,
        {exactStop.predictedVelocityX, exactStop.predictedVelocityY, 0, -1, 0, 2}, normal);
    Expect(afterStop.valid && afterStop.mode == SteeringMode::Accelerating &&
           afterStop.predictedVelocityX < -29.99f && std::abs(afterStop.predictedVelocityY) < 0.000001f &&
           afterStop.forward < -0.99999f && std::abs(afterStop.left) < 0.000001f,
           "the fragment after an exact stop immediately accelerates along the requested axis");
    Trajectory timedExactStop{}; timedExactStop.vx = availableImpulse;
    const auto stoppedCommand = timedExactStop.Step(-1, 0, normal);
    Expect(timedExactStop.Consistent() && timedExactStop.Speed() < 0.000001f && stoppedCommand.summary.headingChange == 0,
           "an exact stop across timed fragments preserves the command budget and reports zero net heading change");

    bool lowCapture = true, lowRecovery = true, lowPhysics = true;
    for (float initial : {0.0f, 10.0f, 30.0f, 50.0f, 80.0f}) {
        for (const auto& direction : directions) {
            for (float camera : {0.0f, 73.0f, -180.0f}) {
                Trajectory path{}; path.vx = initial;
                for (int i = 0; i < 4; ++i) path.Step(direction[0], direction[1], normal, camera);
                lowCapture &= std::abs(HeadingError(path.vx, path.vy, camera, direction[0], direction[1])) <=
                    2 * OptimalGain(path.Speed(), fragment).turn + 0.1;
                for (int i = 4; i < 128; ++i) path.Step(direction[0], direction[1], normal, camera);
                lowRecovery &= path.Speed() > 500;
                lowPhysics &= path.Consistent();
            }
        }
    }
    Expect(lowCapture, "all eight intents capture low-speed travel promptly instead of getting stuck in a turn");
    Expect(lowRecovery && lowPhysics, "10/30/50/80-speed and stationary starts recover strong speed with consistent timed fragments");

    SteeringState singleState{};
    const auto single = PlanAirSteering(singleState, {250, 0, 0, 1, 0, 1}, normal);
    const double singleGain = static_cast<double>(single.predictedVelocityX) * single.predictedVelocityX +
        static_cast<double>(single.predictedVelocityY) * single.predictedVelocityY - 250.0 * 250;
    Trajectory oneCommand{}; oneCommand.vx = 250;
    const auto integrated = oneCommand.Step(1, 0, normal);
    const double commandGain = static_cast<double>(oneCommand.vx) * oneCommand.vx + static_cast<double>(oneCommand.vy) * oneCommand.vy - 250.0 * 250;
    const double fullFragmentGain = AirSteeringSliceCount * OptimalGain(250, fragment).squared;
    // Capturing the tiny alternating heading error may spend some of the
    // theoretical unconstrained gain. The benefit must remain substantial.
    Expect(single.valid && commandGain > 2 * singleGain && commandGain <= fullFragmentGain + 0.5 && oneCommand.Consistent(),
           "eight timed gain impulses improve constant-heading acceleration while sharing one command time budget");
    const auto summaryOnly = Apply(250, 0, 0, integrated.summary, normal, normal.interval);
    Expect(std::hypot(summaryOnly.x - oneCommand.vx, summaryOnly.y - oneCommand.vy) > 1,
           "replaying only the summary axes cannot substitute for emitting the eight timed transitions");
    std::printf("constant_heading_single_squared_gain=%.6f timed_squared_gain=%.6f theoretical_fragment_gain=%.6f impulse=%.6f budget=%.6f\n",
                singleGain, commandGain, fullFragmentGain, integrated.summary.accelerationStep,
                static_cast<double>(normal.acceleration) * normal.wishSpeed * normal.surfaceFriction * normal.interval);

    Trajectory forward{}; forward.vx = 250;
    bool forwardHeading = true;
    for (int i = 0; i < 128; ++i) {
        forward.Step(1, 0, normal);
        forwardHeading &= std::abs(HeadingError(forward.vx, forward.vy, 0, 1, 0)) <= 2 * OptimalGain(forward.Speed(), fragment).turn + 0.1;
    }
    Expect(forward.Speed() > 570 && forwardHeading && forward.Consistent(),
           "constant heading keeps strong acceleration and a fragment-scaled heading error as speed rises");
    forward.vx *= 0.02f; forward.vy *= 0.02f;
    for (int i = 0; i < 128; ++i) forward.Step(1, 0, normal);
    Expect(forward.Speed() > 500 && forward.Consistent(), "observed stall immediately restores acceleration without a retained-speed ceiling");

    bool switches = true, immediateSide = true;
    Trajectory switching{}; switching.vx = 250;
    for (const auto& direction : directions) {
        const double previousError = -HeadingError(switching.vx, switching.vy, 0, direction[0], direction[1]);
        const auto command = switching.Step(direction[0], direction[1], normal);
        if (std::abs(previousError) > OptimalGain(switching.Speed(), fragment).turn + 0.1 && std::abs(previousError) < 90)
            immediateSide &= previousError * command.slices[0].headingChange > 0;
        for (int i = 0; i < 15; ++i) switching.Step(direction[0], direction[1], normal);
        switches &= std::abs(HeadingError(switching.vx, switching.vy, 0, direction[0], direction[1])) <=
                    2 * OptimalGain(switching.Speed(), fragment).turn + 0.1;
    }
    Expect(switches && immediateSide && switching.Consistent(), "all eight intent changes steer immediately and settle near the new direction");
    Trajectory sides{}; sides.vx = 250;
    const auto towardLeft = sides.Step(0, 1, normal);
    const auto towardRight = sides.Step(0, -1, normal);
    Expect(towardLeft.slices[0].headingChange > 0 && towardRight.slices[0].headingChange < 0 && sides.Consistent(),
           "A-to-D changes the response on the first real fragment of the next command");

    bool circles = true, circlePhysics = true;
    for (float rate : {90.0f, 240.0f}) {
        for (float side : {-1.0f, 1.0f}) {
            Trajectory circle{}; circle.vx = circle.vy = 250 / std::sqrt(2.0f);
            double greatestLag = 0;
            for (int i = 1; i <= 128; ++i) {
                const float camera = static_cast<float>(std::remainder(side * i * rate / 64, 360.0));
                circle.Step(1, 1, normal, camera);
                const double lag = std::abs(HeadingError(circle.vx, circle.vy, camera, 1, 1));
                greatestLag = std::max(greatestLag, lag);
                circles &= lag <= 2 * OptimalGain(circle.Speed(), fragment).turn + 0.1;
            }
            circles &= circle.Speed() > (rate == 90 ? 500 : 350);
            circlePhysics &= circle.Consistent();
            std::printf("circle_rate=%.0f side=%.0f commands=128 start_speed=250 final_speed=%.6f maximum_heading_lag_degrees=%.6f\n",
                         rate, side, circle.Speed(), greatestLag);
        }
    }
    Expect(circles && circlePhysics, "rotating diagonal intent follows both circle directions with realistic capture and timed acceleration");

    SteeringState wrapStateA{}, wrapStateB{};
    const auto wrapA = PlanAirSteeringCommand(wrapStateA, {250, 80, 179.9f, 1, 1, 1}, normal);
    const auto wrapB = PlanAirSteeringCommand(wrapStateB, {250, 80, -180.1f, 1, 1, 1}, normal);
    bool wrapped = wrapA.valid && wrapB.valid && wrapA.count == wrapB.count;
    for (std::size_t i = 0; i < wrapA.count && wrapped; ++i)
        wrapped &= std::abs(wrapA.slices[i].predictedVelocityX - wrapB.slices[i].predictedVelocityX) < 0.004f &&
            std::abs(wrapA.slices[i].predictedVelocityY - wrapB.slices[i].predictedVelocityY) < 0.004f &&
            std::abs(wrapA.slices[i].forward - wrapB.slices[i].forward) < 0.00003f &&
            std::abs(wrapA.slices[i].left - wrapB.slices[i].left) < 0.00003f;
    Expect(wrapped, "camera wrap produces equivalent timed steering sequences");

    bool matrixPhysics = true, matrixDirection = true, matrixSpeed = true;
    for (float speed : {30.0f, 250.0f, 800.0f}) {
        for (float friction : {0.2f, 1.0f, 2.0f}) {
            for (float interval : {1.0f / 128, 1.0f / 64, 1.0f / 32, 0.004f}) {
                const AirPhysics p{250, 30, 12, friction, interval};
                auto part = p; part.interval /= static_cast<float>(AirSteeringSliceCount);
                Trajectory path{}; path.vx = speed;
                for (int step = 0; step < static_cast<int>(8 / interval); ++step) path.Step(0, -1, p);
                matrixSpeed &= path.Speed() > speed + 10;
                matrixDirection &= std::abs(HeadingError(path.vx, path.vy, 0, 0, -1)) <= 2 * OptimalGain(path.Speed(), part).turn + 0.1;
                matrixPhysics &= path.Consistent();
            }
        }
    }
    Expect(matrixSpeed && matrixDirection, "weak/high-friction and fractional-duration steering captures intent and keeps building speed");
    Expect(matrixPhysics, "all varied commands match independent timed integration without multiplying their acceleration budget");

    bool extremePhysics = true;
    for (const AirPhysics p : {AirPhysics{250, 3, 100, 1, 0.1f}, AirPhysics{250, 30, 100, 1, 0.1f},
                              AirPhysics{250, 250, 100, 1, 0.1f}, AirPhysics{250, 30, 12, 1, 0.0001f},
                              AirPhysics{20, 30, 12, 1, 1.0f / 64}}) {
        for (float speed : {0.0f, 1.0f, 30.0f, 250.0f, 800.0f}) {
            Trajectory path{}; path.vx = speed;
            for (int i = 0; i < 64; ++i) path.Step(1, 0, p);
            extremePhysics &= path.Consistent();
        }
    }
    Expect(extremePhysics, "large impulses, tiny caps and tiny fragments preserve normalized independent timed physics");

    AirPhysics weak = normal; weak.acceleration = 0.01f;
    Trajectory constrained{}; constrained.vx = 250;
    for (int i = 0; i < 64; ++i) constrained.Step(0, 1, weak);
    Expect(constrained.vy > 0 && constrained.vy < 3 && constrained.Consistent(),
           "very weak acceleration cannot manufacture instant capture or extra command-time impulse");
    weak.acceleration = 0;
    Trajectory frozen{}; frozen.vx = 250;
    frozen.Step(0, -1, weak);
    Expect(frozen.vx == 250 && frozen.vy == 0 && frozen.Consistent(), "zero acceleration leaves observed velocity unchanged across all fragments");
    weak = normal; weak.surfaceFriction = 0;
    Trajectory frictionless{}; frictionless.vx = 250;
    frictionless.Step(-1, 0, weak);
    Expect(frictionless.vx == 250 && frictionless.vy == 0 && frictionless.Consistent(), "zero surface friction cannot manufacture braking or turning");

    SteeringState state{};
    PlanAirSteeringCommand(state, {250, 80, 0, 0, 1, 1}, normal);
    const auto release = PlanAirSteeringCommand(state, {250, 80, 0, 0, 0, 2}, normal);
    Expect(!release.valid && release.count == 0 && !release.summary.valid && state.mode == SteeringMode::Idle && !state.turnSide,
           "release clears synthesized fragments and retained steering without autostop");
    bool rejected = true;
    for (float duration : {0.0f, -0.01f, 0.2f, std::numeric_limits<float>::infinity()}) {
        auto bad = normal; bad.interval = duration;
        const auto out = PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 3}, bad);
        rejected &= !out.valid && !out.count && state.mode == SteeringMode::Idle;
    }
    Expect(rejected, "invalid original command durations remain invalid before splitting into shorter fragments");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Expect(!PlanAirSteeringCommand(state, {nan, 0, 0, 1, 0, 4}, normal).valid,
           "nonfinite observed velocity rejects the entire synthesis sequence");
    auto bad = normal; bad.acceleration = std::numeric_limits<float>::infinity();
    Expect(!PlanAirSteeringCommand(state, {250, 0, 0, 1, 0, 5}, bad).valid,
           "nonfinite acceleration rejects the entire synthesis sequence");
    ResetAirSteering(state);
    Expect(state.mode == SteeringMode::Idle && !state.turnSide, "lifecycle reset clears direction and reversal state");
    return failures ? 1 : 0;
}
