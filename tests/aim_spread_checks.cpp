#include "aim_spread.h"
#include "aim_spread_offsets.h"
#include "aim_weapon_offsets.h"
#include "cs2_offsets.h"
#include "silent_aim_offsets.h"
#include "movement_offsets.h"
#include <Windows.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = aim_spread_offsets;
namespace weaponOff = aim_weapon_offsets;
unsigned checks = 0, failures = 0;
unsigned char* memory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 0x100> entry{}, decoy{}, identity{}, patternStorage{}, punchService{};
std::array<unsigned char, 0x88> engineRandom{};
AimSource source{};
AimWeaponSnapshot weapon{};
AimSpreadContext context{};
AimShot shot{};
struct Mock {
    AimSpreadTime now{100, .6f}, observedPunchTime{};
    AimAngles punch{1, -2, .25f}, lastSeedAngles{};
    int tickOffset = 7, historyTick = 90, scene = 2;
    int convertCalls = 0, timeCalls = 0, punchCalls = 0, seedCalls = 0, ctorCalls = 0, floatCalls = 0;
    int weaponCalls = 0;
    unsigned sampledTicks = 0;
    int expectedAlternateTick = -1;
    int badReturn = 0, lastSeedTick = -1;
    std::uint32_t seed = 128;
    bool arguments = true, weaponCurrent = true;
    std::array<float, 4> draws{.25f, .125f, .75f, .625f};
    std::array<float, 4> highs{};
} mock;
struct PrivateStream { std::uint32_t magic = 0, seed = 0, draws = 0; };
constexpr std::uint32_t streamMagic = 0xc021735a;
template<class T> void Put(void* p, std::size_t at, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + at, &value, sizeof(value));
}
template<class T> T Get(const void* p, std::size_t at) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(p) + at, sizeof(value)); return value;
}
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
void Check(bool condition, const char* label) {
    ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
bool Same(const AimSpreadTime& a, const AimSpreadTime& b) {
    return a.tick == b.tick && a.fraction == b.fraction;
}
int* Convert(int* out, int tick, int scene) {
    ++mock.convertCalls;
    mock.arguments &= out && tick == mock.historyTick && scene == mock.scene;
    *out = tick + mock.tickOffset;
    return mock.badReturn == 1 ? nullptr : out;
}
AimSpreadTime* CurrentTime(AimSpreadTime* out, int scene) {
    ++mock.timeCalls; mock.arguments &= out && scene == mock.scene;
    *out = mock.now;
    return mock.badReturn == 2 ? reinterpret_cast<AimSpreadTime*>(entry.data()) : out;
}
AimAngles* Punch(void* services, AimAngles* out, const AimSpreadTime* time, bool scale) {
    ++mock.punchCalls;
    mock.arguments &= services == punchService.data() && out && time && scale;
    if (time) mock.observedPunchTime = *time;
    *out = mock.punch;
    return mock.badReturn == 3 ? nullptr : out;
}
std::uint32_t Seed(void* unused, const AimAngles* angles, int tick) {
    ++mock.seedCalls;
    mock.arguments &= !unused && angles && std::isfinite(angles->pitch) && std::isfinite(angles->yaw);
    mock.lastSeedAngles = *angles; mock.lastSeedTick = tick;
    if (tick == mock.observedPunchTime.tick) mock.sampledTicks |= 1;
    else if (tick == (mock.expectedAlternateTick >= 0 ? mock.expectedAlternateTick : mock.observedPunchTime.tick + 1)) mock.sampledTicks |= 2;
    else mock.arguments = false;
    return mock.seed;
}
void* RandomCtor(void* owned, int seed) {
    ++mock.ctorCalls;
    const auto address = Address(owned);
    mock.arguments &= owned && address % 16 == 0 &&
        !(address >= client && address < client + cs2_offsets::ClientImageSize) &&
        owned != engineRandom.data() && owned != punchService.data() && owned != entry.data() &&
        std::bit_cast<std::uint32_t>(seed) == mock.seed + std::uint32_t{1};
    PrivateStream state{streamMagic, std::bit_cast<std::uint32_t>(seed), 0};
    std::memcpy(owned, &state, sizeof(state));
    return owned;
}
float RandomFloat(void* owned, float low, float high) {
    ++mock.floatCalls;
    auto state = Get<PrivateStream>(owned, 0);
    mock.arguments &= state.magic == streamMagic && state.draws < 4 && low == 0 &&
        state.seed == mock.seed + std::uint32_t{1};
    if (state.draws >= 4) return std::numeric_limits<float>::quiet_NaN();
    mock.highs[state.draws] = high;
    const float value = mock.draws[state.draws++] * high;
    std::memcpy(owned, &state, sizeof(state));
    return value;
}
void Reset() {
    mock = {}; entry.fill(0); decoy.fill(0); identity.fill(0); patternStorage.fill(0); punchService.fill(0);
    engineRandom.fill(0xa7);
    Put(memory, off::RandomPattern + weaponOff::CvarValuePointer, Address(patternStorage.data()));
    Put(patternStorage.data(), weaponOff::CvarValue, std::uint8_t{0});
    Put(identity.data(), weaponOff::IdentityScene, mock.scene);
    Put(entry.data(), silent_aim_offsets::history::Presence, std::uint32_t{off::HistoryTimePresent});
    Put(entry.data(), off::HistoryTick, mock.historyTick);
    Put(entry.data(), off::HistoryFraction, .25f);
    source = {client + 0x100, client + 0x200, client + 0x300};
    Put(reinterpret_cast<void*>(source.pawn), movement_offsets::pawn::Flags, std::uint32_t{1});
    Put(reinterpret_cast<void*>(source.pawn), movement_offsets::pawn::Velocity, AimVector{120, -30, 0});
    weapon = {}; weapon.identity = Address(identity.data()); weapon.punchServices = Address(punchService.data());
    weapon.weapon = client + 0x400; weapon.handle = 0x18005;
    weapon.spread = .02f; weapon.inaccuracy = .03f; weapon.ready = true;
    context = {client, client + 0x500, &Convert, &CurrentTime, &Punch, &Seed, &RandomCtor, &RandomFloat};
    shot = {}; shot.index = 1; shot.count = 2;
    shot.history[0].entry = Address(decoy.data()); shot.history[1].entry = Address(entry.data());
}
void ReadAndClamp() {
    Reset(); const auto originalEntry = entry, originalIdentity = identity, originalStorage = patternStorage;
    AimSpreadSnapshot snapshot{};
    Check(AimSpreadRead(context, source, weapon, shot, snapshot) && snapshot.valid, "indexed native shot model accepted");
    Check(snapshot.movementStateKnown && snapshot.movementFlags == 1 && snapshot.velocity.x == 120 &&
        snapshot.velocity.y == -30 && snapshot.velocity.z == 0, "callback movement diagnostics copy verified flags and velocity");
    Check(snapshot.entry == Address(entry.data()) && snapshot.historyTick == 90 && snapshot.historyFraction == .25f &&
        snapshot.scene == 2 && Same(snapshot.time, {97, .25f}) && Same(snapshot.now, {100, .6f}),
        "selected history entry uses native tick conversion and preserves canonical fraction");
    Check(mock.arguments && mock.weaponCalls >= 2 && mock.convertCalls == 2 && mock.timeCalls == 2 && mock.punchCalls == 2 &&
        Same(mock.observedPunchTime, snapshot.time) && !std::memcmp(&snapshot.punch, &mock.punch, sizeof(mock.punch)),
        "native converter, current scene clock and scaled shot-time punch ABIs use caller-owned outputs");
    Check(entry == originalEntry && identity == originalIdentity && patternStorage == originalStorage,
        "shot-model adapter does not write native history, identity or cvars");
    struct Case { int tick; float fraction; AimSpreadTime expected; };
    const Case cases[]{ {89, .59f, {96, .6f}}, {89, .6f, {96, .6f}}, {89, .61f, {96, .61f}},
        {20, .9f, {96, .6f}}, {93, .59f, {100, .59f}}, {93, .7f, {100, .6f}}, {94, 0, {100, .6f}} };
    for (const auto& value : cases) {
        Reset(); mock.historyTick = value.tick;
        Put(entry.data(), off::HistoryTick, value.tick); Put(entry.data(), off::HistoryFraction, value.fraction);
        Check(AimSpreadRead(context, source, weapon, shot, snapshot) && Same(snapshot.time, value.expected) &&
            Same(mock.observedPunchTime, value.expected), "four-tick bounds compare both tick and fraction before shot punch");
    }
    Reset();
    Put(reinterpret_cast<void*>(source.pawn), movement_offsets::pawn::Velocity,
        AimVector{0, 0, std::numeric_limits<float>::quiet_NaN()});
    Check(AimSpreadRead(context, source, weapon, shot, snapshot) && snapshot.valid &&
        !snapshot.movementStateKnown && snapshot.movementFlags == 0 && snapshot.velocity.z == 0,
        "unavailable movement diagnostics do not become a spread eligibility gate");
}
void RejectAndInvalidate() {
    for (int invalid = 0; invalid < 27; ++invalid) {
        Reset(); AimSpreadSnapshot snapshot{};
        switch (invalid) {
        case 0: shot.index = -1; break;
        case 1: shot.index = shot.count; break;
        case 2: shot.count = 0; break;
        case 3: shot.history[1].entry = 0; break;
        case 4: Put(entry.data(), silent_aim_offsets::history::Presence, std::uint32_t{0x800}); break;
        case 5: Put(entry.data(), off::HistoryFraction, std::numeric_limits<float>::quiet_NaN()); break;
        case 6: Put(entry.data(), off::HistoryFraction, -.01f); break;
        case 7: Put(entry.data(), off::HistoryFraction, 1.0f); break;
        case 8: Put(entry.data(), off::HistoryTick, -1); break;
        case 9: mock.now.fraction = std::numeric_limits<float>::infinity(); break;
        case 10: mock.now.fraction = 1; break;
        case 11: mock.now.tick = 3; break;
        case 12: mock.punch.pitch = std::numeric_limits<float>::quiet_NaN(); break;
        case 13: context.convert = nullptr; break;
        case 14: context.time = nullptr; break;
        case 15: context.punch = nullptr; break;
        case 16: weapon.punchServices = 0; break;
        case 17: mock.badReturn = 1; break;
        case 18: mock.badReturn = 2; break;
        case 19: mock.badReturn = 3; break;
        case 20: Put(identity.data(), weaponOff::IdentityScene, -1); break;
        case 21: Put(patternStorage.data(), weaponOff::CvarValue, std::uint8_t{2}); break;
        case 22: mock.now.fraction = std::numeric_limits<float>::quiet_NaN(); break;
        case 23: mock.weaponCurrent = false; break;
        case 24: shot.index = 256; shot.count = 257; break;
        case 25: shot.history[1].entry = 0x800000000000ULL; break;
        case 26: weapon.identity = 0x800000000000ULL; break;
        }
        Check(!AimSpreadRead(context, source, weapon, shot, snapshot) && !snapshot.valid,
            "absent/held primary index, malformed time, unavailable ABI or foreign native output fails closed");
    }
    for (int changed = 0; changed < 9; ++changed) {
        Reset(); AimSpreadSnapshot snapshot{};
        Check(AimSpreadRead(context, source, weapon, shot, snapshot), "valid model before late native mutation");
        switch (changed) {
        case 0: ++mock.now.tick; break;
        case 1: mock.now.fraction += .001f; break;
        case 2: Put(entry.data(), off::HistoryTick, 91); break;
        case 3: Put(entry.data(), off::HistoryFraction, .26f); break;
        case 4: Put(identity.data(), weaponOff::IdentityScene, 3); break;
        case 5: mock.punch.roll += .001f; break;
        case 6: Put(patternStorage.data(), weaponOff::CvarValue, std::uint8_t{1}); break;
        case 7: Put(entry.data(), silent_aim_offsets::history::Presence, std::uint32_t{0}); break;
        case 8: mock.weaponCurrent = false; break;
        }
        Check(!AimSpreadStillCurrent(context, source, snapshot), "late time/history/scene/punch/pattern mutation cancels model");
    }
    Reset(); AimSpreadSnapshot snapshot{}; AimSpreadTestRuntime(false);
    Check(!AimSpreadRead(context, source, weapon, shot, snapshot), "unloaded or replaced runtime rejects stale model");
    AimSpreadTestRuntime(true);
}
void SolveKnownDraws() {
    constexpr float circle = 6.283185482025146484375f;
    constexpr double degrees = 57.29577951308232088;
    for (int mode = 0; mode < 7; ++mode) {
        Reset(); mock.punch = {};
        if (mode == 1) Put(patternStorage.data(), weaponOff::CvarValue, std::uint8_t{1});
        if (mode == 2) { weapon.negev = true; weapon.recoilIndex = 0; }
        if (mode == 3) { weapon.negev = true; weapon.recoilIndex = 2; }
        if (mode == 4) weapon.forceMaxRadius = true;
        if (mode == 5) weapon.fixedSpreadAngle = true;
        if (mode == 6) { weapon.negev = true; weapon.recoilIndex = 0; weapon.forceMaxRadius = true; weapon.fixedSpreadAngle = true; mock.seed = UINT32_MAX; }
        AimSpreadSnapshot snapshot{};
        Check(AimSpreadRead(context, source, weapon, shot, snapshot), "known random-draw fixture supplies current native model");
        const auto beforeEngine = engineRandom;
        const auto beforeEntry = entry, beforePunch = punchService;
        const auto solved = AimSpreadSolve(context, snapshot, {});
        float a = .25f, b = mode == 1 ? .625f : .75f;
        float angleA = .125f * circle, angleB = (mode == 1 ? .75f : .625f) * circle;
        if (mode == 2 || mode == 6) { a = 1 - std::pow(a, 8.0f); b = 1 - std::pow(b, 8.0f); }
        if (mode == 3) { a = 1 - a*a; b = 1 - b*b; }
        if (mode == 4 || mode == 6) a = b = 1;
        if (mode == 5 || mode == 6) angleA = angleB = circle * .25f;
        const float ax = a * weapon.inaccuracy, bx = b * weapon.spread;
        const float x = std::cos(angleA) * ax + std::cos(angleB) * bx;
        const float y = std::sin(angleA) * ax + std::sin(angleB) * bx;
        const double expectedPitch = std::atan(static_cast<double>(y)) * degrees;
        const double expectedYaw = std::atan2(static_cast<double>(x), std::sqrt(1.0 + static_cast<double>(y)*y)) * degrees;
        Check(solved.valid && solved.residualDegrees < .0002f &&
            std::abs(solved.command.pitch - expectedPitch) < .00002 && std::abs(solved.command.yaw - expectedYaw) < .00002,
            "native samples and special shaping produce the independently calculated compensated command");
        Check(mock.arguments && mock.seedCalls == static_cast<int>(solved.evaluations) &&
            mock.ctorCalls == mock.seedCalls && mock.floatCalls == 4 * mock.seedCalls && mock.lastSeedTick == snapshot.time.tick &&
            solved.seed == mock.seed + std::uint32_t{1}, "each candidate seeds a separate owned stream using native effective-angle/tick seed plus one");
        Check(mock.highs[0] == 1 && mock.highs[1] == circle && mock.highs[2] == (mode == 1 ? circle : 1) &&
            mock.highs[3] == (mode == 1 ? 1 : circle), "native pattern flag selects the correct radius/angle pair ordering");
        Check(engineRandom == beforeEngine && entry == beforeEntry && punchService == beforePunch,
            "spread search never advances engine random state or mutates borrowed game buffers");
    }
    for (int invalid = 0; invalid < 5; ++invalid) {
        Reset(); AimSpreadSnapshot snapshot{};
        Check(AimSpreadRead(context, source, weapon, shot, snapshot), "baseline for rejected sampler result");
        if (invalid == 0) context.seed = nullptr;
        if (invalid == 1) context.randomCtor = nullptr;
        if (invalid == 2) context.randomFloat = nullptr;
        if (invalid == 3) mock.draws[0] = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 4) mock.draws[2] = 1.01f;
        Check(!AimSpreadSolve(context, snapshot, {}).valid, "missing native random ABI or invalid native samples reject compensation");
    }
    Reset(); mock.punch = {}; weapon.name = "C_WeaponSSG08";
    AimSpreadSnapshot scout{};
    Check(AimSpreadRead(context, source, weapon, shot, scout) && scout.seedVariants == 2 &&
        Same(scout.converted, {97, .25f}), "Scout retains raw converted time and enables the two evidenced clock variants");
    const auto robust = AimSpreadSolve(context, scout, {});
    Check(robust.valid && robust.variants == 2 && mock.sampledTicks == 3 && mock.arguments &&
        mock.lastSeedTick == scout.time.tick + 1 && robust.evaluations <= 8192,
        "Scout samples original and next integer tick with copied punch and isolated streams within one shared budget");
    ++mock.tickOffset;
    Check(!AimSpreadStillCurrent(context, source, scout), "changed scene conversion invalidates copied unclamped timing");
    for (int lead : {1, 3, 5}) {
        Reset(); weapon.name = "C_WeaponSSG08"; weapon.aimPunch = {};
        mock.historyTick = mock.now.tick + lead - mock.tickOffset;
        Put(entry.data(), off::HistoryTick, mock.historyTick);
        AimSpreadSnapshot raw{};
        Check(AimSpreadRead(context, source, weapon, shot, raw), "Scout timing model accepts independently captured indexed time");
        const bool useRaw = lead > 1;
        Check(raw.rawClockAlternative == useRaw && raw.alternateTime.tick == mock.now.tick + lead &&
            raw.alternateTime.fraction == (useRaw ? .25f : mock.now.fraction) &&
            (useRaw ? raw.alternatePunch.pitch == 0 : raw.alternatePunch.pitch == mock.punch.pitch),
            "late raw timestamp replaces the next-tick alternative with copied current recoil; one-tick lead retains old policy");
        mock.expectedAlternateTick = raw.alternateTime.tick;
        const int punchCalls = mock.punchCalls, timeCalls = mock.timeCalls;
        const auto rawSolved = AimSpreadSolve(context, raw, {});
        Check(rawSolved.valid && rawSolved.variants == 2 && mock.sampledTicks == 3 && mock.arguments &&
            mock.punchCalls == punchCalls && mock.timeCalls == timeCalls,
            "raw-clock search uses both selected integer ticks and performs no future native punch/time queries");
        Check(useRaw ? std::abs(rawSolved.effective.pitch - rawSolved.effectiveAlternate.pitch - mock.punch.pitch) < .00001f :
            std::abs(rawSolved.effective.pitch - rawSolved.effectiveAlternate.pitch) < .00001f,
            "alternate effective angle uses its own copied recoil basis");
    }
}
}

bool AimWeaponStillCurrent(const AimWeaponContext& actualContext, const AimSource& actual, const AimWeaponSnapshot& actualWeapon) noexcept {
    ++mock.weaponCalls;
    mock.arguments &= actualContext.client == client && actual.pawn == source.pawn && actualWeapon.weapon == weapon.weapon && actualWeapon.handle == weapon.handle;
    return mock.weaponCurrent;
}

int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, cs2_offsets::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    client = Address(memory); AimSpreadTestRuntime(true);
    ReadAndClamp(); RejectAndInvalidate(); SolveKnownDraws();
    AimSpreadTestRuntime(false); VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("aim_spread_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
