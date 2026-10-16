#include "anti_aim_history.h"
#include "anti_aim_math.h"
#include "movement_offsets.h"
#include "silent_aim_offsets.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
int failures = 0, checks = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
bool Near(float left, float right) { return std::abs(left - right) < 0.00001f; }
AntiAimHistoryCommand Example(int sequence = 42) {
    return {sequence, 17, {37, 725, 0}, {0, -175, 0}, 2, -1, 0, 0x10, true};
}
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
template<class T> void Put(std::uintptr_t address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
struct NativeCommand {
    std::array<unsigned char, 0x98> command{}, base{};
    std::array<unsigned char, 0x28> angles{};
    explicit NativeCommand(const AntiAimHistoryCommand& value) {
        namespace mo = movement_offsets; namespace so = silent_aim_offsets;
        Put(Address(command) + mo::command::Sequence, value.sequence);
        Put(Address(command) + mo::command::Base, Address(base));
        Put(Address(command) + mo::command::Buttons, value.directions);
        Put(Address(base) + mo::base::Angles, Address(angles));
        Put(Address(base) + mo::base::Forward, value.forward);
        Put(Address(base) + mo::base::Left, value.left);
        Put(Address(angles) + so::angle::Presence, value.anglePresence);
        Put(Address(angles) + so::angle::Pitch, value.outgoing);
    }
};
}

int main() {
    // Static to keep the deliberately fixed-capacity history off thread stacks.
    static AntiAimHistoryState state;
    AntiAimHistoryCommand matched{};
    const auto first = Example();
    Check(AntiAimHistoryPublish(state, first), "publish outgoing values");
    NativeCommand producer(first), prediction(first);
    AntiAimHistoryCommand nativeProducer{}, nativePrediction{};
    Check(AntiAimHistoryReadCommand(Address(producer.command), first.source, nativeProducer) &&
          AntiAimHistoryReadCommand(Address(prediction.command), first.source, nativePrediction) &&
          Address(producer.command) != Address(prediction.command) && Address(producer.base) != Address(prediction.base) &&
          Address(producer.angles) != Address(prediction.angles) &&
          AntiAimHistoryMatch(state, nativePrediction, matched) && matched.edited,
          "different native command/base/angle addresses match copied publication");
    Put(Address(prediction.command) + movement_offsets::command::Presence, std::uint32_t{0x7fff});
    Put(Address(prediction.base) + movement_offsets::base::Presence, std::uint32_t{0});
    Put(Address(prediction.base) + 0x38, std::uintptr_t{0xdeadbeef});
    Check(AntiAimHistoryReadCommand(Address(prediction.command), first.source, nativePrediction) &&
          AntiAimHistoryMatch(state, nativePrediction, matched),
          "CRC pointer and base/command presence changes do not alter ingested values");
    Put(Address(prediction.angles) + silent_aim_offsets::angle::Presence, std::uint32_t{0});
    Check(!AntiAimHistoryReadCommand(Address(prediction.command), first.source, nativePrediction),
          "absent nonzero angle scalar rejects malformed sparse payload");
    Check(!AntiAimHistoryReadCommand(0, first.source, nativePrediction) &&
          !AntiAimHistoryReadCommand(std::numeric_limits<std::uintptr_t>::max(), first.source, nativePrediction) &&
          !AntiAimHistoryReadCommand(Address(producer.command), 0, nativePrediction),
          "invalid native command pointers and absent source reject safely");
    auto cloned = first;
    cloned.physical = {NAN, NAN, NAN}; cloned.edited = false;
    Check(AntiAimHistoryMatch(state, cloned, matched) && matched.edited && matched.physical.pitch == 37,
          "copied prediction payload recovers physical basis without pointers");
    cloned.anglePresence |= 0x8000;
    cloned.directions |= 1;
    Check(AntiAimHistoryMatch(state, cloned, matched), "ignore non-scalar metadata and unrelated action buttons");
    cloned.anglePresence ^= 1;
    Check(!AntiAimHistoryMatch(state, cloned, matched), "scalar presence differs");
    cloned = first; ++cloned.source;
    Check(!AntiAimHistoryMatch(state, cloned, matched), "source epoch differs");
    cloned = first; ++cloned.sequence;
    Check(!AntiAimHistoryMatch(state, cloned, matched), "sequence differs");
    cloned = first; cloned.forward = -0.99f;
    Check(!AntiAimHistoryMatch(state, cloned, matched), "analog payload differs");
    cloned = first; cloned.directions ^= 0x18;
    Check(!AntiAimHistoryMatch(state, cloned, matched), "direction-down payload differs");
    cloned = first; cloned.outgoing.yaw = std::nextafter(first.outgoing.yaw, 0.0f);
    Check(!AntiAimHistoryMatch(state, cloned, matched), "nearby rebuild yaw is distinct");
    cloned = first; cloned.outgoing.pitch = -0.0f; cloned.left = -0.0f;
    Check(AntiAimHistoryMatch(state, cloned, matched), "native signed-zero canonicalization");

    auto passthrough = first;
    passthrough.outgoing = passthrough.physical; passthrough.anglePresence = 3;
    passthrough.forward = 1; passthrough.directions = 8; passthrough.edited = false;
    Check(AntiAimHistoryPublish(state, passthrough), "publish same-sequence physical passthrough");
    Check(AntiAimHistoryMatch(state, first, matched) && matched.edited,
          "old edited copy survives physical rebuild");
    Check(AntiAimHistoryMatch(state, passthrough, matched) && !matched.edited,
          "physical copy remains physical");
    for (int i = 0; i < 10000; ++i) AntiAimHistoryPublish(state, first);
    Check(state.buckets[42].count == 2 && AntiAimHistoryMatch(state, passthrough, matched),
          "identical republishes do not evict other wire variants");
    auto updated = first; updated.physical.pitch = 60;
    AntiAimHistoryPublish(state, updated);
    Check(AntiAimHistoryMatch(state, first, matched) && matched.physical.pitch == 60,
          "identical publication updates copied basis");
    const auto physical = AntiAimCorrectWalkMovement(matched.outgoing, matched.physical, matched.forward, matched.left);
    Check(physical.valid && Near(physical.forward, 1) && Near(physical.left, 0),
          "matched basis reverses movement correction across wrapped physical yaw");

    AntiAimHistoryReset(state);
    for (int i = 0; i < 9; ++i) {
        auto variant = first; variant.outgoing.yaw += i;
        AntiAimHistoryPublish(state, variant);
    }
    Check(state.buckets[42].count == 8 && !AntiAimHistoryMatch(state, first, matched), "ninth rebuild bounds history");
    auto eighth = first; eighth.outgoing.yaw += 8;
    Check(AntiAimHistoryMatch(state, eighth, matched), "newest rebuild retained");
    auto collision = Example(42 + 256);
    AntiAimHistoryPublish(state, collision);
    Check(!AntiAimHistoryMatch(state, eighth, matched) && AntiAimHistoryMatch(state, collision, matched),
          "sequence ring collision invalidates only overwritten bucket");
    auto newSource = collision; ++newSource.source;
    AntiAimHistoryPublish(state, newSource);
    Check(!AntiAimHistoryMatch(state, collision, matched) && AntiAimHistoryMatch(state, newSource, matched),
          "source change replaces same-sequence bucket");

    auto invalid = first; invalid.sequence = 0;
    Check(!AntiAimHistoryPublish(state, invalid), "reject nonpositive sequence");
    invalid = first; invalid.source = 0;
    Check(!AntiAimHistoryPublish(state, invalid), "reject absent source epoch");
    invalid = first; invalid.physical.pitch = NAN;
    Check(!AntiAimHistoryPublish(state, invalid), "reject nonfinite publication basis");
    invalid = first; invalid.outgoing.yaw = std::numeric_limits<float>::infinity();
    Check(!AntiAimHistoryPublish(state, invalid) && !AntiAimHistoryMatch(state, invalid, matched), "reject nonfinite observed angles");
    invalid = first; invalid.forward = NAN;
    Check(!AntiAimHistoryPublish(state, invalid) && !AntiAimHistoryMatch(state, invalid, matched), "reject nonfinite axes");
    AntiAimHistoryReset(state);
    const auto unchanged = matched;
    Check(!AntiAimHistoryMatch(state, newSource, matched) && matched.source == unchanged.source &&
          matched.sequence == unchanged.sequence, "reset invalidates all copied publications; failed match preserves output");
    std::printf("anti aim history: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
