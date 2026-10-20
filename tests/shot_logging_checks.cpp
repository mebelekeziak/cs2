#include "shot_logging.h"
#include "shot_events.h"
#include "shot_log_settings.h"
#include "movement_telemetry.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t LocalController = 0x10001, LocalPawn = 0x20002;
constexpr std::uint32_t VictimController = 0x30003, VictimPawn = 0x40004;
unsigned failures = 0, checks = 0;
std::vector<std::string> lines, telemetry;
ShotEventsValueCallback eventCallback = nullptr;
bool prepareResult = true, registerOnPump = true, nativeActive = false, nativeRegistered = false;
bool injectDuringStop = false, captureFromSink = false, captureCompleted = false;
unsigned prepareCalls = 0, activateCalls = 0, pumpCalls = 0, stopCalls = 0, flushCalls = 0;
ShotLogMetadata sinkMetadata{};

void Expect(bool ok, const char* description) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
    failures += !ok;
}
template<std::size_t N> void Copy(char (&output)[N], const char* input) {
    std::snprintf(output, N, "%s", input);
}
bool Has(const char* text) {
    for (const auto& line : lines) if (line.find(text) != std::string::npos) return true;
    return false;
}
unsigned Count(const char* prefix) {
    unsigned count = 0;
    for (const auto& line : lines) if (line.rfind(prefix, 0) == 0) ++count;
    return count;
}
unsigned ReadyCount() {
    unsigned count = 0;
    for (const auto& line : telemetry) if (line.rfind("AIM_SHOT_LOG_READY", 0) == 0) ++count;
    return count;
}
void Sink(const char* line) {
    if (line) lines.emplace_back(line);
    if (captureFromSink && line && std::strncmp(line, "AIM_SHOT #", 10) == 0) {
        captureFromSink = false;
        ShotLoggingCapture(sinkMetadata);
        captureCompleted = true;
    }
}
ShotNativeActor Actor(bool local = true) {
    ShotNativeActor result{};
    result.controller = LocalController; result.pawn = LocalPawn;
    result.known = true; result.local = local;
    Copy(result.name, "local player");
    return result;
}
ShotNativeActor Victim() {
    ShotNativeActor result{};
    result.controller = VictimController; result.pawn = VictimPawn; result.known = true;
    Copy(result.name, "BOT\r\n  Delta");
    return result;
}
ShotLogMetadata Metadata(std::uint64_t receipt = 1000, int sequence = 42) {
    ShotLogMetadata result{};
    result.source = {999, LocalController, LocalPawn}; // Runtime assigns its own lifecycle.
    result.receiptMs = receipt; result.commandSequence = sequence;
    result.weaponHandle = 0x50005;
    result.targetController = VictimController; result.targetPawn = VictimPawn;
    Copy(result.weapon, "weapon_AK47"); Copy(result.targetName, "BOT Delta");
    result.eye = {0, 0, 0}; result.aim = {0, 0, 0};
    result.capsule = {{100, -2, 0}, {100, 2, 0}, 5};
    result.hitchance = 75; result.hits = 192; result.samples = 256;
    result.spread = 0.01f; result.inaccuracy = 0.02f; result.range = 8192;
    result.automatic = result.chanceValid = result.capsuleValid = true;
    result.damageValid = true; result.predictedDamage = 42.25f; result.minDamage = 35; result.predictedHitgroup = 1;
    return result;
}
ShotNativeEvent Fire(std::uint64_t at = 1010, std::uint64_t delivery = 1, const char* weapon = "ak47") {
    ShotNativeEvent result{};
    result.kind = ShotNativeEventKind::WeaponFire;
    result.receiptMs = at; result.deliveryId = delivery; result.actor = Actor();
    Copy(result.weapon, weapon);
    return result;
}
ShotNativeEvent Impact(std::uint64_t at = 1020, std::uint64_t delivery = 2) {
    ShotNativeEvent result{};
    result.kind = ShotNativeEventKind::BulletImpact;
    result.receiptMs = at; result.deliveryId = delivery; result.actor = Actor();
    result.positionKnown = true; result.position = {200, 0, 0};
    return result;
}
ShotNativeEvent Hurt(std::uint64_t at = 1030, std::uint64_t delivery = 3, const char* weapon = "ak47") {
    ShotNativeEvent result{};
    result.kind = ShotNativeEventKind::PlayerHurt;
    result.receiptMs = at; result.deliveryId = delivery; result.actor = Actor(false);
    result.actor.controller = UINT32_MAX; result.actor.known = false;
    result.victim = Victim(); result.damageKnown = true;
    result.hitgroup = 1; result.damage = 40; result.remainingHp = 60;
    Copy(result.weapon, weapon);
    return result;
}
void Deliver(const ShotNativeEvent& event) {
    ShotLoggingTestClock(event.receiptMs);
    if (eventCallback) eventCallback(event);
}
void FlushAt(std::uint64_t at) {
    ShotLoggingTestClock(at);
    ShotLoggingFlushTelemetry();
}
void Start(bool enabled = true, bool deferredRegistration = false) {
    ShotLoggingStop("test reset");
    ShotLoggingTestReset();
    lines.clear(); telemetry.clear();
    eventCallback = nullptr;
    prepareResult = true; registerOnPump = !deferredRegistration;
    nativeActive = nativeRegistered = injectDuringStop = false;
    captureFromSink = captureCompleted = false;
    prepareCalls = activateCalls = pumpCalls = stopCalls = flushCalls = 0;
    ShotLogSetSettings({enabled, false});
    ShotLoggingTestClock(1000); ShotLoggingTestSource(LocalController, LocalPawn);
    Expect(ShotLoggingPrepare(Sink), "verified native adapter prepares the runtime");
    ShotLoggingActivate(); ShotLoggingPump();
}
void TestUnavailableAndReady() {
    ShotLoggingTestClock(1000); ShotLoggingTestSource(LocalController, LocalPawn);
    ShotLogSetSettings({true, false});
    prepareResult = false; eventCallback = nullptr;
    lines.clear(); telemetry.clear();
    Expect(!ShotLoggingPrepare(Sink), "unverified native adapter rejects preparation");
    ShotLoggingActivate(); ShotLoggingPump(); ShotLoggingCapture(Metadata());
    Deliver(Fire()); FlushAt(1500);
    Expect(Has("AIM_SHOT_LOG_UNAVAILABLE") && ShotLogGetCounters().fires == 0,
        "failed preparation reports unavailable and ignores commands and events");
    Expect(activateCalls == 0 && pumpCalls == 0 && ReadyCount() == 0,
        "failed preparation never activates or announces native listeners");

    Start(true, true);
    Expect(Has("AIM_SHOT_LOG_WAITING") && ReadyCount() == 0 && activateCalls == 1,
        "preparation distinguishes waiting registration from active listeners");
    ShotLoggingPump();
    Expect(ReadyCount() == 0, "unregistered engine-owner pump cannot report ready");
    registerOnPump = true;
    ShotLoggingPump(); ShotLoggingPump();
    Expect(ReadyCount() == 1, "native listener readiness is reported exactly once");
}
void TestActualShotAndMetadata() {
    Start();
    ShotLoggingCapture(Metadata());
    FlushAt(1000);
    Expect(ShotLogGetCounters().fires == 0 && Count("AIM_SHOT #") == 0,
        "command metadata alone allocates no shot and emits no result");
    const auto fired = Fire();
    Deliver(fired); Deliver(fired);
    auto impact = Impact(); impact.actor.local = false;
    Deliver(impact); Deliver(Hurt());
    Expect(Count("AIM_SHOT #") == 0 && Count("AIM_SHOT_DETAIL") == 0,
        "native callbacks enqueue copied evidence without invoking the sink");
    Expect(ShotLogGetCounters().fires == 1 && ShotLogGetCounters().duplicates == 1,
        "one observed fire occurrence creates one shot despite duplicate delivery");
    FlushAt(1320);
    Expect(Count("AIM_SHOT #") == 0, "result remains open at the exact outcome and reorder boundary");
    FlushAt(1321);
    Expect(Count("AIM_SHOT #") == 1 && Count("AIM_SHOT_DETAIL") == 1,
        "settled shot emits exactly one human line and one file detail line");
    Expect(Has("AIM_SHOT #1 HIT BOT Delta in head for 40 (60 HP)") &&
        Has("weapon ak47 | target BOT Delta [0x00040004]") && Has("HC 75% (192/256) | delay 20ms"),
        "copied raw pawn damage produces a clean readable hit, damage, health and delay");
    Expect(Has("metadata=1 ambiguous_metadata=0 metadata_age_ms=10 sequence=42") &&
        Has("automatic=1 chance_valid=1 hc=75.00 hits=192 samples=256"),
        "native weapon name normalization attaches the successful command context");
    Expect(!Has("lifecycle=999") && Has("controller=0x00010001 pawn=0x00020002"),
        "runtime replaces caller lifecycle metadata with its current copied source identity");
    Expect(Has("weapon_handle=0x00050005 target_controller=0x00030003 target_pawn=0x00040004") &&
        Has("spread=0.0100000 inaccuracy=0.0200000 range=8192.0 impacts=1 damage_events=1"),
        "full file detail retains handles, weapon model, impact count and damage count");
    Expect(Count("AIM_SHOT_DAMAGE") == 1 && Has("hitgroup=1 damage=40 hp=60 receipt_delay_ms=20"),
        "file damage detail preserves each copied hitgroup, damage, remaining health and receipt delay");
    Expect(Has("| predicted 42.2 (target) | actual 40 (target)") &&
        Has("predicted_valid=1 predicted_damage=42.250 min_damage=35 predicted_hitgroup=1") &&
        Has("actual_damage_known=1 actual_damage=40 target_actual_known=1 target_actual_damage=40 actual_partial=0"),
        "human and file output retain the captured prediction alongside confirmed same-target damage");
    FlushAt(1500);
    Expect(Count("AIM_SHOT #") == 1, "repeated render flush never repeats an already drained shot");
}
void TestIdentityAndWeapon() {
    Start(); ShotLoggingCapture(Metadata());
    auto remote = Fire(); remote.actor.local = false;
    remote.actor.controller = VictimController; remote.actor.pawn = VictimPawn;
    Deliver(remote);
    auto recycled = Fire(1011, 2); recycled.actor.local = false; recycled.actor.pawn += 0x8000;
    Deliver(recycled);
    auto wrongController = Fire(1012, 3); wrongController.actor.local = false;
    wrongController.actor.controller += 0x8000;
    Deliver(wrongController);
    Expect(ShotLogGetCounters().fires == 0, "remote, recycled pawn and mismatching controller identities are ignored");
    auto raw = Fire(1020, 4); raw.actor.local = raw.actor.known = false; raw.actor.controller = UINT32_MAX;
    Deliver(raw); FlushAt(1331);
    Expect(Count("AIM_SHOT #") == 1 && Has("AIM_SHOT #1 MISS") && Has("HC 75%"),
        "exact serialized local pawn survives unavailable current actor enrichment");

    Start(); ShotLoggingCapture(Metadata());
    Deliver(Fire(1010, 1, "m4a1")); Deliver(Hurt(1030, 3, "m4a1")); FlushAt(1321);
    Expect(Has("HIT BOT Delta") && Has("HC unknown") && Has("metadata=0"),
        "a different native weapon cannot inherit a candidate weapon's hitchance");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire(1010, 1, "")); FlushAt(1321);
    Expect(Has("HC unknown") && Has("metadata=0"), "missing native weapon never guesses command metadata");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire(1251)); FlushAt(1562);
    Expect(Has("HC unknown") && Has("metadata=0"), "command context expires outside the metadata receipt window");
}
void TestLifecycle() {
    const ShotNativeEventKind lifecycle[] = {
        ShotNativeEventKind::RoundStart, ShotNativeEventKind::RoundEnd,
        ShotNativeEventKind::RoundPrestart, ShotNativeEventKind::NewMap,
        ShotNativeEventKind::ClientDisconnected, ShotNativeEventKind::PlayerDisconnect
    };
    for (const auto kind : lifecycle) {
        Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
        ShotNativeEvent event{}; event.kind = kind; event.receiptMs = 1020;
        Deliver(event); FlushAt(1020);
        Expect(Has("UNCONFIRMED") && Has("source changed before settlement") && Count("AIM_SHOT #") == 1,
            "round, map and disconnect lifecycle changes flush pending shots as unconfirmed");
        auto old = Fire(1030, 2); old.actor.local = false;
        Deliver(old);
        Expect(ShotLogGetCounters().fires == 1, "unknown lifecycle rejects old raw pawn events until a fresh owner snapshot");
        ShotLoggingTestClock(1040); ShotLoggingPump();
        Deliver(Fire(1050, 3)); FlushAt(1361);
        Expect(Count("AIM_SHOT #") == 2 && Has("AIM_SHOT #2 MISS") && Has("metadata=0"),
            "fresh local source resumes tracking without carrying old lifecycle metadata");
    }

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire()); Deliver(Hurt());
    ShotNativeEvent death{}; death.kind = ShotNativeEventKind::PlayerDeath; death.receiptMs = 1040;
    death.victim = Victim(); Deliver(death); FlushAt(1040);
    Expect(Count("AIM_SHOT #") == 0, "remote death does not close the local shot lifecycle");
    death.receiptMs = 1050; death.victim = Actor(false);
    death.victim.controller = UINT32_MAX; death.victim.known = false;
    Deliver(death); FlushAt(1050);
    Expect(Count("AIM_SHOT #") == 1 && Has("source changed before settlement"),
        "local death uses the copied full pawn even after current actor enrichment disappears");
    Expect(Count("AIM_HIT ") == 1 && Has("BOT Delta in head for 40 (60 HP)"),
        "confirmed damage remains visible when death interrupts per-shot settlement");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotNativeEvent spawn{}; spawn.kind = ShotNativeEventKind::PlayerSpawn;
    spawn.receiptMs = 1020; spawn.actor = Actor(); spawn.actor.pawn += 0x8000;
    Deliver(spawn); FlushAt(1020);
    Expect(Has("source changed before settlement"), "verified local spawn starts a new full pawn lifecycle");
    auto previousPawn = Fire(1030, 2); previousPawn.actor.local = false;
    Deliver(previousPawn);
    Expect(ShotLogGetCounters().fires == 1, "previous spawn's serialized pawn cannot create a new shot");
}
void TestSettings() {
    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLogSetSettings({false, false}); ShotLoggingTestClock(1020); ShotLoggingPump();
    ShotLoggingCapture(Metadata(1020, 43)); Deliver(Fire(1025, 2)); FlushAt(1500);
    Expect(Count("AIM_SHOT #") == 0 && ShotLogGetCounters().fires == 1 &&
        ShotLogGetCounters().abandonedDisabled == 1,
        "disabling logs clears pending context and ignores subsequent commands and native fires");
    ShotLogSetSettings({true, false}); ShotLoggingTestClock(1510); ShotLoggingPump();
    Deliver(Fire(1520, 3)); FlushAt(1831);
    Expect(Count("AIM_SHOT #") == 1 && Has("AIM_SHOT #2 MISS") && Has("HC unknown") && Has("metadata=0"),
        "reenabling logs does not resurrect a pending result or stale command context");

    Start(false); ShotLoggingCapture(Metadata()); Deliver(Fire()); FlushAt(1321);
    Expect(ShotLogGetCounters().fires == 0 && Count("AIM_SHOT #") == 0,
        "disabled startup suppresses shots even though native listeners are available");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLogSetSettings({true, true}); ShotLoggingTestClock(1020); ShotLoggingPump();
    Deliver(Hurt()); FlushAt(1321);
    Expect(Has("HIT BOT Delta") && Has("HC 75%"), "verbose diagnostics toggle preserves in-flight shot tracking");
}
void TestTransientLocalLoss() {
    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLoggingTestSource(UINT32_MAX, UINT32_MAX);
    ShotLoggingTestClock(1020); ShotLoggingPump();
    Deliver(Hurt()); FlushAt(1321);
    Expect(Count("AIM_SHOT #") == 1 && Has("HIT BOT Delta") && Has("HC 75%"),
        "transient missing local globals retain copied full identity for late damage evidence");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLoggingTestSource(UINT32_MAX, UINT32_MAX);
    ShotLoggingTestClock(1020); ShotLoggingPump();
    ShotNativeEvent death{}; death.kind = ShotNativeEventKind::PlayerDeath;
    death.receiptMs = 1050; death.victim = Actor(false);
    death.victim.controller = UINT32_MAX; death.victim.known = false;
    Deliver(death); FlushAt(1050);
    Expect(Count("AIM_SHOT #") == 1 && Has("UNCONFIRMED") && Has("source changed before settlement"),
        "late local death ends the retained source even after local globals disappear");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLoggingTestSource(UINT32_MAX, UINT32_MAX);
    ShotLoggingTestClock(1020); ShotLoggingPump();
    ShotLoggingTestClock(2021); ShotLoggingPump(); FlushAt(2021);
    Expect(Count("AIM_SHOT #") == 1 && Has("UNCONFIRMED") && Has("source changed before settlement"),
        "missing local identity expires after the bounded late-event grace period");
    Deliver(Hurt(2022, 3)); FlushAt(2100);
    Expect(Count("AIM_HIT ") == 0, "events from the old copied pawn cannot extend expired source retention");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLoggingTestSource(UINT32_MAX, UINT32_MAX);
    ShotLoggingTestClock(1020); ShotLoggingPump();
    Deliver(Hurt(2022, 3)); FlushAt(2100);
    Expect(Count("AIM_HIT ") == 0 && !Has("HIT BOT Delta"),
        "raw pawn evidence expires after the grace bound even when owner pumps have ceased");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    ShotLoggingTestSource(UINT32_MAX, UINT32_MAX);
    ShotLoggingTestClock(1020); ShotLoggingPump();
    ShotLoggingTestSource(LocalController, LocalPawn + 0x8000);
    ShotLoggingTestClock(1030); ShotLoggingPump(); FlushAt(1030);
    Expect(Count("AIM_SHOT #") == 1 && Has("source changed before settlement"),
        "a new valid full pawn replaces the retained source immediately");
    Deliver(Hurt(1040, 3)); FlushAt(1120);
    Expect(Count("AIM_HIT ") == 0, "previous full pawn damage cannot leak into the replacement source");
}
void TestStopAndUnlockedSink() {
    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    injectDuringStop = true;
    ShotLoggingTestClock(1020); ShotLoggingStop("runtime test");
    Expect(stopCalls == 1 && Count("AIM_SHOT #") == 0 && Count("AIM_SHOT_DETAIL") == 0,
        "cooperative stop closes tracking without moving sink I/O off the render owner");
    FlushAt(1020);
    Expect(Count("AIM_SHOT #") == 1 && Has("stopped before settlement") &&
        Count("AIM_SHOT_DETAIL") == 1,
        "render flush after stop drains one unconfirmed result and its full detail");
    const auto fires = ShotLogGetCounters().fires;
    const auto pumps = pumpCalls;
    const auto activations = activateCalls;
    Deliver(Fire(1030, 55)); Deliver(Hurt(1040, 56));
    ShotLoggingCapture(Metadata(1050, 43)); ShotLoggingPump();
    ShotLoggingActivate(); ShotLoggingPump();
    ShotLoggingStop("repeat stop"); FlushAt(1500); FlushAt(1800);
    Expect(ShotLogGetCounters().fires == fires && pumpCalls == pumps && activateCalls == activations &&
        Count("AIM_SHOT #") == 1,
        "activate after stop, repeated stop and render flush cannot restart tracking or accept callbacks");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    sinkMetadata = Metadata(1321, 43); sinkMetadata.hitchance = 88;
    captureFromSink = true; FlushAt(1321);
    Expect(captureCompleted, "result sink can capture copied metadata reentrantly because runtime locks are released");
    Deliver(Fire(1330, 5)); FlushAt(1641);
    Expect(Count("AIM_SHOT #") == 2 && Has("AIM_SHOT #2 MISS") && Has("HC 88%") && Has("sequence=43"),
        "metadata captured from the unlocked sink belongs to the next observed shot");
}
void TestReasonOutput() {
    Start();
    auto metadata = Metadata(); metadata.capsule = {{100, 0, -2}, {100, 0, 2}, 1};
    ShotLoggingCapture(metadata); Deliver(Fire());
    auto impact = Impact(); impact.position = {200, 4, 0}; Deliver(impact); FlushAt(1321);
    Expect(Has("AIM_SHOT #1 MISS spread") && Has("capsule miss 1.00u") && !Has("no uniquely attributable damage"),
        "runtime emits a short spread reason with useful geometry instead of generic uncertainty");
    Expect(Has("reason=\"spread\" diagnostic=\"impact ray missed saved capsule\" inferred=1") &&
        Has("aim_ray_valid=1 aim_intersects=1 geometry_impacts=1 short_impacts=0 spread_miss_impacts=1") && Has("seed_known=0"),
        "file reason details distinguish geometric inference from unavailable native seed evidence");
    Expect(Count("AIM_SHOT_GEOMETRY") == 1 && Has("furthest_impact=(200.0000,4.0000,0.0000)"),
        "file geometry retains the copied capsule, aim and furthest impact used for diagnostics");

    Start(); metadata = Metadata(); metadata.attackPublished = true;
    ShotLoggingCapture(metadata); FlushAt(2060);
    Expect(Count("AIM_SHOT cmd #") == 0, "runtime command timeout includes its full acknowledgement ordering allowance");
    FlushAt(2061);
    Expect(Count("AIM_SHOT cmd #") == 1 && Has("cmd #42 UNCONFIRMED unregistered shot") &&
        Has("native_fire=0 reason=\"unregistered shot\"") && Has("attack_published=1"),
        "runtime command-only timeout is distinguished from native fire in both human and file output");
    Expect(ShotLogGetCounters().fires == 0 && ShotLogGetCounters().unregistered == 1,
        "command-only timeout never increments actual native-fire accounting");
    FlushAt(3000);
    Expect(Count("AIM_SHOT cmd #") == 1, "runtime emits one timeout per issued command");

    Start(); ShotLoggingCapture(metadata); Deliver(Fire()); FlushAt(2061);
    Expect(Has("AIM_SHOT #1 MISS no impact") && Count("AIM_SHOT cmd #") == 0,
        "an observed native fire with missing effects says no impact instead of unregistered shot");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire()); Deliver(Fire(1050, 4)); Deliver(Hurt(1060)); FlushAt(1361);
    Expect(Has("UNCONFIRMED shot overlap") && Has("AIM_HIT HIT BOT Delta") && Has("shot overlap (2 possible shots"),
        "overlapping fire reports actual damage separately with an understandable mapping explanation");
    Expect(Has("| predicted unknown | actual 40 (confirmed)") &&
        Has("actual_damage_known=0 actual_damage=0 target_actual_known=0 target_actual_damage=0"),
        "ambiguous effects keep confirmed event damage separate from each shot's unknown actual damage");
}
void TestDamagePredictionOutput() {
    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire()); FlushAt(1321);
    Expect(Has("| predicted 42.2 (target) | actual unknown") &&
        Has("predicted_valid=1 predicted_damage=42.250 min_damage=35 predicted_hitgroup=1 actual_damage_known=0"),
        "no damage event leaves actual damage unknown in human and numeric output");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire());
    auto other = Hurt(); other.victim.pawn += 0x8000; Deliver(other); FlushAt(1321);
    Expect(Has("| actual 40 (unmatched victim; target unknown)") &&
        Has("actual_damage_known=1 actual_damage=40 target_actual_known=0 target_actual_damage=0"),
        "a different full victim identity cannot masquerade as measured damage to the predicted target");

    Start(); ShotLoggingCapture(Metadata()); Deliver(Fire()); Deliver(Hurt());
    auto second = Hurt(1031, 4); second.damage = 20; Deliver(second);
    other = Hurt(1032, 5); other.victim.pawn += 1; other.damage = 5; Deliver(other); FlushAt(1321);
    Expect(Has("| actual 60 (target) + 5 (unmatched victims)") &&
        Has("actual_damage_known=1 actual_damage=65 target_actual_known=1 target_actual_damage=60 actual_partial=0") &&
        Count("AIM_SHOT_DAMAGE") == 3,
        "multiple native hurts retain per-event details and separate intended-target and overall damage totals");

    Start(); auto unknown = Metadata(); unknown.damageValid = false;
    ShotLoggingCapture(unknown); Deliver(Fire()); Deliver(Hurt()); FlushAt(1321);
    Expect(Has("| predicted unknown | actual 40 (target)") && Has("predicted_valid=0 predicted_damage=0.000"),
        "unavailable damage model cannot publish a numerical prediction beside a confirmed hurt");
}
}

bool ShotEventsPrepare(ShotEventsLogCallback, ShotEventsValueCallback callback) noexcept {
    ++prepareCalls; eventCallback = callback;
    return prepareResult;
}
void ShotEventsActivate() noexcept { ++activateCalls; nativeActive = true; }
void ShotEventsPump() noexcept {
    ++pumpCalls;
    if (nativeActive && registerOnPump) nativeRegistered = true;
}
void ShotEventsStop(const char*) noexcept {
    ++stopCalls;
    nativeActive = false;
    if (injectDuringStop && eventCallback) {
        injectDuringStop = false;
        eventCallback(Fire(1020, 999));
    }
}
void ShotEventsFlushTelemetry() noexcept { ++flushCalls; }
bool ShotEventsRegistered() noexcept { return nativeRegistered; }
const char* ShotEventsStatus() noexcept { return nativeRegistered ? "registered" : "waiting"; }
void MovementTelemetryPush(const char* message) noexcept { if (message) telemetry.emplace_back(message); }

int main() {
    TestUnavailableAndReady();
    TestActualShotAndMetadata();
    TestIdentityAndWeapon();
    TestLifecycle();
    TestSettings();
    TestTransientLocalLoss();
    TestStopAndUnlockedSink();
    TestReasonOutput();
    TestDamagePredictionOutput();
    captureFromSink = false;
    ShotLoggingStop("test complete");
    ShotLogSetSettings({});
    std::printf("%u/%u shot logging runtime checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
