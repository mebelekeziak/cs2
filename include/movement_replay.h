#pragma once
#include "movement_command.h"
#include "movement_offsets.h"
#include "movement_steering.h"
#include <cmath>
#include <cstring>

struct MovementReplayIntent {
    float forward = 0, left = 0, viewYaw = 0;
    bool viewValid = false;
    std::uint8_t heldMask = 0;
};
namespace movement_physical_keys {
inline constexpr std::uint8_t Forward = 1, Back = 2, Left = 4, Right = 8;
}
struct MovementPhysicalIntentState {
    std::uint8_t heldMask = 0;
    int forward = 0, left = 0;
};
inline MovementReplayIntent MovementResolvePhysicalIntent(MovementPhysicalIntentState& state,
                                                          std::uint8_t heldMask) noexcept {
    const auto pressed = static_cast<std::uint8_t>(heldMask & ~state.heldMask);
    const auto resolve = [heldMask, pressed](std::uint8_t positive, std::uint8_t negative, int previous) {
        const bool positiveHeld = (heldMask & positive) != 0, negativeHeld = (heldMask & negative) != 0;
        if (positiveHeld != negativeHeld) return positiveHeld ? 1 : -1;
        if (!positiveHeld) return 0;
        const bool positivePressed = (pressed & positive) != 0, negativePressed = (pressed & negative) != 0;
        if (positivePressed != negativePressed) return positivePressed ? 1 : -1;
        // Both first observed together have no provable newest key.
        return positivePressed ? 0 : previous;
    };
    namespace keys = movement_physical_keys;
    state.forward = resolve(keys::Forward, keys::Back, state.forward);
    state.left = resolve(keys::Left, keys::Right, state.left);
    state.heldMask = heldMask;
    MovementReplayIntent intent{};
    intent.forward = static_cast<float>(state.forward); intent.left = static_cast<float>(state.left);
    intent.heldMask = heldMask;
    return intent;
}
inline bool MovementReplayIntentChanged(const MovementReplayIntent& cached,
                                        const MovementReplayIntent& current) noexcept {
    return cached.heldMask != current.heldMask || cached.forward != current.forward || cached.left != current.left ||
        cached.viewValid != current.viewValid || (current.viewValid &&
            std::abs(std::remainder(current.viewYaw - cached.viewYaw, 360.0f)) > 0.0001f);
}

// Rebuilding an existing sequence is a producer callback, not another planner
// step. Jump identity deliberately excludes physical steering intent: changed
// keys/camera can refresh analog input without planning the jump edge again.
struct MovementReplayContext {
    bool valid = false, held = false, bunnyhop = false, autostrafe = false, grounded = false;
    int sequence = -1, commandTick = -1;
    std::uint32_t owner = 0, simulationTick = 0;
    std::uintptr_t command = 0, base = 0, pawn = 0, pawnIdentity = 0, services = 0, system = 0;
    std::uintptr_t controller = 0, controllerIdentity = 0;
    std::uint32_t pawnHandle = UINT32_MAX, controllerHandle = UINT32_MAX;
    MovementReplayIntent intent{};
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
        // Ground is a result of predicting this input, not a new input identity.
        // Changing its planned edge based on that result can erase the very
        // jump that produced a takeoff during the next prediction replay.
        cached.commandTick == current.commandTick &&
        current.simulationTick >= cached.simulationTick;
}

struct MovementReplayPlan {
    bool jump = false, strafe = false, jumpEdge = false, jumpDown = false;
    float forward = 0, left = 0;
    MovementButtons buttons{};
};
inline MovementReplayPlan MovementRefreshReplaySteering(MovementReplayPlan plan,
    float forward, float left, std::uint64_t previousDirections) noexcept {
    // Zero axes are also owned: a release must replace historical assisted
    // axes before integration, rather than only changing the final base value.
    plan.strafe = true; plan.forward = forward; plan.left = left;
    MovementEncodeStrafe(plan.buttons, forward, left, previousDirections);
    return plan;
}
struct MovementReplaySteeringStart {
    bool available = false;
    StrafeInput input{};
    SteeringState state{};
    AirPhysics physics{};
    std::uint64_t directions = 0;
};
struct MovementReplaySteeringResult {
    bool refreshed = false;
    MovementReplayPlan plan{};
    SteeringState state{};
    SteeringOutput steering{};
};
inline MovementReplaySteeringResult MovementPlanReplaySteering(const MovementReplaySteeringStart& start,
    const MovementReplayIntent& cachedIntent, const MovementReplayIntent& currentIntent,
    const MovementReplayPlan& plan) noexcept {
    MovementReplaySteeringResult result{}; result.plan = plan;
    if (!start.available || !MovementReplayIntentChanged(cachedIntent, currentIntent)) return result;
    result.refreshed = true; result.state = start.state;
    auto input = start.input;
    input.forward = currentIntent.forward; input.left = currentIntent.left; input.viewYaw = currentIntent.viewYaw;
    if (currentIntent.viewValid) result.steering = PlanAirSteering(result.state, input, start.physics);
    else ResetAirSteering(result.state);
    result.plan = MovementRefreshReplaySteering(plan,
        result.steering.valid ? result.steering.forward : currentIntent.forward,
        result.steering.valid ? result.steering.left : currentIntent.left, start.directions);
    return result;
}
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
