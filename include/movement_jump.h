#pragma once
#include <cstdint>
#include <cstddef>

struct JumpStamp {
    int tick = -1024;
    float fraction = 0;
};
static_assert(sizeof(JumpStamp) == 8 && offsetof(JumpStamp, fraction) == 4);
enum class JumpPhase : unsigned char { Ready, Pending, Releasing };
struct JumpState {
    JumpPhase phase = JumpPhase::Ready;
    bool down = false;
    std::uint32_t requestTick = 0, releaseTick = 0, lastSimulationTick = 0, consumptionTick = 0;
    bool haveClock = false, haveConsumption = false, usableBuffered = false;
    bool timedPress = false, timedRequest = false;
    JumpStamp baselineActual{}, baselineUsable{};
};
struct JumpInput {
    bool grounded = false, engineDown = false, modern = false, autoBunnyhop = false;
    int commandTick = 0;
    std::uint32_t simulationTick = 0;
    float interval = 0, penalty = 0, bufferWindow = 0;
    JumpStamp actual{}, usable{}, landed{};
};
struct JumpOutput {
    bool valid = false, down = false, fresh = false, released = false, acknowledged = false;
    JumpPhase phase = JumpPhase::Ready;
};
JumpOutput PlanJump(JumpState& state, const JumpInput& input) noexcept;
void ResetJump(JumpState& state) noexcept;
// A separately scheduled jump still owns a down input. Retain that ownership
// until a following command publishes its explicit time-zero release.
void RecordTimedJump(JumpState& state, std::uint32_t simulationTick) noexcept;
JumpOutput ReleaseTimedJump(JumpState& state, std::uint32_t simulationTick) noexcept;

// Separate from the ordinary hop phase: airborne hop resets must not forget
// the cooldown of a timed attempt that missed. This clock uses controller
// simulation ticks only, never engine press timestamps from another domain.
struct JumpAttemptClock {
    bool haveRequest = false, haveClock = false;
    std::uint32_t requestTick = 0, lastTick = 0;
};
bool TimedJumpAttemptReady(JumpAttemptClock& clock, std::uint32_t simulationTick,
                          float interval, float penalty) noexcept;
void RecordJumpAttempt(JumpAttemptClock& clock, std::uint32_t simulationTick) noexcept;
