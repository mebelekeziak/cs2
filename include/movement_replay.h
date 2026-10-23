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
    bool jumpbug = false;
};
inline bool MovementReplaySameSource(const MovementReplayContext& cached,
                                    const MovementReplayContext& current) noexcept {
    return cached.valid && current.valid && current.held && cached.held &&
        cached.bunnyhop == current.bunnyhop && cached.autostrafe == current.autostrafe &&
        cached.jumpbug == current.jumpbug &&
        cached.owner == current.owner &&
        cached.command == current.command && cached.base == current.base &&
        cached.pawn == current.pawn && cached.pawnIdentity == current.pawnIdentity &&
        cached.services == current.services && cached.system == current.system &&
        cached.controller == current.controller && cached.controllerIdentity == current.controllerIdentity &&
        cached.pawnHandle == current.pawnHandle && cached.controllerHandle == current.controllerHandle;
}
inline bool MovementCanReplay(const MovementReplayContext& cached,
                              const MovementReplayContext& current) noexcept {
    // Ground is a prediction result. Creation tick and controller TickBase can
    // also be corrected while the producer rebuilds this same sequence. None
    // is a new command: replanning can serialize opposite jump edges for one
    // sequence, or erase the very press that produced a predicted takeoff.
    return MovementReplaySameSource(cached, current) && cached.sequence == current.sequence;
}

struct MovementReplayPlan {
    bool jump = false, strafe = false, jumpEdge = false, jumpDown = false;
    float forward = 0, left = 0;
    MovementButtons buttons{};
    struct Analog { float forward = 0, left = 0, when = 0; };
    std::size_t analogCount = 0;
    std::array<Analog, AirSteeringSliceCount> analogs{};
    bool jumpbug = false;
    float landingFraction = 0;
};
inline MovementReplayPlan MovementRefreshReplaySteering(MovementReplayPlan plan,
    float forward, float left, std::uint64_t previousDirections) noexcept {
    // Zero axes are also owned: a release must replace historical assisted
    // axes before integration, rather than only changing the final base value.
    plan.strafe = true; plan.forward = forward; plan.left = left;
    plan.analogCount = 1; plan.analogs = {}; plan.analogs[0] = {forward, left, 0};
    MovementEncodeStrafe(plan.buttons, forward, left, previousDirections);
    return plan;
}
inline MovementReplayPlan MovementRefreshReplaySteeringCommand(MovementReplayPlan plan,
    const SteeringCommand& command, std::uint64_t previousDirections) noexcept {
    plan = MovementRefreshReplaySteering(plan, command.summary.forward, command.summary.left, previousDirections);
    plan.analogCount = command.count;
    for (std::size_t slice = 0; slice < command.count && slice < plan.analogs.size(); ++slice)
        plan.analogs[slice] = {command.slices[slice].forward, command.slices[slice].left,
            static_cast<float>(slice) / static_cast<float>(command.count)};
    return plan;
}
inline std::size_t MovementReplayAnalogCount(const MovementReplayPlan& plan) noexcept {
    return plan.strafe ? (plan.analogCount ? plan.analogCount : 1) : 0;
}
inline bool MovementReplayAnalogTransition(const MovementReplayPlan& plan, std::size_t slice,
    float historicalForward, float historicalLeft, MovementSubtick& event) noexcept {
    const auto count = MovementReplayAnalogCount(plan);
    if (count > plan.analogs.size() || slice >= count ||
        !std::isfinite(historicalForward) || !std::isfinite(historicalLeft)) return false;
    const auto analog = plan.analogCount ? plan.analogs[slice] : MovementReplayPlan::Analog{plan.forward, plan.left, 0};
    if (!std::isfinite(analog.forward) || !std::isfinite(analog.left) || !std::isfinite(analog.when) ||
        analog.when < 0 || analog.when >= 1 || (!slice && analog.when != 0) ||
        (slice && analog.when <= plan.analogs[slice - 1].when)) return false;
    const auto previous = slice ? plan.analogs[slice - 1] : MovementReplayPlan::Analog{historicalForward, historicalLeft, 0};
    if (!std::isfinite(previous.forward) || !std::isfinite(previous.left)) return false;
    event = MovementAnalogTransition(analog.forward, analog.left, previous.forward, previous.left, analog.when);
    return true;
}
inline constexpr std::size_t MovementReplayJumpEventIndex = AirSteeringSliceCount;
inline constexpr std::size_t MovementReplayJumpbugEventIndex = AirSteeringSliceCount + 1;
inline constexpr std::size_t MovementReplayEventCount = AirSteeringSliceCount + 5;
inline bool MovementReplayTransition(const MovementReplayPlan& plan, std::size_t eventIndex,
    float historicalForward, float historicalLeft, MovementSubtick& event, bool& active) noexcept {
    if (eventIndex >= MovementReplayEventCount || MovementReplayAnalogCount(plan) > AirSteeringSliceCount ||
        (plan.jumpbug && (!std::isfinite(plan.landingFraction) || plan.landingFraction <= 0 ||
            plan.landingFraction >= 1))) return false;
    active = false; event = {}; event.presence = 4; // Neutralize retired owned events at time zero.
    if (eventIndex < MovementReplayAnalogCount(plan)) {
        active = true;
        return MovementReplayAnalogTransition(plan, eventIndex, historicalForward, historicalLeft, event);
    }
    if (eventIndex == MovementReplayJumpEventIndex && (plan.jumpEdge || plan.jumpbug)) {
        // The landing press must start from released input, including when
        // native historical state still contains physical held Space.
        active = true; event = MovementJumpTransition(plan.jumpbug ? false : plan.jumpDown);
    }
    if (eventIndex >= MovementReplayJumpbugEventIndex && plan.jumpbug) {
        const auto transition = eventIndex - MovementReplayJumpbugEventIndex;
        active = true;
        event.presence = 7;
        event.button = transition < 2 ? movement_offsets::Duck : movement_offsets::Jump;
        event.pressed = transition == 0 || transition == 3;
        event.when = transition ? plan.landingFraction : 0;
    }
    return true;
}
struct MovementReplaySteeringStart {
    bool available = false;
    StrafeInput input{};
    SteeringState state{};
    AirPhysics physics{};
    std::uint64_t directions = 0;
    std::size_t sliceCount = AirSteeringSliceCount;
};
inline std::size_t MovementAirSteeringSlices(float verticalVelocity, const MovementButtons& buttons) noexcept {
    // Avoid our artificial movement boundaries around possible ground contact
    // or a jump input (including manual jump with bunnyhop disabled).
    return verticalVelocity > 0 &&
        !((buttons.down | buttons.changed | buttons.third) & movement_offsets::Jump) ? AirSteeringSliceCount : 1;
}
struct MovementReplaySteeringResult {
    bool refreshed = false;
    MovementReplayPlan plan{};
    SteeringState state{};
    SteeringOutput steering{};
    SteeringCommand command{};
};
inline MovementReplaySteeringResult MovementPlanReplaySteering(const MovementReplaySteeringStart& start,
    const MovementReplayIntent& cachedIntent, const MovementReplayIntent& currentIntent,
    const MovementReplayPlan& plan) noexcept {
    MovementReplaySteeringResult result{}; result.plan = plan;
    if (!start.available || !MovementReplayIntentChanged(cachedIntent, currentIntent)) return result;
    result.refreshed = true; result.state = start.state;
    auto input = start.input;
    input.forward = currentIntent.forward; input.left = currentIntent.left; input.viewYaw = currentIntent.viewYaw;
    if (currentIntent.viewValid) {
        result.command = PlanAirSteeringCommand(result.state, input, start.physics, plan.jumpbug ? 1 : start.sliceCount);
        result.steering = result.command.summary;
    }
    else ResetAirSteering(result.state);
    result.plan = result.command.valid ? MovementRefreshReplaySteeringCommand(plan, result.command, start.directions) :
        MovementRefreshReplaySteering(plan,
        result.steering.valid ? result.steering.forward : currentIntent.forward,
        result.steering.valid ? result.steering.left : currentIntent.left, start.directions);
    return result;
}
inline std::uint64_t MovementReplayMask(const MovementReplayPlan& plan) noexcept {
    return ((plan.jump || plan.jumpbug) ? movement_offsets::Jump : 0) |
           (plan.jumpbug ? movement_offsets::Duck : 0) |
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
