#include "movement_jump.h"
#include <algorithm>
#include <cmath>

void ResetJump(JumpState& state) noexcept { state = {}; }
JumpOutput PlanJump(JumpState& state, const JumpInput& in) noexcept {
    JumpOutput out{};
    if (in.commandTick < 0 || !std::isfinite(in.interval) || in.interval <= 0 || in.interval > 0.1f ||
        !std::isfinite(in.penalty) || in.penalty < 0 || !std::isfinite(in.bufferWindow) || in.bufferWindow < 0) return out;
    if (state.haveClock && in.simulationTick < state.lastSimulationTick) ResetJump(state);
    state.haveClock = true; state.lastSimulationTick = in.simulationTick;
    const bool previous = state.down;
    if (in.autoBunnyhop) {
        state.down = true; state.phase = JumpPhase::Pending;
    } else if (!in.grounded) {
        ResetJump(state);
    } else if (state.phase == JumpPhase::Ready) {
        state.phase = JumpPhase::Pending; state.down = true;
        state.requestTick = in.simulationTick; state.baselineActual = in.actual;
    } else if (state.phase == JumpPhase::Pending) {
        const bool pressConsumed = in.modern && (in.actual.tick != state.baselineActual.tick ||
            in.actual.fraction != state.baselineActual.fraction);
        out.acknowledged = pressConsumed || in.engineDown;
        // Command callbacks are not simulation steps. Retain the request across
        // duplicate ticks while a bounded simulation grace interval allows
        // consumption/takeoff. Failed attempts get an acknowledged release
        // before another fresh edge, with a full quantum of cooldown margin.
        const double elapsed = static_cast<double>(in.simulationTick - state.requestTick) * in.interval;
        const double grace = std::max(static_cast<double>(in.penalty), static_cast<double>(in.bufferWindow)) + in.interval;
        // A usable stamp is not an indefinite acknowledgement: the engine can
        // retain it after an obstructed attempt. Even an undelivered request is
        // retried once real predicted simulation has advanced beyond grace.
        if (elapsed > grace) {
            state.phase = JumpPhase::Releasing; state.down = false; state.releaseTick = in.simulationTick;
        }
    } else {
        const double releasedFor = static_cast<double>(in.simulationTick - state.releaseTick) * in.interval;
        const double sincePress = static_cast<double>(in.simulationTick - state.requestTick) * in.interval;
        if (!in.engineDown && releasedFor >= in.interval && sincePress > in.penalty + in.interval) {
            state.phase = JumpPhase::Pending; state.down = true;
            state.requestTick = in.simulationTick; state.baselineActual = in.actual;
        }
    }
    out.valid = true; out.down = state.down;
    out.fresh = state.down && !previous; out.released = previous && !state.down; out.phase = state.phase;
    return out;
}
