#include "silent_aim_math.h"
#include "silent_aim_command.h"
#include "silent_aim_target.h"
#include "silent_aim_offsets.h"
#include "silent_aim_target_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "movement_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace cmd = silent_aim_offsets;
namespace ent = cs2_offsets;
namespace bone = silent_aim_hitbox_offsets;
int failures = 0;
void Expect(bool result, const char* label) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", label);
    failures += !result;
}
template<class T> void Put(std::uintptr_t address, const T& value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

struct ShotFixture {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> baseAngles{};
    std::array<std::array<unsigned char, 0x38>, 3> entries{}, angles{};
    std::array<unsigned char, 0x20> rep{};
    ShotFixture() {
        command.fill(0xA5); base.fill(0xA5); baseAngles.fill(0xA5);
        Put(Address(command) + cmd::command::Presence, std::uint32_t{cmd::command::Attack1Present | 0x8000});
        Put(Address(command) + cmd::command::Attack1Index, 1);
        Put(Address(command) + cmd::command::HistorySize, 3);
        Put(Address(command) + cmd::command::HistoryCapacity, 3);
        Put(Address(command) + cmd::command::HistoryRep, Address(rep));
        Put(Address(command) + movement_offsets::command::Buttons, std::uint64_t{0x209});
        Put(Address(command) + movement_offsets::command::Base, Address(base));
        Put(Address(base) + movement_offsets::base::Angles, Address(baseAngles));
        Put(Address(rep) + cmd::repeated::Allocated, 3);
        for (unsigned i = 0; i < 3; ++i) {
            entries[i].fill(0xA5); angles[i].fill(0xA5);
            Put(Address(rep) + cmd::repeated::Entries + i * sizeof(std::uintptr_t), Address(entries[i]));
            Put(Address(entries[i]) + cmd::history::Presence, std::uint32_t{0x8001});
            Put(Address(entries[i]) + cmd::history::Angles, Address(angles[i]));
            Put(Address(angles[i]) + cmd::angle::Presence, std::uint32_t{0x8007});
            Put(Address(angles[i]) + cmd::angle::Pitch, AimAngles{static_cast<float>(i), static_cast<float>(i + 4), 0});
        }
    }
};
void MathChecks() {
    auto result = SilentAimSolve({10, 20, 30}, {110, 20, 30}, {}, 5);
    Expect(result.valid && Near(result.angles.pitch, 0) && Near(result.angles.yaw, 0), "translated eye aims along world X");
    result = SilentAimSolve({}, {-100, -1, 0}, {0, 179, 0}, 3);
    Expect(result.valid && result.separation < 2 && result.angles.yaw < -179, "FOV wraps through the yaw boundary");
    result = SilentAimSolve({}, {100, 0, 100}, {-45, 0, 0}, 1);
    Expect(result.valid && Near(result.angles.pitch, -45), "upward target uses negative Source pitch");
    Expect(!SilentAimSolve({}, {100, 100, 0}, {}, 5).valid, "outside-FOV enemy is rejected");
    result = SilentAimSolve({}, {0, 100, 0}, {}, 90);
    Expect(result.valid && Near(result.separation, 90), "90 degree FOV includes a perpendicular target at its boundary");
    result = SilentAimSolve({}, {-100, 0, 0}, {}, 180);
    Expect(result.valid && Near(result.separation, 180) && Near(result.angles.yaw, 180),
           "180 degree FOV includes a target directly behind");
    Expect(!SilentAimSolve({}, {-100, 0, 0}, {}, 179.99f).valid,
           "target directly behind remains outside a smaller FOV");
    Expect(!SilentAimSolve({}, {100, 0, 0}, {}, 180.01f).valid,
           "FOV above 180 degrees is rejected");
    Expect(!SilentAimSolve({}, {}, {}, 5).valid && !SilentAimSolve({}, {0, 0, 100}, {-89, 0, 0}, 5).valid,
           "zero-length and out-of-range pitch have no aim solution");
    Expect(!SilentAimSolve({}, {100, 0, 0}, {0, std::numeric_limits<float>::quiet_NaN(), 0}, 5).valid,
           "nonfinite camera values are rejected");
    AimAngles command{}, effective{};
    Expect(SilentAimCompensate({10, 179, 0}, {5, -7, .25f}, command, effective) &&
        Near(command.pitch, 5) && Near(command.yaw, -174) && command.roll == 0 &&
        Near(effective.pitch, 10) && Near(effective.yaw, 179) && Near(effective.roll, .25f),
        "recoil compensation wraps command yaw and reconstructs the native effective basis");
    Expect(!SilentAimCompensate({-85,0,0}, {5,0,0}, command, effective) &&
        !SilentAimCompensate({}, {std::numeric_limits<float>::quiet_NaN(),0,0}, command, effective),
        "impossible pitch and nonfinite punch never clamp into a misleading shot");
    SilentAimSetSettings({true, std::numeric_limits<float>::quiet_NaN(), static_cast<AimHitbox>(99)});
    const auto settings = SilentAimGetSettings();
    Expect(settings.enabled && settings.fov == 5 && settings.hitbox == AimHitbox::Pelvis, "one atomic settings snapshot sanitizes FOV and hitbox");
    auto nativeSettings = settings;
    nativeSettings.nativeHitchance = true;
    nativeSettings.removeSpread = true;
    nativeSettings.autowall = true;
    nativeSettings.hitgroups = AimAllHitgroups;
    SilentAimSetSettings(nativeSettings);
    Expect(SilentAimGetSettings() == nativeSettings, "native sampler bit roundtrips alongside adjacent spread, autowall and hitgroup bits");
    nativeSettings.nativeHitchance = false;
    SilentAimSetSettings(nativeSettings);
    Expect(SilentAimGetSettings() == nativeSettings, "clearing the native sampler preserves every other aim option");
    SilentAimSetSettings({});
    Expect(!SilentAimGetSettings().enabled && !SilentAimGetSettings().nativeHitchance && SilentAimGetSettings().hitbox == AimHitbox::Chest,
        "silent aim starts disabled with chest selected and the baseline hitchance sampler");
}
void CommandChecks() {
    ShotFixture fixture;
    AimShot shot{};
    AimCommandDiagnostics diagnostic{};
    const auto commandBefore = fixture.command, baseBefore = fixture.base;
    const auto baseAnglesBefore = fixture.baseAngles;
    const auto entriesBefore = fixture.entries;
    auto expectedAngles = fixture.angles;
    const AimAngles desired{-10, 20, 0};
    for (auto& message : expectedAngles) Put(Address(message) + cmd::angle::Pitch, desired);
    Expect(SilentAimReadShot(Address(fixture.command), shot) && shot.index == 1 && shot.count == 3 && Near(shot.view.yaw, 5),
           "native primary index selects the reference subtick view");
    Expect(SilentAimReadShot(Address(fixture.command), shot, &diagnostic) &&
        !std::strcmp(diagnostic.reason, "ready") && diagnostic.size == 3 && diagnostic.index == 1,
        "command diagnostics identify valid history and native shot index");
    Expect(SilentAimWriteShot(shot, desired) && fixture.angles == expectedAngles, "primary fire updates all existing angle payloads");
    Expect(fixture.command == commandBefore && fixture.base == baseBefore && fixture.baseAngles == baseAnglesBefore && fixture.entries == entriesBefore,
           "camera/base/CRC/buttons/indices/timestamps/presence/entry order are byte-for-byte preserved");

    Put(Address(fixture.command) + cmd::command::Attack1Index, -1);
    Put(Address(fixture.command) + cmd::command::Presence, std::uint32_t{0});
    Expect(SilentAimReadShot(Address(fixture.command), shot) && shot.index == -1 &&
        SilentAimWriteShot(shot, {5, -20, 0}), "held primary fire feeds history without synthesizing an index");
    Put(Address(fixture.command) + movement_offsets::command::Buttons, std::uint64_t{0x208});
    const auto nonfire = fixture.angles;
    Expect(!SilentAimReadShot(Address(fixture.command), shot) && fixture.angles == nonfire, "no primary fire leaves every angle unchanged");
    Expect(!SilentAimReadShot(Address(fixture.command), shot, &diagnostic) &&
        !std::strcmp(diagnostic.reason, "no_primary_attack") && diagnostic.buttons == 0x208,
        "no attack diagnostic distinguishes inactivity from bad history");
    Put(Address(fixture.command) + cmd::command::Presence, std::uint32_t{cmd::command::Attack1Present});
    Put(Address(fixture.command) + cmd::command::Attack1Index, 0);
    Expect(SilentAimReadShot(Address(fixture.command), shot), "intra-command primary tap works after button release");
    Put(Address(fixture.command) + cmd::command::HistorySize, 4);
    Expect(!SilentAimReadShot(Address(fixture.command), shot), "invalid repeated-container count is rejected");
    Expect(!SilentAimReadShot(Address(fixture.command), shot, &diagnostic) &&
        !std::strcmp(diagnostic.reason, "history_capacity_below_size") && diagnostic.size == 4 && diagnostic.capacity == 3,
        "rejected container diagnostics retain the actual count and capacity");
    Put(Address(fixture.command) + cmd::command::HistorySize, 3);
    Put(Address(fixture.command) + cmd::command::HistoryCapacity, 512);
    Expect(SilentAimReadShot(Address(fixture.command), shot), "large retained capacity does not invalidate a small current history");
    Put(Address(fixture.entries[2]) + cmd::history::Angles, std::uintptr_t{1});
    Expect(!SilentAimReadShot(Address(fixture.command), shot) && fixture.angles == nonfire, "unmapped later message rejects the entire command without partial writes");
    Put(Address(fixture.entries[2]) + cmd::history::Angles, Address(fixture.angles[2]));
    Expect(SilentAimReadShot(Address(fixture.command), shot), "restored native layout can be read again");
    Put(Address(fixture.angles[2]) + cmd::angle::Pitch, AimAngles{3, 4, 0});
    const auto changed = fixture.angles;
    Expect(!SilentAimWriteShot(shot, desired) && fixture.angles == changed, "rebuilt angle payload invalidates the borrowed command snapshot");
    Put(Address(fixture.entries[2]) + cmd::history::Angles, Address(fixture.baseAngles));
    Expect(!SilentAimReadShot(Address(fixture.command), shot), "history aliasing the base camera message is rejected");

    ShotFixture protectedFixture;
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Expect(page != nullptr, "allocate independent page for readonly-history regression");
    if (page) {
        std::memcpy(page, protectedFixture.angles[2].data(), protectedFixture.angles[2].size());
        Put(Address(protectedFixture.entries[2]) + cmd::history::Angles, reinterpret_cast<std::uintptr_t>(page));
        DWORD previous = 0;
        const bool protectedPage = VirtualProtect(page, 4096, PAGE_READONLY, &previous) != 0;
        const auto before = protectedFixture.angles;
        Expect(protectedPage && SilentAimReadShot(Address(protectedFixture.command), shot) &&
            !SilentAimWriteShot(shot, desired) && protectedFixture.angles == before,
            "all messages pass writable preflight before the first angle changes");
        VirtualFree(page, 0, MEM_RELEASE);
    }
}

bool missingBone = false;
int FakeLookup(void*, const char* name) {
    if (missingBone) return -1;
    if (!std::strcmp(name, "head_0")) return 3;
    if (!std::strcmp(name, "spine_3")) return 1;
    if (!std::strcmp(name, "spine_0")) return 2;
    if (!std::strcmp(name, "pelvis")) return 0;
    constexpr const char* limbs[]{"arm_upper_l", "arm_upper_r", "arm_lower_l", "arm_lower_r", "hand_l", "hand_r",
        "leg_upper_l", "leg_upper_r", "leg_lower_l", "leg_lower_r", "ankle_l", "ankle_r", "spine_1", "spine_2"};
    for (unsigned i = 0; i < std::size(limbs); ++i) if (!std::strcmp(name, limbs[i])) return 4 + i;
    return -1;
}
struct ActorFixture {
    std::array<unsigned char, 0x1000> controller{};
    std::array<unsigned char, 0x2100> pawn{};
    std::array<unsigned char, 0x500> scene{};
    std::array<unsigned char, 0x80> collision{};
    std::array<unsigned char, 0x200> model{};
    std::array<unsigned char, 18 * bone::BoneStride> pose{};
    std::uintptr_t modelPointer = 0;
};
struct TargetFixture {
    std::array<unsigned char, 0x2200> system{};
    std::vector<unsigned char> bucket = std::vector<unsigned char>(512 * ent::entity_layout::IdentityStride);
    std::array<std::uintptr_t, 16> vtable{};
    std::array<char, 64> controllerName{}, pawnName{};
    ActorFixture local{}, enemy{}, farther{};
    AimSource source{};
    AimBoneContext bones{};
    void Configure(ActorFixture& actor, unsigned index, std::uint8_t team, float y) {
        const auto controllerIdentity = reinterpret_cast<std::uintptr_t>(bucket.data()) + index * ent::entity_layout::IdentityStride;
        const auto pawnIdentity = controllerIdentity + ent::entity_layout::IdentityStride;
        const std::uint32_t controllerHandle = 0x8000 | index, pawnHandle = 0x8000 | (index + 1);
        Put(controllerIdentity, Address(actor.controller));
        Put(controllerIdentity + ent::entity_layout::HandleOffset, controllerHandle);
        Put(controllerIdentity + ent::CEntityIdentity::m_designerName, Address(controllerName));
        Put(pawnIdentity, Address(actor.pawn));
        Put(pawnIdentity + ent::entity_layout::HandleOffset, pawnHandle);
        Put(pawnIdentity + ent::CEntityIdentity::m_designerName, Address(pawnName));
        Put(Address(actor.controller) + ent::CEntityInstance::m_pEntity, controllerIdentity);
        Put(Address(actor.pawn) + ent::CEntityInstance::m_pEntity, pawnIdentity);
        Put(Address(actor.controller) + ent::CCSPlayerController::m_hPlayerPawn, pawnHandle);
        Put(Address(actor.pawn) + ent::C_BasePlayerPawn::m_hController, controllerHandle);
        Put(Address(actor.pawn) + ent::C_BaseEntity::m_iHealth, 100);
        Put(Address(actor.pawn) + ent::C_BaseEntity::m_iTeamNum, team);
        Put(Address(actor.pawn) + ent::C_BaseEntity::m_pGameSceneNode, Address(actor.scene));
        Put(Address(actor.pawn) + ent::C_BaseEntity::m_pCollision, Address(actor.collision));
        Put(Address(actor.scene), Address(vtable));
        Put(Address(actor.scene) + ent::CGameSceneNode::m_flAbsScale, 1.0f);
        Put(Address(actor.pawn) + silent_aim_target_offsets::ViewOffset, AimVector{0, 0, 64});
        actor.modelPointer = Address(actor.model);
        Put(Address(actor.scene) + bone::ModelState + bone::ModelHandle, Address(actor.modelPointer));
        Put(Address(actor.model) + bone::BoneCount, 18);
        Put(Address(actor.scene) + bone::BoneArray, Address(actor.pose));
        for (unsigned i = 0; i < 18; ++i)
            Put(Address(actor.pose) + i * bone::BoneStride, AimVector{1000, y, i < 4 ? 40 + i * 8.0f : 64.0f});
    }
    TargetFixture(bool playerSkeleton = false) {
        strcpy_s(controllerName.data(), controllerName.size(), "cs_player_controller");
        strcpy_s(pawnName.data(), pawnName.size(), "c_cs_player_for_precache");
        Put(Address(system) + ent::entity_layout::TableOffset, reinterpret_cast<std::uintptr_t>(bucket.data()));
        // Exercise the networked player wrapper using the current independently
        // verified RTTI table, rather than a historical captured module RVA.
        bones = {Address(vtable) - (playerSkeleton ? bone::PlayerSkeletonVtable : bone::SkeletonVtable), &FakeLookup};
        vtable[bone::SkeletonQuerySlot / sizeof(std::uintptr_t)] = bones.client + bone::SkeletonQuery;
        Configure(local, 1, 3, 0); Configure(enemy, 3, 2, 20); Configure(farther, 5, 2, 40);
        source = {Address(system), Address(local.controller), Address(local.pawn)};
    }
    bool Select(AimTarget& result, AimHitbox hitbox = AimHitbox::Chest, float fov = 5,
                AimTargetDiagnostics* diagnostics = nullptr) {
        return SilentAimSelectTarget(source, {}, fov, hitbox, bones, result, diagnostics);
    }
};
void TargetChecks() {
    TargetFixture fixture;
    AimTarget target{};
    AimTargetDiagnostics diagnostic{};
    TargetFixture playerFixture(true);
    Expect(playerFixture.Select(target, AimHitbox::Chest, 5, &diagnostic),
           "player body-component skeleton wrapper supports chest acquisition");
    Expect(playerFixture.Select(target, AimHitbox::Head, 5, &diagnostic),
           "player body-component skeleton wrapper supports head acquisition");
    playerFixture.bones.client -= 0x100;
    playerFixture.vtable[bone::SkeletonQuerySlot / sizeof(std::uintptr_t)] = playerFixture.bones.client + bone::SkeletonQuery;
    Expect(!playerFixture.Select(target, AimHitbox::Chest, 5, &diagnostic) && diagnostic.boneRejected == 2 &&
        !std::strcmp(diagnostic.boneFailure, "skeleton vtable"), "unverified skeleton class remains rejected even with the same query method");
    Expect(fixture.Select(target) && target.enemy.pawn == Address(fixture.enemy.pawn), "nearest angular enemy is selected from validated identities");
    Expect(fixture.Select(target, AimHitbox::Chest, 5, &diagnostic) && !std::strcmp(diagnostic.stage, "selected") &&
        diagnostic.controllers == 2 && diagnostic.boneRejected == 0,
        "target diagnostics distinguish a selected enemy from no candidate");
    const float chestPitch = target.solution.angles.pitch;
    Expect(fixture.Select(target, AimHitbox::Head) && Near(target.solution.angles.pitch, 0), "head resolves its named bone dynamically");
    Expect(fixture.Select(target, AimHitbox::Stomach) && target.solution.angles.pitch < chestPitch,
           "stomach selector reads a different animated landmark");
    Expect(fixture.Select(target, AimHitbox::Pelvis) && target.solution.angles.pitch > chestPitch,
           "pelvis selector uses its named bone rather than a fixed shared index");
    Put(Address(fixture.local.pawn) + silent_aim_target_offsets::ViewOffset, AimVector{0, 0, 32});
    Expect(fixture.Select(target, AimHitbox::Head) && target.solution.angles.pitch < -1.8f,
           "local eye follows changed crouching view offset rather than fixed standing height");
    Put(Address(fixture.local.pawn) + silent_aim_target_offsets::ViewOffset, AimVector{0, 0, 64});
    Expect(!fixture.Select(target, AimHitbox::Head, 0.5f), "target acquisition enforces FOV");
    Put(Address(fixture.enemy.pawn) + ent::C_BaseEntity::m_iTeamNum, std::uint8_t{3});
    Expect(fixture.Select(target) && target.enemy.pawn == Address(fixture.farther.pawn), "teammates are excluded regardless of ESP settings");
    Put(Address(fixture.farther.pawn) + ent::C_BaseEntity::m_iHealth, 0);
    Expect(!fixture.Select(target), "dead players are excluded");
    Put(Address(fixture.enemy.pawn) + ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
    Put(Address(fixture.enemy.scene) + ent::CGameSceneNode::m_bDormant, std::uint8_t{1});
    Expect(!fixture.Select(target), "dormant players are excluded");
    Put(Address(fixture.enemy.scene) + ent::CGameSceneNode::m_bDormant, std::uint8_t{0});
    missingBone = true;
    Expect(!fixture.Select(target), "missing selected bone skips target without hull fallback");
    missingBone = false;
    Put(Address(fixture.enemy.scene) + bone::BoneArray, std::uintptr_t{0});
    Expect(!fixture.Select(target), "missing animated pose skips the target");
    Expect(!fixture.Select(target, AimHitbox::Chest, 5, &diagnostic) && diagnostic.boneRejected == 1 &&
        !std::strcmp(diagnostic.boneFailure, "pose cache"), "missing pose diagnostics identify the exact bone-read stage");
    Put(Address(fixture.enemy.scene) + bone::BoneArray, Address(fixture.enemy.pose));
    Put(Address(fixture.enemy.model) + bone::BoneCount, 1);
    Expect(!fixture.Select(target), "bone index is checked against current model count");
    Put(Address(fixture.enemy.model) + bone::BoneCount, 18);
    Expect(fixture.Select(target) && SilentAimTargetValid(fixture.source, target), "fresh target validates before command commit");
    Put(Address(fixture.enemy.pawn) + ent::C_BaseEntity::m_iHealth, 40);
    Expect(!SilentAimTargetValid(fixture.source, target), "health change invalidates copied health used for damage-cap ranking");
    Put(Address(fixture.enemy.pawn) + ent::C_BaseEntity::m_iHealth, 100);
    const auto query = fixture.vtable[bone::SkeletonQuerySlot / sizeof(std::uintptr_t)];
    fixture.vtable[bone::SkeletonQuerySlot / sizeof(std::uintptr_t)] = 0;
    Expect(!SilentAimTargetValid(fixture.source, target), "changed skeleton type invalidates selected pose");
    fixture.vtable[bone::SkeletonQuerySlot / sizeof(std::uintptr_t)] = query;
    const auto savedModel = fixture.enemy.modelPointer;
    fixture.enemy.modelPointer = 0;
    Expect(!SilentAimTargetValid(fixture.source, target), "model replacement invalidates selected pose");
    fixture.enemy.modelPointer = savedModel;
    Put(Address(fixture.enemy.pawn) + ent::C_BasePlayerPawn::m_hController, std::uint32_t{UINT32_MAX});
    Expect(!SilentAimTargetValid(fixture.source, target) && !fixture.Select(target), "changed reciprocal owner invalidates target");
    Expect(!fixture.Select(target, AimHitbox::Chest, 5, &diagnostic) && diagnostic.actorRejected >= 1,
        "invalid reciprocal owner is counted separately from missing bones and FOV");
    const auto localPawn = fixture.source.pawn;
    fixture.source.pawn = 0;
    Expect(!fixture.Select(target, AimHitbox::Chest, 5, &diagnostic) &&
        !std::strcmp(diagnostic.localFailure, "local pawn global mismatch"), "local source mismatch is diagnosed before enemy scanning");
    fixture.source.pawn = localPawn;
    Put(Address(fixture.local.pawn) + ent::C_BaseEntity::m_lifeState, std::uint8_t{1});
    Expect(!fixture.Select(target), "dead local player cannot acquire a target");
}
struct FilterContext { std::uintptr_t rejectedPawn = 0; unsigned calls = 0; bool rejectAll = false; };
bool Eligible(const AimTarget& target, void* opaque) noexcept {
    auto& context = *static_cast<FilterContext*>(opaque);
    ++context.calls;
    return !context.rejectAll && target.enemy.pawn != context.rejectedPawn;
}
void FilteredTargetChecks() {
    TargetFixture fixture;
    for (unsigned i = 0; i < 4; ++i)
        Put(Address(fixture.farther.pose) + i * bone::BoneStride, AimVector{-1000, 0, 40 + i * 8.0f});
    AimTarget target{};
    AimTargetDiagnostics diagnostics{};
    FilterContext context{Address(fixture.enemy.pawn)};
    Expect(SilentAimSelectTarget(fixture.source, {}, 180, AimHitbox::Head, fixture.bones, target,
        &diagnostics, &Eligible, &context) && target.enemy.pawn == Address(fixture.farther.pawn) &&
        Near(target.solution.separation, 180) && context.calls == 2 && diagnostics.eligibilityRejected == 1,
        "native entity scan continues behind the camera after nearest candidate fails eligibility");
    Expect(fixture.Select(target, AimHitbox::Head, 180) && target.enemy.pawn == Address(fixture.enemy.pawn),
        "unfiltered manual entity scan retains angular-nearest priority");
    const AimVector nativeEye{0, 100, 64};
    Expect(SilentAimSelectTarget(fixture.source, {0,-4.573921f,0}, .1f, AimHitbox::Head, fixture.bones, target,
        nullptr, nullptr, nullptr, &nativeEye) && target.nativeEye && target.eye.y == 100 &&
        SilentAimTargetValid(fixture.source, target), "native shooting origin can differ from scene eye without invalidating its target");
    Put(Address(fixture.local.scene) + ent::CGameSceneNode::m_vecAbsOrigin, AimVector{1,0,0});
    Expect(!SilentAimTargetValid(fixture.source, target), "scene movement still invalidates a target acquired from native eye");
    Put(Address(fixture.local.scene) + ent::CGameSceneNode::m_vecAbsOrigin, AimVector{});
    context.rejectAll = true;
    Expect(!SilentAimSelectTarget(fixture.source, {}, 180, AimHitbox::Head, fixture.bones, target,
        &diagnostics, &Eligible, &context) && !target.solution.valid && diagnostics.eligibilityRejected == 2,
        "all ineligible candidates leave no automatic target");
    context.rejectAll = false; context.calls = 0;
    Expect(!SilentAimSelectTarget(fixture.source, {}, 5, AimHitbox::Head, fixture.bones, target,
        &diagnostics, &Eligible, &context) && diagnostics.outsideFov == 1 && context.calls == 1,
        "eligibility cannot admit candidates outside the configured FOV");
    context.rejectedPawn = 0; context.calls = 0;
    Expect(SilentAimSelectTarget(fixture.source, {}, 180, AimHitbox::Head, fixture.bones, target,
        &diagnostics, &Eligible, &context) && context.calls == 2 && target.enemy.pawn == Address(fixture.farther.pawn),
        "eligibility sees farther candidates and owns ranking after an earlier candidate qualifies");
}
struct LandmarkRanking { unsigned calls = 0; AimHitbox chosen = AimHitbox::RightHand; };
bool ChooseLandmark(const AimTarget& target, void* opaque) noexcept {
    auto& ranking = *static_cast<LandmarkRanking*>(opaque);
    ++ranking.calls;
    return target.hitbox == ranking.chosen;
}
void MultiHitgroupChecks() {
    TargetFixture fixture;
    AimTarget target{};
    AimTargetDiagnostics diagnostics{};
    const auto hands = AimHitgroupBit(AimHitgroup::Hands);
    Expect(!SilentAimSelectTarget(fixture.source, {}, 180, 0u, fixture.bones, target, &diagnostics) &&
        diagnostics.controllers == 0 && !target.solution.valid, "empty group mask selects no target");
    Expect(!SilentAimSelectTarget(fixture.source, {}, 180, 0x100u, fixture.bones, target),
        "unknown group bits cannot select a landmark");
    Put(Address(fixture.enemy.pose) + 8 * bone::BoneStride, AimVector{1000, 0, 64});
    Put(Address(fixture.enemy.pose) + 9 * bone::BoneStride, AimVector{1000, 100, 64});
    LandmarkRanking ranking{};
    Expect(SilentAimSelectTarget(fixture.source, {}, 180, hands, fixture.bones, target, &diagnostics,
        &ChooseLandmark, &ranking) && target.hitbox == AimHitbox::RightHand && ranking.calls == 4 &&
        target.enemy.boneIndex == 9, "both hands on each enemy reach eligibility despite an earlier closer landmark");
    Expect(target.enemy.health == 100, "candidate copies target health for expected damage ranking");
    Put(Address(fixture.enemy.pose) + 8 * bone::BoneStride, AimVector{-1000, 0, 64});
    Put(Address(fixture.farther.pose) + 8 * bone::BoneStride, AimVector{-1000, 0, 64});
    Expect(SilentAimSelectTarget(fixture.source, {}, 10, hands, fixture.bones, target) &&
        target.hitbox == AimHitbox::RightHand, "off-FOV left hand does not exclude independent right-hand selection");
    for (unsigned i = 0; i < static_cast<unsigned>(AimHitbox::Count); ++i) {
        const auto hitbox = static_cast<AimHitbox>(i);
        Expect(AimHitboxGroup(hitbox) != 0 && fixture.Select(target, hitbox, 180) && target.hitbox == hitbox,
            "every supported landmark has a group and independent named-bone selection");
    }
    ranking.calls = 0; ranking.chosen = AimHitbox::Chest;
    Expect(SilentAimSelectTarget(fixture.source, {}, 180, AimHitgroupBit(AimHitgroup::Chest), fixture.bones,
        target, &diagnostics, &ChooseLandmark, &ranking) && ranking.calls == 6,
        "chest group evaluates upper, middle and lower authored torso landmarks per enemy");
}
}
int main() {
    MathChecks(); CommandChecks(); TargetChecks(); FilteredTargetChecks(); MultiHitgroupChecks();
    std::printf("Silent aim failures: %d\n", failures);
    return failures ? 1 : 0;
}
