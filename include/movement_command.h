#pragma once
#include <cstddef>
#include <cstdint>
#include <array>

struct MovementButtons {
    std::uint64_t down = 0, changed = 0, third = 0;
};
static_assert(sizeof(MovementButtons) == 24);
struct MovementSubtick {
    std::uintptr_t vtable = 0, arena = 0;
    std::uint32_t presence = 0, cachedSize = 0;
    std::uint64_t button = 0;
    bool pressed = false;
    std::array<std::byte, 3> padding{};
    float when = 0, forwardDelta = 0, leftDelta = 0, pitchDelta = 0, yawDelta = 0;
};
static_assert(sizeof(MovementSubtick) == 0x38);
static_assert(offsetof(MovementSubtick, when) == 0x24);
static_assert(offsetof(MovementSubtick, yawDelta) == 0x34);

void MovementEncodeJump(MovementButtons& buttons, bool down, bool previousDown) noexcept;
void MovementEncodeStrafe(MovementButtons& buttons, float forward, float left, std::uint64_t previousDown) noexcept;
void MovementSanitizeSubtick(MovementSubtick& step, bool jump, bool strafe) noexcept;
MovementSubtick MovementAnalogTransition(float desiredForward, float desiredLeft,
                                        float historicalForward, float historicalLeft) noexcept;
MovementSubtick MovementJumpTransition(bool down) noexcept;
