#include "movement_replay.h"
#include <cmath>
#include <cstdio>
#include <vector>

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
    for (int change = 0; change < 18; ++change) {
        current = cached;
        switch (change) {
        case 0: current.held = false; break;
        case 1: current.valid = false; break;
        case 2: current.bunnyhop = false; break;
        case 3: current.autostrafe = false; break;
        case 4: ++current.sequence; break;
        case 5: ++current.owner; break;
        case 6: --current.simulationTick; break;
        case 7: ++current.command; break;
        case 8: ++current.base; break;
        case 9: ++current.pawn; break;
        case 10: ++current.pawnIdentity; break;
        case 11: ++current.services; break;
        case 12: ++current.system; break;
        case 13: ++current.controller; break;
        case 14: ++current.controllerIdentity; break;
        case 15: ++current.pawnHandle; break;
        case 16: ++current.controllerHandle; break;
        case 17: ++current.commandTick; break;
        }
        invalidated &= !MovementCanReplay(cached, current);
    }
    Expect(invalidated, "Space loss, settings, rollback, command tick, owner and all source identity changes reject cached plans");
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
    Expect(MovementReplaySameSource(cached, current),
           "command tick progress can request another plan while preserving its pending state and owned messages");
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
    const auto initialSteering = PlanAirSteering(initialState, steeringStart.input, steeringStart.physics);
    auto initialAirPlan = press;
    initialAirPlan = MovementRefreshReplaySteering(initialAirPlan, initialSteering.forward,
        initialSteering.left, steeringStart.directions);
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
    const auto expectedSteering = PlanAirSteering(expectedState, expectedInput, steeringStart.physics);
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
    const auto expectedCamera = PlanAirSteering(expectedState, expectedInput, steeringStart.physics);
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
    return failures ? 1 : 0;
}
