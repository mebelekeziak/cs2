#include "shot_log.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
unsigned failures = 0, checks = 0;
constexpr ShotLogSource Local{1, 0x8002, 0x8003};
void Expect(bool result, const char* label) {
    ++checks; failures += !result;
    std::printf("%s %s\n", result ? "PASS" : "FAIL", label);
}
template<std::size_t N> void Text(char (&output)[N], const char* input) {
    std::snprintf(output, N, "%s", input);
}
ShotLogMetadata Metadata(std::uint64_t time = 990, int sequence = 1) {
    ShotLogMetadata sample{};
    sample.source = Local; sample.receiptMs = time; sample.commandSequence = sequence;
    sample.weaponHandle = 0x8008; sample.targetController = 0x8005; sample.targetPawn = 0x8006;
    Text(sample.weapon, "weapon_AK47"); Text(sample.targetName, "Mayer");
    sample.eye = {}; sample.aim = {}; sample.capsule = {{100, 0, -2}, {100, 0, 2}, 4};
    sample.hitchance = 78.125f; sample.hits = 200; sample.samples = 256;
    sample.spread = .01f; sample.inaccuracy = .02f; sample.range = 8192;
    sample.automatic = sample.chanceValid = sample.capsuleValid = true;
    return sample;
}
ShotLogFire Fire(std::uint64_t time = 1000, std::uint64_t delivery = 1, const char* weapon = "ak47") {
    ShotLogFire event{}; event.source = Local; event.receiptMs = time; event.deliveryId = delivery;
    Text(event.weapon, weapon); return event;
}
ShotLogImpact Impact(std::uint64_t time = 1005, std::uint64_t delivery = 2, AimVector position = {200, 0, 0}) {
    ShotLogImpact event{}; event.source = Local; event.receiptMs = time; event.deliveryId = delivery;
    event.position = position; return event;
}
ShotLogHurt Hurt(std::uint64_t time = 1022, std::uint64_t delivery = 3, const char* weapon = "WEAPON_ak47") {
    ShotLogHurt event{}; event.source = Local; event.receiptMs = time; event.deliveryId = delivery;
    event.victimController = 0x8005; event.victimPawn = 0x8006;
    Text(event.victimName, "Mayer"); Text(event.weapon, weapon);
    event.hitgroup = 2; event.damage = 34; event.remainingHp = 66; return event;
}
std::vector<ShotLogRecord> Drain() {
    std::vector<ShotLogRecord> records;
    ShotLogRecord record{};
    while (ShotLogPollCompleted(record)) records.push_back(record);
    return records;
}
std::string Format(const ShotLogRecord& record) {
    char line[2048]{};
    Expect(ShotLogFormat(record, line, sizeof(line)), "full shot/evidence line fits bounded render buffer");
    return line;
}

void BasicChecks() {
    ShotLogInitialize(Local, 900);
    ShotLogAddMetadata(Metadata());
    Expect(ShotLogGetCounters().fires == 0 && Drain().empty(), "aim metadata never allocates an actual-shot ID");
    const auto id = ShotLogOnFire(Fire());
    ShotLogOnImpact(Impact()); ShotLogOnHurt(Hurt());
    ShotLogAdvance(1310);
    Expect(Drain().empty(), "settlement preserves the inclusive reorder boundary");
    ShotLogAdvance(1311);
    auto records = Drain();
    Expect(id == 1 && records.size() == 1 && records[0].id == id &&
        records[0].kind == ShotLogRecordKind::Shot && records[0].outcome == ShotLogOutcome::Hit,
        "one actual fire with uniquely attributable damage yields one HIT record");
    if (records.empty()) return;
    const auto& record = records[0];
    Expect(record.metadataKnown && record.metadataAgeMs == 10 && record.metadata.commandSequence == 1 &&
        record.metadata.weaponHandle == 0x8008 && record.metadata.hits == 200 && record.metadata.samples == 256,
        "latest preceding metadata is copied with HC sample counts, weapon and command identity");
    Expect(record.damageCount == 1 && record.damage[0].damage == 34 && record.damage[0].remainingHp == 66 &&
        record.damage[0].receiptDelayMs == 22 && record.damage[0].victimPawn == 0x8006 &&
        record.uniqueImpacts == 1 && record.trajectory == ShotLogTrajectory::IntersectsRecordedCapsule,
        "confirmed damage, full victim handle, receipt delay and copied-capsule geometry survive settlement");
    const auto line = Format(record);
    std::printf("EXAMPLE %s\n", line.c_str());
    Expect(line.find("AIM_SHOT #1 HIT Mayer in chest for 34 (66 HP)") == 0 &&
        line.find("weapon ak47") != std::string::npos && line.find("target Mayer [0x00008006]") != std::string::npos &&
        line.find("HC 78% (200/256)") != std::string::npos && line.find("delay 22ms") != std::string::npos &&
        line.find('\n') == std::string::npos, "human HIT line contains actual victim, hitgroup, damage, HP, HC and receipt delay");
    ShotLogAdvance(5000);
    Expect(Drain().empty(), "settled shot and consumed damage are never emitted twice");

    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire());
    ShotLogOnImpact(Impact(1005, 2, {200, 100, 0})); ShotLogAdvance(1311);
    records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Miss &&
        records[0].trajectory == ShotLogTrajectory::MissesRecordedCapsule,
        "observed impact segment can miss the copied capsule without inventing a physical cause");
    if (!records.empty()) {
        const auto miss = Format(records[0]);
        Expect(miss.find("MISS no confirmed damage in 250ms") != std::string::npos &&
            miss.find("impact segment misses recorded capsule") != std::string::npos &&
            miss.find("spread") == std::string::npos && miss.find("resolver") == std::string::npos &&
            miss.find("prediction") == std::string::npos, "MISS text reports observed evidence without asserting spread/resolver/prediction");
    }
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogAdvance(1311);
    records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown && records[0].uniqueImpacts == 0,
        "actual fire remains loggable without an aim candidate or impact");
    if (!records.empty()) {
        const auto unknown = Format(records[0]);
        Expect(unknown.find("HC unknown") != std::string::npos && unknown.find("impact missing") != std::string::npos,
            "unknown metadata and missing impact stay explicit in clean output");
    }
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire());
    ShotLogOnImpact(Impact()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Miss &&
        records[0].trajectory == ShotLogTrajectory::IntersectsRecordedCapsule,
        "crossing a recorded capsule does not promote unconfirmed damage into a HIT");
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire());
    ShotLogOnImpact(Impact(1005, 2, {})); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].uniqueImpacts == 1 && records[0].trajectory == ShotLogTrajectory::Unknown,
        "zero-length impact segments leave trajectory unknown rather than asserting they miss the capsule");
}

void MetadataChecks() {
    ShotLogInitialize(Local, 900);
    auto older = Metadata(980, 1), newer = Metadata(995, 2), wrong = Metadata(999, 3);
    Text(wrong.weapon, "deagle");
    ShotLogAddMetadata(older); ShotLogAddMetadata(newer); ShotLogAddMetadata(wrong);
    ShotLogOnFire(Fire()); ShotLogAdvance(1311); auto records = Drain();
    Expect(records.size() == 1 && records[0].metadataKnown && records[0].metadata.commandSequence == 2 &&
        records[0].metadataAgeMs == 5, "metadata picks latest preceding exact weapon match with optional prefix/case normalization");
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata(749));
    ShotLogAddMetadata(Metadata(1001, 2)); ShotLogOnFire(Fire()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown, "expired and future metadata cannot describe an earlier shot");
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata(990, 1)); ShotLogAddMetadata(Metadata(990, 2));
    ShotLogOnFire(Fire()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown && records[0].metadataAmbiguous,
        "competing same-millisecond commands leave metadata unknown instead of choosing an arbitrary command");
    ShotLogInitialize(Local, 900);
    auto replacement = Metadata(995); replacement.hitchance = 50;
    ShotLogAddMetadata(Metadata()); ShotLogAddMetadata(replacement);
    ShotLogOnFire(Fire()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].metadataKnown && records[0].metadata.hitchance == 50,
        "a newer copied sample for the same command replaces its earlier snapshot");
    ShotLogInitialize(Local, 900); auto unknownWeapon = Metadata(); unknownWeapon.weapon[0] = 0;
    ShotLogAddMetadata(unknownWeapon); ShotLogOnFire(Fire()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown, "an unknown metadata weapon never bridges a weapon change");
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire(1000, 1, ""));
    ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown, "an unknown native fire weapon leaves HC metadata unknown");
    ShotLogInitialize(Local, 900); auto bad = Metadata();
    bad.hitchance = std::numeric_limits<float>::quiet_NaN(); bad.capsule.radius = -1;
    ShotLogAddMetadata(bad); ShotLogOnFire(Fire()); ShotLogOnImpact(Impact()); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].metadataKnown && !records[0].metadata.chanceValid &&
        records[0].trajectory == ShotLogTrajectory::Unknown, "nonfinite HC and invalid copied geometry are not presented as valid evidence");
}

void CorrelationChecks() {
    ShotLogInitialize(Local, 900);
    ShotLogOnFire(Fire(1000, 1)); ShotLogOnFire(Fire(1100, 2)); ShotLogOnHurt(Hurt(1122, 3));
    ShotLogAdvance(1183); auto records = Drain();
    Expect(records.size() == 1 && records[0].kind == ShotLogRecordKind::AmbiguousHit &&
        records[0].damageCount == 1 && records[0].damage[0].damage == 34 && records[0].candidateCount == 2,
        "rapid-fire ambiguity retains a separate confirmed-damage evidence line");
    if (!records.empty()) {
        const auto line = Format(records[0]);
        Expect(line.find("AIM_HIT AMBIGUOUS Mayer in chest for 34 (66 HP)") == 0 &&
            line.find("shot mapping unknown") != std::string::npos,
            "ambiguous damage text never invents a per-shot hit claim");
    }
    ShotLogAdvance(1411); records = Drain();
    Expect(records.size() == 2 && records[0].outcome == ShotLogOutcome::Unconfirmed &&
        records[1].outcome == ShotLogOutcome::Unconfirmed && records[0].damageCount == 0 && records[1].damageCount == 0,
        "overlapping shots never steal the same damage or turn an ambiguous hurt into a claimed miss");

    ShotLogInitialize(Local, 900);
    ShotLogOnHurt(Hurt(980, 3)); ShotLogOnImpact(Impact(990, 2)); ShotLogOnFire(Fire());
    ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Hit && records[0].reorderedEvent &&
        records[0].damage[0].receiptDelayMs == -20, "bounded event reordering can match uniquely while keeping signed receipt order explicit");
    ShotLogInitialize(Local, 900);
    ShotLogOnHurt(Hurt(980, 3)); ShotLogOnFire(Fire(1000, 1)); ShotLogOnFire(Fire(1020, 2));
    ShotLogAdvance(1331); records = Drain();
    unsigned hits = 0, ambiguous = 0;
    for (const auto& record : records) { hits += record.outcome == ShotLogOutcome::Hit; ambiguous += record.kind == ShotLogRecordKind::AmbiguousHit; }
    Expect(hits == 0 && ambiguous == 1 && records.size() == 3,
        "a second reordered candidate invalidates tentative uniqueness before settlement");

    ShotLogInitialize(Local, 900); ShotLogOnHurt(Hurt()); ShotLogAdvance(1083); records = Drain();
    Expect(records.size() == 1 && records[0].kind == ShotLogRecordKind::UnmatchedHit && records[0].candidateCount == 0 &&
        records[0].damage[0].remainingHp == 66, "damage without a known local fire is still shown as unmatched evidence");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogOnHurt(Hurt(1022, 3, ""));
    ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 2 && records[0].kind == ShotLogRecordKind::Shot &&
        records[0].outcome != ShotLogOutcome::Hit && records[1].kind == ShotLogRecordKind::UnmatchedHit,
        "unknown hurt weapon preserves actual damage without associating it across unknown weapons");

    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogAdvance(1311); Drain();
    ShotLogOnFire(Fire(1400, 2)); ShotLogOnHurt(Hurt(1410, 3)); ShotLogAdvance(1471); records = Drain();
    Expect(records.size() == 1 && records[0].kind == ShotLogRecordKind::AmbiguousHit && records[0].possibleLateEvent,
        "retained closed-shot history prevents late old damage from being stolen by a newer shot");
    ShotLogAdvance(1711); records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Unconfirmed,
        "newer shot remains unconfirmed when recent closed history is a plausible late candidate");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogAdvance(1311); Drain();
    ShotLogOnFire(Fire(1400, 2, "deagle")); ShotLogOnHurt(Hurt(1410, 3, "weapon_DEAGLE"));
    ShotLogAdvance(1711); records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Hit,
        "exact weapon identity rejects an impossible different-weapon late association");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogAdvance(1311); Drain();
    ShotLogOnFire(Fire(2101, 2)); ShotLogOnHurt(Hurt(2110, 3)); ShotLogAdvance(2412); records = Drain();
    Expect(records.size() == 1 && records[0].outcome == ShotLogOutcome::Hit,
        "closed-history ambiguity is explicitly bounded rather than retained forever");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogAdvance(1200);
    ShotLogOnHurt(Hurt(1022, 3)); ShotLogAdvance(1311); records = Drain();
    bool lateHit = false, lateEvidence = false;
    for (const auto& record : records) {
        lateHit = lateHit || record.outcome == ShotLogOutcome::Hit;
        lateEvidence = lateEvidence || record.kind == ShotLogRecordKind::UnmatchedHit;
    }
    Expect(!lateHit && lateEvidence && ShotLogGetCounters().lateEvents == 1,
        "copied deliveries arriving beyond the reorder allowance retain actual hurt evidence without retroactive certainty");
}

void DuplicateAndLifecycleChecks() {
    ShotLogInitialize(Local, 900);
    const auto first = ShotLogOnFire(Fire()); const auto duplicate = ShotLogOnFire(Fire());
    ShotLogOnImpact(Impact()); ShotLogOnImpact(Impact()); ShotLogOnHurt(Hurt()); ShotLogOnHurt(Hurt());
    ShotLogAdvance(1311); auto records = Drain();
    Expect(first == duplicate && ShotLogGetCounters().fires == 1 && ShotLogGetCounters().duplicates == 3 &&
        records.size() == 1 && records[0].damageCount == 1 && records[0].uniqueImpacts == 1,
        "stable copied-delivery IDs suppress repeated fire, impact and damage deliveries exactly once");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire(1000, 0)); ShotLogOnFire(Fire(1000, 0));
    ShotLogOnHurt(Hurt()); ShotLogAdvance(1311); records = Drain();
    Expect(ShotLogGetCounters().fires == 2 && records.size() == 3,
        "same-millisecond identical events without delivery IDs remain distinct legitimate shot candidates");

    ShotLogInitialize(Local, 900);
    auto remote = Fire(); remote.source.controller += 0x8000;
    auto stalePawn = Impact(); stalePawn.source.pawn += 0x8000;
    auto oldEpoch = Hurt(); oldEpoch.source.lifecycle = 2;
    Expect(ShotLogOnFire(remote) == 0, "controller handle serial mismatch rejects a noncurrent local source");
    ShotLogOnImpact(stalePawn); ShotLogOnHurt(oldEpoch);
    ShotLogAdvance(5000);
    Expect(Drain().empty() && ShotLogGetCounters().ignoredSource == 3,
        "full pawn handles and lifecycle cookies keep remote/stale evidence out of local correlation");

    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire()); ShotLogOnHurt(Hurt());
    const ShotLogSource respawn{2, Local.controller, Local.pawn + 0x8000};
    ShotLogReset(respawn, 1030); records = Drain();
    Expect(records.size() == 2 && records[0].outcome == ShotLogOutcome::Unconfirmed &&
        records[0].closeReason == ShotLogCloseReason::SourceChanged && records[1].kind == ShotLogRecordKind::UnmatchedHit,
        "source reset closes pending attribution while separately retaining observed damage");
    ShotLogOnHurt(Hurt(1040, 8));
    auto fresh = Fire(1100, 1); fresh.source = respawn;
    const auto second = ShotLogOnFire(fresh); ShotLogAdvance(1411); records = Drain();
    Expect(second == 2 && records.size() == 1 && !records[0].metadataKnown &&
        ShotLogGetCounters().ignoredSource == 1,
        "lifecycle reset retains shot numbering but drops old aim samples, duplicate keys and stale source events");

    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire()); ShotLogStop(1010); records = Drain();
    Expect(records.size() == 1 && records[0].closeReason == ShotLogCloseReason::Stopped &&
        records[0].outcome == ShotLogOutcome::Unconfirmed && ShotLogOnFire(Fire(1100, 2)) == 0,
        "stop preserves a truthful unfinished result then rejects further event mutation");
    ShotLogInitialize(Local, 900); ShotLogAddMetadata(Metadata()); ShotLogOnFire(Fire());
    ShotLogSetEnabled(false, 1010); ShotLogAdvance(5000);
    Expect(Drain().empty() && ShotLogGetCounters().abandonedDisabled == 1,
        "logging off explicitly abandons pending output without fabricating a MISS");
    ShotLogSetEnabled(true, 5100); ShotLogOnFire(Fire(5200, 2)); ShotLogAdvance(5511); records = Drain();
    Expect(records.size() == 1 && !records[0].metadataKnown && records[0].id == 2,
        "logging resumes with a fresh context and monotonic actual-shot numbering");
}

void CapacityChecks() {
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 70; ++i) ShotLogAddMetadata(Metadata(1000 + i, static_cast<int>(i + 1)));
    ShotLogOnFire(Fire(1070, 1)); ShotLogAdvance(1381); auto records = Drain();
    Expect(ShotLogGetCounters().metadataOverflow == 6 && records.size() == 1 &&
        records[0].metadata.commandSequence == 70, "metadata ring rollover is counted and retains the newest applicable sample");
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 70; ++i) {
        const auto time = 1000ull + i * 2000;
        ShotLogOnFire(Fire(time, i + 1)); ShotLogAdvance(time + 311);
    }
    records = Drain();
    Expect(records.size() == ShotLogCompletedCapacity && records.front().id == 7 && records.back().id == 70 &&
        ShotLogGetCounters().completedDropped == 6,
        "completed queue overflow is explicit and retains bounded FIFO order for the newest results");
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 70; ++i) ShotLogOnFire(Fire(1000, i + 1));
    ShotLogOnHurt(Hurt(1001, 1000)); ShotLogAdvance(1311); records = Drain();
    bool claimedHit = false, retainedDamage = false;
    for (const auto& record : records) {
        claimedHit = claimedHit || record.outcome == ShotLogOutcome::Hit;
        retainedDamage = retainedDamage || record.kind == ShotLogRecordKind::AmbiguousHit;
    }
    Expect(ShotLogGetCounters().pendingOverflow == 6 && ShotLogGetCounters().completedDropped > 0 &&
        !claimedHit && retainedDamage, "pending rollover never resolves lost/overlapping shot history into a false HIT and preserves damage evidence");
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 257; ++i) ShotLogOnHurt(Hurt(1000, i + 1));
    ShotLogAdvance(1061); records = Drain();
    Expect(ShotLogGetCounters().eventOverflow == 1 && ShotLogGetCounters().completed == 257 &&
        ShotLogGetCounters().completedDropped == 193 && records.size() == 64,
        "effect overflow emits displaced damage evidence and reports every bounded completed-queue drop");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire());
    for (unsigned i = 0; i < ShotLogDamageCapacity + 1; ++i) ShotLogOnHurt(Hurt(1020 + i, 20 + i));
    ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 2 && records[0].outcome == ShotLogOutcome::Hit && records[0].damageOverflow &&
        records[0].damageCount == ShotLogDamageCapacity && records[1].kind == ShotLogRecordKind::UnmatchedHit,
        "a shot with many damage events keeps bounded detail and emits excess actual damage separately");
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 270; ++i) ShotLogOnImpact(Impact(1000, i + 1));
    Expect(ShotLogGetCounters().dedupEvictions == 14 && ShotLogGetCounters().eventOverflow == 14,
        "delivery and effect histories expose bounded-ring eviction counters");
    ShotLogInitialize(Local, 900);
    for (unsigned i = 0; i < 130; ++i) {
        ShotLogOnFire(Fire(1000, i + 1));
        ShotLogAdvance(1311);
    }
    ShotLogOnFire(Fire(1400, 1000)); ShotLogOnHurt(Hurt(1410, 1001)); ShotLogAdvance(1711); records = Drain();
    bool unsafeHit = false;
    for (const auto& record : records) unsafeHit = unsafeHit || record.outcome == ShotLogOutcome::Hit;
    Expect(ShotLogGetCounters().closedOverflow > 0 && !unsafeHit,
        "closed-history rollover quarantines incomplete evidence instead of enabling a new false unique attribution");
}

void FormattingChecks() {
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire());
    auto hostile = Hurt(); Text(hostile.victimName, " \rMayer\n|%n\t\x1b"); hostile.hitgroup = 123;
    ShotLogOnHurt(hostile); ShotLogAdvance(1311); auto records = Drain();
    if (!records.empty()) {
        const auto line = Format(records[0]);
        Expect(line.find("HIT Mayer /%n in body") != std::string::npos && line.find('\r') == std::string::npos &&
            line.find('\n') == std::string::npos && line.find('\t') == std::string::npos && line.find('\x1b') == std::string::npos,
            "player names cannot inject control lines or delimiters; percent text stays literal and unknown hitgroups stay generic");
        char single[1]{'x'};
        Expect(!ShotLogFormat(records[0], single, sizeof(single)) && single[0] == 0 &&
            !ShotLogFormat(records[0], nullptr, 100), "insufficient/null output buffers fail with bounded termination");
    }
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire());
    auto unterminated = Hurt(); std::memset(unterminated.victimName, 'X', sizeof(unterminated.victimName));
    ShotLogOnHurt(unterminated); ShotLogAdvance(1311); records = Drain();
    Expect(records.size() == 1 && records[0].damage[0].victimName[63] == 0,
        "fixed-size event names are terminated before retaining or formatting them");
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire());
    auto unicode = Hurt(); Text(unicode.victimName, "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87\xe2\x80\xa8\xe2\x80\xae|%n");
    ShotLogOnHurt(unicode); ShotLogAdvance(1311); records = Drain();
    if (!records.empty()) {
        const auto line = Format(records[0]);
        Expect(line.find("Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 /%n") != std::string::npos &&
            line.find("\xe2\x80\xa8") == std::string::npos && line.find("\xe2\x80\xae") == std::string::npos,
            "valid UTF-8 names survive while Unicode line separators and direction overrides cannot spoof output");
    }
    ShotLogInitialize(Local, 900); ShotLogOnFire(Fire());
    auto malformed = Hurt(); Text(malformed.victimName, "A\xe2\x82");
    ShotLogOnHurt(malformed); ShotLogAdvance(1311); records = Drain();
    if (!records.empty()) {
        const auto line = Format(records[0]);
        Expect(line.find("A?? in chest") != std::string::npos,
            "truncated UTF-8 bytes are bounded replacement characters rather than an unterminated or invalid name");
    }
    ShotLogInitialize(Local, 900);
    auto invalidImpact = Impact(); invalidImpact.position.x = std::numeric_limits<float>::quiet_NaN();
    auto invalidHurt = Hurt(); invalidHurt.damage = -1;
    auto invalidMetadata = Metadata(); invalidMetadata.commandSequence = 0;
    ShotLogOnImpact(invalidImpact); ShotLogOnHurt(invalidHurt); ShotLogAddMetadata(invalidMetadata);
    Expect(ShotLogGetCounters().invalid == 3 && Drain().empty(), "invalid copied positions, damage and command identities are counted without partial tracking");
}
}

int main() {
    BasicChecks(); MetadataChecks(); CorrelationChecks(); DuplicateAndLifecycleChecks(); CapacityChecks(); FormattingChecks();
    std::printf("Shot log checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
