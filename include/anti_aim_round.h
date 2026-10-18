#pragma once
#include "anti_aim_offsets.h"
#include "movement_memory.h"

enum class AntiAimRoundState { Unavailable, FreezePeriod, Playable };

// Read on each command: warmup is playable, and every round's freeze applies.
// Never latch readiness across rounds, rule replacements or map changes.
inline AntiAimRoundState AntiAimReadRoundState(std::uintptr_t client) noexcept {
    std::uintptr_t rules = 0;
    std::uint8_t freeze = 0;
    if (!client || !MovementCopy(client + anti_aim_offsets::GameRules, &rules, sizeof(rules)) || !rules ||
        !MovementCopy(rules + anti_aim_offsets::FreezePeriod, &freeze, sizeof(freeze)) || freeze > 1)
        return AntiAimRoundState::Unavailable;
    return freeze ? AntiAimRoundState::FreezePeriod : AntiAimRoundState::Playable;
}
