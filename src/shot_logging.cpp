#include "shot_logging.h"
#include "shot_log_settings.h"
#include "shot_events.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include "movement_telemetry.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
SRWLOCK stateLock = SRWLOCK_INIT;
std::atomic<bool> active{false}, reportedReady{false};
ShotLoggingCallback sink = nullptr;
bool prepared = false, stopped = false;
std::uintptr_t client = 0;
ShotLogSource source{};
std::uint64_t generation = 1, lastDropCount = 0;
std::uint64_t lastSourceMs = 0;
bool sourceSeen = false;
#ifdef CS2_SHOT_LOGGING_CHECKS
bool testClock = false;
std::uint64_t testNow = 0;
ShotLogSource testSource{};
#endif
std::uint64_t Now() noexcept {
#ifdef CS2_SHOT_LOGGING_CHECKS
    if (testClock) return testNow;
#endif
    return GetTickCount64();
}
template<class T> bool Read(std::uintptr_t at, T& value) noexcept { return MovementCopy(at, &value, sizeof(value)); }
bool Identity(std::uintptr_t system, std::uintptr_t entity, std::uint32_t& handle) noexcept {
    namespace off = cs2_offsets;
    std::uintptr_t identity = 0, back = 0, bucket = 0;
    if (!entity || !Read(entity + off::CEntityInstance::m_pEntity, identity) || !identity ||
        !Read(identity, back) || back != entity || !Read(identity + off::entity_layout::HandleOffset, handle) || handle == UINT32_MAX)
        return false;
    const auto index = handle & off::entity_layout::IndexMask;
    return Read(system + off::entity_layout::TableOffset + (index >> off::entity_layout::BucketShift) * sizeof(bucket), bucket) &&
        bucket && identity == bucket + (index & off::entity_layout::BucketMask) * off::entity_layout::IdentityStride;
}
ShotLogSource Local() noexcept {
#ifdef CS2_SHOT_LOGGING_CHECKS
    if (testClock) return testSource;
#endif
    namespace off = cs2_offsets;
    ShotLogSource result{};
    std::uintptr_t system = 0, controller = 0, pawn = 0;
    std::uint32_t linked = UINT32_MAX, reverse = UINT32_MAX;
    if (!client || reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) != client ||
        !Read(client + off::globals::dwEntityList, system) || !system ||
        !Read(client + off::globals::dwLocalPlayerController, controller) || !controller ||
        !Read(client + off::globals::dwLocalPlayerPawn, pawn) || !pawn ||
        !Identity(system, controller, result.controller) || !Identity(system, pawn, result.pawn) ||
        !Read(controller + off::CCSPlayerController::m_hPlayerPawn, linked) || linked != result.pawn ||
        !Read(pawn + off::C_BasePlayerPawn::m_hController, reverse) || reverse != result.controller) return {};
    return result;
}
void Reset(std::uint32_t controller, std::uint32_t pawn, std::uint64_t now) noexcept {
    if (!++generation) ++generation;
    source = {generation, controller, pawn};
    sourceSeen = controller != UINT32_MAX && pawn != UINT32_MAX;
    lastSourceMs = now;
    ShotLogReset(source, now);
}
void Adopt(std::uint32_t controller, std::uint32_t pawn, std::uint64_t now) noexcept {
    if (source.controller != controller || source.pawn != pawn) Reset(controller, pawn, now);
    if (controller != UINT32_MAX && pawn != UINT32_MAX) { sourceSeen = true; lastSourceMs = now; }
}
bool Ours(const ShotNativeActor& actor, std::uint64_t now) noexcept {
    if (actor.local && actor.pawn != UINT32_MAX && actor.controller != UINT32_MAX)
        Adopt(actor.controller, actor.pawn, now);
    else if (sourceSeen && now >= lastSourceMs && now - lastSourceMs > ShotLogLateGuardMs)
        Reset(UINT32_MAX, UINT32_MAX, now);
    return actor.pawn != UINT32_MAX && actor.pawn == source.pawn &&
        (actor.controller == UINT32_MAX || actor.controller == source.controller);
}
template<std::size_t N> void Copy(char (&to)[N], const char* from) noexcept {
    if (!from) return;
    std::snprintf(to, N, "%s", from);
}
void Observe(const ShotNativeEvent& event) {
    if (!active.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&stateLock);
    if (!active.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&stateLock); return; }
    ShotLogSetEnabled(ShotLogGetSettings().enabled, event.receiptMs);
    switch (event.kind) {
    case ShotNativeEventKind::WeaponFire:
        if (Ours(event.actor, event.receiptMs)) {
            ShotLogFire fire{};
            fire.source = source; fire.receiptMs = event.receiptMs; fire.deliveryId = event.deliveryId;
            Copy(fire.weapon, event.weapon);
            ShotLogOnFire(fire);
        }
        break;
    case ShotNativeEventKind::BulletImpact:
        if (event.positionKnown && Ours(event.actor, event.receiptMs))
            ShotLogOnImpact({source, event.receiptMs, event.deliveryId, event.position});
        break;
    case ShotNativeEventKind::PlayerHurt:
        if (event.damageKnown && Ours(event.actor, event.receiptMs)) {
            ShotLogHurt hurt{};
            hurt.source = source; hurt.receiptMs = event.receiptMs; hurt.deliveryId = event.deliveryId;
            hurt.victimController = event.victim.controller; hurt.victimPawn = event.victim.pawn;
            Copy(hurt.victimName, event.victim.name); Copy(hurt.weapon, event.weapon);
            hurt.hitgroup = event.hitgroup; hurt.damage = event.damage; hurt.remainingHp = event.remainingHp;
            ShotLogOnHurt(hurt);
        }
        break;
    case ShotNativeEventKind::PlayerDeath:
        if (Ours(event.victim, event.receiptMs)) Reset(source.controller, source.pawn, event.receiptMs);
        break;
    case ShotNativeEventKind::PlayerSpawn:
        if (event.actor.local) Reset(event.actor.controller, event.actor.pawn, event.receiptMs);
        break;
    case ShotNativeEventKind::RoundStart: case ShotNativeEventKind::RoundEnd:
    case ShotNativeEventKind::RoundPrestart: case ShotNativeEventKind::NewMap:
    case ShotNativeEventKind::ClientDisconnected: case ShotNativeEventKind::PlayerDisconnect:
        Reset(UINT32_MAX, UINT32_MAX, event.receiptMs);
        break;
    }
    ReleaseSRWLockExclusive(&stateLock);
}
void Drain() noexcept {
    if (!sink) return;
    for (unsigned i = 0; i < ShotLogCompletedCapacity; ++i) {
        ShotLogRecord record{};
        AcquireSRWLockExclusive(&stateLock);
        const bool available = ShotLogPollCompleted(record);
        ReleaseSRWLockExclusive(&stateLock);
        if (!available) break;
        if (!ShotLogGetSettings().enabled) continue;
        char line[1536]{};
        ShotLogFormat(record, line, sizeof(line));
        if (*line) sink(line);
        char detail[2048]{};
        const auto damageSummary = ShotLogSummarizeDamage(record);
        std::snprintf(detail, sizeof(detail),
            "AIM_SHOT_DETAIL id=%llu kind=%u outcome=%u lifecycle=%llu controller=0x%08x pawn=0x%08x fire_ms=%llu settled_ms=%llu event_ms=%llu candidates=%u candidate_ids=%llu..%llu metadata=%u ambiguous_metadata=%u metadata_age_ms=%llu sequence=%d weapon_handle=0x%08x target_controller=0x%08x target_pawn=0x%08x automatic=%u chance_valid=%u hc=%.2f hits=%u samples=%u spread=%.7f inaccuracy=%.7f range=%.1f impacts=%u damage_events=%u ambiguous_events=%u trajectory=%u evidence_lost=%u late=%u reordered=%u close_reason=%u predicted_valid=%u predicted_damage=%.3f min_damage=%d predicted_hitgroup=%d actual_damage_known=%u actual_damage=%d target_actual_known=%u target_actual_damage=%d actual_partial=%u spread_variants=%u",
            record.id, static_cast<unsigned>(record.kind), static_cast<unsigned>(record.outcome), record.source.lifecycle,
            record.source.controller, record.source.pawn, record.fireMs, record.settledMs, record.eventMs,
            record.candidateCount, record.firstCandidateId, record.lastCandidateId, record.metadataKnown, record.metadataAmbiguous,
            record.metadataAgeMs, record.metadata.commandSequence, record.metadata.weaponHandle,
            record.metadata.targetController, record.metadata.targetPawn, record.metadata.automatic, record.metadata.chanceValid,
            record.metadata.hitchance, record.metadata.hits, record.metadata.samples, record.metadata.spread, record.metadata.inaccuracy,
            record.metadata.range, record.uniqueImpacts, record.damageCount, record.ambiguousEvents, static_cast<unsigned>(record.trajectory),
            record.evidenceLost, record.possibleLateEvent, record.reorderedEvent, static_cast<unsigned>(record.closeReason),
            damageSummary.predictionKnown, damageSummary.predictionKnown ? record.metadata.predictedDamage : 0,
            record.metadata.minDamage, record.metadata.predictedHitgroup, damageSummary.actualKnown, damageSummary.actualDamage,
            damageSummary.targetActualKnown, damageSummary.targetActualDamage, damageSummary.partial, record.metadata.spreadVariants);
        const auto detailLength = std::strlen(detail);
        std::snprintf(detail + detailLength, sizeof(detail) - detailLength, " publication_id=%llu",
            static_cast<unsigned long long>(record.metadata.publicationId));
        sink(detail);
        std::snprintf(detail, sizeof(detail),
            "AIM_SHOT_REASON id=%llu sequence=%d native_fire=%u reason=\"%s\" diagnostic=\"%s\" inferred=%u attack_published=%u aim_ray_valid=%u aim_intersects=%u geometry_impacts=%u short_impacts=%u spread_miss_impacts=%u miss_distance=%.4f impact_distance=%.4f impact_angle_deg=%.4f spread_cone_deg=%.4f overlapping_events=%u fire_ack_timeout_ms=%llu seed_known=0 server_seed_known=0 spread_corrected=%u client_spread_seed=0x%08x spread_variants=%u alternate_client_spread_seed=0x%08x",
            record.id, record.metadata.commandSequence, record.kind == ShotLogRecordKind::Shot,
            ShotLogReasonText(record.reason), ShotLogDiagnosticText(record.diagnostic), record.reason == ShotLogReason::Spread,
            record.metadata.attackPublished, record.aimRayValid, record.aimIntersectsCapsule,
            record.geometryImpacts, record.shortImpacts, record.spreadMissImpacts, record.missDistance,
            record.impactDistance, record.impactAngle, record.spreadCone, record.overlappingEvents, ShotLogFireAckWindowMs,
            record.metadata.spreadCorrected, record.metadata.clientSpreadSeed, record.metadata.spreadVariants,
            record.metadata.alternateClientSpreadSeed);
        sink(detail);
        if (record.metadataKnown) {
            const auto& metadata = record.metadata;
            std::snprintf(detail, sizeof(detail),
                "AIM_SHOT_GEOMETRY id=%llu sequence=%d capsule_valid=%u eye=(%.4f,%.4f,%.4f) aim=(%.4f,%.4f,%.4f) capsule_start=(%.4f,%.4f,%.4f) capsule_end=(%.4f,%.4f,%.4f) capsule_radius=%.4f furthest_impact=(%.4f,%.4f,%.4f) spread_corrected=%u client_spread_seed=0x%08x predicted_ray=(%.4f,%.4f,%.4f) server_seed_known=0 spread_variants=%u alternate_client_spread_seed=0x%08x alternate_predicted_ray=(%.4f,%.4f,%.4f)",
                record.id, metadata.commandSequence, metadata.capsuleValid,
                metadata.eye.x, metadata.eye.y, metadata.eye.z, metadata.aim.pitch, metadata.aim.yaw, metadata.aim.roll,
                metadata.capsule.start.x, metadata.capsule.start.y, metadata.capsule.start.z,
                metadata.capsule.end.x, metadata.capsule.end.y, metadata.capsule.end.z, metadata.capsule.radius,
                record.furthestImpact.x, record.furthestImpact.y, record.furthestImpact.z, metadata.spreadCorrected,
                metadata.clientSpreadSeed, metadata.predictedRay.pitch, metadata.predictedRay.yaw, metadata.predictedRay.roll,
                metadata.spreadVariants, metadata.alternateClientSpreadSeed, metadata.alternatePredictedRay.pitch,
                metadata.alternatePredictedRay.yaw, metadata.alternatePredictedRay.roll);
            sink(detail);
        }
        for (unsigned index = 0; index < record.damageCount && index < record.damage.size(); ++index) {
            const auto& damage = record.damage[index];
            std::snprintf(detail, sizeof(detail),
                "AIM_SHOT_DAMAGE id=%llu kind=%u event_ms=%llu index=%u victim_controller=0x%08x victim_pawn=0x%08x hitgroup=%d damage=%d hp=%d receipt_delay_ms=%lld",
                record.id, static_cast<unsigned>(record.kind), record.eventMs, index,
                damage.victimController, damage.victimPawn, damage.hitgroup, damage.damage, damage.remainingHp, damage.receiptDelayMs);
            sink(detail);
        }
    }
}
}
bool ShotLoggingPrepare(ShotLoggingCallback logger) noexcept {
    sink = logger;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    active.store(false); reportedReady.store(false);
    AcquireSRWLockExclusive(&stateLock);
    generation = 1; source = {generation, UINT32_MAX, UINT32_MAX}; lastDropCount = 0;
    sourceSeen = false; lastSourceMs = 0;
    ShotLogInitialize(source, Now());
    ReleaseSRWLockExclusive(&stateLock);
    prepared = ShotEventsPrepare(logger, &Observe);
    if (sink) sink(prepared ? "AIM_SHOT_LOG_WAITING native contracts verified; listener registration awaits the engine owner" :
        "AIM_SHOT_LOG_UNAVAILABLE native event verification failed; shot results unavailable");
    return prepared;
}
void ShotLoggingActivate() noexcept {
    AcquireSRWLockExclusive(&stateLock);
    const bool allowed = prepared && !stopped;
    if (allowed) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&stateLock);
    if (allowed) ShotEventsActivate();
}
void ShotLoggingPump() noexcept {
    if (!active.load(std::memory_order_acquire)) return;
    ShotEventsPump();
    const bool ready = ShotEventsRegistered();
    const bool wasReady = reportedReady.exchange(ready);
    if (ready && !wasReady)
        MovementTelemetryPush("AIM_SHOT_LOG_READY native firing, impact and damage listeners registered");
    else if (!ready && wasReady)
        MovementTelemetryPush("AIM_SHOT_LOG_WAITING native event interests unavailable; shot results may be incomplete");
    const auto local = Local();
    AcquireSRWLockExclusive(&stateLock);
    if (active.load(std::memory_order_acquire)) {
        const auto now = Now();
        ShotLogSetEnabled(ShotLogGetSettings().enabled, now);
        if (local.controller != UINT32_MAX && local.pawn != UINT32_MAX)
            Adopt(local.controller, local.pawn, now);
        else if (sourceSeen && now >= lastSourceMs && now - lastSourceMs > ShotLogLateGuardMs)
            Reset(UINT32_MAX, UINT32_MAX, now);
    }
    ReleaseSRWLockExclusive(&stateLock);
}
void ShotLoggingCapture(const ShotLogMetadata& copied) noexcept {
    if (!active.load(std::memory_order_acquire) || !ShotLogGetSettings().enabled ||
        copied.source.controller == UINT32_MAX || copied.source.pawn == UINT32_MAX) return;
    AcquireSRWLockExclusive(&stateLock);
    if (active.load(std::memory_order_acquire)) {
        Adopt(copied.source.controller, copied.source.pawn, copied.receiptMs);
        auto metadata = copied;
        metadata.source = source;
        ShotLogAddMetadata(metadata);
    }
    ReleaseSRWLockExclusive(&stateLock);
}
void ShotLoggingWithdrawCommand(const ShotLogSource& copiedSource, int sequence,
    std::uint32_t weaponHandle) noexcept {
    if (!active.load(std::memory_order_acquire) || !ShotLogGetSettings().enabled ||
        copiedSource.controller == UINT32_MAX || copiedSource.pawn == UINT32_MAX) return;
    AcquireSRWLockExclusive(&stateLock);
    if (active.load(std::memory_order_acquire) && copiedSource.controller == source.controller &&
        copiedSource.pawn == source.pawn)
        ShotLogWithdrawCommand(source, sequence, weaponHandle, Now());
    ReleaseSRWLockExclusive(&stateLock);
}
void ShotLoggingStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    ShotEventsStop(reason); // Never hold stateLock while draining native listeners.
    AcquireSRWLockExclusive(&stateLock);
    stopped = true;
    active.store(false, std::memory_order_release);
    ShotLogStop(Now());
    ReleaseSRWLockExclusive(&stateLock);
}
void ShotLoggingFlushTelemetry() noexcept {
    ShotEventsFlushTelemetry();
    AcquireSRWLockExclusive(&stateLock);
    if (active.load(std::memory_order_acquire)) {
        ShotLogSetEnabled(ShotLogGetSettings().enabled, Now());
        ShotLogAdvance(Now());
    }
    const auto counters = ShotLogGetCounters();
    ReleaseSRWLockExclusive(&stateLock);
    Drain();
    const auto dropped = counters.completedDropped + counters.pendingOverflow + counters.eventOverflow + counters.closedOverflow;
    if (sink && dropped != lastDropCount) {
        lastDropCount = dropped;
        char message[256]{};
        std::snprintf(message, sizeof(message), "AIM_SHOT_LOG_WARNING bounded storage overflow: completed_dropped=%llu pending_overflow=%llu event_overflow=%llu closed_overflow=%llu",
            counters.completedDropped, counters.pendingOverflow, counters.eventOverflow, counters.closedOverflow);
        sink(message);
    }
}
#ifdef CS2_SHOT_LOGGING_CHECKS
void ShotLoggingTestReset() noexcept { stopped = false; }
void ShotLoggingTestClock(std::uint64_t now) noexcept { testClock = true; testNow = now; }
void ShotLoggingTestSource(std::uint32_t controller, std::uint32_t pawn) noexcept { testSource = {0, controller, pawn}; }
#endif
