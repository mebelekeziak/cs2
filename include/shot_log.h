#pragma once
#include "aim_hitchance.h"
#include <array>
#include <cstddef>
#include <cstdint>

// Pure copied-value tracker. All calls, including Poll/Advance, must be
// serialized by its owner. No engine pointers, allocation, clock or sink I/O.
inline constexpr std::uint64_t ShotLogMetadataWindowMs = 250;
inline constexpr std::uint64_t ShotLogOutcomeWindowMs = 250;
inline constexpr std::uint64_t ShotLogReorderWindowMs = 60;
inline constexpr std::uint64_t ShotLogLateGuardMs = 1000;
inline constexpr std::uint64_t ShotLogFireAckWindowMs = 1000;
inline constexpr unsigned ShotLogPendingCapacity = 64;
inline constexpr unsigned ShotLogCompletedCapacity = 64;
inline constexpr unsigned ShotLogDamageCapacity = 8;

struct ShotLogSource {
    std::uint64_t lifecycle = 0;
    std::uint32_t controller = UINT32_MAX, pawn = UINT32_MAX;
    bool operator==(const ShotLogSource&) const = default;
};

struct ShotLogMetadata {
    ShotLogSource source{};
    std::uint64_t receiptMs = 0;
    int commandSequence = 0;
    std::uint32_t weaponHandle = UINT32_MAX, targetController = UINT32_MAX, targetPawn = UINT32_MAX;
    char weapon[48]{}, targetName[64]{};
    AimVector eye{};
    AimAngles aim{};
    AimCapsule capsule{};
    float hitchance = 0, spread = 0, inaccuracy = 0, range = 0;
    unsigned hits = 0, samples = 0;
    bool automatic = false, chanceValid = false, capsuleValid = false;
    // Set only after publishing a ready primary attack. Estimates and held
    // attacks during cooldown must not produce unregistered-shot timeouts.
    bool attackPublished = false;
    // Copied direct-hit estimate for the intended target; never actual damage.
    bool damageValid = false;
    float predictedDamage = 0;
    int minDamage = 1;
    int predictedHitgroup = 0;
};

// deliveryId optionally identifies a copied delivery occurrence. Repeating the
// same nonzero ID/kind is a duplicate. It is NOT a native shot/event ID. When
// zero, identical events remain distinct because legitimate bursts can share
// both payload and millisecond receipt time.
struct ShotLogFire {
    ShotLogSource source{};
    std::uint64_t receiptMs = 0, deliveryId = 0;
    char weapon[48]{};
};
struct ShotLogImpact {
    ShotLogSource source{};
    std::uint64_t receiptMs = 0, deliveryId = 0;
    AimVector position{};
};
struct ShotLogHurt {
    ShotLogSource source{}; // Attacker, including full serialized handles.
    std::uint64_t receiptMs = 0, deliveryId = 0;
    std::uint32_t victimController = UINT32_MAX, victimPawn = UINT32_MAX;
    char victimName[64]{}, weapon[48]{};
    int hitgroup = 0, damage = 0, remainingHp = 0;
};

enum class ShotLogOutcome { Hit, Miss, Unconfirmed };
enum class ShotLogRecordKind { Shot, AmbiguousHit, UnmatchedHit, CommandAttempt };
enum class ShotLogCloseReason { Settled, SourceChanged, Stopped, Capacity };
enum class ShotLogTrajectory { Unknown, IntersectsRecordedCapsule, MissesRecordedCapsule };
enum class ShotLogReason { None, Spread, ImpactShort, NoImpact, UnregisteredShot,
    ShotOverlap, LateEvent, Interrupted, TrackingOverflow, Unknown };
enum class ShotLogDiagnostic { None, NoFireEvent, NoImpactEvent, OverlappingEvents,
    LateDelivery, SourceChanged, Stopped, Capacity, NoMetadata, OverlappingMetadata,
    NoCapsule, InvalidAim, AimMissedCapsule, ImpactMissedCapsule, ImpactBeforeCapsule,
    ImpactCrossedCapsule, OutsideSpreadCone, MixedImpacts, InvalidTrajectory };
struct ShotLogDamage {
    std::uint32_t victimController = UINT32_MAX, victimPawn = UINT32_MAX;
    char victimName[64]{};
    int hitgroup = 0, damage = 0, remainingHp = 0;
    std::int64_t receiptDelayMs = 0;
};
struct ShotLogRecord {
    ShotLogRecordKind kind = ShotLogRecordKind::Shot;
    std::uint64_t id = 0, fireMs = 0, settledMs = 0;
    std::uint64_t eventMs = 0, firstCandidateId = 0, lastCandidateId = 0;
    unsigned candidateCount = 0;
    ShotLogSource source{};
    char weapon[48]{};
    ShotLogMetadata metadata{};
    std::uint64_t metadataAgeMs = 0;
    bool metadataKnown = false, metadataAmbiguous = false;
    ShotLogOutcome outcome = ShotLogOutcome::Unconfirmed;
    ShotLogCloseReason closeReason = ShotLogCloseReason::Settled;
    ShotLogTrajectory trajectory = ShotLogTrajectory::Unknown;
    ShotLogReason reason = ShotLogReason::Unknown;
    ShotLogDiagnostic diagnostic = ShotLogDiagnostic::None;
    bool aimRayValid = false, aimIntersectsCapsule = false;
    unsigned geometryImpacts = 0, shortImpacts = 0, spreadMissImpacts = 0;
    float missDistance = -1, impactDistance = 0, impactAngle = 0, spreadCone = 0;
    AimVector furthestImpact{};
    std::array<ShotLogDamage, ShotLogDamageCapacity> damage{};
    unsigned damageCount = 0, uniqueImpacts = 0, ambiguousEvents = 0, overlappingEvents = 0;
    bool damageOverflow = false, evidenceLost = false, possibleLateEvent = false, reorderedEvent = false;
};
struct ShotLogCounters {
    std::uint64_t fires = 0, completed = 0, duplicates = 0, ignoredSource = 0, invalid = 0;
    std::uint64_t pendingOverflow = 0, closedOverflow = 0, eventOverflow = 0, metadataOverflow = 0, completedDropped = 0;
    std::uint64_t dedupEvictions = 0, lateEvents = 0, orphanEvents = 0, abandonedDisabled = 0, unregistered = 0;
};

struct ShotLogDamageSummary {
    bool predictionKnown = false, actualKnown = false, targetActualKnown = false;
    bool partial = false;
    int actualDamage = 0, targetActualDamage = 0;
};
// Actual values sum confirmed native hurt events retained in this record.
// Missing or ambiguous shot attribution never becomes zero observed damage.
ShotLogDamageSummary ShotLogSummarizeDamage(const ShotLogRecord& record) noexcept;

// Initialize is a fresh tracker (IDs/counters reset). Reset closes current
// pending shots as UNCONFIRMED, starts a new source and retains completed lines
// and monotonic shot IDs. Use a new lifecycle cookie on map/round/pawn changes.
void ShotLogInitialize(const ShotLogSource& source, std::uint64_t nowMs) noexcept;
void ShotLogReset(const ShotLogSource& source, std::uint64_t nowMs,
    ShotLogCloseReason reason = ShotLogCloseReason::SourceChanged) noexcept;
void ShotLogSetEnabled(bool enabled, std::uint64_t nowMs) noexcept;
void ShotLogStop(std::uint64_t nowMs) noexcept;
void ShotLogAddMetadata(const ShotLogMetadata& metadata) noexcept;
std::uint64_t ShotLogOnFire(const ShotLogFire& event) noexcept;
void ShotLogOnImpact(const ShotLogImpact& event) noexcept;
void ShotLogOnHurt(const ShotLogHurt& event) noexcept;

// A result settles strictly AFTER fire+250+60ms so events at a boundary can
// still add candidates. HIT requires at least one uniquely attributable hurt.
// Overlapping candidates and late-event guards are never resolved by target
// preference or FIFO. MISS means no confirmed damage in this receipt window,
// not proof of a server-side cause. Spread is a geometric inference against
// the copied capsule and cone. Short impacts do not prove world occlusion.
// Every ambiguous/unmatched hurt also produces a separate evidence record, so
// confirmed damage is visible even when no shot can honestly claim it.
void ShotLogAdvance(std::uint64_t nowMs) noexcept;
bool ShotLogPollCompleted(ShotLogRecord& record) noexcept;
ShotLogCounters ShotLogGetCounters() noexcept;

// One bounded human line, no trailing newline. Names/control bytes are cleaned;
// native printf consumers must still pass this line through a literal "%s".
bool ShotLogFormat(const ShotLogRecord& record, char* output, std::size_t capacity) noexcept;
const char* ShotLogReasonText(ShotLogReason reason) noexcept;
const char* ShotLogDiagnosticText(ShotLogDiagnostic diagnostic) noexcept;
