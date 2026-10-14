#include "movement_command.h"
#include "movement_offsets.h"
#include <array>
#include <cmath>

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
                                        float historicalForward, float historicalLeft, float when) noexcept {
    MovementSubtick step{};
    step.presence = 0x1c;
    step.forwardDelta = desiredForward - historicalForward;
    step.leftDelta = desiredLeft - historicalLeft;
    step.when = when;
    return step;
}
MovementSubtick MovementJumpTransition(bool down) noexcept {
    MovementSubtick step{};
    step.presence = 7; step.button = movement_offsets::Jump; step.pressed = down;
    return step;
}

bool MovementOrderSubticks(std::span<const MovementSubtickOrderEntry> entries,
                          std::span<std::uintptr_t> ordered) noexcept {
    constexpr std::size_t limit = 128;
    if (entries.size() > limit || ordered.size() < entries.size()) return false;
    std::array<std::size_t, limit> originals{}, generated{};
    std::size_t originalCount = 0, generatedCount = 0;
    float previousOriginalWhen = -1;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!entry.address || !std::isfinite(entry.when) || entry.when < 0 || entry.when > 1) return false;
        if (entry.owned) {
            // The small generated stream can change times during a rebuild;
            // stable insertion leaves equal-time analog/jump ordering intact.
            std::size_t position = generatedCount++;
            while (position && entries[generated[position - 1]].when > entry.when) {
                generated[position] = generated[position - 1]; --position;
            }
            generated[position] = index;
        } else {
            if (entry.when < previousOriginalWhen) return false;
            previousOriginalWhen = entry.when;
            originals[originalCount++] = index;
        }
    }
    std::array<std::uintptr_t, limit> result{};
    std::size_t original = 0, generatedIndex = 0, written = 0;
    while (original < originalCount || generatedIndex < generatedCount) {
        const bool takeGenerated = generatedIndex < generatedCount &&
            (original == originalCount || entries[generated[generatedIndex]].when <= entries[originals[original]].when);
        const auto index = takeGenerated ? generated[generatedIndex++] : originals[original++];
        result[written++] = entries[index].address;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) ordered[index] = result[index];
    return true;
}
