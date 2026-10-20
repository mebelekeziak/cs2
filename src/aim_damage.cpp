#include "aim_damage.h"
#include "aim_damage_offsets.h"
#include "aim_autowall_offsets.h"
#include "aim_occlusion.h"
#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "silent_aim_target.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
namespace off = aim_damage_offsets;
#ifdef CS2_AIM_DAMAGE_CHECKS
bool testRuntime = false;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
bool CurrentClient(std::uintptr_t client) noexcept {
#ifdef CS2_AIM_DAMAGE_CHECKS
    if (testRuntime) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Scale(std::uintptr_t client, std::ptrdiff_t wrapper, float& value) noexcept {
    std::uintptr_t storage = 0;
    return Read(client + wrapper + off::CvarValuePointer, storage) && storage &&
        Read(storage + off::CvarValue, value);
}
bool Sample(const AimDamageContext& context, const AimSource& source, const AimTarget& target,
    const AimTargetVolume& volume, const AimWeaponSnapshot& weapon, AimDamageResult& result) noexcept {
    result.reason = "damage source/geometry unavailable";
    // Volume validation includes target identity, health and pose validation.
    // Keep one complete check at this boundary instead of nesting it twice.
    if (!context.prepared || !CurrentClient(context.client) || !weapon.weapon || !weapon.data ||
        !SilentAimTargetVolumeValid(source, target, volume)) return false;
    std::uintptr_t pawn = 0, controller = 0, system = 0, data = 0, table = 0, owner = 0, storage = 0;
    std::uint8_t immune = 0, helmet = 0, team = 0, headshotOnly = 0;
    float sourceBoost = 0, targetBoost = 0;
    auto& input = result.input;
    result.reason = "damage data/armor unavailable or stale";
    if (!Read(context.client + cs2_offsets::globals::dwLocalPlayerPawn, pawn) || pawn != source.pawn ||
        !Read(context.client + cs2_offsets::globals::dwLocalPlayerController, controller) || controller != source.controller ||
        !Read(context.client + cs2_offsets::globals::dwEntityList, system) || system != source.system ||
        !Read(weapon.weapon + aim_weapon_offsets::Data, data) || data != weapon.data ||
        !Read(data + off::Damage, input.baseDamage) ||
        !Read(data + off::HeadshotMultiplier, input.headshotMultiplier) ||
        !Read(data + off::ArmorRatio, input.armorRatio) ||
        !Read(data + off::RangeModifier, input.rangeModifier) ||
        !Read(data + aim_weapon_offsets::Range, input.range) || input.range != weapon.range ||
        !Read(target.enemy.pawn + off::ArmorValue, input.armor) ||
        !Read(target.enemy.pawn + off::ItemServices, result.itemServices) || !result.itemServices ||
        !Read(result.itemServices, table) || table != context.client + off::ItemServicesVtable ||
        !Read(result.itemServices + off::ServicePawn, owner) || owner != target.enemy.pawn ||
        !Read(result.itemServices + off::HasHelmet, helmet) || helmet > 1 ||
        !Read(target.enemy.pawn + off::GunGameImmunity, immune) || immune > 1 ||
        !Read(target.enemy.pawn + cs2_offsets::C_BaseEntity::m_iTeamNum, team) ||
        team != target.enemy.team || (team != 2 && team != 3) ||
        !Scale(context.client, team == 3 ? off::DamageScaleCtHead : off::DamageScaleTHead, input.headScale) ||
        !Scale(context.client, team == 3 ? off::DamageScaleCtBody : off::DamageScaleTBody, input.bodyScale) ||
        !Read(context.client + off::HeadshotOnly + off::CvarValuePointer, storage) || !storage ||
        !Read(storage + off::CvarValue, headshotOnly) || headshotOnly > 1) return false;
    if (input.penetrationDamage >= 0 &&
        !Read(data + aim_autowall_offsets::Penetration, input.penetrationPower)) return false;
    if (immune) { result.reason = "damage target immune"; return false; }
    result.reason = "damage healthshot modifier unavailable or active";
    if (!std::isfinite(weapon.currentTime) || weapon.currentTime < 0 ||
        !Read(source.pawn + off::HealthShotBoostExpiration, sourceBoost) || !std::isfinite(sourceBoost) ||
        !Read(target.enemy.pawn + off::HealthShotBoostExpiration, targetBoost) || !std::isfinite(targetBoost) ||
        sourceBoost > weapon.currentTime || targetBoost > weapon.currentTime) return false;
    input.helmet = helmet != 0;
    input.headshotOnly = headshotOnly != 0;
    return true;
}
}

AimDamageResult AimDamageCalculate(const AimDamageInput& input) noexcept {
    AimDamageResult result{};
    result.input = input;
    result.reason = "invalid direct-bullet damage inputs";
    if (input.baseDamage <= 0 || input.baseDamage > 100000 || input.armor < 0 || input.armor > 10000 ||
        input.hitgroup < 1 || input.hitgroup > 8 ||
        !std::isfinite(input.headshotMultiplier) || input.headshotMultiplier < 0 || input.headshotMultiplier > 100 ||
        !std::isfinite(input.armorRatio) || input.armorRatio < 0 || input.armorRatio > 2 ||
        !std::isfinite(input.rangeModifier) || input.rangeModifier <= 0 || input.rangeModifier > 1 ||
        !std::isfinite(input.distance) || input.distance < 0 ||
        !std::isfinite(input.range) || input.range <= 0 || input.range > 1000000 || input.distance > input.range ||
        !std::isfinite(input.headScale) || input.headScale < 0 || input.headScale > 100 ||
        !std::isfinite(input.bodyScale) || input.bodyScale < 0 || input.bodyScale > 100) return result;
    if (!std::isfinite(input.penetrationDamage) || input.penetrationDamage < -1 ||
        (input.penetrationDamage != -1 && (input.penetrationDamage < 0 || input.penetrationDamage > input.baseDamage ||
            !std::isfinite(input.penetrationPower) || input.penetrationPower <= 0 || input.penetrationPower > 100))) return result;
    float damage = input.penetrationDamage >= 0 ? input.penetrationDamage :
        static_cast<float>(input.baseDamage) * std::pow(input.rangeModifier, input.distance / 500.0f);
    if (input.headshotOnly && input.hitgroup != 1) damage = 0;
    else if (input.hitgroup == 1) { damage *= input.headshotMultiplier; damage *= input.headScale; }
    else if (input.hitgroup == 3) { damage *= 1.25f; damage *= input.bodyScale; }
    else if (input.hitgroup == 6 || input.hitgroup == 7) { damage *= .75f; damage *= input.bodyScale; }
    else damage *= input.bodyScale;
    const bool armored = input.armor > 0 && (input.hitgroup == 1 ? input.helmet : input.hitgroup != 6 && input.hitgroup != 7);
    if (armored) {
        const float scaled = damage * (input.armorRatio * .5f);
        damage = (damage - scaled) * .5f > static_cast<float>(input.armor) ?
            damage - static_cast<float>(input.armor) / .5f : scaled;
    }
    if (!std::isfinite(damage) || damage < 0 || damage > 100000) return result;
    result.damage = damage;
    // Deliberately conservative whole-HP threshold policy, not a claim about
    // every server's event rounding or damage hooks.
    result.healthDamage = static_cast<int>(std::floor(damage));
    result.valid = true;
    result.reason = input.penetrationDamage >= 0 ? "native penetration damage estimate" : "direct-bullet damage estimate";
    return result;
}

bool AimDamagePrepare(std::uintptr_t client, AimDamageContext& context) noexcept {
    context = {};
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!CurrentClient(client) || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize) return false;
    for (const auto& window : off::ContractWindows) {
        unsigned char copied[64]{};
        for (std::size_t offset = 0; offset < window.size; offset += sizeof(copied)) {
            const auto count = std::min(sizeof(copied), window.size - offset);
            if (!MovementCopy(client + window.rva + offset, copied, count) || std::memcmp(copied, window.bytes + offset, count)) return false;
        }
    }
    context = {client, true};
    return true;
}

AimDamageResult AimDamageRead(const AimDamageContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon,
    const AimOcclusionResult& path) noexcept {
    AimDamageResult sample{};
    sample.reason = "damage native contract unavailable";
    if (!context.prepared || !CurrentClient(context.client)) return sample;
    sample.reason = "damage requires direct target hitgroup";
    if (!path.valid || !path.clear || !path.hitgroupKnown) return sample;
    sample.input.hitgroup = path.hitgroup;
    sample.input.distance = path.distance;
    sample.input.penetrationDamage = path.penetrationDamage;
    if (!Sample(context, source, target, volume, weapon, sample)) return sample;
    if (path.penetrationDamage >= 0 && (sample.input.baseDamage != path.bulletBaseDamage ||
        sample.input.rangeModifier != path.bulletRangeModifier || sample.input.penetrationPower != path.bulletPenetration)) {
        sample.reason = "weapon damage inputs changed during penetration"; return sample;
    }
    auto result = AimDamageCalculate(sample.input);
    result.itemServices = sample.itemServices;
    if (result.valid && !AimDamageStillCurrent(context, source, target, volume, weapon, result)) {
        result.valid = false; result.reason = "damage source changed while reading";
    }
    return result;
}

bool AimDamageStillCurrent(const AimDamageContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon,
    const AimDamageResult& snapshot) noexcept {
    if (!snapshot.valid) return false;
    AimDamageResult current{};
    current.input.hitgroup = snapshot.input.hitgroup;
    current.input.distance = snapshot.input.distance;
    current.input.penetrationDamage = snapshot.input.penetrationDamage;
    return Sample(context, source, target, volume, weapon, current) &&
        current.itemServices == snapshot.itemServices && current.input == snapshot.input;
}
#ifdef CS2_AIM_DAMAGE_CHECKS
void AimDamageTestRuntime(bool enabled) noexcept { testRuntime = enabled; }
#endif
