#pragma once
#include <cstdint>

struct AimSource;
struct AimTarget;
struct AimTargetVolume;
struct AimWeaponSnapshot;
struct AimOcclusionResult;

struct AimDamageContext {
    std::uintptr_t client = 0;
    bool prepared = false;
};
// All values are copied on the native command owner. No engine pointers are
// dereferenced by render/logging consumers. This estimates one direct bullet.
struct AimDamageInput {
    int baseDamage = 0, armor = 0, hitgroup = 0;
    float headshotMultiplier = 0, armorRatio = 0, rangeModifier = 0;
    float distance = 0, range = 0, headScale = 0, bodyScale = 0;
    bool helmet = false, headshotOnly = false;
    bool operator==(const AimDamageInput&) const = default;
};
struct AimDamageResult {
    AimDamageInput input{};
    std::uintptr_t itemServices = 0;
    float damage = 0;
    int healthDamage = 0;
    bool valid = false;
    const char* reason = "damage not evaluated";
};

AimDamageResult AimDamageCalculate(const AimDamageInput& input) noexcept;
bool AimDamagePrepare(std::uintptr_t client, AimDamageContext& context) noexcept;
AimDamageResult AimDamageRead(const AimDamageContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon,
    const AimOcclusionResult& path) noexcept;
bool AimDamageStillCurrent(const AimDamageContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon,
    const AimDamageResult& snapshot) noexcept;
#ifdef CS2_AIM_DAMAGE_CHECKS
void AimDamageTestRuntime(bool enabled) noexcept;
#endif
