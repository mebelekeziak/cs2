#include "silent_aim_target.h"
#include "silent_aim_target_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <array>
#include <cmath>
#include <cstring>

namespace {
namespace off = cs2_offsets;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
struct Identity { std::uintptr_t entity = 0; std::uint32_t handle = UINT32_MAX; };
bool Resolve(std::uintptr_t system, std::uint32_t handle, Identity& identity) noexcept {
    if (!system || handle == UINT32_MAX) return false;
    const auto index = handle & off::entity_layout::IndexMask;
    std::uintptr_t bucket = 0, back = 0;
    if (!Read(system + off::entity_layout::TableOffset +
        (index >> off::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) || !bucket) return false;
    const auto address = bucket + (index & off::entity_layout::BucketMask) * off::entity_layout::IdentityStride;
    return Read(address + off::entity_layout::EntityPointerOffset, identity.entity) && identity.entity &&
        Read(address + off::entity_layout::HandleOffset, identity.handle) && identity.handle == handle &&
        Read(identity.entity + off::CEntityInstance::m_pEntity, back) && back == address;
}
bool FromPointer(std::uintptr_t system, std::uintptr_t pointer, Identity& identity) noexcept {
    std::uintptr_t address = 0;
    std::uint32_t handle = UINT32_MAX;
    return pointer && Read(pointer + off::CEntityInstance::m_pEntity, address) && address &&
        Read(address + off::entity_layout::HandleOffset, handle) && Resolve(system, handle, identity) && identity.entity == pointer;
}
bool Designer(const Identity& identity, const char* expected) noexcept {
    std::uintptr_t address = 0, name = 0;
    char text[64]{};
    return Read(identity.entity + off::CEntityInstance::m_pEntity, address) &&
        Read(address + off::CEntityIdentity::m_designerName, name) &&
        MovementCopy(name, text, sizeof(text) - 1) && std::strcmp(text, expected) == 0;
}
bool Live(const AimActor& actor) noexcept {
    int health = 0;
    std::uint8_t life = 1, team = 0, dormant = 1;
    std::uintptr_t scene = 0, collision = 0;
    return Read(actor.pawn + off::C_BaseEntity::m_iHealth, health) && health > 0 && health <= 1000 &&
        Read(actor.pawn + off::C_BaseEntity::m_lifeState, life) && life == 0 &&
        Read(actor.pawn + off::C_BaseEntity::m_iTeamNum, team) && team == actor.team && (team == 2 || team == 3) &&
        Read(actor.pawn + off::C_BaseEntity::m_pGameSceneNode, scene) && scene && scene == actor.scene &&
        Read(scene + off::CGameSceneNode::m_bDormant, dormant) && dormant == 0 &&
        Read(actor.pawn + off::C_BaseEntity::m_pCollision, collision) && collision && collision == actor.collision;
}
bool Actor(std::uintptr_t system, std::uintptr_t controller, AimActor& actor,
           const char*& failure, AimTargetDiagnostics* diagnostics) noexcept {
    Identity owner{}, pawn{};
    std::uint32_t handle = UINT32_MAX, reverse = UINT32_MAX;
    failure = "controller identity";
    if (!FromPointer(system, controller, owner)) return false;
    if (diagnostics) {
        diagnostics->controllerHandle = owner.handle;
        diagnostics->controllerFlags = 0;
        diagnostics->reverseController = UINT32_MAX;
        std::uintptr_t identity = 0;
        if (Read(controller + off::CEntityInstance::m_pEntity, identity))
            Read(identity + off::CEntityIdentity::m_flags, diagnostics->controllerFlags);
    }
    failure = "controller class";
    if (!Designer(owner, "cs_player_controller")) return false;
    failure = "forward pawn handle";
    if (!Read(controller + off::CCSPlayerController::m_hPlayerPawn, handle) || !Resolve(system, handle, pawn)) return false;
    failure = "pawn class";
    if (!Designer(pawn, "c_cs_player_for_precache")) return false;
    failure = "reverse controller read";
    if (!Read(pawn.entity + off::C_BasePlayerPawn::m_hController, reverse)) return false;
    if (diagnostics) diagnostics->reverseController = reverse;
    failure = "reverse controller mismatch";
    if (reverse != owner.handle) return false;
    actor.controller = controller; actor.pawn = pawn.entity;
    actor.controllerHandle = owner.handle; actor.pawnHandle = handle;
    failure = "actor life/team/scene/collision";
    if (!(Read(actor.pawn + off::C_BaseEntity::m_iTeamNum, actor.team) &&
        Read(actor.pawn + off::C_BaseEntity::m_pGameSceneNode, actor.scene) &&
        Read(actor.pawn + off::C_BaseEntity::m_pCollision, actor.collision) && Live(actor))) return false;
    failure = "none";
    return true;
}
bool Same(std::uintptr_t system, const AimActor& actor) noexcept {
    Identity controller{}, pawn{};
    std::uint32_t linked = UINT32_MAX, reverse = UINT32_MAX;
    if (!(Resolve(system, actor.controllerHandle, controller) && controller.entity == actor.controller &&
        Resolve(system, actor.pawnHandle, pawn) && pawn.entity == actor.pawn &&
        Read(controller.entity + off::CCSPlayerController::m_hPlayerPawn, linked) && linked == actor.pawnHandle &&
        Read(pawn.entity + off::C_BasePlayerPawn::m_hController, reverse) && reverse == actor.controllerHandle && Live(actor))) return false;
    if (!actor.model) return true;
    namespace bone = silent_aim_hitbox_offsets;
    std::uintptr_t handle = 0, model = 0, array = 0, vtable = 0, query = 0;
    return Read(actor.scene, vtable) && vtable == actor.skeletonVtable &&
        Read(vtable + bone::SkeletonQuerySlot, query) && query == actor.skeletonQuery &&
        Read(actor.scene + bone::ModelState + bone::ModelHandle, handle) && handle == actor.modelHandle &&
        Read(handle, model) && model == actor.model && Read(actor.scene + bone::BoneArray, array) && array == actor.boneArray;
}
bool Vector(const AimVector& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) < 1000000 && std::abs(value.y) < 1000000 && std::abs(value.z) < 1000000;
}
bool Point(const AimActor& actor, const AimVector& offset, AimVector& point) noexcept {
    AimVector origin{}, angles{};
    float scale = 0;
    if (!Vector(offset) || !Read(actor.scene + off::CGameSceneNode::m_vecAbsOrigin, origin) || !Vector(origin) ||
        !Read(actor.scene + off::CGameSceneNode::m_angAbsRotation, angles) || !Vector(angles) ||
        !Read(actor.scene + off::CGameSceneNode::m_flAbsScale, scale) || !std::isfinite(scale) || scale <= 0 || scale > 100) return false;
    constexpr float radians = 0.01745329251994329577f;
    const float sp = std::sin(angles.x * radians), cp = std::cos(angles.x * radians);
    const float sy = std::sin(angles.y * radians), cy = std::cos(angles.y * radians);
    const float sr = std::sin(angles.z * radians), cr = std::cos(angles.z * radians);
    point = {
        origin.x + scale * (cp * cy * offset.x + (sp * sr * cy - cr * sy) * offset.y + (sp * cr * cy + sr * sy) * offset.z),
        origin.y + scale * (cp * sy * offset.x + (sp * sr * sy + cr * cy) * offset.y + (sp * cr * sy - sr * cy) * offset.z),
        origin.z + scale * (-sp * offset.x + sr * cp * offset.y + cr * cp * offset.z)};
    return Vector(point);
}
bool Lookup(const AimBoneContext& bones, std::uintptr_t pawn, const char* name, int& index) noexcept {
    if (!bones.lookup) return false;
    __try { index = bones.lookup(reinterpret_cast<void*>(pawn), name); return index >= 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool BonePoint(AimActor& actor, AimHitbox hitbox, const AimBoneContext& bones, AimVector& point,
               AimTargetDiagnostics* diagnostics) noexcept {
    namespace bone = silent_aim_hitbox_offsets;
    const char* name = nullptr;
    switch (hitbox) {
    case AimHitbox::Head: name = bone::HeadName; break;
    case AimHitbox::Chest: name = bone::ChestName; break;
    case AimHitbox::Stomach: name = bone::StomachName; break;
    case AimHitbox::Pelvis: name = bone::PelvisName; break;
    default: if (diagnostics) diagnostics->boneFailure = "invalid hitbox"; return false;
    }
    std::uintptr_t vtable = 0, query = 0, handle = 0, model = 0, array = 0;
    int count = 0, index = -1;
    const auto fail = [&](const char* reason) noexcept {
        if (diagnostics) {
            diagnostics->boneFailure = reason;
            diagnostics->sceneVtable = vtable; diagnostics->modelHandle = handle;
            diagnostics->model = model; diagnostics->pose = array;
            diagnostics->boneCount = count; diagnostics->boneIndex = index;
        }
        return false;
    };
    if (!bones.client || !Read(actor.scene, vtable) || vtable != bones.client + bone::SkeletonVtable)
        return fail("skeleton vtable");
    if (!Read(vtable + bone::SkeletonQuerySlot, query) || query != bones.client + bone::SkeletonQuery)
        return fail("skeleton query");
    if (!Read(actor.scene + bone::ModelState + bone::ModelHandle, handle) || !handle)
        return fail("model handle");
    if (!Read(handle, model) || !model) return fail("model pointer");
    if (!Read(model + bone::BoneCount, count) || count <= 0 || count > 1024) return fail("bone count");
    if (!Read(actor.scene + bone::BoneArray, array) || !array) return fail("pose cache");
    if (!Lookup(bones, actor.pawn, name, index) || index >= count) return fail("named bone lookup");
    if (!Read(array + static_cast<std::uintptr_t>(index) * bone::BoneStride + bone::Position, point) || !Vector(point))
        return fail("bone position");
    std::uintptr_t currentScene = 0, currentHandle = 0, currentModel = 0, currentArray = 0;
    actor.modelHandle = handle; actor.model = model; actor.boneArray = array;
    actor.skeletonVtable = vtable; actor.skeletonQuery = query;
    std::uintptr_t currentVtable = 0, currentQuery = 0;
    if (!(Read(actor.scene, currentVtable) && currentVtable == vtable &&
        Read(currentVtable + bone::SkeletonQuerySlot, currentQuery) && currentQuery == query &&
        Read(actor.pawn + off::C_BaseEntity::m_pGameSceneNode, currentScene) && currentScene == actor.scene &&
        Read(actor.scene + bone::ModelState + bone::ModelHandle, currentHandle) && currentHandle == handle &&
        Read(handle, currentModel) && currentModel == model &&
        Read(actor.scene + bone::BoneArray, currentArray) && currentArray == array)) return fail("pose source changed");
    fail("none");
    return true;
}
}

bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, AimHitbox hitbox,
                           const AimBoneContext& bones, AimTarget& target, AimTargetDiagnostics* diagnostics) noexcept {
    target = {};
    if (diagnostics) { *diagnostics = {}; diagnostics->stage = "local actor"; }
    AimActor local{};
    AimVector offset{}, eye{};
    const char* failure = "entity system unavailable";
    if (!source.system || !Actor(source.system, source.controller, local, failure, diagnostics)) {
        if (diagnostics) diagnostics->localFailure = failure;
        return false;
    }
    if (local.pawn != source.pawn) {
        if (diagnostics) diagnostics->localFailure = "local pawn global mismatch";
        return false;
    }
    if (diagnostics) diagnostics->stage = "local eye";
    if (!Read(local.pawn + silent_aim_target_offsets::ViewOffset, offset) || !Point(local, offset, eye)) {
        if (diagnostics) diagnostics->localFailure = "view offset/scene transform";
        return false;
    }
    if (diagnostics) diagnostics->stage = "enumerating controllers";
    std::array<unsigned char, (off::entity_layout::BucketMask + 1) * off::entity_layout::IdentityStride> entries{};
    for (unsigned i = 0; i < off::entity_layout::BucketCount; ++i) {
        std::uintptr_t bucket = 0;
        if (!Read(source.system + off::entity_layout::TableOffset + i * sizeof(std::uintptr_t), bucket) || !bucket ||
            !MovementCopy(bucket, entries.data(), entries.size())) continue;
        for (unsigned slot = 0; slot <= off::entity_layout::BucketMask; ++slot) {
            Identity identity{};
            const auto* record = entries.data() + slot * off::entity_layout::IdentityStride;
            std::memcpy(&identity.entity, record + off::entity_layout::EntityPointerOffset, sizeof(identity.entity));
            std::memcpy(&identity.handle, record + off::entity_layout::HandleOffset, sizeof(identity.handle));
            if (!identity.entity || identity.entity == local.controller || identity.entity == local.pawn ||
                identity.handle == UINT32_MAX || (identity.handle & off::entity_layout::IndexMask) !=
                    (i << off::entity_layout::BucketShift) + slot || !Designer(identity, "cs_player_controller")) continue;
            AimActor enemy{};
            AimVector point{};
            if (diagnostics) ++diagnostics->controllers;
            if (!Actor(source.system, identity.entity, enemy, failure, diagnostics)) {
                if (diagnostics) { ++diagnostics->actorRejected; diagnostics->actorFailure = failure; }
                continue;
            }
            if (enemy.controllerHandle != identity.handle) {
                if (diagnostics) { ++diagnostics->actorRejected; diagnostics->actorFailure = "controller identity changed"; }
                continue;
            }
            if (enemy.pawn == local.pawn || enemy.team == local.team) {
                if (diagnostics) ++diagnostics->teammates;
                continue;
            }
            if (!BonePoint(enemy, hitbox, bones, point, diagnostics)) {
                if (diagnostics) ++diagnostics->boneRejected;
                continue;
            }
            const auto solution = SilentAimSolve(eye, point, view, fov);
            if (!solution.valid) {
                if (diagnostics) ++diagnostics->outsideFov;
                continue;
            }
            if (target.solution.valid && solution.separation >= target.solution.separation) continue;
            if (!Same(source.system, enemy)) continue;
            target = {local, enemy, solution};
        }
    }
    if (diagnostics) diagnostics->stage = target.solution.valid ? "target revalidation" : "no candidate selected";
    const bool valid = target.solution.valid && SilentAimTargetValid(source, target);
    if (valid && diagnostics) diagnostics->stage = "selected";
    return valid;
}
bool SilentAimTargetValid(const AimSource& source, const AimTarget& target) noexcept {
    return target.solution.valid && target.local.controller == source.controller && target.local.pawn == source.pawn &&
        target.enemy.pawn != source.pawn && target.enemy.team != target.local.team &&
        Same(source.system, target.local) && Same(source.system, target.enemy);
}
