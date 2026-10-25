#include "aim_spread.h"
#include "aim_spread_offsets.h"
#include "aim_weapon_offsets.h"
#include "silent_aim_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include "movement_offsets.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
namespace off = aim_spread_offsets;
namespace weaponOff = aim_weapon_offsets;
#ifdef CS2_AIM_SPREAD_CHECKS
bool testRuntime = false;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept { return MovementCopy(at, &value, sizeof(value)); }
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&bytes)[N]) noexcept {
    unsigned char copy[N]{};
    return MovementCopy(at, copy, N) && !std::memcmp(copy, bytes, N);
}
bool Current(const AimSpreadContext& context) noexcept {
#ifdef CS2_AIM_SPREAD_CHECKS
    if (testRuntime) return context.client != 0;
#endif
    return context.client && context.tier0 &&
        context.client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) &&
        context.tier0 == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"));
}
bool WeaponCurrent(const AimSpreadContext& context, const AimSource& source, const AimWeaponSnapshot& weapon) noexcept {
    AimWeaponContext check{}; check.client = context.client;
    return AimWeaponStillCurrent(check, source, weapon);
}
bool TimeValid(const AimSpreadTime& time) noexcept {
    return time.tick >= 4 && time.tick < std::numeric_limits<int>::max() - 4 &&
        std::isfinite(time.fraction) && time.fraction >= 0 && time.fraction < 1;
}
bool Before(const AimSpreadTime& a, const AimSpreadTime& b) noexcept {
    return a.tick < b.tick || (a.tick == b.tick && a.fraction < b.fraction);
}
bool Same(const AimSpreadTime& a, const AimSpreadTime& b) noexcept {
    return a.tick == b.tick && a.fraction == b.fraction;
}
bool Pattern(const AimSpreadContext& context, bool& pattern) noexcept {
    std::uintptr_t storage = 0;
    std::uint8_t value = 0;
    if (!Read(context.client + off::RandomPattern + weaponOff::CvarValuePointer, storage) || !storage ||
        !Read(storage + weaponOff::CvarValue, value) || value > 1) return false;
    pattern = value != 0;
    return true;
}
bool ReadPunch(const AimSpreadContext& context, const AimWeaponSnapshot& weapon,
    const AimSpreadTime& time, AimAngles& punch) noexcept {
    if (!context.punch || !weapon.punchServices || time.tick < 0 ||
        time.tick >= std::numeric_limits<int>::max() - 4 ||
        !std::isfinite(time.fraction) || time.fraction < 0 || time.fraction >= 1) return false;
    // A partial native output is unavailable, rather than a mixture of an old
    // current-prediction value and the selected timestamp's punch.
    punch = {NAN, NAN, NAN};
    __try {
        if (context.punch(reinterpret_cast<void*>(weapon.punchServices), &punch, &time, true) != &punch) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::isfinite(punch.pitch) && std::isfinite(punch.yaw) && std::isfinite(punch.roll);
}
bool ReadTimePunch(const AimSpreadContext& context, const AimWeaponSnapshot& weapon,
    int historyTick, float historyFraction, int scene, AimSpreadTime& shotTime,
    AimSpreadTime& now, AimAngles& punch, AimSpreadTime& converted) noexcept {
    if (!context.convert || !context.time || !context.punch || !weapon.punchServices ||
        !std::isfinite(historyFraction) || historyFraction < 0 || historyFraction >= 1 || historyTick < 0 || scene < 0) return false;
    __try {
        if (context.convert(&shotTime.tick, historyTick, scene) != &shotTime.tick ||
            context.time(&now, scene) != &now || !TimeValid(now)) return false;
        shotTime.fraction = historyFraction;
        converted = shotTime;
        // Native indexed history is normalized, then clamped to the last four
        // predicted ticks. Canonical fractions need no normalization here.
        const AimSpreadTime minimum{now.tick - 4, now.fraction};
        if (Before(shotTime, minimum)) shotTime = minimum;
        if (Before(now, shotTime)) shotTime = now;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ReadPunch(context, weapon, shotTime, punch);
}
struct CachedSample {
    std::uint32_t pitch = 0, yaw = 0, seed = 0;
    int tick = 0;
    double x = 0, y = 0;
    bool valid = false;
};
struct SampleContext {
    const AimSpreadContext& native;
    const AimSpreadSnapshot& shot;
    // Values belong to this solve only. Native half-degree seed bins are
    // constant, so refinements inside a bin need no repeated SHA/RNG work.
    // Noncanonical angles bypass this cache instead of duplicating native
    // normalization (especially its distinct +180/-180 endpoints).
    std::array<CachedSample, 2048> cache{};
    unsigned nativeSeedCalls = 0, nativeRandomStreams = 0, cacheHits = 0;
};
bool Sample(void* opaque, const AimAngles& effective, unsigned variant, double& x, double& y, std::uint32_t& seed) noexcept {
    auto& sample = *static_cast<SampleContext*>(opaque);
    const auto& native = sample.native;
    const auto& shot = sample.shot;
    if (!native.seed || !native.randomCtor || !native.randomFloat || variant >= shot.seedVariants || variant > 1 ||
        shot.selectedVariant > 1) return false;
    const unsigned selected = shot.seedVariants == 1 ? shot.selectedVariant : variant;
    const int tick = selected == 0 ? shot.time.tick : shot.alternateTime.tick;
    CachedSample* slot = nullptr;
    std::uint32_t pitch = 0, yaw = 0;
    if (effective.pitch >= -180 && effective.pitch <= 180 && effective.yaw >= -180 && effective.yaw <= 180) {
        pitch = std::bit_cast<std::uint32_t>(std::floor(effective.pitch * 2.0f) * .5f);
        yaw = std::bit_cast<std::uint32_t>(std::floor(effective.yaw * 2.0f) * .5f);
        std::uint32_t hash = pitch ^ std::rotl(yaw, 11) ^ (static_cast<std::uint32_t>(tick) * 0x9e3779b9u);
        hash ^= hash >> 16; hash *= 0x85ebca6bu; hash ^= hash >> 13;
        slot = &sample.cache[hash % sample.cache.size()];
        if (slot->valid && slot->pitch == pitch && slot->yaw == yaw && slot->tick == tick) {
            ++sample.cacheHits;
            x = slot->x; y = slot->y; seed = slot->seed;
            return true;
        }
    }
    // Native implementation owns 34 int32 words: seed, shuffle index, 32 slots.
    // It has no vtable or mutex. No borrowed engine random state is accessed.
    alignas(16) unsigned char stream[off::RandomStateSize]{};
    float radiusA = 0, angleA = 0, radiusB = 0, angleB = 0;
    constexpr float circle = 6.283185482025146484375f;
    __try {
        // The chosen clock and its recoil are copied from verified native
        // input. Two-variant copied test models remain supported separately.
        ++sample.nativeSeedCalls;
        seed = native.seed(nullptr, &effective, tick) + std::uint32_t{1};
        ++sample.nativeRandomStreams;
        if (native.randomCtor(stream, std::bit_cast<int>(seed)) != stream) return false;
        radiusA = native.randomFloat(stream, 0, 1);
        angleA = native.randomFloat(stream, 0, circle);
        if (shot.pattern) {
            angleB = native.randomFloat(stream, 0, circle);
            radiusB = native.randomFloat(stream, 0, 1);
        } else {
            radiusB = native.randomFloat(stream, 0, 1);
            angleB = native.randomFloat(stream, 0, circle);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(radiusA) || !std::isfinite(radiusB) || radiusA < 0 || radiusA > 1 || radiusB < 0 || radiusB > 1 ||
        !std::isfinite(angleA) || !std::isfinite(angleB) || angleA < 0 || angleA > circle || angleB < 0 || angleB > circle) return false;
    if (shot.weapon.negev && shot.weapon.recoilIndex < 3) {
        int stage = 3;
        do { --stage; radiusA *= radiusA; radiusB *= radiusB; } while (static_cast<float>(stage) > shot.weapon.recoilIndex);
        radiusA = 1 - radiusA; radiusB = 1 - radiusB;
    }
    if (shot.weapon.forceMaxRadius) radiusA = radiusB = 1;
    if (shot.weapon.fixedSpreadAngle) angleA = angleB = circle * .25f;
    // Preserve native float rounding before handing copied offsets to the solver.
    const float a = radiusA * shot.weapon.inaccuracy, b = radiusB * shot.weapon.spread;
    x = std::cos(angleA) * a + std::cos(angleB) * b;
    y = std::sin(angleA) * a + std::sin(angleB) * b;
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (slot) *slot = {pitch, yaw, seed, tick, x, y, true};
    return true;
}
}

bool AimSpreadPrepare(std::uintptr_t client, AimSpreadContext& context) noexcept {
    context = {};
    const auto tier0 = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!client || !tier0 || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Read(tier0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(tier0 + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != off::Tier0Timestamp || nt.OptionalHeader.SizeOfImage != off::Tier0ImageSize ||
        !Match(client + off::TickConvert, off::TickConvertBytes) || !Match(client + off::CurrentTick, off::CurrentTickBytes) ||
        !Match(client + off::ShotPunch, off::ShotPunchBytes) || !Match(client + off::Seed, off::SeedBytes) ||
        !Match(tier0 + off::RandomCtor, off::RandomCtorBytes) || !Match(tier0 + off::RandomFloat, off::RandomFloatBytes) ||
        !Match(tier0 + off::RandomGenerate, off::RandomGenerateBytes)) return false;
    context = {client, tier0, reinterpret_cast<AimSpreadTickFn>(client + off::TickConvert),
        reinterpret_cast<AimSpreadTimeFn>(client + off::CurrentTick), reinterpret_cast<AimSpreadPunchFn>(client + off::ShotPunch),
        reinterpret_cast<AimSpreadSeedFn>(client + off::Seed), reinterpret_cast<AimSpreadRandomCtor>(tier0 + off::RandomCtor),
        reinterpret_cast<AimSpreadRandomFloat>(tier0 + off::RandomFloat)};
    return true;
}
bool AimSpreadReadPattern(const AimSpreadContext& context, bool& pattern) noexcept {
    return Current(context) && Pattern(context, pattern);
}
bool AimSpreadRead(const AimSpreadContext& context, const AimSource& source, const AimWeaponSnapshot& weapon,
    const AimShot& shot, AimSpreadSnapshot& out) noexcept {
    out = {};
    if (!Current(context) || shot.index < 0 || shot.index >= shot.count || shot.count > 256 || !WeaponCurrent(context, source, weapon)) {
        out.reason = "indexed primary history unavailable"; return false;
    }
    out.entry = shot.history[static_cast<std::size_t>(shot.index)].entry;
    out.weapon = weapon;
    out.historyIndex = shot.index;
    Read(shot.command + movement_offsets::command::Sequence, out.commandSequence);
    Read(source.controller + movement_offsets::controller::TickBase, out.controllerTick);
    // Diagnostic copies describe this callback, not the eventual firing state.
    // Missing movement diagnostics do not change the indexed spread contract.
    out.movementStateKnown = Read(source.pawn + movement_offsets::pawn::Flags, out.movementFlags) &&
        Read(source.pawn + movement_offsets::pawn::Velocity, out.velocity) &&
        std::isfinite(out.velocity.x) && std::isfinite(out.velocity.y) && std::isfinite(out.velocity.z);
    if (!out.movementStateKnown) { out.movementFlags = 0; out.velocity = {}; }
    // Deagle replays reproduce the same current/raw timing discrepancy as the
    // Scout. All supported weapons share this indexed native shot-time path;
    // a weapon-name exception cannot establish a different firing clock.
    out.seedVariants = 1;
    std::uint32_t presence = 0;
    if (!out.entry || !Read(out.entry + silent_aim_offsets::history::Presence, presence) ||
        (presence & off::HistoryTimePresent) != off::HistoryTimePresent ||
        !Read(out.entry + off::HistoryTick, out.historyTick) || !Read(out.entry + off::HistoryFraction, out.historyFraction) ||
        !Read(weapon.identity + weaponOff::IdentityScene, out.scene) || !Pattern(context, out.pattern) ||
        !ReadTimePunch(context, weapon, out.historyTick, out.historyFraction, out.scene, out.time, out.now, out.punch, out.converted)) {
        out.reason = "shot timing or punch unavailable"; return false;
    }
    out.alternateTime = {out.time.tick + 1, out.time.fraction};
    out.alternatePunch = out.punch;
    // The later user trial places raw indexed input 2-5 ticks ahead of this
    // callback's clock. Its observed rays fit that raw timestamp much better.
    // Keep the one-tick alternative for a one-tick lead. Larger leads retain
    // raw indexed time. Its recoil must be queried at that same time below.
    if (out.converted.tick > out.time.tick + 1) {
        if (out.converted.tick > std::numeric_limits<int>::max() - 4) {
            out.reason = "raw history time outside supported range"; return false;
        }
        out.rawClockAlternative = true;
        out.alternateTime = out.converted;
    }
    // Compensate the one model chosen by indexed timing. Requiring a second,
    // unselected random ray to hit postpones otherwise usable automatic shots.
    out.selectedVariant = out.converted.tick > out.time.tick ? 1U : 0U;
    // The native builder passes its selected scene-domain TimePair to this
    // exact getter. Current-prediction punch is a different query and can cross
    // a seed bin even when the selected firing tick is otherwise correct.
    if (out.selectedVariant == 1 && !ReadPunch(context, weapon, out.alternateTime, out.alternatePunch)) {
        out.reason = "selected-time shot punch unavailable"; return false;
    }
    out.valid = true;
    if (!AimSpreadStillCurrent(context, source, out)) { out.valid = false; out.reason = "shot model changed"; return false; }
    out.reason = "indexed native shot model";
    return true;
}
bool AimSpreadStillCurrent(const AimSpreadContext& context, const AimSource& source, const AimSpreadSnapshot& snapshot) noexcept {
    int tick = 0, scene = -1;
    float fraction = 0;
    std::uint32_t presence = 0;
    bool pattern = false;
    AimSpreadTime time{}, now{}, converted{}; AimAngles punch{};
    if (!(snapshot.valid && snapshot.selectedVariant <= 1 && Current(context) && WeaponCurrent(context, source, snapshot.weapon) &&
        Read(snapshot.entry + silent_aim_offsets::history::Presence, presence) &&
        (presence & off::HistoryTimePresent) == off::HistoryTimePresent &&
        Read(snapshot.entry + off::HistoryTick, tick) && tick == snapshot.historyTick &&
        Read(snapshot.entry + off::HistoryFraction, fraction) && fraction == snapshot.historyFraction &&
        Read(snapshot.weapon.identity + weaponOff::IdentityScene, scene) && scene == snapshot.scene &&
        Pattern(context, pattern) && pattern == snapshot.pattern &&
        ReadTimePunch(context, snapshot.weapon, tick, fraction, scene, time, now, punch, converted) &&
        Same(time, snapshot.time) && Same(now, snapshot.now) && Same(converted, snapshot.converted) &&
        !std::memcmp(&punch, &snapshot.punch, sizeof(punch)))) return false;
    // Freshness includes the punch actually used by the solve, not only the
    // original clamped query. The native evaluator may refresh memoized state.
    if (snapshot.selectedVariant == 1) {
        AimAngles selectedPunch{};
        return ReadPunch(context, snapshot.weapon, snapshot.alternateTime, selectedPunch) &&
            !std::memcmp(&selectedPunch, &snapshot.alternatePunch, sizeof(selectedPunch));
    }
    return true;
}
AimSpreadSearchResult AimSpreadSolve(const AimSpreadContext& context, const AimSpreadSnapshot& snapshot,
    const AimAngles& desired, double stopResidualDegrees, unsigned maxEvaluations) noexcept {
    if (!snapshot.valid || !Current(context) || snapshot.selectedVariant > 1) {
        AimSpreadSearchResult result{}; result.reason = "spread model unavailable"; return result;
    }
    LARGE_INTEGER begin{}, end{}, frequency{};
    QueryPerformanceCounter(&begin);
    SampleContext sample{context, snapshot};
    AimSpreadSearchInput input{};
    input.desired = desired;
    input.punch = snapshot.seedVariants == 1 && snapshot.selectedVariant == 1 ? snapshot.alternatePunch : snapshot.punch;
    input.maxOffset = static_cast<double>(snapshot.weapon.inaccuracy) + snapshot.weapon.spread;
    input.variants = snapshot.seedVariants;
    input.alternatePunchValid = snapshot.seedVariants == 2 && snapshot.rawClockAlternative;
    input.alternatePunch = snapshot.alternatePunch;
    input.stopResidualDegrees = stopResidualDegrees;
    input.maxEvaluations = maxEvaluations;
    auto result = AimSearchSpread(input, &Sample, &sample);
    result.nativeSeedCalls = sample.nativeSeedCalls;
    result.nativeRandomStreams = sample.nativeRandomStreams;
    result.nativeCacheHits = sample.cacheHits;
    if (QueryPerformanceCounter(&end) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        result.elapsedMicroseconds = 1000000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart;
    return result;
}
#ifdef CS2_AIM_SPREAD_CHECKS
void AimSpreadTestRuntime(bool enabled) noexcept { testRuntime = enabled; }
#endif
