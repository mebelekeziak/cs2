#include "movement_jump.h"
#include <cstdio>
namespace {
int failures = 0;
void Expect(bool value, const char* text) { printf("%s %s\n", value ? "PASS" : "FAIL", text); failures += !value; }
}
int main() {
    JumpState state{};
    JumpInput input{};
    input.grounded = true; input.modern = true; input.commandTick = 100;
    input.simulationTick = 2000;
    input.interval = 1.0f / 64; input.penalty = input.interval; input.bufferWindow = input.interval / 2;
    auto out = PlanJump(state, input);
    Expect(out.valid && out.fresh && out.down, "landing begins one persistent fresh request");
    for (int i = 0; i < 12; ++i) out = PlanJump(state, input);
    Expect(out.down && !out.fresh && state.requestTick == 2000, "repeated command callbacks in one tick cannot cancel or re-press");
    input.commandTick = 110;
    out = PlanJump(state, input);
    Expect(out.down && !out.fresh, "unconsumed request stays pending despite command clock progress");
    input.engineDown = true; input.actual = {2000, 0.6f}; input.usable = {2000, 0.6f};
    out = PlanJump(state, input);
    Expect(out.down && out.acknowledged && !out.released, "engine consumption uses stamp identity without equating clock domains");
    input.simulationTick = 2003;
    out = PlanJump(state, input);
    Expect(out.released && !out.down && state.phase == JumpPhase::Releasing, "retained usable stamp cannot stall failed takeoff indefinitely");
    input.commandTick = 111;
    input.simulationTick = 2004;
    out = PlanJump(state, input);
    Expect(!out.down, "release waits for actual engine input release acknowledgement");
    input.engineDown = false;
    out = PlanJump(state, input);
    Expect(out.fresh && out.down, "retry follows acknowledged release and cooldown margin");
    input.grounded = false;
    out = PlanJump(state, input);
    Expect(out.released && !out.down && state.phase == JumpPhase::Ready, "observed takeoff releases and rearms next landing");
    input.grounded = true; input.commandTick = 170;
    out = PlanJump(state, input);
    Expect(out.fresh, "next landing gets a fresh request");
    input.autoBunnyhop = true; input.commandTick = 180;
    out = PlanJump(state, input);
    Expect(out.down && !out.fresh, "current server automatic mode retains native held input");
    input.grounded = false;
    out = PlanJump(state, input);
    Expect(out.down && !out.released, "server automatic mode keeps held input throughout flight and landing");
    input.interval = 0;
    Expect(!PlanJump(state, input).valid, "invalid timing leaves planner inactive");
    ResetJump(state);
    input.interval = 1.0f / 64; input.autoBunnyhop = false; input.grounded = true;
    input.engineDown = false; input.simulationTick = 900;
    PlanJump(state, input);
    input.simulationTick = 903;
    out = PlanJump(state, input);
    Expect(out.released, "unacknowledged request retries after observed simulation advances");
    input.simulationTick = 100;
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh && state.phase == JumpPhase::Releasing && state.releaseTick == 100,
        "predicted clock rollback preserves emitted release and restarts its wait");
    for (int i = 0; i < 12; ++i) { ++input.commandTick; out = PlanJump(state, input); }
    Expect(!out.down && !out.fresh, "duplicate corrected simulation callbacks cannot complete the release wait");
    input.simulationTick = 102;
    out = PlanJump(state, input);
    Expect(!out.down, "corrected release retains the conservative unobserved cooldown margin");
    input.simulationTick = 103;
    out = PlanJump(state, input);
    Expect(out.fresh, "corrected release can retry after simulation progresses beyond its restarted cooldown");

    ResetJump(state); input = {};
    input.grounded = true; input.modern = true; input.commandTick = 100;
    input.simulationTick = 5000; input.interval = 1.0f / 64;
    input.penalty = input.interval; input.bufferWindow = input.interval / 2;
    input.actual = {700, 0.5f};
    PlanJump(state, input);
    input.engineDown = true; input.simulationTick = 5001; input.commandTick = 101;
    out = PlanJump(state, input);
    Expect(out.down && !out.released && !state.haveConsumption,
        "raw engine-down feedback alone cannot classify a modern request as processed");
    input.actual = {701, 0.5f};
    out = PlanJump(state, input);
    Expect(out.down && !out.released && out.acknowledged && state.consumptionTick == 5001,
        "observing a processed press alone cannot establish failed takeoff");
    for (int i = 0; i < 12; ++i) { ++input.commandTick; out = PlanJump(state, input); }
    Expect(out.down && !out.fresh && state.consumptionTick == 5001,
        "new command sequences within a simulation tick cannot bypass observed-press release timing");
    input.engineDown = false; input.simulationTick = 5002;
    out = PlanJump(state, input);
    Expect(out.released && !out.down,
        "one subsequent grounded simulation tick without a buffer signal begins an early release");
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh, "release and re-press cannot occur within the same simulation tick");
    input.simulationTick = 5003; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(!out.down, "elapsed cooldown never bypasses actual engine release acknowledgement");
    input.engineDown = false;
    out = PlanJump(state, input);
    Expect(out.fresh && out.down && !state.haveConsumption && !state.usableBuffered,
        "retry uses strict observed-consumption cooldown and starts new stamp baselines");

    ResetJump(state); input.simulationTick = 6000; input.actual = {20, 0.1f}; input.usable = {};
    PlanJump(state, input);
    input.actual = {21, 0.1f}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.down && !out.released && state.haveConsumption,
        "even observed press cannot release before simulation advances beyond the request");
    input.commandTick += 100;
    out = PlanJump(state, input);
    Expect(out.down && !out.released, "command clock changes do not replace real simulation progress");
    input.simulationTick = 6001;
    out = PlanJump(state, input);
    Expect(out.released, "first subsequent grounded simulation tick releases processed input lacking observed takeoff");

    ResetJump(state); input.simulationTick = 7000; input.actual = {40, 0.2f}; input.usable = {40, 0.2f};
    input.engineDown = false; PlanJump(state, input);
    input.simulationTick = 7001; input.actual = {41, 0.2f}; input.usable = {41, 0.2f}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.down && !out.released && state.usableBuffered,
        "advancing usable stamp keeps buffered modern input pending for takeoff");
    input.simulationTick = 7002; input.usable = {};
    out = PlanJump(state, input);
    Expect(out.down && !out.released && state.usableBuffered,
        "later usable-stamp clearing preserves the earlier buffer signal through its grace period");
    input.simulationTick = 7003;
    out = PlanJump(state, input);
    Expect(out.released, "buffered but obstructed request still has bounded grace");

    ResetJump(state); input.simulationTick = 8000; input.actual = {50, 0.2f}; input.usable = {50, 0.2f};
    input.engineDown = false; PlanJump(state, input);
    input.simulationTick = 8001; input.actual = {51, 0.2f}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.down && !state.usableBuffered,
        "retained earlier usable stamp is not a buffer signal for a newly processed press");
    input.simulationTick = 8002;
    out = PlanJump(state, input);
    Expect(out.released, "retained earlier buffer cannot prevent a bounded retry when takeoff remains unobserved");

    ResetJump(state); input.simulationTick = 9000; input.actual = {60, 0.2f}; input.usable = {};
    input.engineDown = false; PlanJump(state, input);
    input.simulationTick = 9001; input.actual = {60, 0.3f}; input.usable = {60, 0.3f}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.down && state.usableBuffered, "fractional advance within an engine tick acknowledges a usable buffer signal");
    input.simulationTick = 9002; input.grounded = false;
    out = PlanJump(state, input);
    Expect(out.released && !state.haveConsumption && !state.usableBuffered,
        "takeoff clears observed-consumption and usable buffer state");

    ResetJump(state); input.grounded = true; input.simulationTick = 10000; input.actual = {90, 0.4f};
    input.usable = {}; input.engineDown = false; PlanJump(state, input);
    input.simulationTick = 10001; input.actual = {89, 0.9f};
    out = PlanJump(state, input);
    Expect(out.down && !state.haveConsumption,
        "engine stamp rollback alone cannot masquerade as a newly consumed request");
    input.simulationTick = 10002; input.actual = {-1024, 0};
    out = PlanJump(state, input);
    Expect(out.down && !state.haveConsumption, "invalidated actual stamp retains unacknowledged grace");
    input.simulationTick = 10003;
    out = PlanJump(state, input);
    Expect(out.released, "invalid or unacknowledged input still retries after bounded grace");

    ResetJump(state); input.simulationTick = 11000; input.actual = {900, 0.4f}; input.usable = {};
    input.engineDown = false; input.penalty = 2 * input.interval;
    PlanJump(state, input);
    input.simulationTick = 11001; input.actual = {901, 0.4f}; input.engineDown = true;
    PlanJump(state, input);
    input.simulationTick = 11002;
    out = PlanJump(state, input);
    Expect(out.released, "long cooldown does not delay releasing a processed request lacking observed takeoff");
    input.engineDown = false; input.simulationTick = 11003;
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh, "cooldown equality blocks re-press after a full acknowledged release tick");
    input.simulationTick = 11004;
    out = PlanJump(state, input);
    Expect(out.fresh && out.down,
        "strict observed-consumption cooldown requires no unconditional extra tick once elapsed exceeds penalty");
    input.simulationTick = 11005; input.actual = {902, 0.4f}; input.engineDown = true;
    PlanJump(state, input);
    input.simulationTick = 400;
    out = PlanJump(state, input);
    Expect(out.down && !out.fresh && !state.haveConsumption && !state.usableBuffered && state.requestTick == 400,
        "pending prediction rollback discards branch processing proof without emitting a fresh press");
    for (int i = 0; i < 12; ++i) { ++input.commandTick; out = PlanJump(state, input); }
    Expect(out.down && !out.fresh && !out.released,
        "duplicate corrected pending callbacks cannot consume restarted grace");
    input.simulationTick = 403;
    out = PlanJump(state, input);
    Expect(out.down && !state.haveConsumption, "discarded branch consumption cannot trigger an early release after rewind");
    input.simulationTick = 404;
    out = PlanJump(state, input);
    Expect(out.released, "pending corrected request still retries after bounded simulation grace");

    ResetJump(state); input.simulationTick = 47778; input.commandTick = 47775;
    input.penalty = input.interval; input.engineDown = false;
    input.actual = {47738, 0.8125f}; input.usable = {};
    PlanJump(state, input);
    input.simulationTick = 47781; input.commandTick = 47779;
    input.actual = {47778, 0.25f}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.released && state.phase == JumpPhase::Releasing,
        "sequence 4728 trace begins a release after a grounded failed attempt");
    input.simulationTick = 47780; input.commandTick = 47780;
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh && state.phase == JumpPhase::Releasing,
        "sequence 4729 prediction rewind cannot re-press while engine input remains down");
    input.simulationTick = 47784;
    out = PlanJump(state, input);
    Expect(!out.down, "corrected elapsed cooldown never bypasses engine release acknowledgement");
    input.engineDown = false;
    out = PlanJump(state, input);
    Expect(out.fresh, "corrected trace retries once both simulation cooldown and engine release are satisfied");

    ResetJump(state); input.simulationTick = 12000; input.modern = false; input.actual = {1000, 0};
    input.engineDown = false; PlanJump(state, input);
    input.simulationTick = 12001; input.actual = {1001, 0}; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(out.down && !state.haveConsumption, "legacy mode does not interpret modern stamp changes as processing feedback");
    RecordTimedJump(state, 562);
    Expect(state.down && state.timedPress, "timed jumpbug retains the final published jump-down ownership");
    // Captured failure: tick 563 inherited fresh jump on every analog slice
    // because jumpbug had ResetJump() while its outgoing button was still down.
    input = {}; input.simulationTick = 563; input.commandTick = 563;
    input.interval = input.penalty = 1.0f / 64; input.grounded = false;
    out = PlanJump(state, input);
    Expect(out.valid && out.released && !out.down && !out.fresh && !state.timedPress,
        "captured 562-to-563 jumpbug path emits a release instead of silently retaining native fresh input");
    Expect(!ReleaseTimedJump(state, 563).valid, "timed press cleanup is consumed once, replay retains its emitted event");
    RecordTimedJump(state, 900);
    out = ReleaseTimedJump(state, 901);
    Expect(out.valid && out.released && !out.down,
        "jumpbug alone can release without bunnyhop rules or a valid physics interval");
    RecordTimedJump(state, 900);
    out = ReleaseTimedJump(state, 850);
    Expect(out.released && !out.down && state.requestTick == 850 && state.releaseTick == 850,
        "new-command prediction rewind cannot discard a timed press release obligation");
    input.simulationTick = 850; input.grounded = true; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh, "ground after timed jump waits for release acknowledgement before retry");
    input.simulationTick = 854; input.engineDown = false;
    out = PlanJump(state, input);
    Expect(out.fresh && out.down, "acknowledged timed jump release permits a bounded ordinary retry");
    RecordTimedJump(state, 1000); input.simulationTick = 1001; input.autoBunnyhop = true;
    out = PlanJump(state, input);
    Expect(out.released && !out.down, "server auto-bhop mode cannot preserve a fresh timed press across steering slices");

    // Captured revision 12: timed request 1645/.546875, release 1646,
    // landed 1646/.468750, first grounded observation 1647. Waiting until
    // 1648 spends an unnecessary full command on the ground (330 -> 156).
    input = {}; input.modern = true; input.commandTick = 1646;
    input.interval = input.penalty = 1.0f / 64; input.bufferWindow = input.interval / 2;
    RecordTimedJump(state, 1645);
    input.simulationTick = 1646;
    out = PlanJump(state, input);
    Expect(out.released && state.timedRequest, "timed cleanup retains the exclusive end-of-request-tick bound");
    input.grounded = true; input.engineDown = false;
    out = PlanJump(state, input);
    Expect(!out.down && !out.fresh, "timed recovery cannot retry in the release tick");
    input.simulationTick = 1647; input.engineDown = true;
    out = PlanJump(state, input);
    Expect(!out.down, "timed recovery still requires observed engine release");
    input.engineDown = false;
    out = PlanJump(state, input);
    Expect(out.fresh && out.down && !state.timedRequest,
        "captured landing retries at 1647 after release and full command uncertainty plus cooldown");
    out = PlanJump(state, input);
    Expect(out.down && !out.fresh, "timed fallback publishes only one fresh ordinary retry");

    RecordTimedJump(state, 3000); ReleaseTimedJump(state, 3001);
    input.simulationTick = 3002; input.penalty = 2 * input.interval;
    out = PlanJump(state, input);
    Expect(!out.down, "long timed penalty keeps recovery behind the latest possible subtick press");
    input.simulationTick = 3003;
    out = PlanJump(state, input);
    Expect(out.fresh, "long timed penalty does not add a second margin tick at the conservative bound");

    RecordTimedJump(state, 4000); ReleaseTimedJump(state, 4001);
    input.penalty = input.interval; input.simulationTick = 100;
    out = PlanJump(state, input);
    Expect(!out.down && !state.timedRequest, "rewind invalidates the timed request clock bound");
    input.simulationTick = 102;
    out = PlanJump(state, input);
    Expect(!out.down, "corrected timed recovery retains the ordinary strict uncertainty margin");
    input.simulationTick = 103;
    Expect(PlanJump(state, input).fresh, "corrected timed recovery remains bounded after sufficient clock progress");
    return failures ? 1 : 0;
}
