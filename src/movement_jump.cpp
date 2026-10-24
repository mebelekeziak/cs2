#include "movement_jump.h"
#include <algorithm>
#include <cmath>

namespace {
bool ValidStamp(const JumpStamp& stamp) noexcept {
    return stamp.tick >= 0 && std::isfinite(stamp.fraction) && stamp.fraction >= 0 && stamp.fraction < 1;
}
bool AdvancedStamp(const JumpStamp& stamp, const JumpStamp& baseline) noexcept {
    return ValidStamp(stamp) && (!ValidStamp(baseline) || stamp.tick > baseline.tick ||
        (stamp.tick == baseline.tick && stamp.fraction > baseline.fraction));
}
void BeginRequest(JumpState& state, const JumpInput& in) noexcept {
    state.phase = JumpPhase::Pending; state.down = true;
    state.timedRequest = false;
    state.requestTick = in.simulationTick; state.baselineActual = in.actual; state.baselineUsable = in.usable;
    state.haveConsumption = false; state.usableBuffered = false;
}
}

void ResetJump(JumpState& state) noexcept { state = {}; }
bool TimedJumpAttemptReady(JumpAttemptClock& clock, std::uint32_t simulationTick,
                          float interval, float penalty) noexcept {
    if (!std::isfinite(interval) || interval <= 0 || interval > 0.1f ||
        !std::isfinite(penalty) || penalty < 0 || penalty >= 10) return false;
    if (clock.haveClock && simulationTick < clock.lastTick && clock.haveRequest)
        clock.requestTick = simulationTick;
    clock.haveClock = true; clock.lastTick = simulationTick;
    // A previous timed press can be anywhere strictly inside requestTick.
    // Include that whole interval before comparing with the native cooldown.
    return !clock.haveRequest || static_cast<double>(simulationTick - clock.requestTick) * interval >=
        static_cast<double>(penalty) + interval;
}
void RecordJumpAttempt(JumpAttemptClock& clock, std::uint32_t simulationTick) noexcept {
    clock.haveClock = clock.haveRequest = true;
    clock.requestTick = clock.lastTick = simulationTick;
}
void RecordTimedJump(JumpState& state, std::uint32_t simulationTick) noexcept {
    ResetJump(state);
    state.down = state.timedPress = state.timedRequest = state.haveClock = true;
    state.phase = JumpPhase::Pending;
    state.requestTick = state.lastSimulationTick = simulationTick;
}
JumpOutput ReleaseTimedJump(JumpState& state, std::uint32_t simulationTick) noexcept {
    if (!state.timedPress) return {};
    if (simulationTick < state.lastSimulationTick) {
        state.requestTick = simulationTick;
        state.timedRequest = false;
    }
    state.timedPress = state.down = false;
    state.phase = JumpPhase::Releasing;
    state.releaseTick = state.lastSimulationTick = simulationTick;
    JumpOutput out{};
    out.valid = out.released = true;
    out.phase = state.phase;
    return out;
}
JumpOutput PlanJump(JumpState& state, const JumpInput& in) noexcept {
    JumpOutput out{};
    if (in.commandTick < 0 || !std::isfinite(in.interval) || in.interval <= 0 || in.interval > 0.1f ||
        !std::isfinite(in.penalty) || in.penalty < 0 || !std::isfinite(in.bufferWindow) || in.bufferWindow < 0) return out;
    if (state.timedPress) return ReleaseTimedJump(state, in.simulationTick);
    if (state.haveClock && in.simulationTick < state.lastSimulationTick) {
        // Prediction can rewind without undoing the input already emitted.
        // Keep its press/release phase, including the release acknowledgement
        // barrier. Restart waits in the corrected simulation clock instead of
        // counting replayed ticks or subtracting across the correction.
        state.requestTick = state.releaseTick = state.consumptionTick = in.simulationTick;
        // Processing/buffer observations from the discarded prediction branch
        // do not prove processing in this branch. Establish fresh baselines
        // without manufacturing a new press.
        state.haveConsumption = false; state.usableBuffered = false;
        state.timedRequest = false;
        state.baselineActual = in.actual; state.baselineUsable = in.usable;
    }
    state.haveClock = true; state.lastSimulationTick = in.simulationTick;
    const bool previous = state.down;
    if (in.autoBunnyhop) {
        state.down = true; state.phase = JumpPhase::Pending;
    } else if (!in.grounded) {
        ResetJump(state);
    } else if (state.phase == JumpPhase::Ready) {
        BeginRequest(state, in);
    } else if (state.phase == JumpPhase::Pending) {
        const bool pressConsumed = in.modern && AdvancedStamp(in.actual, state.baselineActual);
        out.acknowledged = pressConsumed || in.engineDown;
        if (pressConsumed && !state.haveConsumption) {
            // Observing consumption occurs at or after the engine processed
            // the press. This simulation clock is only compared with itself;
            // actual/usable stamps remain in their own engine clock domain.
            state.haveConsumption = true; state.consumptionTick = in.simulationTick;
        }
        if (in.modern && AdvancedStamp(in.usable, state.baselineUsable)) state.usableBuffered = true;
        // Command callbacks are not simulation steps. Retain the request across
        // duplicate ticks. Usable stamps can clear on successful jumps and
        // actual stamps alone prove processing, not takeoff. If another real
        // simulation tick after observing the press still finds us grounded,
        // with no advancing buffer signal, begin a release/retry instead of
        // waiting the entire grace period. This also bounds obstructed input.
        const double elapsed = static_cast<double>(in.simulationTick - state.requestTick) * in.interval;
        const double grace = std::max(static_cast<double>(in.penalty), static_cast<double>(in.bufferWindow)) + in.interval;
        // A usable stamp is not an indefinite acknowledgement: the engine can
        // retain it after an obstructed attempt. Even an undelivered request is
        // retried once real predicted simulation has advanced beyond grace.
        const bool missingTakeoff = state.haveConsumption && !state.usableBuffered &&
            in.simulationTick > state.consumptionTick;
        if (missingTakeoff || elapsed > grace) {
            state.phase = JumpPhase::Releasing; state.down = false; state.releaseTick = in.simulationTick;
        }
    } else {
        const double releasedFor = static_cast<double>(in.simulationTick - state.releaseTick) * in.interval;
        const auto cooldownStart = state.haveConsumption ? state.consumptionTick : state.requestTick;
        const double sincePress = static_cast<double>(in.simulationTick - cooldownStart) * in.interval;
        // Modern consumption has a conservative observed start, so another
        // arbitrary tick of cooldown margin is unnecessary. Without that
        // observation retain the earlier request-time safety margin.
        const double cooldown = static_cast<double>(in.penalty) + (state.haveConsumption ? 0 : in.interval);
        // A timed command's last possible press lies strictly before the end
        // of its request tick. The full interval above already covers that
        // uncertainty: equality with penalty + interval is sufficient here.
        // Using > adds an entire grounded tick for integral-tick penalties.
        // Ordinary requests and corrected clocks retain their existing rule.
        const bool cooled = state.timedRequest && !state.haveConsumption ?
            sincePress >= cooldown : sincePress > cooldown;
        if (!in.engineDown && releasedFor >= in.interval && cooled) {
            BeginRequest(state, in);
        }
    }
    out.valid = true; out.down = state.down;
    out.fresh = state.down && !previous; out.released = previous && !state.down; out.phase = state.phase;
    return out;
}
