#include "movement_command.h"
#include "movement_offsets.h"

void MovementEncodeJump(MovementButtons& buttons, bool down, bool previousDown) noexcept {
    constexpr auto jump = movement_offsets::Jump;
    buttons.down = (buttons.down & ~jump) | (down ? jump : 0);
    buttons.changed = (buttons.changed & ~jump) | (down != previousDown ? jump : 0);
    buttons.third &= ~jump;
}
void MovementEncodeStrafe(MovementButtons& buttons, float forward, float left, std::uint64_t previousDown) noexcept {
    namespace off = movement_offsets;
    std::uint64_t directions = 0;
    if (forward > 0.0001f) directions |= off::Forward;
    if (forward < -0.0001f) directions |= off::Back;
    if (left > 0.0001f) directions |= off::Left;
    if (left < -0.0001f) directions |= off::Right;
    buttons.down = (buttons.down & ~off::Directions) | directions;
    buttons.changed = (buttons.changed & ~off::Directions) | ((directions ^ previousDown) & off::Directions);
    buttons.third &= ~off::Directions;
}

void MovementSanitizeSubtick(MovementSubtick& step, bool jump, bool strafe) noexcept {
    const std::uint64_t mask = (jump ? movement_offsets::Jump : 0) |
                              (strafe ? movement_offsets::Directions : 0);
    if ((step.presence & 1) && (step.button & mask)) {
        step.button &= ~mask;
        if (!step.button) {
            step.presence &= ~3u;
            step.pressed = false;
        }
    }
    if (strafe) {
        step.forwardDelta = step.leftDelta = 0;
        step.presence &= ~0x18u;
    }
}
MovementSubtick MovementAnalogTransition(float desiredForward, float desiredLeft,
                                        float historicalForward, float historicalLeft) noexcept {
    MovementSubtick step{};
    step.presence = 0x1c;
    step.forwardDelta = desiredForward - historicalForward;
    step.leftDelta = desiredLeft - historicalLeft;
    return step;
}
MovementSubtick MovementJumpTransition(bool down) noexcept {
    MovementSubtick step{};
    step.presence = 7; step.button = movement_offsets::Jump; step.pressed = down;
    return step;
}
