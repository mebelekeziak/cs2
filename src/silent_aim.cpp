#include "silent_aim.h"
#include "menu_input.h"
#include "silent_aim_command.h"
#include "silent_aim_target.h"
#include "silent_aim_settings.h"
#include "silent_aim_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "aim_autofire_offsets.h"
#include "aim_hitchance.h"
#include "aim_weapon.h"
#include "aim_occlusion.h"
#include "aim_damage.h"
#include "shot_logging.h"
#include "shot_log_settings.h"
#include "movement.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include "movement_telemetry.h"
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
namespace off = silent_aim_offsets;
using HistoryFn = void (*)(void*, int, void*);
HistoryFn original = nullptr;
std::uintptr_t client = 0;
AimBoneContext bones{};
AimWeaponContext weaponContext{};
AimOcclusionContext occlusionContext{};
AimDamageContext damageContext{};
AimRecordHistoryFn recordHistory = nullptr;
bool weaponPrepared = false, autofirePrepared = false, finalizerReady = false;
SilentAimLogCallback logger = nullptr;
std::atomic<bool> active{false};
std::atomic<const char*> state{"silent aim not initialized"};
SRWLOCK mutationLock = SRWLOCK_INIT;
bool stopped = false, prepared = false;
std::uint64_t callbacks = 0, edited = 0;
std::atomic<std::uint64_t> historyCalls{0};
std::uint64_t shots = 0, selected = 0, sourceRejected = 0, writeRejected = 0;
AimCommandDiagnostics commandDiagnostics{};
AimTargetDiagnostics targetDiagnostics{};
ULONGLONG lastReport = 0;
ULONGLONG lastSummary = 0; // Render owner only.
std::uint64_t decisions = 0, requested = 0, fired = 0, cancelled = 0, chanceRejected = 0, occlusionRejected = 0, damageRejected = 0;
AimAutofireDiagnostics fireDiagnostics{};
AimViewDiagnostics viewDiagnostics{};
std::uint64_t prepareCalls = 0, prepareEvaluated = 0, viewRejected = 0, prepareBlocked = 0;
const char* prepareStage = "not_called";
struct Decision {
    AimSource source{};
    AimTarget target{};
    AimTargetVolume volume{};
    AimWeaponSnapshot weapon{};
    AimHitchanceResult chance{};
    AimOcclusionResult occlusion{};
    AimDamageResult damage{};
    SilentAimSettings settings{};
    const char* reason = "not evaluated";
    bool selected = false;
    bool automaticEligible = false;
    bool quickstopEligible = false;
};
struct Pending {
    AimAutofirePatch patch{};
    Decision decision{};
    bool waiting = false;
};
std::array<Pending, 256> requests{};
Decision lastDecision{};
Decision stopCycle{};
bool stopping = false;
#ifdef CS2_AIM_PIPELINE_CHECKS
bool testRuntime = false, testForeground = true;
#endif
const char* lastDecisionReason = "not evaluated";
std::uint32_t lastDecisionTarget = UINT32_MAX;
ULONGLONG lastDecisionReport = 0;
ULONGLONG lastEventReport = 0;
const char* lastObservedAction = "not evaluated";

template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
void Status(const char* value) noexcept { state.store(value, std::memory_order_release); }
bool Source(AimSource& source) noexcept {
    return Read(client + cs2_offsets::globals::dwEntityList, source.system) && source.system &&
        Read(client + cs2_offsets::globals::dwLocalPlayerController, source.controller) && source.controller &&
        Read(client + cs2_offsets::globals::dwLocalPlayerPawn, source.pawn) && source.pawn;
}
bool SameSource(const AimSource& before) noexcept {
    AimSource current{};
    return Source(current) && current.system == before.system && current.controller == before.controller && current.pawn == before.pawn;
}
bool ClientCurrent() noexcept {
#ifdef CS2_AIM_PIPELINE_CHECKS
    if (testRuntime) return true;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
bool Foreground() noexcept {
    if (MenuInputCapturing()) return false;
#ifdef CS2_AIM_PIPELINE_CHECKS
    if (testRuntime) return testForeground;
#endif
    DWORD foreground = 0;
    const auto window = GetForegroundWindow();
    GetWindowThreadProcessId(window, &foreground);
    return window && foreground == GetCurrentProcessId() && !IsIconic(window);
}
bool Usable(void* input) noexcept {
    if (!ClientCurrent() ||
        reinterpret_cast<std::uintptr_t>(input) != client + off::Input) {
        active.store(false, std::memory_order_release);
        stopped = true; Status("client/input changed; silent aim stopped"); return false;
    }
    if (!Foreground()) { Status(MenuInputCapturing() ? "menu owns input" : "game unfocused"); return false; }
    if (
#ifdef CS2_AIM_PIPELINE_CHECKS
        !testRuntime &&
#endif
        (GetAsyncKeyState(VK_END) & 0x8000)) {
        active.store(false, std::memory_order_release);
        stopped = true; Status("stopped and resident"); return false;
    }
    return active.load(std::memory_order_acquire);
}
Pending* Request(std::uintptr_t command) noexcept {
    int sequence = 0;
    if (!Read(command + movement_offsets::command::Sequence, sequence) || sequence <= 0) return nullptr;
    return &requests[static_cast<unsigned>(sequence) % requests.size()];
}
struct WeaponSample {
    AimWeaponSnapshot value{};
    bool read = false, valid = false;
};
bool Reachable(Decision& decision, bool enforceMinimum = true) noexcept;
Decision EvaluateTarget(Decision result, const AimAngles& view, WeaponSample& weapon) noexcept {
    const auto& settings = result.settings;
    const char* geometryReason = "geometry unavailable";
    const bool geometry = SilentAimReadTargetVolume(result.source, result.target, result.volume, geometryReason);
    if (geometry) {
        const auto centered = SilentAimSolve(result.target.eye, result.volume.center, view, settings.fov);
        if (!centered.valid) { result.reason = "hitbox center outside FOV"; return result; }
        result.target.solution = centered;
    }
    // One native accuracy/readiness sample per decision, shared by candidates.
    if (!weapon.read) {
        weapon.read = true;
        weapon.valid = weaponPrepared && AimWeaponRead(weaponContext, result.source, weapon.value);
    }
    result.weapon = weapon.value;
    if (!weapon.valid) {
        result.reason = weaponPrepared ? result.weapon.reason : "weapon model verification unavailable"; return result;
    }
    if (!geometry) { result.reason = geometryReason; return result; }
    AimHitchanceInput cone{};
    cone.eye = result.target.eye;
    cone.angles = {result.target.solution.angles.pitch + result.weapon.aimPunch.pitch,
        result.target.solution.angles.yaw + result.weapon.aimPunch.yaw,
        result.target.solution.angles.roll + result.weapon.aimPunch.roll};
    cone.capsule = result.volume.capsule;
    cone.spread = result.weapon.spread; cone.inaccuracy = result.weapon.inaccuracy;
    cone.range = result.weapon.range; cone.recoilIndex = result.weapon.recoilIndex;
    cone.samples = static_cast<unsigned>(settings.samples); cone.negev = result.weapon.negev;
    cone.forceMaxRadius = result.weapon.forceMaxRadius; cone.fixedSpreadAngle = result.weapon.fixedSpreadAngle;
    result.chance = AimEstimateHitchance(cone);
    if (!result.chance.valid) result.reason = result.chance.reason;
    else if (result.chance.percent < settings.hitchance) { ++chanceRejected; result.reason = "below hitchance"; }
    else if (!result.weapon.ready) result.reason = result.weapon.reason;
    else result.reason = settings.autofire ? "autofire eligible" : "manual mode";
    return result;
}
struct AutofireSelection {
    Decision prototype{}, accepted{}, rejected{}, brake{};
    AimAngles view{};
    WeaponSample weapon{};
    float rejectedSeparation = 181;
    bool automatic = true;
};
float ExpectedDamage(const Decision& decision) noexcept {
    const int health = decision.target.enemy.health;
    const float damage = std::min(decision.damage.damage, static_cast<float>(health));
    return damage * decision.chance.percent * .01f;
}
bool BetterShot(const Decision& candidate, const Decision& current) noexcept {
    if (!current.selected) return true;
    const float score = ExpectedDamage(candidate), previous = ExpectedDamage(current);
    if (score != previous) return score > previous;
    if (candidate.chance.percent != current.chance.percent) return candidate.chance.percent > current.chance.percent;
    if (candidate.target.solution.separation != current.target.solution.separation)
        return candidate.target.solution.separation < current.target.solution.separation;
    if (candidate.target.enemy.pawnHandle != current.target.enemy.pawnHandle)
        return candidate.target.enemy.pawnHandle < current.target.enemy.pawnHandle;
    return candidate.target.hitbox < current.target.hitbox;
}
bool EligibleTarget(const AimTarget& target, void* context) noexcept {
    auto& selection = *static_cast<AutofireSelection*>(context);
    auto candidate = selection.prototype;
    candidate.target = target;
    candidate.selected = true;
    candidate = EvaluateTarget(candidate, selection.view, selection.weapon);
    if (candidate.chance.valid && (!selection.automatic || candidate.weapon.ready) &&
        (!selection.automatic || candidate.chance.percent >= candidate.settings.hitchance || candidate.settings.quickstop) &&
        Reachable(candidate, selection.automatic)) {
        if (!selection.automatic || candidate.chance.percent >= candidate.settings.hitchance) {
            candidate.automaticEligible = selection.automatic;
            if (BetterShot(candidate, selection.accepted)) {
                selection.accepted = candidate;
                return true;
            }
            return false;
        }
        // Preserve a clear, ready fallback while continuing the search for an
        // immediately shootable enemy. A covered nearer enemy cannot own stop.
        const auto dx = candidate.volume.center.x - candidate.target.eye.x;
        const auto dy = candidate.volume.center.y - candidate.target.eye.y;
        const auto dz = candidate.volume.center.z - candidate.target.eye.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance <= candidate.weapon.range &&
            BetterShot(candidate, selection.brake)) {
            candidate.quickstopEligible = true;
            selection.brake = candidate;
        }
    }
    // Keep a useful rejection when no candidate qualifies; later failed
    // candidates must not replace an already accepted decision.
    if (target.solution.separation < selection.rejectedSeparation) {
        selection.rejected = candidate;
        selection.rejectedSeparation = target.solution.separation;
    }
    return false;
}
Decision Evaluate(const AimAngles& view, const SilentAimSettings& settings, bool selectEligible = false) noexcept {
    Decision result{};
    result.settings = settings;
    ++decisions;
    if (!Source(result.source)) {
        ++sourceRejected; result.reason = "local source unavailable"; return result;
    }
    AutofireSelection selection{};
    selection.prototype = result;
    selection.view = view;
    selection.automatic = selectEligible;
    const bool found = SilentAimSelectTarget(result.source, view, settings.fov, settings.hitgroups, bones,
        result.target, &targetDiagnostics, &EligibleTarget, &selection);
    if (found) { ++selected; return selection.accepted; }
    if (selection.brake.selected) { ++selected; return selection.brake; }
    if (selection.rejected.selected) { ++selected; return selection.rejected; }
    result.reason = std::strcmp(targetDiagnostics.localFailure, "none") ?
        "local actor unavailable" : settings.hitgroups ? "no enemy in aim FOV" : "no hitgroups selected";
    return result;
}
bool SameStopCycle(const Decision& decision) noexcept {
    return stopping && decision.settings == stopCycle.settings &&
        decision.source.system == stopCycle.source.system && decision.source.controller == stopCycle.source.controller &&
        decision.source.pawn == stopCycle.source.pawn &&
        decision.target.local.pawnHandle == stopCycle.target.local.pawnHandle &&
        decision.target.local.controllerHandle == stopCycle.target.local.controllerHandle &&
        decision.target.enemy.pawnHandle == stopCycle.target.enemy.pawnHandle &&
        decision.target.enemy.controllerHandle == stopCycle.target.enemy.controllerHandle &&
        decision.weapon.weapon == stopCycle.weapon.weapon && decision.weapon.handle == stopCycle.weapon.handle &&
        decision.weapon.identity == stopCycle.weapon.identity && decision.weapon.services == stopCycle.weapon.services &&
        decision.weapon.data == stopCycle.weapon.data && decision.weapon.mode == stopCycle.weapon.mode;
}
void Report(const Decision& decision, const char* action, bool event = false) noexcept {
    lastDecision = decision;
    lastObservedAction = action;
    const auto now = GetTickCount64();
    const auto handle = decision.selected ? decision.target.enemy.pawnHandle : UINT32_MAX;
    const bool changed = std::strcmp(lastDecisionReason, action) || lastDecisionTarget != handle;
    // Continuous estimates are capped at 5 Hz. Decision transitions and native
    // shot requests are capped at 20 Hz; no callback performs sink I/O.
    auto& reportTime = event ? lastEventReport : lastDecisionReport;
    if (reportTime && now - reportTime < ((changed || event) ? 50u : 200u)) return;
    reportTime = now; lastDecisionReason = action; lastDecisionTarget = handle;
    char message[1536]{};
    std::snprintf(message, sizeof(message),
        "AIM_HITCHANCE target=0x%08x hitbox=%d estimate_valid=%u estimated=%.2f%% hits=%u samples=%u minimum=%.2f%% autofire=%u ready=%u weapon=%s handle=0x%08x spread=%.7f inaccuracy=%.7f recoil_index=%.3f punch=(%.3f,%.3f,%.3f) range=%.1f clip=%d action=\"%s\" model_reason=\"%s\" occlusion_valid=%u clear=%u fraction=%.5f occlusion_reason=\"%s\" separation=%.3f aim=(%.3f,%.3f) eligibility_rejected=%u damage_valid=%u predicted_damage=%.3f predicted_hp=%d min_damage=%d predicted_hitgroup=%d damage_reason=\"%s\"; direct-bullet estimate; no penetration",
        handle, static_cast<int>(decision.target.hitbox), decision.chance.valid,
        decision.chance.percent, decision.chance.hits, decision.chance.samples, decision.settings.hitchance,
        decision.settings.autofire, decision.weapon.ready, decision.weapon.name, decision.weapon.handle,
        decision.weapon.spread, decision.weapon.inaccuracy, decision.weapon.recoilIndex,
        decision.weapon.aimPunch.pitch, decision.weapon.aimPunch.yaw, decision.weapon.aimPunch.roll,
        decision.weapon.range, decision.weapon.clip, action, decision.reason,
        decision.occlusion.valid, decision.occlusion.clear, decision.occlusion.fraction, decision.occlusion.reason,
        decision.target.solution.separation, decision.target.solution.angles.pitch, decision.target.solution.angles.yaw,
        targetDiagnostics.eligibilityRejected, decision.damage.valid, decision.damage.damage, decision.damage.healthDamage,
        decision.settings.minDamage, decision.damage.input.hitgroup, decision.damage.reason);
    MovementTelemetryPush(message);
}
bool Current(const Decision& decision) noexcept {
    return SameSource(decision.source) && SilentAimTargetValid(decision.source, decision.target) &&
        SilentAimTargetVolumeValid(decision.source, decision.target, decision.volume) &&
        AimWeaponStillCurrent(weaponContext, decision.source, decision.weapon) &&
        (!decision.damage.valid || AimDamageStillCurrent(damageContext, decision.source, decision.target,
            decision.volume, decision.weapon, decision.damage));
}
bool Reachable(Decision& decision, bool enforceMinimum) noexcept {
    if (!Current(decision)) { decision.reason = "target or weapon changed before occlusion check"; return false; }
    decision.occlusion = AimOcclusionCheck(occlusionContext, decision.source, decision.target, decision.volume);
    if (!decision.occlusion.valid || !decision.occlusion.clear) {
        ++occlusionRejected;
        decision.reason = decision.occlusion.reason; return false;
    }
    // Native tracing can invoke callbacks. Do not publish from state invalidated
    // by that call, or reuse a clear path from command preparation at history time.
    if (!ClientCurrent() || !active.load(std::memory_order_acquire) || !Foreground() ||
        !(SilentAimGetSettings() == decision.settings) || !Current(decision)) {
        decision.reason = "aim state changed during occlusion check"; return false;
    }
    decision.damage = AimDamageRead(damageContext, decision.source, decision.target,
        decision.volume, decision.weapon, decision.occlusion);
    if (!decision.damage.valid || (enforceMinimum && decision.damage.healthDamage < decision.settings.minDamage)) {
        ++damageRejected;
        decision.reason = decision.damage.valid ? "below minimum damage" : decision.damage.reason;
        return false;
    }
    if (!Current(decision)) { decision.reason = "damage source changed"; return false; }
    return true;
}
template<std::size_t N> void CopyNativeText(std::uintptr_t address, char (&text)[N]) noexcept {
    if (!address) return;
    for (std::size_t i = 0; i + 1 < N; ++i) {
        char value = 0;
        if (!Read(address + i, value)) { text[0] = 0; return; }
        text[i] = value;
        if (!value) return;
    }
    text[N - 1] = 0;
}
void CaptureShot(const Decision& decision, std::uintptr_t command, bool automatic) noexcept {
    if (!ShotLogGetSettings().enabled || !SameSource(decision.source) ||
        !SilentAimTargetValid(decision.source, decision.target)) return;
    const bool volumeCurrent = decision.volume.valid && SilentAimTargetVolumeValid(decision.source, decision.target, decision.volume);
    const bool weaponCurrent = AimWeaponStillCurrent(weaponContext, decision.source, decision.weapon);
    ShotLogMetadata metadata{};
    metadata.receiptMs = GetTickCount64();
    metadata.source.controller = decision.target.local.controllerHandle;
    metadata.source.pawn = decision.target.local.pawnHandle;
    Read(command + movement_offsets::command::Sequence, metadata.commandSequence);
    metadata.weaponHandle = decision.weapon.handle;
    metadata.targetController = decision.target.enemy.controllerHandle;
    metadata.targetPawn = decision.target.enemy.pawnHandle;
    if (decision.target.enemy.controller)
        CopyNativeText(decision.target.enemy.controller + cs2_offsets::CBasePlayerController::m_iszPlayerName, metadata.targetName);
    std::uintptr_t designer = 0;
    if (weaponCurrent && decision.weapon.identity && Read(decision.weapon.identity + cs2_offsets::CEntityIdentity::m_designerName, designer))
        CopyNativeText(designer, metadata.weapon);
    metadata.eye = decision.target.eye;
    metadata.aim = {decision.target.solution.angles.pitch + decision.weapon.aimPunch.pitch,
        decision.target.solution.angles.yaw + decision.weapon.aimPunch.yaw,
        decision.target.solution.angles.roll + decision.weapon.aimPunch.roll};
    metadata.capsule = decision.volume.capsule;
    metadata.capsuleValid = volumeCurrent;
    metadata.hitchance = decision.chance.percent;
    metadata.hits = decision.chance.hits; metadata.samples = decision.chance.samples;
    metadata.chanceValid = decision.chance.valid && volumeCurrent && weaponCurrent;
    metadata.spread = decision.weapon.spread; metadata.inaccuracy = decision.weapon.inaccuracy;
    metadata.range = decision.weapon.range; metadata.automatic = automatic;
    metadata.damageValid = decision.damage.valid && volumeCurrent && weaponCurrent &&
        AimDamageStillCurrent(damageContext, decision.source, decision.target, decision.volume, decision.weapon, decision.damage);
    metadata.predictedDamage = decision.damage.damage;
    metadata.minDamage = decision.settings.minDamage;
    metadata.predictedHitgroup = decision.damage.input.hitgroup;
    metadata.attackPublished = weaponCurrent && decision.weapon.ready && *metadata.weapon;
    ShotLoggingCapture(metadata);
}
bool Cancel(Pending& request, void* input, void* command) noexcept {
    const bool restored = SilentAimCancelAutofire(request.patch, &fireDiagnostics);
    bool checksum = true;
    if (fireDiagnostics.commandRestored) {
        checksum = MovementRefinalizeCommand(input, command);
        if (!checksum) {
            active.store(false, std::memory_order_release); stopped = true;
            Status("autofire cancellation checksum unavailable; stopped");
        }
        ++cancelled;
    }
    if (!restored) {
        active.store(false, std::memory_order_release); stopped = true;
        Status("autofire ownership changed; stopped");
    }
    request.waiting = false;
    return restored && checksum;
}
void Process(void* input, void* command) noexcept {
    ++callbacks;
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    auto* request = Request(address);
    int sequence = 0;
    const bool automatic = request && request->waiting && request->patch.command == address &&
        request->patch.input == reinterpret_cast<std::uintptr_t>(input) &&
        Read(address + movement_offsets::command::Sequence, sequence) && request->patch.sequence == sequence;
    const bool restored = !automatic || SilentAimFinishAutofireHistory(request->patch, &fireDiagnostics);
    const bool usable = Usable(input);
    const auto settings = SilentAimGetSettings();
    if (automatic) {
        AimShot shot{};
        auto decision = request->decision;
        const char* rejection = !restored ? "input ownership changed" : !usable ? "aim stopped or game unfocused" :
            !settings.enabled ? "silent aim disabled" : !settings.autofire ? "autofire disabled" :
            !(settings == decision.settings) ? "aim settings changed" :
            !SameSource(decision.source) ? "local source changed" :
            !SilentAimTargetValid(decision.source, decision.target) ? "target changed" :
            !SilentAimTargetVolumeValid(decision.source, decision.target, decision.volume) ? "hitbox geometry changed" :
            !AimWeaponStillCurrent(weaponContext, decision.source, decision.weapon) ? "weapon changed" :
            !Reachable(decision) ? decision.reason :
            !SilentAimReadShot(address, shot, &commandDiagnostics) ? commandDiagnostics.reason :
            !SilentAimWriteShot(shot, decision.target.solution.angles) ? "history changed before angle write" : nullptr;
        if (rejection) {
            decision.reason = rejection;
            ++writeRejected;
            const bool cancelledCompletely = Cancel(*request, input, command);
            Report(decision, cancelledCompletely ? "automatic shot cancelled: state/history/path changed" :
                "automatic cancellation incomplete; aim stopped", true); return;
        }
        ++shots; ++edited; ++fired; request->waiting = false;
        CaptureShot(decision, address, true);
        Status("autofire primary-fire history updated");
        Report(decision, "automatic primary published", true); return;
    }
    if (!usable) return;
    if (!settings.enabled) { Status("disabled"); return; }
    AimShot shot{};
    if (!SilentAimReadShot(address, shot, &commandDiagnostics)) {
        Status("awaiting primary-fire history"); return;
    }
    ++shots;
    auto decision = Evaluate(shot.view, settings);
    if (!decision.selected) { Status(decision.reason); Report(decision, decision.reason); return; }
    // Manual input keeps its existing behavior; a failed damage/path estimate
    // stays unknown in logs and never turns the slider into a manual-fire gate.
    if (decision.chance.valid) Reachable(decision, false);
    const auto& target = decision.target;
    if (!active.load(std::memory_order_acquire) || !Foreground() || !(SilentAimGetSettings() == settings) ||
        !SameSource(decision.source) || !SilentAimTargetValid(decision.source, target) ||
        !SilentAimWriteShot(shot, target.solution.angles)) {
        ++writeRejected;
        Status("source/history changed; shot unchanged"); return;
    }
    ++edited;
    CaptureShot(decision, address, false);
    Status("primary-fire history updated");
    Report(decision, "manual primary aimed", true);
    const auto now = GetTickCount64();
    if (!lastReport || now - lastReport >= 2000) {
        lastReport = now;
        char message[512]{};
        std::snprintf(message, sizeof(message),
            "AIM_HISTORY callbacks=%llu edited=%llu target=0x%08x history_index=%d entries=%d hitbox=%d separation=%.3f pitch=%.3f yaw=%.3f owner=%lu; base/camera/buttons unchanged; history edit is not observed hit registration",
            callbacks, edited, target.enemy.pawnHandle, shot.index, shot.count, static_cast<int>(target.hitbox), target.solution.separation,
            target.solution.angles.pitch, target.solution.angles.yaw, GetCurrentThreadId());
        MovementTelemetryPush(message);
    }
}
void HookHistory(void* input, int slot, void* command) noexcept {
    historyCalls.fetch_add(1, std::memory_order_relaxed);
    original(input, slot, command);
    if (slot != 0) return;
    AcquireSRWLockExclusive(&mutationLock);
    // Pending requests must be cancelled even after disable/focus loss/stop.
    Process(input, command);
    ReleaseSRWLockExclusive(&mutationLock);
}
}

bool SilentAimPrepare(SilentAimLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, sizeof(off::HistoryBuilderBytes)> bytes{};
    namespace bone = silent_aim_hitbox_offsets;
    std::array<unsigned char, sizeof(bone::LookupBoneBytes)> lookupBytes{};
    std::array<unsigned char, sizeof(bone::SkeletonQueryBytes)> queryBytes{};
    std::uintptr_t vtable = 0, method = 0;
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Read(client + off::Input, vtable) || vtable != client + off::InputVtable ||
        !Read(vtable + off::HistorySlot * sizeof(std::uintptr_t), method) || method != client + off::HistoryBuilder ||
        !MovementCopy(method, bytes.data(), bytes.size()) || std::memcmp(bytes.data(), off::HistoryBuilderBytes, bytes.size()) ||
        !MovementCopy(client + bone::LookupBone, lookupBytes.data(), lookupBytes.size()) ||
        std::memcmp(lookupBytes.data(), bone::LookupBoneBytes, lookupBytes.size()) ||
        !MovementCopy(client + bone::SkeletonQuery, queryBytes.data(), queryBytes.size()) ||
        std::memcmp(queryBytes.data(), bone::SkeletonQueryBytes, queryBytes.size())) {
        Status("installed history build verification failed"); return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(method), reinterpret_cast<void*>(&HookHistory),
        reinterpret_cast<void**>(&original)) != MH_OK || !original ||
        MH_QueueEnableHook(reinterpret_cast<void*>(method)) != MH_OK) {
        Status("history hook preparation failed"); return false;
    }
    prepared = true;
    bones = {client, reinterpret_cast<AimBoneLookup>(client + bone::LookupBone)};
    weaponPrepared = AimWeaponPrepare(client, weaponContext);
    AimOcclusionPrepare(client, occlusionContext);
    AimDamagePrepare(client, damageContext);
    namespace fire = aim_autofire_offsets;
    std::array<unsigned char, sizeof(fire::RecordHistoryBytes)> recorderBytes{};
    autofirePrepared = MovementCopy(client + fire::RecordHistory, recorderBytes.data(), recorderBytes.size()) &&
        !std::memcmp(recorderBytes.data(), fire::RecordHistoryBytes, recorderBytes.size());
    if (autofirePrepared) recordHistory = reinterpret_cast<AimRecordHistoryFn>(client + fire::RecordHistory);
    Status("awaiting history hook activation");
    if (logger) logger("AIM_BUILD_VERIFIED native history slot=25; primary-fire history edited after original; dynamic named bones from copied pose; aim angles edit primary history only; optional quickstop edits command movement before native CRC");
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message),
            "AIM_HITCHANCE_BUILD weapon_model=%u native_attack_recorder=%u occlusion=%u damage=%u; native accuracy and direct-bullet damage snapshots; path and minimum damage checked before request and publication; autofire starts off; command finalizer required for native CRC",
            weaponPrepared, autofirePrepared, occlusionContext.prepared, damageContext.prepared);
        logger(message);
    }
    return true;
}
void SilentAimActivate(bool commandHookReady) noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    finalizerReady = commandHookReady;
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void SilentAimPrepareCommand(void* input, void* command, bool commandReady) noexcept {
    ShotLoggingPump();
    AcquireSRWLockExclusive(&mutationLock);
    ++prepareCalls;
    bool movementHandled = false;
    if (!commandReady) {
        stopping = false;
        ++prepareBlocked; prepareStage = "anti_aim_restore_unresolved";
        ReleaseSRWLockExclusive(&mutationLock);
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    auto* request = Request(address);
    const bool identity = client && reinterpret_cast<std::uintptr_t>(input) == client + off::Input &&
        ClientCurrent();
    bool restored = true;
    if (identity && request && request->patch.active)
        restored = SilentAimRestoreAutofireRebuild(reinterpret_cast<std::uintptr_t>(input), address, request->patch, &fireDiagnostics);
    if (!restored) {
        ++prepareBlocked;
        active.store(false, std::memory_order_release); stopped = true;
        Status("autofire rebuild ownership unavailable; stopped");
    }
    if (request && !request->patch.active) *request = {};
    prepareStage = "inactive_or_unfocused";
    if (!restored) prepareStage = "autofire_restore_failed";
    else if (Usable(input)) {
        const auto settings = SilentAimGetSettings();
        AimAngles view{};
        if (!settings.enabled) { prepareStage = "disabled"; Status("disabled"); }
        else if (!SilentAimReadView(address, view, &viewDiagnostics)) {
            ++viewRejected; prepareStage = viewDiagnostics.reason; Status("native physical view unavailable");
        }
        else {
            ++prepareEvaluated;
            auto decision = Evaluate(view, settings, settings.autofire);
            const char* action = decision.reason;
            const bool wantsStop = settings.autofire && settings.quickstop && autofirePrepared && finalizerReady &&
                request && (decision.quickstopEligible || (decision.automaticEligible && SameStopCycle(decision)));
            // Recheck after native selection/tracing callbacks before movement
            // ownership is acquired. Accuracy is observed again next command;
            // we never pretend braking already changed this command's cone.
            const bool stopValid = wantsStop && active.load(std::memory_order_acquire) && Foreground() &&
                settings == SilentAimGetSettings() && Current(decision);
            const bool movementReady = MovementQuickstop(command, stopValid);
            movementHandled = true;
            stopping = stopValid && movementReady;
            if (stopping) stopCycle = decision;
            if (!movementReady || (wantsStop && !stopping)) action = "quickstop movement unavailable";
            else if (decision.quickstopEligible && stopping) action = "quickstop braking below hitchance";
            else if (settings.autofire && (!autofirePrepared || !finalizerReady)) action = "native autofire contract unavailable";
            else if (settings.autofire && request && decision.automaticEligible) {
                if (!active.load(std::memory_order_acquire) || !Foreground() ||
                    !(settings == SilentAimGetSettings()) || !Current(decision)) action = "aim state changed before request";
                else if (!Reachable(decision)) action = decision.reason;
                else {
                    request->decision = decision;
                    if (SilentAimStartAutofire(reinterpret_cast<std::uintptr_t>(input), address,
                        recordHistory, request->patch, &fireDiagnostics)) {
                        request->waiting = true; ++requested; action = "automatic primary requested";
                    } else action = fireDiagnostics.reason;
                }
            }
            Status(action);
            prepareStage = action;
            Report(decision, action);
        }
    }
    if (!movementHandled) { stopping = false; MovementQuickstop(command, false); }
    ReleaseSRWLockExclusive(&mutationLock);
}
void SilentAimStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    stopping = false;
    active.store(false, std::memory_order_release);
    Status("stopped and resident");
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "AIM_STOPPED_RESIDENT reason=%s; history mutations drained; forwarding hook/trampoline retained until process exit", reason ? reason : "stop");
        logger(message);
    }
}
const char* SilentAimStatus() noexcept { return state.load(std::memory_order_acquire); }
void SilentAimFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (lastSummary && now - lastSummary < 2000) return;
    lastSummary = now;
    char message[1536]{};
    const auto settings = SilentAimGetSettings();
    AcquireSRWLockShared(&mutationLock);
    const auto& command = commandDiagnostics;
    const auto& target = targetDiagnostics;
    std::snprintf(message, sizeof(message),
        "AIM_DIAGNOSTICS hook_calls=%llu callbacks=%llu active=%u enabled=%u hitgroups=0x%02x fov=%.2f status=\"%s\" shots=%llu selected=%llu edited=%llu source_rejected=%llu write_rejected=%llu command_reason=%s history=%d/%d allocated=%d attack_index=%d buttons=0x%llx command_presence=0x%x entry=%d entry_presence=0x%x angle_presence=0x%x angles=0x%llx base_angles=0x%llx target_stage=%s local_failure=%s controllers=%u actor_rejected=%u teammates=%u bone_rejected=%u outside_fov=%u bone_failure=%s scene_vtable=0x%llx model_handle=0x%llx model=0x%llx pose=0x%llx bone_count=%d bone_index=%d",
        historyCalls.load(std::memory_order_relaxed), callbacks, active.load(std::memory_order_acquire), settings.enabled,
        settings.hitgroups, settings.fov, SilentAimStatus(), shots, selected, edited, sourceRejected, writeRejected,
        command.reason, command.size, command.capacity, command.allocated, command.index,
        command.buttons, command.presence, command.entry, command.entryPresence, command.anglePresence,
        static_cast<unsigned long long>(command.angles), static_cast<unsigned long long>(command.baseAngles),
        target.stage, target.localFailure, target.controllers, target.actorRejected, target.teammates,
        target.boneRejected, target.outsideFov, target.boneFailure,
        static_cast<unsigned long long>(target.sceneVtable), static_cast<unsigned long long>(target.modelHandle),
        static_cast<unsigned long long>(target.model), static_cast<unsigned long long>(target.pose), target.boneCount, target.boneIndex);
    ReleaseSRWLockShared(&mutationLock);
    logger(message);
    char fireMessage[1280]{};
    AcquireSRWLockShared(&mutationLock);
    std::snprintf(fireMessage, sizeof(fireMessage),
        "AIM_FIRE_DIAGNOSTICS decisions=%llu requested=%llu published=%llu cancelled=%llu below_chance=%llu autofire=%u minimum=%.2f samples=%d native_recorder=%u weapon_model=%u finalizer=%u last_valid=%u last_estimated=%.2f last_hits=%u last_samples=%u fire_reason=%s sequence=%d native_index=%d last_action=\"%s\" last_reason=\"%s\" occlusion_ready=%u occlusion_rejected=%llu last_path_valid=%u last_path_clear=%u last_path_reason=\"%s\" damage_ready=%u damage_rejected=%llu predicted_valid=%u predicted_damage=%.3f predicted_hp=%d min_damage=%d; publication is not observed hit registration",
        decisions, requested, fired, cancelled, chanceRejected, settings.autofire, settings.hitchance, settings.samples,
        autofirePrepared, weaponPrepared, finalizerReady, lastDecision.chance.valid, lastDecision.chance.percent,
        lastDecision.chance.hits, lastDecision.chance.samples, fireDiagnostics.reason, fireDiagnostics.sequence, fireDiagnostics.nativeIndex,
        lastObservedAction, lastDecision.reason, occlusionContext.prepared, occlusionRejected,
        lastDecision.occlusion.valid, lastDecision.occlusion.clear, lastDecision.occlusion.reason,
        damageContext.prepared, damageRejected, lastDecision.damage.valid, lastDecision.damage.damage,
        lastDecision.damage.healthDamage, settings.minDamage);
    ReleaseSRWLockShared(&mutationLock);
    logger(fireMessage);
    char prepareMessage[768]{};
    AcquireSRWLockShared(&mutationLock);
    std::snprintf(prepareMessage, sizeof(prepareMessage),
        "AIM_PREPARE_DIAGNOSTICS calls=%llu evaluated=%llu view_rejected=%llu blocked=%llu stage=\"%s\" view_reason=%s base=0x%llx base_presence=0x%x angles=0x%llx angle_presence=0x%x native_default=%u view=(%.3f,%.3f,%.3f); preparation state retained separately from manual shot history",
        prepareCalls, prepareEvaluated, viewRejected, prepareBlocked, prepareStage, viewDiagnostics.reason,
        static_cast<unsigned long long>(viewDiagnostics.base), viewDiagnostics.basePresence,
        static_cast<unsigned long long>(viewDiagnostics.angles), viewDiagnostics.presence, viewDiagnostics.nativeDefault,
        viewDiagnostics.values.pitch, viewDiagnostics.values.yaw, viewDiagnostics.values.roll);
    ReleaseSRWLockShared(&mutationLock);
    logger(prepareMessage);
}

#ifdef CS2_AIM_PIPELINE_CHECKS
void SilentAimTestConfigure(std::uintptr_t base, AimTestHistoryFn builder, AimTestRecordFn recorder, SilentAimLogCallback callback) noexcept {
    testRuntime = true; testForeground = true;
    client = base; original = builder; recordHistory = recorder;
    active.store(true); stopped = false; prepared = weaponPrepared = autofirePrepared = finalizerReady = true;
    stopping = false; stopCycle = {};
    requests = {}; lastDecision = {}; lastDecisionReport = 0; lastDecisionReason = "not evaluated";
    lastEventReport = 0; lastObservedAction = "not evaluated";
    weaponContext.client = base;
    damageContext = {base, true};
    occlusionContext = {base, true};
    callbacks = edited = shots = selected = sourceRejected = writeRejected = 0;
    decisions = requested = fired = cancelled = chanceRejected = occlusionRejected = damageRejected = 0;
    commandDiagnostics = {}; targetDiagnostics = {}; fireDiagnostics = {};
    viewDiagnostics = {}; prepareCalls = prepareEvaluated = viewRejected = prepareBlocked = 0;
    prepareStage = "not_called"; logger = callback; lastSummary = 0;
}
void SilentAimTestHistory(void* input, void* command) noexcept { HookHistory(input, 0, command); }
void SilentAimTestForeground(bool foreground) noexcept { testForeground = foreground; }
#endif
