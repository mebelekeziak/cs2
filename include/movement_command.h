#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

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
void MovementSanitizeSubtick(MovementSubtick& step, bool jump, bool strafe, bool duck = false) noexcept;
MovementSubtick MovementAnalogTransition(float desiredForward, float desiredLeft,
                                        float historicalForward, float historicalLeft, float when = 0) noexcept;
MovementSubtick MovementJumpTransition(bool down) noexcept;

struct MovementSubtickOrderEntry {
    std::uintptr_t address = 0;
    float when = 0;
    bool owned = false;
};
// Merge generated timed events into a monotone original stream. Equal-time
// generated events precede original actions, whose relative order is retained.
// Invalid input returns false without changing the caller's output buffer.
bool MovementOrderSubticks(std::span<const MovementSubtickOrderEntry> entries,
                          std::span<std::uintptr_t> ordered) noexcept;
