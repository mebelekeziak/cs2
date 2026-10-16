#include "anti_aim_history.h"
#include "movement_memory.h"
#include "silent_aim_offsets.h"
#include <cmath>

namespace {
constexpr std::uint64_t directions = movement_offsets::Directions;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Finite(const AimAngles& value) noexcept {
    return std::isfinite(value.pitch) && std::isfinite(value.yaw) && std::isfinite(value.roll);
}
bool Valid(const AntiAimHistoryCommand& value) noexcept {
    return value.sequence > 0 && value.source && Finite(value.outgoing) &&
        std::isfinite(value.forward) && std::isfinite(value.left);
}
bool SameWire(const AntiAimHistoryCommand& left, const AntiAimHistoryCommand& right) noexcept {
    // Numeric equality accepts canonicalized signed zero without accepting a
    // nearby rebuild's different yaw. Message metadata/cached-size bits do not
    // belong to the scalar-angle presence contract.
    return left.sequence == right.sequence && left.source == right.source &&
        left.outgoing.pitch == right.outgoing.pitch && left.outgoing.yaw == right.outgoing.yaw &&
        left.outgoing.roll == right.outgoing.roll &&
        (left.anglePresence & 7) == (right.anglePresence & 7) &&
        left.forward == right.forward && left.left == right.left &&
        (left.directions & directions) == (right.directions & directions);
}
}

bool AntiAimHistoryPublish(AntiAimHistoryState& state, const AntiAimHistoryCommand& command) noexcept {
    if (!Valid(command) || !Finite(command.physical)) return false;
    auto& bucket = state.buckets[static_cast<unsigned>(command.sequence) % state.buckets.size()];
    if (bucket.sequence != command.sequence || bucket.source != command.source) {
        bucket.sequence = command.sequence;
        bucket.source = command.source;
        bucket.next = bucket.count = 0;
    }
    for (std::size_t i = 0; i < bucket.count; ++i) {
        if (SameWire(bucket.variants[i], command)) {
            bucket.variants[i] = command;
            return true;
        }
    }
    bucket.variants[bucket.next] = command;
    bucket.next = (bucket.next + 1) % bucket.variants.size();
    if (bucket.count < bucket.variants.size()) ++bucket.count;
    return true;
}

bool AntiAimHistoryMatch(const AntiAimHistoryState& state, const AntiAimHistoryCommand& observed,
                         AntiAimHistoryCommand& matched) noexcept {
    if (!Valid(observed)) return false;
    const auto& bucket = state.buckets[static_cast<unsigned>(observed.sequence) % state.buckets.size()];
    if (bucket.sequence != observed.sequence || bucket.source != observed.source) return false;
    for (std::size_t i = 0; i < bucket.count; ++i) {
        if (SameWire(bucket.variants[i], observed)) {
            matched = bucket.variants[i];
            return true;
        }
    }
    return false;
}

void AntiAimHistoryReset(AntiAimHistoryState& state) noexcept {
    // Clearing count invalidates copied values without a large temporary or
    // imposing native command ownership on publication history.
    for (auto& bucket : state.buckets) {
        bucket.sequence = 0;
        bucket.source = 0;
        bucket.count = bucket.next = 0;
    }
}

bool AntiAimHistoryReadCommand(std::uintptr_t command, std::uint64_t source,
                               AntiAimHistoryCommand& observed) noexcept {
    constexpr std::uintptr_t maximum = 0x00007fffffffffffULL;
    namespace mo = movement_offsets;
    namespace so = silent_aim_offsets;
    std::uintptr_t base = 0, angles = 0;
    std::uint64_t down = 0;
    AntiAimHistoryCommand value{};
    value.source = source;
    if (!source || command < 0x10000 || command > maximum - mo::command::Buttons - sizeof(down) ||
        !Read(command + mo::command::Sequence, value.sequence) || value.sequence <= 0 ||
        !Read(command + mo::command::Base, base) || base < 0x10000 || base > maximum - mo::base::Left - sizeof(float) ||
        !Read(base + mo::base::Angles, angles) || angles < 0x10000 || angles > maximum - so::angle::Pitch - sizeof(AimAngles) ||
        !Read(angles + so::angle::Presence, value.anglePresence) ||
        !Read(angles + so::angle::Pitch, value.outgoing) ||
        !Read(base + mo::base::Forward, value.forward) || !Read(base + mo::base::Left, value.left) ||
        !Read(command + mo::command::Buttons, down) || !Valid(value)) return false;
    // Native sparse producers clear absent scalar payloads to zero. Ingestion
    // reads analog payloads directly, independent of forward/left presence.
    if ((!(value.anglePresence & 1) && value.outgoing.pitch != 0) ||
        (!(value.anglePresence & 2) && value.outgoing.yaw != 0) ||
        (!(value.anglePresence & 4) && value.outgoing.roll != 0)) return false;
    value.directions = down & directions;
    value.physical = value.outgoing;
    observed = value;
    return true;
}
