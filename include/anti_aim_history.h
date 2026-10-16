#pragma once
#include "silent_aim_math.h"
#include <array>
#include <cstddef>
#include <cstdint>

// Value-only publication identity. Command/protobuf addresses are deliberately
// absent: native prediction copies messages and rebuilds a sequence in place.
struct AntiAimHistoryCommand {
    int sequence = 0;
    std::uint64_t source = 0;
    AimAngles physical{}, outgoing{};
    std::uint32_t anglePresence = 0;
    float forward = 0, left = 0;
    std::uint64_t directions = 0;
    bool edited = false;
};
struct AntiAimHistoryBucket {
    int sequence = 0;
    std::uint64_t source = 0;
    std::array<AntiAimHistoryCommand, 8> variants{};
    std::size_t next = 0, count = 0;
};
struct AntiAimHistoryState {
    std::array<AntiAimHistoryBucket, 256> buckets{};
};

// Publish every completed command, including physical passthrough. Identical
// wire values update their existing variant; a ninth distinct rebuild evicts
// the oldest variant of the same sequence. Storage is bounded and allocation-free.
bool AntiAimHistoryPublish(AntiAimHistoryState& state, const AntiAimHistoryCommand& command) noexcept;

// Observed physical/edited members are ignored. Only source/sequence, outgoing
// angle values and their scalar presence, analog axes and direction-down bits
// identify a published basis. Failure leaves matched unchanged.
bool AntiAimHistoryMatch(const AntiAimHistoryState& state, const AntiAimHistoryCommand& observed,
                         AntiAimHistoryCommand& matched) noexcept;
void AntiAimHistoryReset(AntiAimHistoryState& state) noexcept;

// Called only on the native command callback owner. Reads copied values through
// guarded memory access; retains no native pointers and never writes a command.
// The caller assigns the source lifecycle cookie. Failure preserves observed.
bool AntiAimHistoryReadCommand(std::uintptr_t command, std::uint64_t source,
                               AntiAimHistoryCommand& observed) noexcept;
