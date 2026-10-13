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
    return failures ? 1 : 0;
}
