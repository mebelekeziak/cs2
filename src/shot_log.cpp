#include "shot_log.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
constexpr unsigned MetadataCapacity = 64, EventCapacity = 256, DedupCapacity = 256;
constexpr unsigned CandidateCapacity = ShotLogPendingCapacity * 2;
enum class EventKind : unsigned { Fire = 1, Impact, Hurt };
struct Pending { bool used = false; ShotLogRecord record{}; };
struct Metadata {
    bool used = false, acknowledged = false, reported = false;
    std::uint64_t publishedMs = 0;
    ShotLogMetadata value{};
};
struct Retired { bool used = false; std::uint64_t id = 0, fireMs = 0; char weapon[48]{}; };
struct Effect {
    bool used = false, reported = false, late = false, lost = false;
    EventKind kind = EventKind::Impact;
    std::uint64_t receiptMs = 0;
    ShotLogHurt hurt{};
    AimVector impact{};
    std::array<std::uint64_t, CandidateCapacity> candidates{};
    unsigned count = 0;
};
struct Delivery { std::uint64_t id = 0, shotId = 0; EventKind kind = EventKind::Fire; };
struct State {
    ShotLogSource source{};
    bool enabled = false;
    std::uint64_t nextId = 1, now = 0, lostUntilMs = 0;
    ShotLogCounters counters{};
    std::array<Pending, ShotLogPendingCapacity> pending{};
    std::array<Metadata, MetadataCapacity> metadata{};
    std::array<Retired, ShotLogPendingCapacity> retired{};
    std::array<Effect, EventCapacity> effects{};
    std::array<Delivery, DedupCapacity> deliveries{};
    std::array<ShotLogRecord, ShotLogCompletedCapacity> completed{};
    unsigned metadataCursor = 0, retiredCursor = 0, deliveryCursor = 0;
    unsigned completedHead = 0, completedCount = 0;
};
State state{};
static_assert(std::is_trivially_copyable_v<State>);

bool Valid(const ShotLogSource& source) noexcept {
    return source.lifecycle && source.controller != UINT32_MAX && source.pawn != UINT32_MAX;
}
bool Accept(const ShotLogSource& source) noexcept {
    if (!state.enabled) return false;
    if (!Valid(source) || source != state.source) { ++state.counters.ignoredSource; return false; }
    return true;
}
bool Elapsed(std::uint64_t now, std::uint64_t before, std::uint64_t duration) noexcept {
    return now > before && now - before > duration;
}
bool Finite(const AimVector& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) < 1000000 && std::abs(value.y) < 1000000 && std::abs(value.z) < 1000000;
}
template<std::size_t N> void Copy(char (&output)[N], const char (&input)[N]) noexcept {
    std::memcpy(output, input, N);
    output[N - 1] = 0;
}
void Normalize(const char (&input)[48], char (&output)[48]) noexcept {
    unsigned begin = 0;
    constexpr char prefix[] = "weapon_";
    bool prefixed = true;
    for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) {
        const char ch = input[i] >= 'A' && input[i] <= 'Z' ? static_cast<char>(input[i] + ('a' - 'A')) : input[i];
        if (ch != prefix[i]) { prefixed = false; break; }
    }
    if (prefixed) begin = sizeof(prefix) - 1;
    unsigned length = 0;
    while (begin + length < sizeof(input) && length < sizeof(output) - 1 && input[begin + length]) {
        const char ch = input[begin + length];
        output[length++] = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
    }
    output[length] = 0;
}
bool WeaponMatch(const char (&left)[48], const char (&right)[48]) noexcept {
    char a[48]{}, b[48]{};
    Normalize(left, a); Normalize(right, b);
    return *a && *b && std::strcmp(a, b) == 0;
}
bool DeliverySeen(EventKind kind, std::uint64_t id, std::uint64_t& shotId) noexcept {
    if (!id) return false;
    for (const auto& delivery : state.deliveries) if (delivery.id == id && delivery.kind == kind) {
        ++state.counters.duplicates; shotId = delivery.shotId; return true;
    }
    return false;
}
void Remember(EventKind kind, std::uint64_t id, std::uint64_t shotId = 0) noexcept {
    if (!id) return;
    auto& delivery = state.deliveries[state.deliveryCursor++ % DedupCapacity];
    if (delivery.id) ++state.counters.dedupEvictions;
    delivery = {id, shotId, kind};
}
void Complete(const ShotLogRecord& record) noexcept {
    if (state.completedCount == ShotLogCompletedCapacity) {
        state.completedHead = (state.completedHead + 1) % ShotLogCompletedCapacity;
        --state.completedCount; ++state.counters.completedDropped;
    }
    state.completed[(state.completedHead + state.completedCount) % ShotLogCompletedCapacity] = record;
    ++state.completedCount; ++state.counters.completed;
}
bool Acknowledges(const ShotLogMetadata& metadata, std::uint64_t fireMs, const char (&weapon)[48]) noexcept {
    if (!WeaponMatch(metadata.weapon, weapon)) return false;
    return fireMs >= metadata.receiptMs ? fireMs - metadata.receiptMs <= ShotLogFireAckWindowMs :
        metadata.receiptMs - fireMs <= ShotLogReorderWindowMs;
}
void CompleteAttempt(Metadata& sample, ShotLogCloseReason close) noexcept {
    if (!sample.used || !sample.value.attackPublished || sample.acknowledged || sample.reported) return;
    ShotLogRecord record{};
    record.kind = ShotLogRecordKind::CommandAttempt;
    record.source = sample.value.source; record.fireMs = sample.publishedMs; record.settledMs = state.now;
    record.metadata = sample.value; record.metadataKnown = true; record.closeReason = close;
    Copy(record.weapon, sample.value.weapon);
    record.reason = close == ShotLogCloseReason::Settled ? ShotLogReason::UnregisteredShot :
        close == ShotLogCloseReason::Capacity ? ShotLogReason::TrackingOverflow : ShotLogReason::Interrupted;
    record.diagnostic = close == ShotLogCloseReason::Settled ? ShotLogDiagnostic::NoFireEvent :
        close == ShotLogCloseReason::Capacity ? ShotLogDiagnostic::Capacity :
        close == ShotLogCloseReason::Stopped ? ShotLogDiagnostic::Stopped : ShotLogDiagnostic::SourceChanged;
    if (close == ShotLogCloseReason::Settled) ++state.counters.unregistered;
    Complete(record); sample.reported = true;
}
Pending* Find(std::uint64_t id) noexcept {
    for (auto& shot : state.pending) if (shot.used && shot.record.id == id) return &shot;
    return nullptr;
}
bool Contains(const Effect& effect, std::uint64_t id) noexcept {
    for (unsigned i = 0; i < std::min(effect.count, CandidateCapacity); ++i)
        if (effect.candidates[i] == id) return true;
    return false;
}
bool Matches(const Effect& effect, std::uint64_t fireMs, const char (&weapon)[48], bool& late) noexcept {
    late = false;
    if (effect.kind == EventKind::Hurt && !WeaponMatch(effect.hurt.weapon, weapon)) return false;
    if (effect.receiptMs < fireMs) return fireMs - effect.receiptMs <= ShotLogReorderWindowMs;
    const auto delay = effect.receiptMs - fireMs;
    if (delay > ShotLogLateGuardMs) return false;
    late = delay > ShotLogOutcomeWindowMs;
    return true;
}
void AddCandidate(Effect& effect, std::uint64_t id, bool late) noexcept {
    if (Contains(effect, id)) return;
    if (effect.count < CandidateCapacity) effect.candidates[effect.count] = id;
    else {
        effect.lost = true;
        for (auto& pending : state.pending) if (pending.used) pending.record.evidenceLost = true;
    }
    ++effect.count;
    effect.late = effect.late || late;
}
void Damage(ShotLogDamage& out, const ShotLogHurt& hurt, std::uint64_t fireMs) noexcept {
    out = {};
    out.victimController = hurt.victimController; out.victimPawn = hurt.victimPawn;
    Copy(out.victimName, hurt.victimName);
    out.hitgroup = hurt.hitgroup; out.damage = hurt.damage; out.remainingHp = hurt.remainingHp;
    const auto difference = hurt.receiptMs >= fireMs ? hurt.receiptMs - fireMs : fireMs - hurt.receiptMs;
    const auto bounded = std::min(difference, static_cast<std::uint64_t>(INT64_MAX));
    out.receiptDelayMs = hurt.receiptMs >= fireMs ? static_cast<std::int64_t>(bounded) : -static_cast<std::int64_t>(bounded);
}
void Evidence(Effect& effect, ShotLogCloseReason reason = ShotLogCloseReason::Settled) noexcept {
    if (effect.kind != EventKind::Hurt || effect.reported) return;
    ShotLogRecord record{};
    record.kind = effect.count > 1 ? ShotLogRecordKind::AmbiguousHit : ShotLogRecordKind::UnmatchedHit;
    record.reason = effect.count > 1 ? ShotLogReason::ShotOverlap : ShotLogReason::Unknown;
    record.eventMs = effect.receiptMs; record.settledMs = state.now;
    record.source = state.source; record.closeReason = reason;
    record.candidateCount = effect.count; record.possibleLateEvent = effect.late;
    record.evidenceLost = effect.lost;
    if (effect.count) {
        record.firstCandidateId = effect.candidates[0];
        record.lastCandidateId = effect.candidates[std::min(effect.count, CandidateCapacity) - 1];
    }
    Copy(record.weapon, effect.hurt.weapon);
    record.damageCount = 1;
    Damage(record.damage[0], effect.hurt, effect.receiptMs);
    Complete(record); effect.reported = true;
}
void Retire(const ShotLogRecord& record) noexcept {
    auto& retired = state.retired[state.retiredCursor++ % state.retired.size()];
    if (retired.used && !Elapsed(state.now, retired.fireMs, ShotLogLateGuardMs + ShotLogReorderWindowMs)) {
        ++state.counters.closedOverflow;
        state.lostUntilMs = state.now > UINT64_MAX - ShotLogLateGuardMs ? UINT64_MAX : state.now + ShotLogLateGuardMs;
        for (auto& pending : state.pending) if (pending.used) pending.record.evidenceLost = true;
    }
    retired.used = true; retired.id = record.id; retired.fireMs = record.fireMs;
    Copy(retired.weapon, record.weapon);
}
bool AimRay(ShotLogRecord& record, AimVector& direction) noexcept {
    const auto& metadata = record.metadata;
    if (!record.metadataKnown || !metadata.capsuleValid || !std::isfinite(metadata.range) ||
        metadata.range <= 0 || metadata.range > 1000000 || !std::isfinite(metadata.aim.pitch) ||
        !std::isfinite(metadata.aim.yaw) || std::abs(metadata.aim.pitch) > 360 || std::abs(metadata.aim.yaw) > 360) return false;
    constexpr double radians = 0.01745329251994329577;
    const double pitch = metadata.aim.pitch * radians, yaw = metadata.aim.yaw * radians;
    direction = {static_cast<float>(std::cos(pitch) * std::cos(yaw)),
        static_cast<float>(std::cos(pitch) * std::sin(yaw)), static_cast<float>(-std::sin(pitch))};
    record.aimRayValid = true;
    record.aimIntersectsCapsule = AimRayIntersectsCapsule(metadata.eye, direction, metadata.range, metadata.capsule);
    if (std::isfinite(metadata.spread) && std::isfinite(metadata.inaccuracy) && metadata.spread >= 0 && metadata.inaccuracy >= 0)
        record.spreadCone = static_cast<float>(std::atan(static_cast<double>(metadata.spread) + metadata.inaccuracy) / radians);
    return true;
}
void Geometry(ShotLogRecord& record, const AimVector& impact, const AimVector& aimDirection) noexcept {
    if (!record.metadataKnown || !record.metadata.capsuleValid) return;
    const auto& metadata = record.metadata;
    const AimVector direction{impact.x - metadata.eye.x, impact.y - metadata.eye.y, impact.z - metadata.eye.z};
    const double distance = std::sqrt(static_cast<double>(direction.x) * direction.x +
        static_cast<double>(direction.y) * direction.y + static_cast<double>(direction.z) * direction.z);
    if (distance <= 0 || distance > 1000000) return;
    if (AimRayCapsuleMissDistance(metadata.eye, direction, static_cast<float>(distance), metadata.capsule) < 0) return;
    ++record.geometryImpacts;
    if (distance >= record.impactDistance) { record.impactDistance = static_cast<float>(distance); record.furthestImpact = impact; }
    const bool segmentHit = AimRayIntersectsCapsule(metadata.eye, direction, static_cast<float>(distance), metadata.capsule);
    if (segmentHit) record.trajectory = ShotLogTrajectory::IntersectsRecordedCapsule;
    else if (record.trajectory == ShotLogTrajectory::Unknown) record.trajectory = ShotLogTrajectory::MissesRecordedCapsule;
    if (!record.aimRayValid) return;
    // Extend only the direction for diagnostics. A short segment must never be
    // mistaken for spread, and later penetration impacts may reach the capsule.
    const bool rayHit = AimRayIntersectsCapsule(metadata.eye, direction, metadata.range, metadata.capsule);
    if (!segmentHit && rayHit) ++record.shortImpacts;
    const float miss = AimRayCapsuleMissDistance(metadata.eye, direction, metadata.range, metadata.capsule);
    if (miss >= 0 && (record.missDistance < 0 || miss < record.missDistance)) record.missDistance = miss;
    const double cosine = (static_cast<double>(direction.x) * aimDirection.x +
        static_cast<double>(direction.y) * aimDirection.y + static_cast<double>(direction.z) * aimDirection.z) / distance;
    constexpr double degrees = 57.29577951308232;
    const float angle = static_cast<float>(std::acos(std::clamp(cosine, -1.0, 1.0)) * degrees);
    record.impactAngle = std::max(record.impactAngle, angle);
    if (!rayHit && !segmentHit && metadata.chanceValid && metadata.spread + metadata.inaccuracy > 0 &&
        angle <= record.spreadCone + 0.01f) ++record.spreadMissImpacts;
}
void Classify(ShotLogRecord& record) noexcept {
    const auto assign = [&](ShotLogReason reason, ShotLogDiagnostic diagnostic) {
        record.reason = reason; record.diagnostic = diagnostic;
    };
    if (record.outcome == ShotLogOutcome::Hit) return assign(ShotLogReason::None, ShotLogDiagnostic::None);
    if (record.closeReason == ShotLogCloseReason::Capacity || record.evidenceLost)
        return assign(ShotLogReason::TrackingOverflow, ShotLogDiagnostic::Capacity);
    if (record.closeReason != ShotLogCloseReason::Settled)
        return assign(ShotLogReason::Interrupted, record.closeReason == ShotLogCloseReason::Stopped ?
            ShotLogDiagnostic::Stopped : ShotLogDiagnostic::SourceChanged);
    if (record.overlappingEvents) return assign(ShotLogReason::ShotOverlap, ShotLogDiagnostic::OverlappingEvents);
    if (record.possibleLateEvent) return assign(ShotLogReason::LateEvent, ShotLogDiagnostic::LateDelivery);
    if (record.ambiguousEvents) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::InvalidTrajectory);
    if (!record.uniqueImpacts) return assign(ShotLogReason::NoImpact, ShotLogDiagnostic::NoImpactEvent);
    if (!record.metadataKnown) return assign(ShotLogReason::Unknown, record.metadataAmbiguous ?
        ShotLogDiagnostic::OverlappingMetadata : ShotLogDiagnostic::NoMetadata);
    if (!record.metadata.capsuleValid) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::NoCapsule);
    if (!record.aimRayValid) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::InvalidAim);
    if (!record.geometryImpacts) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::InvalidTrajectory);
    if (record.trajectory == ShotLogTrajectory::IntersectsRecordedCapsule)
        return assign(ShotLogReason::Unknown, ShotLogDiagnostic::ImpactCrossedCapsule);
    if (!record.aimIntersectsCapsule) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::AimMissedCapsule);
    if (record.geometryImpacts != record.uniqueImpacts) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::InvalidTrajectory);
    if (record.shortImpacts == record.geometryImpacts)
        return assign(ShotLogReason::ImpactShort, ShotLogDiagnostic::ImpactBeforeCapsule);
    if (record.spreadMissImpacts == record.geometryImpacts)
        return assign(ShotLogReason::Spread, ShotLogDiagnostic::ImpactMissedCapsule);
    if (record.shortImpacts) return assign(ShotLogReason::Unknown, ShotLogDiagnostic::MixedImpacts);
    return assign(ShotLogReason::Unknown, record.metadata.chanceValid ? ShotLogDiagnostic::OutsideSpreadCone :
        ShotLogDiagnostic::ImpactMissedCapsule);
}
void Finish(Pending& pending, ShotLogCloseReason reason) noexcept {
    auto& record = pending.record;
    record.settledMs = state.now; record.closeReason = reason;
    AimVector aimDirection{};
    AimRay(record, aimDirection);
    for (auto& effect : state.effects) {
        if (!effect.used || !Contains(effect, record.id)) continue;
        const bool unique = effect.count == 1 && !effect.late && !effect.lost;
        record.possibleLateEvent = record.possibleLateEvent || effect.late;
        record.reorderedEvent = record.reorderedEvent || effect.receiptMs < record.fireMs;
        if (!unique) ++record.ambiguousEvents;
        if (effect.count > 1) ++record.overlappingEvents;
        if (effect.kind == EventKind::Hurt) {
            if (unique && reason == ShotLogCloseReason::Settled && !record.evidenceLost &&
                !record.possibleLateEvent && !effect.reported) {
                if (record.damageCount < record.damage.size()) {
                    Damage(record.damage[record.damageCount++], effect.hurt, record.fireMs);
                    effect.reported = true;
                } else record.damageOverflow = true;
            } else if (unique && effect.reported) ++record.ambiguousEvents;
        } else if (unique) {
            ++record.uniqueImpacts;
            Geometry(record, effect.impact, aimDirection);
        }
    }
    if (reason != ShotLogCloseReason::Settled || record.evidenceLost)
        record.outcome = ShotLogOutcome::Unconfirmed;
    else if (record.damageCount) record.outcome = ShotLogOutcome::Hit;
    else if (record.ambiguousEvents || record.possibleLateEvent) record.outcome = ShotLogOutcome::Unconfirmed;
    else record.outcome = ShotLogOutcome::Miss;
    Classify(record);
    Complete(record); Retire(record); pending.used = false;
}
void ClearContext() noexcept {
    for (auto& shot : state.pending) shot.used = false;
    for (auto& metadata : state.metadata) metadata.used = false;
    for (auto& retired : state.retired) retired.used = false;
    for (auto& effect : state.effects) effect.used = false;
    for (auto& delivery : state.deliveries) delivery = {};
    state.metadataCursor = state.retiredCursor = state.deliveryCursor = 0;
    state.lostUntilMs = 0;
}
void SelectMetadata(ShotLogRecord& record) noexcept {
    const ShotLogMetadata* selected = nullptr;
    bool tied = false;
    for (const auto& sample : state.metadata) {
        if (!sample.used || sample.value.receiptMs > record.fireMs ||
            record.fireMs - sample.value.receiptMs > ShotLogMetadataWindowMs ||
            !WeaponMatch(sample.value.weapon, record.weapon)) continue;
        if (!selected || sample.value.receiptMs > selected->receiptMs) { selected = &sample.value; tied = false; }
        else if (sample.value.receiptMs == selected->receiptMs && sample.value.commandSequence != selected->commandSequence)
            tied = true;
    }
    record.metadataAmbiguous = tied;
    if (selected && !tied) {
        record.metadata = *selected; record.metadataKnown = true;
        record.metadataAgeMs = record.fireMs - selected->receiptMs;
    }
}
Effect& AllocateEffect() noexcept {
    for (auto& effect : state.effects) if (!effect.used) { effect = {}; effect.used = true; return effect; }
    ++state.counters.eventOverflow;
    auto* oldest = &state.effects[0];
    for (auto& effect : state.effects) if (effect.receiptMs < oldest->receiptMs) oldest = &effect;
    oldest->lost = true; Evidence(*oldest, ShotLogCloseReason::Capacity);
    for (auto& shot : state.pending) if (shot.used) shot.record.evidenceLost = true;
    *oldest = {}; oldest->used = oldest->lost = true;
    return *oldest;
}
void MatchExisting(Effect& effect) noexcept {
    for (const auto& pending : state.pending) if (pending.used) {
        bool late = false;
        if (Matches(effect, pending.record.fireMs, pending.record.weapon, late)) AddCandidate(effect, pending.record.id, late);
    }
    for (const auto& retired : state.retired) if (retired.used) {
        bool late = false;
        if (Matches(effect, retired.fireMs, retired.weapon, late)) {
            AddCandidate(effect, retired.id, true);
        }
    }
    if (effect.late) ++state.counters.lateEvents;
}

struct Text {
    char* output;
    std::size_t capacity, length = 0;
    bool complete = true;
    void Add(const char* format, ...) noexcept {
        if (!complete || !capacity) return;
        va_list args;
        va_start(args, format);
        const int result = std::vsnprintf(output + length, capacity - length, format, args);
        va_end(args);
        if (result < 0 || static_cast<std::size_t>(result) >= capacity - length) {
            output[capacity - 1] = 0; complete = false;
        } else length += static_cast<std::size_t>(result);
    }
};
template<std::size_t N> void Name(const char (&input)[N], char (&output)[64]) noexcept {
    unsigned length = 0;
    bool space = false;
    for (unsigned i = 0; i < sizeof(input) && input[i] && length < sizeof(output) - 1;) {
        const auto ch = static_cast<unsigned char>(input[i]);
        unsigned width = ch < 0x80 ? 1 : ch >= 0xC2 && ch <= 0xDF ? 2 :
            ch >= 0xE0 && ch <= 0xEF ? 3 : ch >= 0xF0 && ch <= 0xF4 ? 4 : 0;
        std::uint32_t point = width == 1 ? ch : width == 2 ? ch & 0x1F : width == 3 ? ch & 0xF : ch & 7;
        bool valid = width && i + width <= sizeof(input);
        for (unsigned n = 1; valid && n < width; ++n) {
            const auto next = static_cast<unsigned char>(input[i + n]);
            valid = (next & 0xC0) == 0x80;
            point = (point << 6) | (next & 0x3F);
        }
        valid = valid && (width != 2 || point >= 0x80) && (width != 3 || point >= 0x800) &&
            (width != 4 || point >= 0x10000) && point <= 0x10FFFF && !(point >= 0xD800 && point <= 0xDFFF);
        if (!valid) { output[length++] = '?'; ++i; continue; }
        const bool whitespace = point <= 0x20 || (point >= 0x7F && point <= 0x9F) || point == 0x061C ||
            point == 0x200E || point == 0x200F || (point >= 0x2028 && point <= 0x202E) ||
            (point >= 0x2066 && point <= 0x2069) || point == 0xFEFF;
        if (whitespace) { space = length != 0; i += width; continue; }
        if (space && length < sizeof(output) - 2) output[length++] = ' ';
        space = false;
        if (width > sizeof(output) - 1 - length) break;
        if (point == '|') output[length++] = '/';
        else { std::memcpy(output + length, input + i, width); length += width; }
        i += width;
    }
    output[length] = 0;
    if (!length) std::memcpy(output, "unknown", 8);
}
const char* Hitgroup(int value) noexcept {
    switch (value) {
    case 1: return "head"; case 2: return "chest"; case 3: return "stomach";
    case 4: return "left arm"; case 5: return "right arm"; case 6: return "left leg";
    case 7: return "right leg"; case 8: return "neck"; case 10: return "gear";
    default: return "body";
    }
}
void DamageText(Text& text, const ShotLogDamage& damage) noexcept {
    char name[64]{};
    Name(damage.victimName, name);
    text.Add("%s in %s for %d (%d HP)", name, Hitgroup(damage.hitgroup), damage.damage, damage.remainingHp);
}
void CloseText(Text& text, const ShotLogRecord& record) noexcept {
    if (record.closeReason == ShotLogCloseReason::SourceChanged) text.Add(" | source changed before settlement");
    else if (record.closeReason == ShotLogCloseReason::Stopped) text.Add(" | stopped before settlement");
    else if (record.closeReason == ShotLogCloseReason::Capacity || record.evidenceLost) text.Add(" | tracking capacity reached");
}
}

void ShotLogInitialize(const ShotLogSource& source, std::uint64_t nowMs) noexcept {
    // Avoid a State{} temporary on the engine owner's limited stack.
    std::memset(&state, 0, sizeof(state));
    state.source = source; state.enabled = true; state.nextId = 1; state.now = nowMs;
}
void ShotLogReset(const ShotLogSource& source, std::uint64_t nowMs, ShotLogCloseReason reason) noexcept {
    state.now = std::max(state.now, nowMs);
    for (auto& shot : state.pending) if (shot.used) Finish(shot, reason);
    for (auto& effect : state.effects) if (effect.used) Evidence(effect, reason);
    for (auto& sample : state.metadata) CompleteAttempt(sample, reason);
    ClearContext(); state.source = source;
}
void ShotLogSetEnabled(bool enabled, std::uint64_t nowMs) noexcept {
    state.now = std::max(state.now, nowMs);
    if (state.enabled == enabled) return;
    if (!enabled) {
        for (const auto& shot : state.pending) if (shot.used) ++state.counters.abandonedDisabled;
        for (const auto& effect : state.effects)
            if (effect.used && effect.kind == EventKind::Hurt && !effect.reported) ++state.counters.abandonedDisabled;
        for (const auto& sample : state.metadata) if (sample.used && sample.value.attackPublished &&
            !sample.acknowledged && !sample.reported) ++state.counters.abandonedDisabled;
        state.counters.abandonedDisabled += state.completedCount;
        state.completedCount = state.completedHead = 0;
    }
    ClearContext(); state.enabled = enabled;
}
void ShotLogStop(std::uint64_t nowMs) noexcept {
    ShotLogReset({}, nowMs, ShotLogCloseReason::Stopped);
    state.enabled = false;
}
void ShotLogAddMetadata(const ShotLogMetadata& metadata) noexcept {
    if (!Accept(metadata.source)) return;
    if (metadata.commandSequence <= 0) { ++state.counters.invalid; return; }
    state.now = std::max(state.now, metadata.receiptMs);
    Metadata* destination = nullptr;
    for (auto& sample : state.metadata) if (sample.used &&
        sample.value.commandSequence == metadata.commandSequence && sample.value.weaponHandle == metadata.weaponHandle) {
        if (sample.value.receiptMs > metadata.receiptMs) return;
        destination = &sample; break;
    }
    if (!destination) {
        destination = &state.metadata[state.metadataCursor++ % MetadataCapacity];
        if (destination->used) { ++state.counters.metadataOverflow; CompleteAttempt(*destination, ShotLogCloseReason::Capacity); }
        *destination = {};
    }
    const bool previouslyPublished = destination->used && destination->value.attackPublished;
    if (!previouslyPublished && metadata.attackPublished) destination->publishedMs = metadata.receiptMs;
    destination->used = true; destination->value = metadata;
    destination->value.weapon[47] = destination->value.targetName[63] = 0;
    auto& value = destination->value;
    value.attackPublished = value.attackPublished || previouslyPublished;
    if (!std::isfinite(value.hitchance) || value.hitchance < 0 || value.hitchance > 100 || !value.samples || value.samples > 65536 ||
        value.hits > value.samples || !std::isfinite(value.spread) || value.spread < 0 || value.spread > 1 ||
        !std::isfinite(value.inaccuracy) || value.inaccuracy < 0 || value.inaccuracy > 1) value.chanceValid = false;
    if (!Finite(value.eye) || !Finite(value.capsule.start) || !Finite(value.capsule.end) ||
        !std::isfinite(value.capsule.radius) || value.capsule.radius <= 0 || value.capsule.radius > 10000) value.capsuleValid = false;
    for (const auto& pending : state.pending) if (pending.used && Acknowledges(value, pending.record.fireMs, pending.record.weapon))
        destination->acknowledged = true;
    for (const auto& retired : state.retired) if (retired.used && Acknowledges(value, retired.fireMs, retired.weapon))
        destination->acknowledged = true;
}
std::uint64_t ShotLogOnFire(const ShotLogFire& event) noexcept {
    if (!Accept(event.source)) return 0;
    std::uint64_t previous = 0;
    if (DeliverySeen(EventKind::Fire, event.deliveryId, previous)) return previous;
    const bool delayedDelivery = Elapsed(state.now, event.receiptMs, ShotLogReorderWindowMs);
    state.now = std::max(state.now, event.receiptMs);
    Pending* destination = nullptr;
    for (auto& shot : state.pending) if (!shot.used) { destination = &shot; break; }
    if (!destination) {
        destination = &state.pending[0];
        for (auto& shot : state.pending) if (shot.record.id < destination->record.id) destination = &shot;
        ++state.counters.pendingOverflow;
        Finish(*destination, ShotLogCloseReason::Capacity);
    }
    *destination = {}; destination->used = true;
    auto& record = destination->record;
    record.id = state.nextId++; record.fireMs = event.receiptMs; record.source = event.source;
    record.evidenceLost = state.lostUntilMs && event.receiptMs <= state.lostUntilMs;
    record.possibleLateEvent = delayedDelivery;
    if (delayedDelivery) ++state.counters.lateEvents;
    Copy(record.weapon, event.weapon); SelectMetadata(record);
    // An overlapping fire can acknowledge several candidate commands. Suppress
    // their timeout rather than claiming which command the native fire used.
    for (auto& sample : state.metadata) if (sample.used && Acknowledges(sample.value, event.receiptMs, event.weapon))
        sample.acknowledged = true;
    ++state.counters.fires; Remember(EventKind::Fire, event.deliveryId, record.id);
    for (auto& effect : state.effects) if (effect.used) {
        bool late = false;
        if (Matches(effect, record.fireMs, record.weapon, late)) {
            const bool previouslyLate = effect.late;
            AddCandidate(effect, record.id, late);
            if (!previouslyLate && effect.late) ++state.counters.lateEvents;
        }
    }
    return record.id;
}
void ShotLogOnImpact(const ShotLogImpact& event) noexcept {
    if (!Accept(event.source)) return;
    if (!Finite(event.position)) { ++state.counters.invalid; return; }
    std::uint64_t ignored = 0;
    if (DeliverySeen(EventKind::Impact, event.deliveryId, ignored)) return;
    const bool delayedDelivery = Elapsed(state.now, event.receiptMs, ShotLogReorderWindowMs);
    state.now = std::max(state.now, event.receiptMs);
    auto& effect = AllocateEffect(); effect.kind = EventKind::Impact;
    effect.receiptMs = event.receiptMs; effect.impact = event.position; effect.late = delayedDelivery;
    MatchExisting(effect); Remember(EventKind::Impact, event.deliveryId);
}
void ShotLogOnHurt(const ShotLogHurt& event) noexcept {
    if (!Accept(event.source)) return;
    if (event.damage < 0 || event.damage > 100000 || event.remainingHp < 0 || event.remainingHp > 100000) {
        ++state.counters.invalid; return;
    }
    std::uint64_t ignored = 0;
    if (DeliverySeen(EventKind::Hurt, event.deliveryId, ignored)) return;
    const bool delayedDelivery = Elapsed(state.now, event.receiptMs, ShotLogReorderWindowMs);
    state.now = std::max(state.now, event.receiptMs);
    auto& effect = AllocateEffect(); effect.kind = EventKind::Hurt;
    effect.receiptMs = event.receiptMs; effect.hurt = event; effect.late = delayedDelivery;
    effect.hurt.weapon[47] = effect.hurt.victimName[63] = 0;
    MatchExisting(effect); Remember(EventKind::Hurt, event.deliveryId);
}
void ShotLogAdvance(std::uint64_t nowMs) noexcept {
    state.now = std::max(state.now, nowMs);
    if (!state.enabled) return;
    for (auto& sample : state.metadata) if (sample.used &&
        Elapsed(state.now, sample.publishedMs, ShotLogFireAckWindowMs + ShotLogReorderWindowMs))
        CompleteAttempt(sample, ShotLogCloseReason::Settled);
    for (auto& shot : state.pending) if (shot.used &&
        Elapsed(state.now, shot.record.fireMs, ShotLogOutcomeWindowMs + ShotLogReorderWindowMs))
        Finish(shot, ShotLogCloseReason::Settled);
    for (auto& effect : state.effects) if (effect.used) {
        if (effect.kind == EventKind::Hurt && !effect.reported &&
            Elapsed(state.now, effect.receiptMs, ShotLogReorderWindowMs)) {
            const bool waitingUnique = effect.count == 1 && !effect.late && !effect.lost && Find(effect.candidates[0]);
            if (!waitingUnique) {
                if (!effect.count) ++state.counters.orphanEvents;
                Evidence(effect);
            }
        }
        if (Elapsed(state.now, effect.receiptMs, ShotLogLateGuardMs + ShotLogReorderWindowMs)) {
            if (effect.kind == EventKind::Hurt) Evidence(effect);
            else if (!effect.count) ++state.counters.orphanEvents;
            effect.used = false;
        }
    }
    for (auto& retired : state.retired) if (retired.used &&
        Elapsed(state.now, retired.fireMs, ShotLogLateGuardMs + ShotLogReorderWindowMs)) retired.used = false;
}
bool ShotLogPollCompleted(ShotLogRecord& record) noexcept {
    if (!state.completedCount) return false;
    record = state.completed[state.completedHead];
    state.completedHead = (state.completedHead + 1) % ShotLogCompletedCapacity;
    --state.completedCount;
    return true;
}
ShotLogCounters ShotLogGetCounters() noexcept { return state.counters; }
const char* ShotLogReasonText(ShotLogReason reason) noexcept {
    switch (reason) {
    case ShotLogReason::None: return "none";
    case ShotLogReason::Spread: return "spread";
    case ShotLogReason::ImpactShort: return "impact short";
    case ShotLogReason::NoImpact: return "no impact";
    case ShotLogReason::UnregisteredShot: return "unregistered shot";
    case ShotLogReason::ShotOverlap: return "shot overlap";
    case ShotLogReason::LateEvent: return "late event";
    case ShotLogReason::Interrupted: return "interrupted";
    case ShotLogReason::TrackingOverflow: return "tracking overflow";
    default: return "unknown";
    }
}
const char* ShotLogDiagnosticText(ShotLogDiagnostic diagnostic) noexcept {
    switch (diagnostic) {
    case ShotLogDiagnostic::None: return "none";
    case ShotLogDiagnostic::NoFireEvent: return "published attack has no observed fire event";
    case ShotLogDiagnostic::NoImpactEvent: return "fire observed without an impact or hurt";
    case ShotLogDiagnostic::OverlappingEvents: return "events fit several shots";
    case ShotLogDiagnostic::LateDelivery: return "event exceeded timing allowance";
    case ShotLogDiagnostic::SourceChanged: return "source changed before settlement";
    case ShotLogDiagnostic::Stopped: return "stopped before settlement";
    case ShotLogDiagnostic::Capacity: return "tracking capacity reached";
    case ShotLogDiagnostic::NoMetadata: return "no aim context";
    case ShotLogDiagnostic::OverlappingMetadata: return "several possible aim commands";
    case ShotLogDiagnostic::NoCapsule: return "target capsule unavailable";
    case ShotLogDiagnostic::InvalidAim: return "aim ray unavailable";
    case ShotLogDiagnostic::AimMissedCapsule: return "aim ray missed saved capsule";
    case ShotLogDiagnostic::ImpactMissedCapsule: return "impact ray missed saved capsule";
    case ShotLogDiagnostic::ImpactBeforeCapsule: return "impact ended before saved capsule";
    case ShotLogDiagnostic::ImpactCrossedCapsule: return "impact crossed saved capsule without damage";
    case ShotLogDiagnostic::OutsideSpreadCone: return "impact outside saved spread cone";
    case ShotLogDiagnostic::MixedImpacts: return "impacts disagree on path";
    case ShotLogDiagnostic::InvalidTrajectory: return "impact direction unavailable";
    default: return "insufficient evidence";
    }
}
bool ShotLogFormat(const ShotLogRecord& record, char* output, std::size_t capacity) noexcept {
    if (!output || !capacity) return false;
    *output = 0;
    Text text{output, capacity};
    if (record.kind == ShotLogRecordKind::AmbiguousHit || record.kind == ShotLogRecordKind::UnmatchedHit) {
        text.Add("AIM_HIT HIT ");
        if (record.damageCount) DamageText(text, record.damage[0]);
        else text.Add("confirmed damage event");
        char normalized[48]{}, weapon[64]{};
        Normalize(record.weapon, normalized); Name(normalized, weapon);
        text.Add(" | weapon %s", weapon);
        if (record.candidateCount > 1) text.Add(" | shot overlap (%u possible shots #%llu..#%llu)",
            record.candidateCount, record.firstCandidateId, record.lastCandidateId);
        else if (record.candidateCount) text.Add(" | shot unknown (late or incomplete event; candidate #%llu)", record.firstCandidateId);
        else text.Add(" | shot unknown (no matching fire event)");
        if (record.possibleLateEvent) text.Add(" | possible late event");
        CloseText(text, record);
        return text.complete;
    }
    if (record.kind == ShotLogRecordKind::CommandAttempt) text.Add("AIM_SHOT cmd #%d ", record.metadata.commandSequence);
    else text.Add("AIM_SHOT #%llu ", record.id);
    if (record.outcome == ShotLogOutcome::Hit && record.damageCount) {
        text.Add("HIT ");
        for (unsigned i = 0; i < std::min(record.damageCount, ShotLogDamageCapacity); ++i) {
            if (i) text.Add("; ");
            DamageText(text, record.damage[i]);
        }
    } else {
        text.Add("%s %s", record.outcome == ShotLogOutcome::Miss ? "MISS" : "UNCONFIRMED", ShotLogReasonText(record.reason));
        if (record.reason == ShotLogReason::Unknown) text.Add(": %s", ShotLogDiagnosticText(record.diagnostic));
    }
    char normalized[48]{}, weapon[64]{};
    Normalize(record.weapon, normalized); Name(normalized, weapon);
    text.Add(" | weapon %s", weapon);
    if (record.metadataKnown) {
        char target[64]{}; Name(record.metadata.targetName, target);
        text.Add(" | target %s [0x%08x]", target, record.metadata.targetPawn);
    }
    if (record.metadataKnown && record.metadata.chanceValid && std::isfinite(record.metadata.hitchance))
        text.Add(" | HC %.0f%% (%u/%u)", record.metadata.hitchance, record.metadata.hits, record.metadata.samples);
    else text.Add(" | HC unknown");
    if (record.outcome == ShotLogOutcome::Hit && record.damageCount) {
        text.Add(" | delay %lldms", record.damage[0].receiptDelayMs);
        if (record.reorderedEvent) text.Add(" (receipt order)");
    } else if (record.closeReason == ShotLogCloseReason::Settled) {
        if (record.reason == ShotLogReason::Spread) text.Add(" | capsule miss %.2fu", record.missDistance);
        if (record.kind == ShotLogRecordKind::CommandAttempt) text.Add(" | no fire event after %llums", ShotLogFireAckWindowMs);
    }
    if (record.damageOverflow) text.Add(" | additional damage events logged separately");
    CloseText(text, record);
    return text.complete;
}
