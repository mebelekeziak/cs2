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
    Expect(out.fresh && state.requestTick == 100, "predicted clock rollback clears pending timing baseline");
    return failures ? 1 : 0;
}
