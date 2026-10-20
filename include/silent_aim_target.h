#pragma once
#include "silent_aim_math.h"
#include "silent_aim_settings.h"
#include "aim_hitchance.h"
#include <array>
#include <cstdint>

struct AimSource { std::uintptr_t system = 0, controller = 0, pawn = 0; };
struct AimActor {
    std::uintptr_t controller = 0, pawn = 0, scene = 0, collision = 0;
    std::uintptr_t modelHandle = 0, model = 0, boneArray = 0;
    std::uintptr_t skeletonVtable = 0, skeletonQuery = 0;
    std::uint32_t controllerHandle = UINT32_MAX, pawnHandle = UINT32_MAX;
    std::uint8_t team = 0;
    int boneIndex = -1;
    int health = 0;
};
struct AimTarget {
    AimActor local{}, enemy{};
    AimSolution solution{};
    AimVector eye{}, point{};
    AimHitbox hitbox = AimHitbox::Chest;
};
struct AimTargetVolume {
    AimCapsule capsule{};
    AimVector center{};
    std::uintptr_t pawn = 0, model = 0, poseAddress = 0;
    std::uintptr_t meshArray = 0, meshMasks = 0, mesh = 0, setArray = 0, boxes = 0, box = 0;
    std::uint64_t meshGroup = 0, meshMask = 0;
    std::array<unsigned char, 0x70> record{};
    std::array<float, 8> pose{};
    int meshCount = 0, meshIndex = -1, setCount = 0, boxCount = 0;
    std::uint8_t setIndex = 0;
    bool valid = false;
};
using AimBoneLookup = int (*)(void*, const char*);
struct AimBoneContext { std::uintptr_t client = 0; AimBoneLookup lookup = nullptr; };
struct AimTargetDiagnostics {
    const char* stage = "not attempted";
    const char* localFailure = "none";
    const char* actorFailure = "none";
    const char* boneFailure = "none";
    unsigned controllers = 0, actorRejected = 0, teammates = 0, boneRejected = 0, outsideFov = 0, eligibilityRejected = 0;
    std::uintptr_t sceneVtable = 0, modelHandle = 0, model = 0, pose = 0;
    int boneCount = 0, boneIndex = -1;
    std::uint32_t controllerHandle = UINT32_MAX, controllerFlags = 0, reverseController = UINT32_MAX;
};

// Command-owner only; no shared ESP snapshots or retained engine pointers.
constexpr std::uint32_t AimHitboxGroup(AimHitbox hitbox) noexcept {
    switch (hitbox) {
    case AimHitbox::Head: return AimHitgroupBit(AimHitgroup::Head);
    case AimHitbox::Chest: case AimHitbox::LowerChest: case AimHitbox::MiddleChest:
        return AimHitgroupBit(AimHitgroup::Chest);
    case AimHitbox::Stomach: return AimHitgroupBit(AimHitgroup::Stomach);
    case AimHitbox::Pelvis: return AimHitgroupBit(AimHitgroup::Pelvis);
    case AimHitbox::LeftUpperArm: case AimHitbox::RightUpperArm:
    case AimHitbox::LeftForearm: case AimHitbox::RightForearm: return AimHitgroupBit(AimHitgroup::Arms);
    case AimHitbox::LeftHand: case AimHitbox::RightHand: return AimHitgroupBit(AimHitgroup::Hands);
    case AimHitbox::LeftThigh: case AimHitbox::RightThigh:
    case AimHitbox::LeftCalf: case AimHitbox::RightCalf: return AimHitgroupBit(AimHitgroup::Legs);
    case AimHitbox::LeftFoot: case AimHitbox::RightFoot: return AimHitgroupBit(AimHitgroup::Feet);
    default: return 0;
    }
}
// With a filter, every in-FOV landmark is offered: returning true replaces the
// current best. The callback owns ranking. Without a filter, ranking is angular.
using AimTargetFilter = bool (*)(const AimTarget&, void*) noexcept;
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, std::uint32_t hitgroups,
                           const AimBoneContext& bones, AimTarget& target,
                           AimTargetDiagnostics* diagnostics = nullptr,
                           AimTargetFilter filter = nullptr, void* filterContext = nullptr) noexcept;
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, AimHitbox hitbox,
                           const AimBoneContext& bones, AimTarget& target,
                           AimTargetDiagnostics* diagnostics = nullptr,
                           AimTargetFilter filter = nullptr, void* filterContext = nullptr) noexcept;
bool SilentAimTargetValid(const AimSource& source, const AimTarget& target) noexcept;
// Reads the authored capsule attached to the selected landmark in the current
// enabled mesh/current hitbox set, then transforms its endpoints by copied pose.
// Missing or unsupported geometry leaves manual landmark targeting available.
bool SilentAimReadTargetVolume(const AimSource& source, const AimTarget& target,
                              AimTargetVolume& volume, const char*& reason) noexcept;
bool SilentAimTargetVolumeValid(const AimSource& source, const AimTarget& target,
                               const AimTargetVolume& volume) noexcept;
