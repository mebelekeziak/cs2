#pragma once
#include "movement_command.h"
#include "movement_offsets.h"
#include <cstring>

// Rebuilding an existing sequence is a producer callback, not another planner
// step. Cached output is reusable only for the same current owner and source.
struct MovementReplayContext {
    bool valid = false, held = false, bunnyhop = false, autostrafe = false, grounded = false;
    int sequence = -1, commandTick = -1;
    std::uint32_t owner = 0, simulationTick = 0;
    std::uintptr_t command = 0, base = 0, pawn = 0, pawnIdentity = 0, services = 0, system = 0;
    std::uintptr_t controller = 0, controllerIdentity = 0;
    std::uint32_t pawnHandle = UINT32_MAX, controllerHandle = UINT32_MAX;
};
inline bool MovementReplaySameSource(const MovementReplayContext& cached,
                                    const MovementReplayContext& current) noexcept {
    return cached.valid && current.valid && current.held && cached.held &&
        cached.bunnyhop == current.bunnyhop && cached.autostrafe == current.autostrafe &&
        cached.owner == current.owner &&
        cached.command == current.command && cached.base == current.base &&
        cached.pawn == current.pawn && cached.pawnIdentity == current.pawnIdentity &&
        cached.services == current.services && cached.system == current.system &&
        cached.controller == current.controller && cached.controllerIdentity == current.controllerIdentity &&
        cached.pawnHandle == current.pawnHandle && cached.controllerHandle == current.controllerHandle;
}
inline bool MovementCanReplay(const MovementReplayContext& cached,
                              const MovementReplayContext& current) noexcept {
    return MovementReplaySameSource(cached, current) && cached.sequence == current.sequence &&
        cached.commandTick == current.commandTick && cached.grounded == current.grounded &&
        current.simulationTick >= cached.simulationTick;
}

struct MovementReplayPlan {
    bool jump = false, strafe = false, jumpEdge = false, jumpDown = false;
    float forward = 0, left = 0;
    MovementButtons buttons{};
};
inline std::uint64_t MovementReplayMask(const MovementReplayPlan& plan) noexcept {
    return (plan.jump ? movement_offsets::Jump : 0) |
           (plan.strafe ? movement_offsets::Directions : 0);
}
inline MovementButtons MovementReplayButtons(MovementButtons current,
                                            const MovementReplayPlan& plan) noexcept {
    const auto mask = MovementReplayMask(plan);
    current.down = (current.down & ~mask) | (plan.buttons.down & mask);
    current.changed = (current.changed & ~mask) | (plan.buttons.changed & mask);
    current.third = (current.third & ~mask) | (plan.buttons.third & mask);
    return current;
}

struct MovementReplayStep {
    std::uintptr_t address = 0;
    MovementSubtick value{};
};
inline bool MovementOwnsReplayStep(const MovementReplayStep& saved, std::uintptr_t address,
                                  const MovementSubtick& current) noexcept {
    // Pool entries may become mouse/action messages at the same address.
    // Cached protobuf size is mutable metadata and not part of the payload.
    return saved.address && saved.address == address &&
        saved.value.vtable == current.vtable && saved.value.arena == current.arena &&
        saved.value.presence == current.presence &&
        std::memcmp(reinterpret_cast<const std::byte*>(&saved.value) + offsetof(MovementSubtick, button),
                    reinterpret_cast<const std::byte*>(&current) + offsetof(MovementSubtick, button),
                    sizeof(MovementSubtick) - offsetof(MovementSubtick, button)) == 0;
}
