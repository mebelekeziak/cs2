#include "movement_math.h"
#include "movement_command.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>

namespace {
int failures = 0;
void Expect(bool result, const char* message) {
    printf("%s %s\n", result ? "PASS" : "FAIL", message);
    failures += !result;
}
float SpeedAfter(float x, float y, float yaw, const AirPhysics& physics) {
    const float radians = yaw * 0.01745329251994329577f;
    const float dx = std::cos(radians), dy = std::sin(radians);
    const float gain = std::fmin(physics.acceleration * physics.wishSpeed * physics.surfaceFriction * physics.interval,
        std::fmax(0.0f, std::fmin(physics.wishSpeed, physics.wishSpeedCap) - (x * dx + y * dy)));
    return std::hypot(x + gain * dx, y + gain * dy);
}
}
int main() {
    // Synthetic parameters test the solver, not installed-game cvar defaults.
    AirPhysics physics{250, 30, 12, 1, 1.0f / 64};
    for (float speed : {1.0f, 25.0f, 250.0f, 800.0f}) {
        for (float acceleration : {1.0f, 12.0f, 100.0f}) {
            physics.acceleration = acceleration;
            StrafeInput input{speed, 0, 20, 1, 0, 1};
            const auto out = OptimizeAirStrafe(input, physics);
            const float result = SpeedAfter(speed, 0, out.wishYaw, physics);
            float best = speed;
            for (int i = -1800; i <= 1800; ++i)
                best = std::fmax(best, SpeedAfter(speed, 0, static_cast<float>(i) * 0.1f, physics));
            Expect(out.valid && result + 0.002f >= best && result >= speed,
                   "optimized wish direction maximizes capped acceleration without braking");
            Expect(std::abs(std::hypot(out.forward, out.left) - 1) < 0.00001f,
                   "analog output has unit magnitude");
        }
    }
    bool variedPhysicsValid = true;
    for (float wishSpeed : {80.0f, 250.0f, 320.0f}) {
        for (float cap : {5.0f, 30.0f, 100.0f}) {
            for (float friction : {0.2f, 1.0f}) {
                for (float interval : {1.0f / 128, 1.0f / 32}) {
                    const AirPhysics variant{wishSpeed, cap, 2.0f, friction, interval};
                    const auto result = OptimizeAirStrafe({250, 0, -40, 1, 1, 7}, variant);
                    const float finalSpeed = SpeedAfter(250, 0, result.wishYaw, variant);
                    float best = 250;
                    for (int angle = -1800; angle <= 1800; ++angle)
                        best = std::fmax(best, SpeedAfter(250, 0, static_cast<float>(angle) * 0.1f, variant));
                    variedPhysicsValid &= result.valid && finalSpeed + 0.002f >= best;
                }
            }
        }
    }
    Expect(variedPhysicsValid, "solver follows varied speed limits, caps, friction and durations");
    physics.acceleration = 12;
    auto left = OptimizeAirStrafe({250, 0, 0, 0, 1, 1}, physics);
    auto right = OptimizeAirStrafe({250, 0, 0, 0, -1, 1}, physics);
    Expect(left.left > 0 && right.left < 0, "WASD intent selects steering direction");
    auto rotated = OptimizeAirStrafe({0, 250, 90, 0, 1, 1}, physics);
    Expect(std::abs(left.forward - rotated.forward) < 0.0001f &&
           std::abs(left.left - rotated.left) < 0.0001f, "camera-relative output rotates consistently");
    auto start = OptimizeAirStrafe({0, 0, 45, -1, 0, 1}, physics);
    Expect(start.valid && start.forward < -0.999f, "stationary input follows desired direction");
    auto odd = OptimizeAirStrafe({250, 0, 0, 0, 0, 1}, physics);
    auto even = OptimizeAirStrafe({250, 0, 0, 0, 0, 2}, physics);
    Expect(odd.left > 0 && even.left < 0, "centered steering alternates sides deterministically");
    physics.interval = 0;
    Expect(!OptimizeAirStrafe({}, physics).valid, "missing physics refuses movement synthesis");
    physics.interval = 1.0f / 64;
    StrafeInput invalid{};
    invalid.velocityX = std::numeric_limits<float>::quiet_NaN();
    Expect(!OptimizeAirStrafe(invalid, physics).valid, "invalid velocity refuses movement synthesis");
    MovementButtons buttons{0x102, 0x200, 0x402};
    MovementEncodeJump(buttons, true, false);
    Expect(buttons.down == 0x102 && buttons.changed == 0x202 && buttons.third == 0x400,
           "fresh jump press encoding preserves attack and unrelated buttons");
    MovementEncodeJump(buttons, false, true);
    Expect(buttons.down == 0x100 && buttons.changed == 0x202 && buttons.third == 0x400,
           "airborne jump release encoding preserves unrelated buttons");
    MovementEncodeJump(buttons, false, false);
    Expect(buttons.changed == 0x200, "continued airborne release has no fresh jump edge");
    buttons = {1 | 8, 1, 0};
    MovementEncodeStrafe(buttons, -0.1f, 0.99f, 8);
    Expect(buttons.down == (1 | 16 | 512) && buttons.changed == (1 | 8 | 16 | 512) && !buttons.third,
           "strafe analog signs synchronize required movement buttons and preserve attack");
    MovementSubtick step{};
    step.presence = 0x7f; step.button = 2; step.pressed = true;
    step.when = 0.75f; step.forwardDelta = 0.2f; step.leftDelta = -0.3f;
    step.pitchDelta = 1.2f; step.yawDelta = -4.5f;
    MovementSanitizeSubtick(step, true, true);
    Expect(step.presence == 0x64 && !step.button && !step.pressed && !step.forwardDelta && !step.leftDelta &&
           step.when == 0.75f && step.pitchDelta == 1.2f && step.yawDelta == -4.5f,
           "jump and analog subticks cleared without erasing camera deltas or timing");
    step.presence = 7; step.button = 3; step.pressed = true;
    MovementSanitizeSubtick(step, true, false);
    Expect(step.presence == 7 && step.button == 1 && step.pressed,
           "combined attack/jump subtick preserves attack event");
    step.presence = 7; step.button = 1 | 8 | 512; step.pressed = true;
    MovementSanitizeSubtick(step, false, true);
    Expect(step.presence == 7 && step.button == 1 && step.pressed,
           "synthesized strafe suppresses conflicting directional edges but retains attack");
    step.forwardDelta = 0.2f; step.leftDelta = -0.3f;
    const auto before = step;
    MovementSanitizeSubtick(step, false, false);
    Expect(step.presence == before.presence && step.button == before.button &&
           step.forwardDelta == before.forwardDelta && step.leftDelta == before.leftDelta,
           "disabled movement leaves subtick semantics intact");
    return failures ? 1 : 0;
}
