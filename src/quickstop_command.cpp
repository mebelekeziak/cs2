#include "quickstop_command.h"
#include "movement_memory.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
namespace off = movement_offsets;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool SameStep(const MovementSubtick& a, const MovementSubtick& b) noexcept {
    return a.vtable == b.vtable && a.arena == b.arena && a.presence == b.presence &&
        a.button == b.button && a.pressed == b.pressed &&
        std::memcmp(&a.when, &b.when, sizeof(float) * 5) == 0;
}
bool Pointer(std::uintptr_t address, std::size_t bytes) noexcept {
    return address >= 0x10000 && address <= 0x00007fffffffffffULL - bytes;
}
bool Overlap(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) noexcept {
    return a < b + bn && b < a + an;
}
void PutStep(std::uintptr_t address, const MovementSubtick& value) noexcept {
    // Cached serialization size and allocation metadata belong to the engine.
    std::memcpy(reinterpret_cast<void*>(address + 0x10), &value.presence, sizeof(value.presence));
    std::memcpy(reinterpret_cast<void*>(address + 0x18), &value.button, sizeof(value.button));
    std::memcpy(reinterpret_cast<void*>(address + 0x20), &value.pressed, sizeof(value.pressed));
    std::memcpy(reinterpret_cast<void*>(address + 0x24), &value.when, sizeof(float) * 5);
}
bool Identity(const QuickstopCommandPatch& p) noexcept {
    int sequence = 0; std::uintptr_t base = 0;
    return p.valid && Read(p.boundary.command + off::command::Sequence, sequence) && sequence == p.boundary.sequence &&
        Read(p.boundary.command + off::command::Base, base) && base == p.boundary.base;
}
bool Validate(const QuickstopCommandPatch& p, AntiAimBoundaryPatch& rebound) noexcept {
    if (!Identity(p)) return false;
    const auto& b = p.boundary;
    MovementButtons buttons{}; float forward = 0, left = 0;
    int count = 0, capacity = 0, allocated = 0;
    std::uintptr_t rep = 0; std::uint32_t presence = 0;
    if (!Read(b.command + off::command::Buttons, buttons) ||
        ((buttons.down ^ p.editedButtons.down) & off::Directions) ||
        ((buttons.changed ^ p.editedButtons.changed) & off::Directions) ||
        ((buttons.third ^ p.editedButtons.third) & off::Directions) ||
        !Read(b.base + off::base::Forward, forward) || forward != p.editedForward ||
        !Read(b.base + off::base::Left, left) || left != p.editedLeft ||
        !Read(b.base + off::base::Presence, presence) || (presence & 0xC0u) != (p.editedPresence & 0xC0u) ||
        !Read(b.base + off::base::SubtickSize, count) || count != b.originalCount + (b.appended ? 1 : 0) ||
        !Read(b.base + off::base::SubtickCapacity, capacity) || capacity < count || capacity > 4096 ||
        !Read(b.base + off::base::SubtickRep, rep) ||
        !MovementOwnedWritable(b.command + off::command::Buttons, sizeof(buttons)) ||
        !MovementOwnedWritable(b.base + off::base::Forward, sizeof(float) * 2) ||
        !MovementOwnedWritable(b.base + off::base::Presence, sizeof(presence))) return false;
    const auto repBytes = 8 + static_cast<std::size_t>(capacity) * 8;
    if (rep) {
        if (!Pointer(rep, repBytes) || Overlap(rep, repBytes, b.command, 0x78) || Overlap(rep, repBytes, b.base, 0x60) ||
            !Read(rep, allocated) || allocated < count || allocated > capacity) return false;
    } else if (count || capacity) return false;
    if (b.appended) {
        std::uintptr_t first = 0; MovementSubtick generated{};
        if (!rep || Overlap(b.generated, sizeof(MovementSubtick), rep, repBytes) ||
            !Read(rep + 8, first) || first != b.generated || !Read(first, generated) ||
            !SameStep(generated, b.editedGenerated) ||
            !MovementOwnedWritable(first + 0x10, sizeof(generated) - 0x10) ||
            !MovementOwnedWritable(rep + 8, static_cast<std::size_t>(count) * 8) ||
            !MovementOwnedWritable(b.base + off::base::SubtickSize, sizeof(int))) return false;
    } else if (b.originalCount && !rep) return false;
    for (int i = 0; i < b.originalCount; ++i) {
        std::uintptr_t address = 0; MovementSubtick value{};
        const auto index = static_cast<std::size_t>(i);
        if (!Read(rep + 8 + (index + (b.appended ? 1 : 0)) * 8, address) || address != b.originals[index].address ||
            Overlap(address, sizeof(MovementSubtick), rep, repBytes) ||
            !Read(address, value) || !SameStep(value, p.editedSteps[index]) ||
            !MovementOwnedWritable(address + 0x10, sizeof(value) - 0x10)) return false;
    }
    // A later anti-aim boundary can grow the native arena/container. Its
    // restoration retains that growth. Rebind only after the complete logical
    // event stream has proven our exact allocation and payload ownership.
    rebound = b; rebound.rep = rep; rebound.capacity = capacity; rebound.allocated = allocated;
    return true;
}
bool Write(const QuickstopCommandPatch& p, bool outgoing) noexcept {
    __try {
        const auto& b = p.boundary;
        const auto& desired = outgoing ? p.editedButtons : p.originalButtons;
        auto* buttons = reinterpret_cast<MovementButtons*>(b.command + off::command::Buttons);
        buttons->down = (buttons->down & ~off::Directions) | (desired.down & off::Directions);
        buttons->changed = (buttons->changed & ~off::Directions) | (desired.changed & off::Directions);
        buttons->third = (buttons->third & ~off::Directions) | (desired.third & off::Directions);
        const float forward = outgoing ? p.editedForward : p.originalForward;
        const float left = outgoing ? p.editedLeft : p.originalLeft;
        std::memcpy(reinterpret_cast<void*>(b.base + off::base::Forward), &forward, sizeof(forward));
        std::memcpy(reinterpret_cast<void*>(b.base + off::base::Left), &left, sizeof(left));
        auto* presence = reinterpret_cast<std::uint32_t*>(b.base + off::base::Presence);
        *presence = (*presence & ~0xC0u) | ((outgoing ? p.editedPresence : p.originalPresence) & 0xC0u);
        for (int i = 0; i < b.originalCount; ++i) {
            const auto index = static_cast<std::size_t>(i);
            PutStep(b.originals[index].address, outgoing ? p.editedSteps[index] : b.originals[index].value);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

QuickstopAxes QuickstopBrakeAxes(float vx, float vy, float vz, float yaw, const QuickstopPhysics& physics) noexcept {
    QuickstopAxes result{};
    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) || !std::isfinite(yaw) ||
        std::abs(vx) > 100000 || std::abs(vy) > 100000 || std::abs(vz) > 100000) return result;
    for (const float value : {physics.wishSpeed, physics.acceleration, physics.accelerationSpeedFloor,
        physics.surfaceFriction, physics.groundFriction, physics.pawnFriction, physics.stopSpeed, physics.interval})
        if (!std::isfinite(value) || value < 0 || value > 100000) return result;
    if (physics.wishSpeed <= 0 || physics.accelerationSpeedFloor <= 0 || physics.interval <= 0 ||
        physics.interval > 0.1f) return result;
    const float speed = std::hypot(vx, vy);
    const float speed3 = std::hypot(speed, vz);
    const float frictionDrop = std::max(speed3, physics.stopSpeed) * physics.groundFriction *
        physics.surfaceFriction * physics.pawnFriction * physics.interval;
    result.speedAfterFriction = speed3 > 0 ? speed * std::max(0.0f, 1.0f - frictionDrop / speed3) : 0;
    result.maximumImpulse = physics.acceleration * std::max(physics.wishSpeed, physics.accelerationSpeedFloor) *
        physics.surfaceFriction * physics.interval;
    if (!std::isfinite(result.speedAfterFriction) || !std::isfinite(result.maximumImpulse)) return {};
    result.valid = true;
    // Tuning tolerance, not an engine accuracy threshold. Keep the tiny input
    // below this bound even if a floor-limited impulse crosses through zero.
    constexpr float settleSpeed = 0.25f;
    if (speed <= settleSpeed || result.maximumImpulse == 0) return result;
    result.settling = result.speedAfterFriction <= result.maximumImpulse;
    const float magnitude = result.settling ? std::min(1.0f, settleSpeed / physics.wishSpeed) : 1.0f;
    const float radians = std::remainder(yaw, 360.0f) * 0.01745329251994329577f;
    const float gain = magnitude / speed;
    result.forward = -(vx * std::cos(radians) + vy * std::sin(radians)) * gain;
    result.left = -(-vx * std::sin(radians) + vy * std::cos(radians)) * gain;
    return result;
}

bool QuickstopCommandHasJump(std::uintptr_t command) noexcept {
    MovementButtons buttons{};
    if (!Pointer(command, 0x78) || !Read(command + off::command::Buttons, buttons)) return true;
    if ((buttons.down | buttons.changed | buttons.third) & off::Jump) return true;
    std::uint32_t presence = 0;
    std::uintptr_t base = 0, rep = 0;
    int count = 0, capacity = 0, allocated = 0;
    if (!Read(command + off::command::Presence, presence) || !(presence & 1) ||
        !Read(command + off::command::Base, base) || !Pointer(base, 0x60) ||
        Overlap(base, 0x60, command, 0x78) ||
        !Read(base + off::base::SubtickSize, count) || count < 0 || count > 128 ||
        !Read(base + off::base::SubtickCapacity, capacity) || capacity < count || capacity > 4096 ||
        !Read(base + off::base::SubtickRep, rep)) return true;
    const auto repBytes = 8 + static_cast<std::size_t>(capacity) * 8;
    if (rep) {
        if (!Pointer(rep, repBytes) || Overlap(rep, repBytes, command, 0x78) || Overlap(rep, repBytes, base, 0x60) ||
            !Read(rep, allocated) || allocated < count || allocated > capacity) return true;
    } else if (count || capacity) return true;
    float previousWhen = -1;
    for (int index = 0; index < count; ++index) {
        std::uintptr_t address = 0;
        MovementSubtick step{};
        if (!Read(rep + 8 + static_cast<std::size_t>(index) * 8, address) || !Pointer(address, sizeof(step)) ||
            Overlap(address, sizeof(step), command, 0x78) || Overlap(address, sizeof(step), base, 0x60) ||
            Overlap(address, sizeof(step), rep, repBytes) || !Read(address, step) ||
            !std::isfinite(step.when) || step.when < 0 || step.when > 1 || step.when < previousWhen) return true;
        // The native decoder branches on raw button even without its presence
        // bit. A released edge still means this interval contains jump input.
        if (step.button & off::Jump) return true;
        previousWhen = step.when;
    }
    return false;
}

bool QuickstopApplyCommand(std::uintptr_t command, float forward, float left, float historyF, float historyL,
    std::uint64_t previousDirections, std::uintptr_t allocator, std::uintptr_t append, QuickstopCommandPatch& p,
    bool preserveInput) noexcept {
    p = {};
    if (!std::isfinite(forward) || !std::isfinite(left) || std::abs(forward) > 1.001f || std::abs(left) > 1.001f ||
        !std::isfinite(historyF) || !std::isfinite(historyL)) return false;
    std::uintptr_t base = 0; std::uint32_t commandPresence = 0;
    if (!Read(command + off::command::Base, base) || !base ||
        !Read(command + off::command::Presence, commandPresence) || !(commandPresence & 1) ||
        !Read(command + off::command::Buttons, p.originalButtons) ||
        !Read(base + off::base::Forward, p.originalForward) || !std::isfinite(p.originalForward) ||
        !Read(base + off::base::Left, p.originalLeft) || !std::isfinite(p.originalLeft) ||
        !Read(base + off::base::Presence, p.originalPresence) ||
        !MovementOwnedWritable(command + off::command::Buttons, sizeof(MovementButtons)) ||
        !MovementOwnedWritable(base + off::base::Forward, sizeof(float) * 2) ||
        !MovementOwnedWritable(base + off::base::Presence, sizeof(std::uint32_t))) return false;
    // Append first so its verified snapshot owns the original event stream.
    // Existing event payloads are preflighted before editing anything else.
    if (!AntiAimAppendBoundary(command, forward - historyF, left - historyL, allocator, append, p.boundary)) return false;
    for (int i = 0; i < p.boundary.originalCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        if (!MovementOwnedWritable(p.boundary.originals[index].address + 0x10, sizeof(MovementSubtick) - 0x10)) {
            AntiAimRestoreBoundary(p.boundary); return false;
        }
        p.editedSteps[index] = p.boundary.originals[index].value;
        if (!preserveInput) MovementSanitizeSubtick(p.editedSteps[index], false, true);
    }
    p.editedButtons = p.originalButtons;
    if (!preserveInput) MovementEncodeStrafe(p.editedButtons, forward, left, previousDirections);
    p.editedForward = preserveInput ? p.originalForward : forward;
    p.editedLeft = preserveInput ? p.originalLeft : left;
    p.editedPresence = preserveInput ? p.originalPresence : p.originalPresence | 0xC0u;
    p.valid = true;
    if (!Write(p, true)) { Write(p, false); AntiAimRestoreBoundary(p.boundary); p.valid = false; return false; }
    return true;
}
bool QuickstopRestoreCommand(const QuickstopCommandPatch& p) noexcept {
    AntiAimBoundaryPatch rebound{};
    if (!Validate(p, rebound) || !Write(p, false)) return false;
    return AntiAimRestoreBoundary(rebound);
}
bool QuickstopCommandStillEdited(const QuickstopCommandPatch& p) noexcept {
    if (!Identity(p)) return false;
    std::uintptr_t rep = 0; int count = 0;
    if (Read(p.boundary.base + off::base::SubtickRep, rep) && rep &&
        Read(p.boundary.base + off::base::SubtickSize, count) && count >= 0 && count <= 128) {
        for (int i = 0; i < count; ++i) {
            std::uintptr_t address = 0; MovementSubtick value{};
            if (!Read(rep + 8 + static_cast<std::size_t>(i) * 8, address) || !Read(address, value)) continue;
            if (p.boundary.appended && address == p.boundary.generated && SameStep(value, p.boundary.editedGenerated)) return true;
            for (int original = 0; original < p.boundary.originalCount; ++original) {
                const auto index = static_cast<std::size_t>(original);
                if (address == p.boundary.originals[index].address &&
                    !SameStep(p.boundary.originals[index].value, p.editedSteps[index]) &&
                    SameStep(value, p.editedSteps[index])) return true;
            }
        }
    }
    float forward = 0, left = 0;
    MovementButtons buttons{};
    const bool changedDirections = (((p.originalButtons.down ^ p.editedButtons.down) |
        (p.originalButtons.changed ^ p.editedButtons.changed) | (p.originalButtons.third ^ p.editedButtons.third)) & off::Directions) != 0;
    if (changedDirections && Read(p.boundary.command + off::command::Buttons, buttons) &&
        !(((buttons.down ^ p.editedButtons.down) | (buttons.changed ^ p.editedButtons.changed) |
           (buttons.third ^ p.editedButtons.third)) & off::Directions)) return true;
    return (p.originalForward != p.editedForward || p.originalLeft != p.editedLeft) &&
        Read(p.boundary.base + off::base::Forward, forward) && forward == p.editedForward &&
        Read(p.boundary.base + off::base::Left, left) && left == p.editedLeft;
}
