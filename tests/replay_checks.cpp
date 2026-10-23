#include "movement_replay.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <limits>

namespace {
int failures = 0;
void Expect(bool value, const char* message) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", message); failures += !value;
}
MovementReplayContext Context() {
    MovementReplayContext context{};
    context.valid = context.held = context.bunnyhop = context.autostrafe = true;
    context.sequence = 123; context.commandTick = 300; context.grounded = true;
    context.owner = 45; context.simulationTick = 500;
    context.command = 0x10000; context.base = 0x20000;
    context.pawn = 0x30000; context.pawnIdentity = 0x40000; context.services = 0x50000; context.system = 0x60000;
    context.controller = 0x70000; context.controllerIdentity = 0x80000;
    context.pawnHandle = 17; context.controllerHandle = 18;
    return context;
}
// A model of the game-owned repeated container: addresses stand for pool
// entries. Reconciliation uses the same exact ownership decision as runtime.
void Reconcile(std::vector<MovementReplayStep>& messages, MovementReplayStep& owned,
               const MovementSubtick& desired, std::uintptr_t& allocator) {
    for (auto& message : messages) {
        if (MovementOwnsReplayStep(owned, message.address, message.value)) {
            const auto metadata = message.value;
            message.value = desired;
            message.value.vtable = metadata.vtable; message.value.arena = metadata.arena;
            message.value.cachedSize = metadata.cachedSize;
            owned = message;
            return;
        }
    }
    auto added = desired; added.vtable = 0x100; added.arena = 0x200;
    messages.push_back({++allocator, added}); owned = messages.back();
}
}
int main() {
    namespace off = movement_offsets;
    MovementReplayPlan release{};
    release.jump = release.jumpEdge = true; release.jumpDown = false;
    MovementEncodeJump(release.buttons, false, true);
    const auto originalRelease = release.buttons;
    bool everyReleaseRepaired = true, unrelatedPreserved = true;
    for (int frame = 0; frame < 10000; ++frame) {
        // Producer reconstructs a held physical Space each render frame. The
        // changed and third masks also carry unrelated attack/camera actions.
        const std::uint64_t action = frame & 1 ? 1 : 0x20;
        const MovementButtons rebuilt{off::Jump | action, action, off::Jump | 0x100};
        const auto repaired = MovementReplayButtons(rebuilt, release);
        everyReleaseRepaired &= !(repaired.down & off::Jump) && (repaired.changed & off::Jump) && !(repaired.third & off::Jump);
        unrelatedPreserved &= (repaired.down & ~off::Jump) == (rebuilt.down & ~off::Jump) &&
            (repaired.changed & ~off::Jump) == (rebuilt.changed & ~off::Jump) &&
            (repaired.third & ~off::Jump) == (rebuilt.third & ~off::Jump);
    }
    Expect(everyReleaseRepaired, "rebuilt held Space cannot overwrite a cached release edge across 10000 frames");
    Expect(unrelatedPreserved, "replay preserves every noncontrolled native action mask");
    Expect(release.buttons.down == originalRelease.down && release.buttons.changed == originalRelease.changed &&
           release.jumpEdge && !release.jumpDown, "replay never advances the cached release plan");
    auto press = release; press.jumpDown = true;
    MovementEncodeJump(press.buttons, true, false);
    const auto pressed = MovementReplayButtons({off::Jump, 0, off::Jump}, press);
    Expect((pressed.down & off::Jump) && (pressed.changed & off::Jump) && !(pressed.third & off::Jump),
           "rebuilt held input keeps a cached fresh press edge rather than becoming a held-only press");

    MovementReplayPlan strafe{}; strafe.strafe = true; strafe.forward = -0.3f; strafe.left = 0.7f;
    MovementEncodeStrafe(strafe.buttons, strafe.forward, strafe.left, off::Forward);
    const auto steered = MovementReplayButtons({off::Jump | off::Right | 1, off::Jump | 1, 1}, strafe);
    Expect((steered.down & off::Directions) == (off::Back | off::Left) && (steered.down & off::Jump) &&
           (steered.changed & off::Jump) && (steered.third & 1), "strafe replay repairs directional masks while preserving unmanaged jump/actions");
    auto both = strafe; both.jump = true; both.jumpDown = false;
    const auto combined = MovementReplayButtons({off::Jump | off::Right | 1, off::Jump, 0}, both);
    Expect(!(combined.down & off::Jump) && (combined.down & off::Directions) == (off::Back | off::Left) && (combined.down & 1),
           "independent jump and strafe plans combine only their controlled masks");

    std::vector<MovementReplayStep> messages;
    MovementReplayStep owned{}; std::uintptr_t allocator = 0x1000;
    const auto releaseEvent = MovementJumpTransition(false);
    Reconcile(messages, owned, releaseEvent, allocator);
    for (int i = 0; i < 10000; ++i) {
        messages.front().value.cachedSize = static_cast<std::uint32_t>(i);
        Reconcile(messages, owned, releaseEvent, allocator);
    }
    Expect(messages.size() == 1 && !messages.front().value.pressed && messages.front().value.button == off::Jump,
           "retained exact release event is reused without unbounded repeated-message growth");
    messages.clear(); // Producer rebuilt the repeated messages and lost our event.
    Reconcile(messages, owned, releaseEvent, allocator);
    for (int i = 0; i < 10000; ++i) Reconcile(messages, owned, releaseEvent, allocator);
    Expect(messages.size() == 1 && messages.front().address == owned.address,
           "missing release event is restored once and then retained on subsequent rebuilds");

    const auto oldAddress = messages.front().address;
    messages.front().value.presence = 0x67; messages.front().value.button = 1;
    messages.front().value.pressed = true; messages.front().value.pitchDelta = 1.5f; messages.front().value.yawDelta = -3;
    Expect(!MovementOwnsReplayStep(owned, oldAddress, messages.front().value),
           "a pooled pointer reused for mouse/attack input is no longer our owned event");
    MovementSanitizeSubtick(messages.front().value, true, false);
    Reconcile(messages, owned, releaseEvent, allocator);
    for (int i = 0; i < 10000; ++i) Reconcile(messages, owned, releaseEvent, allocator);
    Expect(messages.size() == 2 && messages.front().address == oldAddress && messages.front().value.button == 1 &&
           messages.front().value.pressed && messages.front().value.pitchDelta == 1.5f && messages.front().value.yawDelta == -3,
           "replaced pool ownership preserves physical camera/attack data and restores only one release event");

    messages.clear(); owned = {};
    Reconcile(messages, owned, MovementAnalogTransition(strafe.forward, strafe.left, 1, 0), allocator);
    const auto analogAddress = owned.address;
    // Prediction history advanced; desired axes stay fixed while the required
    // transition changes. Reusing the old delta would double-apply movement.
    const auto retargeted = MovementAnalogTransition(strafe.forward, strafe.left, -0.1f, 0.4f);
    Reconcile(messages, owned, retargeted, allocator);
    Expect(messages.size() == 1 && owned.address == analogAddress &&
           std::abs(-0.1f + messages.front().value.forwardDelta - strafe.forward) < 0.00001f &&
           std::abs(0.4f + messages.front().value.leftDelta - strafe.left) < 0.00001f,
           "same analog plan recomputes deltas from fresh historical axes in the retained owned event");
    auto altered = owned.value; altered.forwardDelta += 0.001f;
    Expect(!MovementOwnsReplayStep(owned, owned.address, altered), "pointer identity requires exact payload identity before replacement");
    Expect(!MovementOwnsReplayStep(owned, owned.address + 1, owned.value), "matching payload at another address does not grant ownership");

    const auto cached = Context(); auto current = cached;
    current.simulationTick += 1;
    Expect(MovementCanReplay(cached, current), "same source command can rebuild as predicted simulation advances");
    bool invalidated = true;
    for (int change = 0; change < 17; ++change) {
        current = cached;
        switch (change) {
        case 0: current.held = false; break;
        case 1: current.valid = false; break;
        case 2: current.bunnyhop = false; break;
        case 3: current.autostrafe = false; break;
        case 4: ++current.sequence; break;
        case 5: ++current.owner; break;
        case 6: ++current.command; break;
        case 7: ++current.base; break;
        case 8: ++current.pawn; break;
        case 9: ++current.pawnIdentity; break;
        case 10: ++current.services; break;
        case 11: ++current.system; break;
        case 12: ++current.controller; break;
        case 13: ++current.controllerIdentity; break;
        case 14: ++current.pawnHandle; break;
        case 15: ++current.controllerHandle; break;
        case 16: current.jumpbug = true; break;
        }
        invalidated &= !MovementCanReplay(cached, current);
    }
    Expect(invalidated, "Space loss, settings, sequence, owner and all source identity changes reject cached plans");
    auto pendingContext = cached;
    bool pressStable = true, releaseStable = true;
    for (int frame = 0; frame < 10000; ++frame) {
        current = pendingContext; current.grounded = (frame & 1) != 0;
        const auto stablePress = MovementReplayButtons({0, 0, 0}, press);
        pressStable &= MovementCanReplay(pendingContext, current) && (stablePress.down & off::Jump) &&
            (stablePress.changed & off::Jump);
        const auto stableRelease = MovementReplayButtons({off::Jump, 0, 0}, release);
        releaseStable &= MovementCanReplay(pendingContext, current) && !(stableRelease.down & off::Jump) &&
            (stableRelease.changed & off::Jump);
    }
    Expect(pressStable, "predicted takeoff and ground rewinds cannot replace the causative command press with a release");
    Expect(releaseStable, "predicted ground changes cannot invent a new press in an already planned release command");
    current = cached; ++current.commandTick; current.grounded = false;
    Expect(MovementCanReplay(cached, current),
           "command tick changes retain the selected input for the same sequence");
    current.simulationTick -= 3;
    Expect(MovementCanReplay(cached, current),
           "combined creation-tick correction and simulation rewind cannot replan the same command");
    bool correctedReleaseStable = true;
    for (int frame = 0; frame < 10000; ++frame) {
        current.commandTick = cached.commandTick + (frame % 5) - 2;
        current.simulationTick = cached.simulationTick + (frame % 7) - 3;
        current.grounded = (frame & 1) != 0;
        const auto repaired = MovementReplayButtons({off::Jump, off::Jump, off::Jump}, release);
        MovementSubtick event{}; bool active = false;
        correctedReleaseStable &= MovementCanReplay(cached, current) &&
            !(repaired.down & off::Jump) && (repaired.changed & off::Jump) &&
            MovementReplayTransition(release, MovementReplayJumpEventIndex, 0, 0, event, active) &&
            active && !event.pressed && event.button == off::Jump;
    }
    Expect(correctedReleaseStable,
        "10000 clock and ground corrections preserve both native release and serialized subtick edge");
    Expect(!MovementCanReplay({}, cached), "discarded cache cannot replay after release or lifecycle invalidation");

    namespace keys = movement_physical_keys;
    MovementPhysicalIntentState physical{};
    auto physicalW = MovementResolvePhysicalIntent(physical, keys::Forward);
    auto physicalWS = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Back);
    bool newestForwardStable = physicalW.forward == 1 && physicalWS.forward == -1;
    for (int frame = 0; frame < 10000; ++frame)
        newestForwardStable &= MovementResolvePhysicalIntent(physical, keys::Forward | keys::Back).forward == -1;
    Expect(newestForwardStable, "newly pressed S immediately wins over held W and survives repeated producer polls");
    const auto survivingW = MovementResolvePhysicalIntent(physical, keys::Forward);
    const auto repressedS = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Back);
    Expect(survivingW.forward == 1 && repressedS.forward == -1,
        "releasing the winning opposing key restores the survivor and a new press wins again");
    physical = {};
    MovementResolvePhysicalIntent(physical, keys::Back);
    const auto newestW = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Back);
    const auto sameAxesNewMask = MovementResolvePhysicalIntent(physical, keys::Forward);
    Expect(newestW.forward == 1 && sameAxesNewMask.forward == 1 &&
        MovementReplayIntentChanged(newestW, sameAxesNewMask),
        "physical press changes remain distinguishable when newest-key arbitration resolves to the same axes");
    physical = {};
    const auto simultaneous = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Back | keys::Left | keys::Right);
    bool simultaneousStable = !simultaneous.forward && !simultaneous.left;
    for (int frame = 0; frame < 10000; ++frame) {
        const auto heldTogether = MovementResolvePhysicalIntent(physical, simultaneous.heldMask);
        simultaneousStable &= !heldTogether.forward && !heldTogether.left;
    }
    Expect(simultaneousStable, "simultaneous first opposing presses are neutral and never invent a newest key on replay");
    auto survivingDiagonal = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Right);
    auto newLeft = MovementResolvePhysicalIntent(physical, keys::Forward | keys::Left | keys::Right);
    Expect(survivingDiagonal.forward == 1 && survivingDiagonal.left == -1 && newLeft.forward == 1 && newLeft.left == 1,
        "newest A/D arbitration is independent from the forward axis and preserves diagonals");
    MovementResolvePhysicalIntent(physical, 0);
    Expect(!physical.forward && !physical.left && !physical.heldMask,
        "all physical releases clear the retained newest-key axes");
    physical = {};
    const auto lifecycleReset = MovementResolvePhysicalIntent(physical, keys::Left | keys::Right);
    Expect(!lifecycleReset.left, "a lifecycle reset discards prior opposing-key priority");

    MovementReplaySteeringStart steeringStart{};
    steeringStart.available = true;
    steeringStart.input = {250, 0, 0, 1, 0, 123};
    steeringStart.physics = {250, 30, 12, 1, 1.0f / 64};
    steeringStart.state.turnSide = -1;
    steeringStart.directions = off::Forward;
    auto initialState = steeringStart.state;
    auto initialSteering = PlanAirSteeringCommand(initialState, steeringStart.input, steeringStart.physics);
    Expect(initialSteering.valid && initialSteering.count == AirSteeringSliceCount,
        "Velocity planner publishes every timed wish from the real command integration");
    const auto firstAirPlan = MovementRefreshReplaySteeringCommand(press, initialSteering, steeringStart.directions);
    MovementSubtick firstEvent{}; bool firstActive = false;
    Expect(firstAirPlan.analogCount == AirSteeringSliceCount && MovementReplayTransition(firstAirPlan, 0, -0.3f, 0.4f,
        firstEvent, firstActive) && firstActive && firstEvent.when == 0 &&
        std::abs(-0.3f + firstEvent.forwardDelta - initialSteering.slices[0].forward) < 0.00001f &&
        std::abs(0.4f + firstEvent.leftDelta - initialSteering.slices[0].left) < 0.00001f,
        "first timed wish replaces historical axes while preserving the separate jump event");
    auto initialAirPlan = press;
    initialAirPlan = MovementRefreshReplaySteeringCommand(initialAirPlan, initialSteering, steeringStart.directions);
    MovementReplayIntent initialIntent{1, 0, 0, true, keys::Forward};
    auto airContext = cached; airContext.grounded = false; airContext.intent = initialIntent;
    const auto sameJump = [](const MovementReplayPlan& first, const MovementReplayPlan& second) {
        return first.jump == second.jump && first.jumpEdge == second.jumpEdge && first.jumpDown == second.jumpDown &&
            !(((first.buttons.down ^ second.buttons.down) | (first.buttons.changed ^ second.buttons.changed) |
               (first.buttons.third ^ second.buttons.third)) & movement_offsets::Jump);
    };
    const auto near = [](float first, float second) { return std::abs(first - second) < 0.00001f; };
    MovementReplayIntent leftIntent{0, 1, 0, true, keys::Left};
    current = airContext; current.grounded = true; current.intent = leftIntent;
    auto retarget = MovementPlanReplaySteering(steeringStart, initialIntent, leftIntent, initialAirPlan);
    auto expectedState = steeringStart.state;
    auto expectedInput = steeringStart.input; expectedInput.forward = 0; expectedInput.left = 1;
    const auto expectedCommand = PlanAirSteeringCommand(expectedState, expectedInput, steeringStart.physics);
    const auto expectedSteering = expectedCommand.summary;
    Expect(MovementCanReplay(airContext, current) && retarget.refreshed && retarget.steering.valid &&
        near(retarget.plan.forward, expectedSteering.forward) && near(retarget.plan.left, expectedSteering.left) &&
        sameJump(initialAirPlan, retarget.plan),
        "W-to-A during a same-command ground rewind refreshes real steering while retaining the causative jump press");
    bool frozenRetarget = true, stableRetarget = true;
    for (int frame = 0; frame < 10000; ++frame) {
        current = airContext; current.grounded = (frame & 1) != 0; current.intent = leftIntent;
        retarget = MovementPlanReplaySteering(steeringStart, initialIntent, leftIntent, initialAirPlan);
        frozenRetarget &= MovementCanReplay(airContext, current) && sameJump(initialAirPlan, retarget.plan) &&
            near(retarget.steering.predictedVelocityX, expectedSteering.predictedVelocityX) &&
            near(retarget.steering.predictedVelocityY, expectedSteering.predictedVelocityY);
        const auto stable = MovementPlanReplaySteering(steeringStart, leftIntent, leftIntent, retarget.plan);
        stableRetarget &= !stable.refreshed && near(stable.plan.forward, retarget.plan.forward) &&
            near(stable.plan.left, retarget.plan.left) && sameJump(retarget.plan, stable.plan);
    }
    Expect(frozenRetarget && steeringStart.input.velocityX == 250 && !steeringStart.input.velocityY &&
        steeringStart.state.turnSide == -1 && steeringStart.physics.interval == 1.0f / 64,
        "10000 prediction rewinds retarget from immutable command-start velocity, side and physics");
    Expect(stableRetarget, "unchanged physical input reuses refreshed analogs without another solver/state step");
    auto turnedCamera = initialIntent; turnedCamera.viewYaw = 90;
    const auto cameraRefresh = MovementPlanReplaySteering(steeringStart, initialIntent, turnedCamera, initialAirPlan);
    expectedState = steeringStart.state; expectedInput = steeringStart.input; expectedInput.viewYaw = 90;
    const auto expectedCamera = PlanAirSteeringCommand(expectedState, expectedInput, steeringStart.physics).summary;
    Expect(cameraRefresh.refreshed && near(cameraRefresh.plan.forward, expectedCamera.forward) &&
        near(cameraRefresh.plan.left, expectedCamera.left) && sameJump(initialAirPlan, cameraRefresh.plan),
        "same-command camera turn immediately refreshes camera-relative axes with the original jump unchanged");
    auto wrappedCamera = initialIntent; wrappedCamera.viewYaw = 360;
    auto wrapStart = initialIntent; wrapStart.viewYaw = 179;
    auto wrapEnd = initialIntent; wrapEnd.viewYaw = -179;
    Expect(!MovementReplayIntentChanged(initialIntent, wrappedCamera) && MovementReplayIntentChanged(wrapStart, wrapEnd),
        "equivalent full-turn yaw does not refresh while a real turn across the yaw seam does");
    MovementReplayIntent releasedIntent{0, 0, 90, true, 0};
    const auto releaseRefresh = MovementPlanReplaySteering(steeringStart, turnedCamera, releasedIntent, cameraRefresh.plan);
    const auto physicalRelease = MovementReplayButtons({off::Jump | off::Forward | off::Left | 1,
        off::Jump | off::Forward, 0x100}, releaseRefresh.plan);
    const auto stopAssistance = MovementAnalogTransition(releaseRefresh.plan.forward, releaseRefresh.plan.left, -0.6f, 0.8f);
    Expect(releaseRefresh.refreshed && !releaseRefresh.steering.valid && releaseRefresh.plan.strafe &&
        !releaseRefresh.plan.forward && !releaseRefresh.plan.left && !(physicalRelease.down & off::Directions) &&
        sameJump(cameraRefresh.plan, releaseRefresh.plan) && (physicalRelease.down & 1) && physicalRelease.third == 0x100 &&
        near(-0.6f + stopAssistance.forwardDelta, 0) && near(0.8f + stopAssistance.leftDelta, 0),
        "airborne WASD release replaces historical assisted axes before integration without braking or losing jump/attack");
    messages.clear(); owned = {}; allocator = 0x1000;
    Reconcile(messages, owned, MovementAnalogTransition(cameraRefresh.plan.forward, cameraRefresh.plan.left, 1, 0), allocator);
    const auto releasedAddress = owned.address;
    bool releaseEventStable = true;
    for (int frame = 0; frame < 10000; ++frame) {
        const float historyForward = frame & 1 ? 0.25f : -0.6f, historyLeft = frame & 1 ? -0.5f : 0.8f;
        const auto event = MovementAnalogTransition(releaseRefresh.plan.forward, releaseRefresh.plan.left, historyForward, historyLeft);
        Reconcile(messages, owned, event, allocator);
        releaseEventStable &= messages.size() == 1 && owned.address == releasedAddress &&
            near(historyForward + messages.front().value.forwardDelta, 0) &&
            near(historyLeft + messages.front().value.leftDelta, 0);
    }
    Expect(releaseEventStable, "release refresh reuses one owned analog event with fresh history through 10000 rebuilds");
    auto releaseAirPlan = initialAirPlan;
    releaseAirPlan.jumpDown = false; MovementEncodeJump(releaseAirPlan.buttons, false, true);
    const auto retainedRelease = MovementPlanReplaySteering(steeringStart, initialIntent, leftIntent, releaseAirPlan);
    Expect(retainedRelease.refreshed && sameJump(releaseAirPlan, retainedRelease.plan) &&
        !(retainedRelease.plan.buttons.down & off::Jump) && (retainedRelease.plan.buttons.changed & off::Jump),
        "steering refresh also retains an already selected jump release edge exactly");
    auto unobservedView = leftIntent; unobservedView.viewValid = false;
    const auto fallbackAxes = MovementPlanReplaySteering(steeringStart, initialIntent, unobservedView, initialAirPlan);
    Expect(fallbackAxes.refreshed && !fallbackAxes.steering.valid && !fallbackAxes.plan.forward && fallbackAxes.plan.left == 1 &&
        sameJump(initialAirPlan, fallbackAxes.plan),
        "unavailable view returns physical axes rather than keeping stale assisted steering and still preserves the jump");
    auto groundStart = steeringStart; groundStart.available = false;
    const auto frozenGround = MovementPlanReplaySteering(groundStart, initialIntent, leftIntent, press);
    Expect(!frozenGround.refreshed && !frozenGround.plan.strafe && sameJump(press, frozenGround.plan),
        "predicting an originally grounded command airborne does not invent an air plan or replan its jump");
    auto previouslyReleased = press;
    const auto acquireAir = MovementPlanReplaySteering(steeringStart, releasedIntent, leftIntent, previouslyReleased);
    Expect(acquireAir.refreshed && acquireAir.steering.valid && acquireAir.plan.strafe && sameJump(press, acquireAir.plan),
        "a new WASD press can acquire air steering in a command that originally had no directional assistance");

    constexpr std::size_t ordinaryEventCount = AirSteeringSliceCount + 1;
    std::array<MovementSubtick, ordinaryEventCount> timedEvents{};
    std::array<bool, ordinaryEventCount> activeEvents{};
    bool timedTransitionsValid = initialAirPlan.analogCount == AirSteeringSliceCount;
    float effectiveForward = -0.3f, effectiveLeft = 0.4f;
    for (std::size_t index = 0; index < timedEvents.size(); ++index) {
        timedTransitionsValid &= MovementReplayTransition(initialAirPlan, index, -0.3f, 0.4f,
            timedEvents[index], activeEvents[index]);
        if (index < AirSteeringSliceCount) {
            effectiveForward += timedEvents[index].forwardDelta; effectiveLeft += timedEvents[index].leftDelta;
            timedTransitionsValid &= activeEvents[index] && !timedEvents[index].button && timedEvents[index].presence == 0x1c &&
                near(timedEvents[index].when, static_cast<float>(index) / AirSteeringSliceCount) &&
                near(effectiveForward, initialSteering.slices[index].forward) &&
                near(effectiveLeft, initialSteering.slices[index].left);
        }
    }
    Expect(timedTransitionsValid && near(effectiveForward, initialAirPlan.forward) &&
        near(effectiveLeft, initialAirPlan.left) && activeEvents[MovementReplayJumpEventIndex] &&
        timedEvents[MovementReplayJumpEventIndex].button == off::Jump && !timedEvents[MovementReplayJumpEventIndex].when,
        "all timed analog deltas reach every planned axis in order and keep the jump as a separate time-zero event");

    // Decode emitted analog events and independently integrate their actual
    // time fractions. The base axes arrive only after these slices execute.
    double integratedX = steeringStart.input.velocityX, integratedY = steeringStart.input.velocityY;
    double integratedImpulse = 0, integratedDuration = 0;
    effectiveForward = -0.3f; effectiveLeft = 0.4f;
    for (std::size_t index = 0; index < AirSteeringSliceCount; ++index) {
        effectiveForward += timedEvents[index].forwardDelta; effectiveLeft += timedEvents[index].leftDelta;
        const double endWhen = index + 1 < AirSteeringSliceCount ? timedEvents[index + 1].when : 1;
        const double duration = (endWhen - timedEvents[index].when) * steeringStart.physics.interval;
        const double magnitude = std::hypot(effectiveForward, effectiveLeft);
        const double yawRadians = steeringStart.input.viewYaw * 0.017453292519943295;
        const double dx = (effectiveForward * std::cos(yawRadians) - effectiveLeft * std::sin(yawRadians)) / magnitude;
        const double dy = (effectiveForward * std::sin(yawRadians) + effectiveLeft * std::cos(yawRadians)) / magnitude;
        const double wish = steeringStart.physics.wishSpeed * std::min(magnitude, 1.0);
        const double available = steeringStart.physics.acceleration * wish * steeringStart.physics.surfaceFriction * duration;
        const double impulse = std::min(available, std::max(0.0,
            std::min<double>(wish, steeringStart.physics.wishSpeedCap) - integratedX * dx - integratedY * dy));
        integratedX += impulse * dx; integratedY += impulse * dy;
        integratedImpulse += impulse; integratedDuration += duration;
    }
    const double wholeTickBudget = steeringStart.physics.acceleration * steeringStart.physics.wishSpeed *
        steeringStart.physics.surfaceFriction * steeringStart.physics.interval;
    Expect(std::abs(integratedDuration - steeringStart.physics.interval) < 0.000001 &&
        integratedImpulse <= wholeTickBudget + 0.0001 &&
        std::abs(integratedX - initialSteering.summary.predictedVelocityX) < 0.002 &&
        std::abs(integratedY - initialSteering.summary.predictedVelocityY) < 0.002 &&
        std::abs(integratedImpulse - initialSteering.summary.accelerationStep) < 0.002,
        "independent event integration matches real command prediction within one tick's acceleration budget");

    std::array<MovementSubtickOrderEntry, ordinaryEventCount + 6> unordered{};
    const std::array<float, 6> originalTimes{0, 0.25f, 0.5f, 0.5f, 0.93f, 1};
    for (std::size_t index = 0; index < originalTimes.size(); ++index)
        unordered[index] = {0x5000 + index, originalTimes[index], false};
    for (std::size_t index = 0; index < timedEvents.size(); ++index)
        unordered[index + originalTimes.size()] = {0x6000 + index, timedEvents[index].when, true};
    std::array<std::uintptr_t, unordered.size()> merged{};
    const bool mergedOk = MovementOrderSubticks(unordered, merged);
    bool originalOrderPreserved = true, allEventsOnce = true, monotone = true;
    std::size_t nextOriginal = 0;
    float lastTime = -1;
    for (const auto address : merged) {
        const auto entry = std::find_if(unordered.begin(), unordered.end(),
            [address](const auto& candidate) { return candidate.address == address; });
        allEventsOnce &= entry != unordered.end() && std::count(merged.begin(), merged.end(), address) == 1;
        if (entry == unordered.end()) continue;
        monotone &= entry->when >= lastTime; lastTime = entry->when;
        if (!entry->owned) originalOrderPreserved &= address == 0x5000 + nextOriginal++;
    }
    Expect(mergedOk && originalOrderPreserved && nextOriginal == originalTimes.size() && allEventsOnce && monotone &&
        merged[0] == 0x6000 && merged[1] == 0x6000 + MovementReplayJumpEventIndex && merged[2] == 0x5000,
        "timed steering interleaves with physical events while preserving every original/equal-time action and leading time-zero analog/jump");
    auto nonmonotone = unordered;
    nonmonotone[1].when = 0.75f; nonmonotone[2].when = 0.25f;
    merged.fill(0xABCDEF);
    const bool rejectedNonmonotone = !MovementOrderSubticks(nonmonotone, merged);
    Expect(rejectedNonmonotone && std::all_of(merged.begin(), merged.end(), [](auto address) { return address == 0xABCDEF; }),
        "nonmonotone original timings reject the entire ordering operation without modifying its output");
    auto invalidTime = unordered;
    invalidTime.back().when = std::numeric_limits<float>::quiet_NaN();
    Expect(!MovementOrderSubticks(invalidTime, merged) &&
        std::all_of(merged.begin(), merged.end(), [](auto address) { return address == 0xABCDEF; }),
        "invalid generated timing is rejected without a partially rewritten pointer stream");

    messages.clear(); allocator = 0x2000;
    std::array<MovementReplayStep, ordinaryEventCount> timedOwned{};
    bool everyTimedReplayStable = true;
    for (int frame = 0; frame < 10000; ++frame) {
        const float historyForward = frame & 1 ? 0.9f : -0.3f, historyLeft = frame & 1 ? -0.1f : 0.4f;
        effectiveForward = historyForward; effectiveLeft = historyLeft;
        for (std::size_t index = 0; index < timedOwned.size(); ++index) {
            MovementSubtick event{}; bool active = false;
            everyTimedReplayStable &= MovementReplayTransition(initialAirPlan, index, historyForward, historyLeft, event, active) && active;
            Reconcile(messages, timedOwned[index], event, allocator);
            if (index < AirSteeringSliceCount) {
                effectiveForward += event.forwardDelta; effectiveLeft += event.leftDelta;
                everyTimedReplayStable &= near(effectiveForward, initialSteering.slices[index].forward) &&
                    near(effectiveLeft, initialSteering.slices[index].left);
            }
        }
        everyTimedReplayStable &= messages.size() == ordinaryEventCount;
    }
    Expect(everyTimedReplayStable && allocator == 0x2000 + ordinaryEventCount,
        "10000 command rebuilds reuse all owned analog/jump events and refresh only the first historical delta");
    const auto priorTimedAddresses = timedOwned;
    messages.erase(std::remove_if(messages.begin(), messages.end(), [&](const auto& message) {
        return message.address == timedOwned[2].address || message.address == timedOwned[5].address;
    }), messages.end());
    for (std::size_t index = 0; index < timedOwned.size(); ++index) {
        MovementSubtick event{}; bool active = false;
        MovementReplayTransition(initialAirPlan, index, 0, 0, event, active);
        Reconcile(messages, timedOwned[index], event, allocator);
    }
    Expect(messages.size() == ordinaryEventCount && allocator == 0x2000 + ordinaryEventCount + 2 &&
        timedOwned[0].address == priorTimedAddresses[0].address && timedOwned[2].address != priorTimedAddresses[2].address &&
        timedOwned[5].address != priorTimedAddresses[5].address &&
        timedOwned[MovementReplayJumpEventIndex].address == priorTimedAddresses[MovementReplayJumpEventIndex].address,
        "losing two generated pool entries restores only those entries and leaves all retained analog/jump ownership intact");

    bool fullCommandRefresh = retarget.plan.analogCount == expectedCommand.count && sameJump(initialAirPlan, retarget.plan);
    for (std::size_t index = 0; index < expectedCommand.count; ++index)
        fullCommandRefresh &= near(retarget.plan.analogs[index].forward, expectedCommand.slices[index].forward) &&
            near(retarget.plan.analogs[index].left, expectedCommand.slices[index].left);
    Expect(fullCommandRefresh, "changed physical intent refreshes every timed wish from command start and retains the jump edge");
    bool retiredSlicesNeutral = releaseRefresh.plan.analogCount == 1;
    unsigned activeAfterRelease = 0;
    for (std::size_t index = 0; index < timedOwned.size(); ++index) {
        MovementSubtick event{}; bool active = false;
        retiredSlicesNeutral &= MovementReplayTransition(releaseRefresh.plan, index, -0.6f, 0.8f, event, active);
        activeAfterRelease += active;
        Reconcile(messages, timedOwned[index], event, allocator);
        if (index && index < AirSteeringSliceCount)
            retiredSlicesNeutral &= !active && event.presence == 4 && !event.button && !event.when &&
                !event.forwardDelta && !event.leftDelta;
    }
    Expect(retiredSlicesNeutral && activeAfterRelease == 2 && messages.size() == ordinaryEventCount &&
        sameJump(initialAirPlan, releaseRefresh.plan),
        "WASD release retains one time-zero analog handoff and the cached jump while neutralizing every later owned slice");
    const auto groundHandoffPlan = MovementRefreshReplaySteering(initialAirPlan, 1, 0, initialAirPlan.buttons.down);
    MovementSubtick groundHandoff{}; bool groundHandoffActive = false;
    Expect(MovementReplayTransition(groundHandoffPlan, 0, initialAirPlan.forward, initialAirPlan.left,
        groundHandoff, groundHandoffActive) && groundHandoffActive && groundHandoffPlan.analogCount == 1 &&
        near(initialAirPlan.forward + groundHandoff.forwardDelta, 1) &&
        near(initialAirPlan.left + groundHandoff.leftDelta, 0) && sameJump(initialAirPlan, groundHandoffPlan),
        "ground handoff uses one physical-axis transition instead of carrying timed airborne steering into ground movement");
    auto oversizedPlan = initialAirPlan; oversizedPlan.analogCount = AirSteeringSliceCount + 1;
    auto badSliceOrder = initialAirPlan; badSliceOrder.analogs[2].when = badSliceOrder.analogs[1].when;
    MovementSubtick untouchedEvent{}; untouchedEvent.button = 1; untouchedEvent.presence = 7;
    bool untouchedActive = false;
    Expect(!MovementReplayTransition(oversizedPlan, 0, 0, 0, untouchedEvent, untouchedActive) &&
        !MovementReplayAnalogTransition(badSliceOrder, 2, 0, 0, untouchedEvent) &&
        untouchedEvent.button == 1 && untouchedEvent.presence == 7,
        "oversized or nonincreasing analog plans fail validation without replacing an existing event payload");

    auto jumpbugPlan = initialAirPlan;
    jumpbugPlan.jumpbug = true; jumpbugPlan.landingFraction = 0.375f;
    jumpbugPlan.buttons.down = (jumpbugPlan.buttons.down | off::Jump) & ~off::Duck;
    jumpbugPlan.buttons.changed |= off::Jump | off::Duck;
    jumpbugPlan.buttons.third &= ~(off::Jump | off::Duck);
    MovementSubtick suppressedJump{}; bool suppressedActive = false;
    Expect(MovementReplayTransition(jumpbugPlan, MovementReplayJumpEventIndex, 0, 0,
        suppressedJump, suppressedActive) && suppressedActive && suppressedJump.presence == 7 &&
        suppressedJump.button == off::Jump && !suppressedJump.pressed && !suppressedJump.when &&
        MovementReplayMask(jumpbugPlan) == (off::Directions | off::Jump | off::Duck),
        "jumpbug explicitly releases inherited jump at zero before scheduling the landing press");
    auto duckOnlyPlan = jumpbugPlan; duckOnlyPlan.jump = duckOnlyPlan.strafe = false;
    Expect(MovementReplayMask(duckOnlyPlan) == (off::Jump | off::Duck),
        "jumpbug controls both buttons independently of bunnyhop and steering settings");
    const MovementButtons physicalActions{off::Duck | off::Right | 1, off::Duck | 1, off::Jump | off::Duck | 0x100};
    const auto jumpbugButtons = MovementReplayButtons(physicalActions, jumpbugPlan);
    Expect((jumpbugButtons.down & (off::Jump | off::Duck)) == off::Jump &&
        (jumpbugButtons.changed & (off::Jump | off::Duck)) == (off::Jump | off::Duck) &&
        !(jumpbugButtons.third & (off::Jump | off::Duck)) && (jumpbugButtons.down & 1) &&
        (jumpbugButtons.changed & 1) && (jumpbugButtons.third & 0x100),
        "jumpbug publishes its final jump-held duck-released masks and preserves unrelated native actions");

    std::array<MovementSubtick, 4> bugEvents{};
    bool exactBugTiming = true;
    for (std::size_t index = 0; index < bugEvents.size(); ++index) {
        bool active = false;
        exactBugTiming &= MovementReplayTransition(jumpbugPlan, MovementReplayJumpbugEventIndex + index,
            0, 0, bugEvents[index], active) && active && bugEvents[index].presence == 7 &&
            bugEvents[index].button == (index < 2 ? off::Duck : off::Jump) &&
            bugEvents[index].pressed == (index == 0 || index == 3) &&
            bugEvents[index].when == (index ? jumpbugPlan.landingFraction : 0);
    }
    Expect(exactBugTiming,
        "jumpbug serializes duck down at zero then duck up, jump up and jump down at the exact landing fraction");

    std::array<MovementSubtickOrderEntry, 7> bugUnordered{};
    bugUnordered[0] = {0x7100, 0, false};
    bugUnordered[1] = {0x7101, jumpbugPlan.landingFraction, false};
    bugUnordered[2] = {0x7102, jumpbugPlan.landingFraction, false};
    for (std::size_t index = 0; index < bugEvents.size(); ++index)
        bugUnordered[index + 3] = {0x7200 + index, bugEvents[index].when, true};
    std::array<std::uintptr_t, bugUnordered.size()> bugOrdered{};
    const std::array<std::uintptr_t, 7> expectedBugOrder{0x7200, 0x7100, 0x7201, 0x7202, 0x7203, 0x7101, 0x7102};
    Expect(MovementOrderSubticks(bugUnordered, bugOrdered) && bugOrdered == expectedBugOrder,
        "equal-time jumpbug release/press order is stable ahead of original camera and attack events");

    bool invalidBugRejected = true;
    for (const auto fraction : {0.0f, 1.0f, -0.001f, 1.001f,
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto invalidBugPlan = jumpbugPlan; invalidBugPlan.landingFraction = fraction;
        for (std::size_t index = 0; index < MovementReplayEventCount; ++index) {
            MovementSubtick saved{}; saved.presence = 0x67; saved.button = 1; saved.yawDelta = 2;
            bool savedActive = true;
            invalidBugRejected &= !MovementReplayTransition(invalidBugPlan, index, 0, 0, saved, savedActive) &&
                saved.presence == 0x67 && saved.button == 1 && saved.yawDelta == 2 && savedActive;
        }
    }
    Expect(invalidBugRejected,
        "invalid landing fractions reject every jumpbug event before touching existing payload or activity output");

    MovementSubtick mixedDuck{};
    mixedDuck.presence = 0x7f; mixedDuck.button = off::Duck | off::Jump | 1; mixedDuck.pressed = true;
    mixedDuck.when = 0.25f; mixedDuck.pitchDelta = 1.5f; mixedDuck.yawDelta = -2;
    mixedDuck.forwardDelta = 0.4f; mixedDuck.leftDelta = -0.2f;
    auto unmanagedDuck = mixedDuck;
    MovementSanitizeSubtick(unmanagedDuck, true, false);
    Expect(unmanagedDuck.button == (off::Duck | 1) && unmanagedDuck.pressed && unmanagedDuck.presence == 0x7f,
        "ordinary bunnyhop sanitization preserves unowned duck and other actions");
    MovementSanitizeSubtick(mixedDuck, true, false, true);
    Expect(mixedDuck.button == 1 && mixedDuck.pressed && mixedDuck.presence == 0x7f &&
        mixedDuck.when == 0.25f && mixedDuck.pitchDelta == 1.5f && mixedDuck.yawDelta == -2 &&
        mixedDuck.forwardDelta == 0.4f && mixedDuck.leftDelta == -0.2f,
        "jumpbug strips only owned jump/duck from mixed camera, attack and analog input");
    mixedDuck.button = off::Duck;
    MovementSanitizeSubtick(mixedDuck, false, false, true);
    Expect(!mixedDuck.button && !mixedDuck.pressed && mixedDuck.presence == 0x7c &&
        mixedDuck.pitchDelta == 1.5f && mixedDuck.yawDelta == -2 && mixedDuck.when == 0.25f,
        "duck-only button removal clears button/pressed presence while retaining timing and camera payload");

    const auto bugRetarget = MovementPlanReplaySteering(steeringStart, initialIntent, leftIntent, jumpbugPlan);
    const auto bugRelease = MovementPlanReplaySteering(steeringStart, leftIntent, releasedIntent, bugRetarget.plan);
    const auto bugRetained = MovementPlanReplaySteering(steeringStart, releasedIntent, releasedIntent, bugRelease.plan);
    Expect(bugRetarget.refreshed && bugRelease.refreshed && !bugRetained.refreshed &&
        bugRetarget.plan.jumpbug && bugRelease.plan.jumpbug && bugRetained.plan.jumpbug &&
        bugRetarget.plan.landingFraction == jumpbugPlan.landingFraction &&
        bugRelease.plan.landingFraction == jumpbugPlan.landingFraction &&
        bugRetained.plan.landingFraction == jumpbugPlan.landingFraction && sameJump(jumpbugPlan, bugRetained.plan) &&
        !((jumpbugPlan.buttons.down ^ bugRetained.plan.buttons.down) & off::Duck),
        "steering retarget, physical release and unchanged replay retain the selected jumpbug timing and button plan");
    Expect(bugRetarget.plan.analogCount == 1 && bugRetarget.command.count == 1 &&
        bugRetarget.plan.analogs[0].when == 0,
        "jumpbug retarget cannot insert analog boundaries after its timed press");
    messages.clear(); allocator = 0x8000;
    std::array<MovementReplayStep, 4> bugOwned{};
    auto bugContext = airContext; bugContext.jumpbug = true;
    bool stableBug = true;
    for (int frame = 0; frame < 10000; ++frame) {
        current = bugContext; current.grounded = (frame & 1) != 0;
        current.commandTick += frame % 3; current.simulationTick -= frame % 5;
        stableBug &= MovementCanReplay(bugContext, current);
        for (std::size_t index = 0; index < bugOwned.size(); ++index) {
            MovementSubtick event{}; bool active = false;
            stableBug &= MovementReplayTransition(bugRetained.plan, MovementReplayJumpbugEventIndex + index,
                frame & 1 ? -1.0f : 1.0f, 0, event, active) && active;
            Reconcile(messages, bugOwned[index], event, allocator);
            stableBug &= event.button == bugEvents[index].button && event.pressed == bugEvents[index].pressed &&
                event.when == bugEvents[index].when;
        }
    }
    Expect(stableBug && messages.size() == 4 && allocator == 0x8004,
        "10000 prediction rewinds retain exactly four jumpbug messages with unchanged landing timing");
    auto disabledBugContext = bugContext; disabledBugContext.jumpbug = false;
    Expect(!MovementCanReplay(bugContext, disabledBugContext),
        "toggling jumpbug invalidates cached timing even if every command and pawn identity remains unchanged");
    bool retiredBugNeutral = true;
    for (std::size_t index = 0; index < bugOwned.size(); ++index) {
        MovementSubtick event{}; bool active = true;
        retiredBugNeutral &= MovementReplayTransition(initialAirPlan, MovementReplayJumpbugEventIndex + index,
            0, 0, event, active) && !active && event.presence == 4 && !event.when && !event.button;
        Reconcile(messages, bugOwned[index], event, allocator);
    }
    Expect(retiredBugNeutral && messages.size() == 4 && allocator == 0x8004,
        "retired jumpbug slots become neutral in their retained pool entries without allocating new messages");
    Expect(MovementAirSteeringSlices(-256, {}) == 1 && MovementAirSteeringSlices(0, {}) == 1 &&
        MovementAirSteeringSlices(256, {}) == AirSteeringSliceCount &&
        MovementAirSteeringSlices(256, {off::Jump, 0, 0}) == 1 &&
        MovementAirSteeringSlices(256, {0, off::Jump, 0}) == 1 &&
        MovementAirSteeringSlices(256, {0, 0, off::Jump}) == 1,
        "descending, apex and manual or assisted jump inputs suppress artificial intermediate movement slices");
    auto descentStart = steeringStart; descentStart.sliceCount = 1;
    const auto descentRetarget = MovementPlanReplaySteering(descentStart, initialIntent, leftIntent, initialAirPlan);
    Expect(descentRetarget.refreshed && descentRetarget.command.count == 1 && descentRetarget.plan.analogCount == 1 &&
        descentRetarget.plan.analogs[0].when == 0 && sameJump(initialAirPlan, descentRetarget.plan),
        "changed WASD keeps the frozen one-slice landing policy and the original jump edge on replay");
    return failures ? 1 : 0;
}
