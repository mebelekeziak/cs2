#include "aim_damage.h"
#include "aim_damage_offsets.h"
#include "aim_occlusion.h"
#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "silent_aim_target.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace {
namespace off = aim_damage_offsets;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
bool Near(float value, float expected) { return std::fabs(value - expected) < .001f; }
template<class T> void Put(void* buffer, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(buffer) + offset, &value, sizeof(value));
}
std::uintptr_t Address(const void* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }
AimDamageInput Bullet() {
    AimDamageInput input{};
    input.baseDamage = 100; input.hitgroup = 2;
    input.headshotMultiplier = 4; input.armorRatio = 1; input.rangeModifier = .8f;
    input.range = 5000; input.headScale = input.bodyScale = 1;
    return input;
}
void MathChecks() {
    const auto bullet = Bullet();
    Check(!AimDamageCalculate({}).valid, "default inputs cannot fabricate damage");
    for (const auto [distance, expected] : {std::pair{0.0f, 100.0f}, std::pair{500.0f, 80.0f}, std::pair{1000.0f, 64.0f}}) {
        auto input = bullet; input.distance = distance;
        const auto result = AimDamageCalculate(input);
        Check(result.valid && Near(result.damage, expected) && result.input == input,
            "range falloff follows complete 500-unit powers and retains copied inputs");
    }
    for (int hitgroup = 1; hitgroup <= 8; ++hitgroup) {
        auto input = bullet; input.hitgroup = hitgroup;
        const float expected = hitgroup == 1 ? 400.0f : hitgroup == 3 ? 125.0f :
            hitgroup == 6 || hitgroup == 7 ? 75.0f : 100.0f;
        const auto result = AimDamageCalculate(input);
        Check(result.valid && Near(result.damage, expected) && result.healthDamage == static_cast<int>(expected),
            "all native hitgroups use their verified head stomach leg or body scale");
        input.armor = 100; input.helmet = true;
        const auto armored = AimDamageCalculate(input);
        Check(armored.valid && Near(armored.damage, hitgroup == 6 || hitgroup == 7 ? expected : expected * .5f),
            "armor covers helmet and torso groups while legs bypass armor");
    }
    auto input = bullet;
    input.headScale = .5f; input.bodyScale = .25f;
    Check(Near(AimDamageCalculate(input).damage, 25), "body scale applies independently of head scale");
    input.hitgroup = 1;
    Check(Near(AimDamageCalculate(input).damage, 200), "head scale applies independently of body scale");
    input = bullet; input.hitgroup = 1; input.armor = 100;
    Check(Near(AimDamageCalculate(input).damage, 400), "head without helmet bypasses present armor");
    input.helmet = true;
    Check(Near(AimDamageCalculate(input).damage, 200), "helmet applies weapon armor penetration");
    input.armor = 10;
    Check(Near(AimDamageCalculate(input).damage, 380), "exhausted armor cannot absorb more than twice its remaining pool");
    input = bullet; input.armor = 25;
    Check(Near(AimDamageCalculate(input).damage, 50), "exact armor exhaustion boundary retains penetrated damage");
    input.armor = 24;
    Check(Near(AimDamageCalculate(input).damage, 52), "one armor point below exhaustion increases health damage by two");
    input.armorRatio = 2;
    Check(Near(AimDamageCalculate(input).damage, 100), "full armor penetration preserves health damage");
    input.armorRatio = 0; input.armor = 100;
    Check(AimDamageCalculate(input).valid && AimDamageCalculate(input).healthDamage == 0,
        "zero penetration with sufficient armor is valid zero damage");
    input = bullet; input.headshotOnly = true;
    for (int hitgroup = 2; hitgroup <= 8; ++hitgroup) {
        input.hitgroup = hitgroup;
        Check(AimDamageCalculate(input).valid && AimDamageCalculate(input).healthDamage == 0,
            "headshot-only mode predicts no body health damage");
    }
    input.hitgroup = 1;
    Check(AimDamageCalculate(input).healthDamage == 400, "headshot-only mode retains head damage");
    input = bullet; input.baseDamage = 35; input.rangeModifier = .98f; input.distance = 500;
    const auto fractional = AimDamageCalculate(input);
    Check(fractional.valid && Near(fractional.damage, 34.3f) && fractional.healthDamage == 34 &&
        fractional.healthDamage >= 34 && fractional.healthDamage < 35,
        "fractional predicted damage floors at the whole-HP autofire threshold");
    input = bullet; input.bodyScale = std::nextafter(.5f, 0.0f);
    Check(AimDamageCalculate(input).healthDamage == 49, "rounding never promotes just-below-threshold damage");
    input.bodyScale = .5f;
    Check(AimDamageCalculate(input).healthDamage == 50, "exact whole-HP threshold remains eligible");
    input.bodyScale = 0;
    Check(AimDamageCalculate(input).valid && AimDamageCalculate(input).healthDamage == 0,
        "zero team damage scale is valid rather than unavailable");
    for (int AimDamageInput::* member : {&AimDamageInput::baseDamage, &AimDamageInput::armor, &AimDamageInput::hitgroup}) {
        for (int bad : {-1, 100001}) {
            input = bullet; input.*member = bad;
            Check(!AimDamageCalculate(input).valid, "out-of-contract integer damage inputs reject");
        }
    }
    input = bullet; input.baseDamage = 0;
    Check(!AimDamageCalculate(input).valid, "zero weapon base damage is unsupported");
    for (int group : {0, 9}) {
        input = bullet; input.hitgroup = group;
        Check(!AimDamageCalculate(input).valid, "unknown trace hitgroup cannot use a guessed body multiplier");
    }
    for (float AimDamageInput::* member : {&AimDamageInput::headshotMultiplier, &AimDamageInput::armorRatio,
        &AimDamageInput::rangeModifier, &AimDamageInput::distance, &AimDamageInput::range,
        &AimDamageInput::headScale, &AimDamageInput::bodyScale}) {
        for (float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            input = bullet; input.*member = bad;
            Check(!AimDamageCalculate(input).valid, "negative and nonfinite scalar inputs reject");
        }
    }
    input = bullet; input.distance = input.range;
    Check(AimDamageCalculate(input).valid, "range endpoint is included");
    input.distance = std::nextafter(input.range, std::numeric_limits<float>::infinity());
    Check(!AimDamageCalculate(input).valid, "past weapon range rejects without extrapolation");
    for (float modifier : {0.0f, 1.01f}) {
        input = bullet; input.rangeModifier = modifier;
        Check(!AimDamageCalculate(input).valid, "invalid range modifiers reject");
    }
    input = bullet; input.armorRatio = 2.01f;
    Check(!AimDamageCalculate(input).valid, "armor ratio above native supported limit rejects");
    input = bullet; input.headshotMultiplier = 101;
    Check(!AimDamageCalculate(input).valid, "unbounded headshot multiplier rejects");
    input = bullet; input.headScale = 101;
    Check(!AimDamageCalculate(input).valid, "unbounded head team scale rejects");
    input = bullet; input.bodyScale = 101;
    Check(!AimDamageCalculate(input).valid, "unbounded body team scale rejects");
}

unsigned char* memory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 0x2000> local{};
std::array<unsigned char, 16> controller{}, system{};
std::array<unsigned char, 0x4000> enemy{};
std::array<unsigned char, 0x2000> weaponMemory{};
std::array<unsigned char, 0x1000> data{};
std::array<unsigned char, 0x100> itemServices{}, otherServices{};
std::array<std::array<unsigned char, 0x100>, 5> cvars{};
AimSource source{};
AimTarget target{};
AimTargetVolume volume{};
AimWeaponSnapshot weapon{};
AimOcclusionResult path{};
AimDamageContext context{};
bool targetValid = true, volumeValid = true, mutateOnRecheck = false;
unsigned targetValidCalls = 0;
constexpr std::array wrappers{off::DamageScaleCtHead, off::DamageScaleCtBody,
    off::DamageScaleTHead, off::DamageScaleTBody, off::HeadshotOnly};
void Reset() {
    local.fill(0); enemy.fill(0); weaponMemory.fill(0); data.fill(0); itemServices.fill(0); otherServices.fill(0);
    for (auto& cvar : cvars) cvar.fill(0);
    targetValid = volumeValid = true; mutateOnRecheck = false; targetValidCalls = 0;
    source = {Address(system.data()), Address(controller.data()), Address(local.data())};
    target = {}; target.local.pawn = source.pawn; target.enemy.pawn = Address(enemy.data()); target.enemy.team = 3;
    volume = {}; volume.pawn = target.enemy.pawn; volume.valid = true;
    weapon = {}; weapon.weapon = Address(weaponMemory.data()); weapon.data = Address(data.data()); weapon.range = 5000;
    weapon.currentTime = 100;
    path = {}; path.valid = path.clear = path.hitgroupKnown = true; path.hitgroup = 2; path.distance = 500;
    Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, source.pawn);
    Put(memory, cs2_offsets::globals::dwLocalPlayerController, source.controller);
    Put(memory, cs2_offsets::globals::dwEntityList, source.system);
    Put(weaponMemory.data(), aim_weapon_offsets::Data, weapon.data);
    Put(data.data(), off::Damage, 100);
    Put(data.data(), off::HeadshotMultiplier, 4.0f);
    Put(data.data(), off::ArmorRatio, 1.0f);
    Put(data.data(), off::RangeModifier, .8f);
    Put(data.data(), aim_weapon_offsets::Range, weapon.range);
    Put(enemy.data(), off::ArmorValue, 0);
    Put(enemy.data(), off::ItemServices, Address(itemServices.data()));
    Put(itemServices.data(), 0, client + off::ItemServicesVtable);
    Put(itemServices.data(), off::ServicePawn, target.enemy.pawn);
    Put(enemy.data(), cs2_offsets::C_BaseEntity::m_iTeamNum, target.enemy.team);
    for (unsigned i = 0; i < wrappers.size(); ++i) {
        Put(memory, wrappers[i] + off::CvarValuePointer, Address(cvars[i].data()));
        if (i < 4) Put(cvars[i].data(), off::CvarValue, 1.0f);
    }
}
AimDamageResult Predict() { return AimDamageRead(context, source, target, volume, weapon, path); }
bool Current(const AimDamageResult& saved) { return AimDamageStillCurrent(context, source, target, volume, weapon, saved); }
void RuntimeChecks() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, cs2_offsets::ClientImageSize,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(memory != nullptr, "fixture allocates isolated synthetic client image");
    if (!memory) return;
    client = Address(memory);
    AimDamageTestRuntime(true);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE; nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp = cs2_offsets::ClientTimestamp;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = cs2_offsets::ClientImageSize;
    Put(memory, 0, dos); Put(memory, 0x100, nt);
    for (const auto& window : off::ContractWindows) std::memcpy(memory + window.rva, window.bytes, window.size);
    Check(AimDamagePrepare(client, context) && context.prepared && context.client == client,
        "matching PE identity and native contract windows prepare");
    for (const auto& window : off::ContractWindows) {
        for (std::size_t at : {std::size_t{0}, window.size / 2, window.size - 1}) {
            memory[window.rva + at] ^= 1;
            AimDamageContext rejected{client, true};
            Check(!AimDamagePrepare(client, rejected) && !rejected.prepared && rejected.client == 0,
                "entry middle and tail byte mutations clear prepared context");
            memory[window.rva + at] ^= 1;
        }
    }
    AimDamageContext rejected{client, true};
    auto badNt = nt; badNt.FileHeader.TimeDateStamp ^= 1; Put(memory, 0x100, badNt);
    Check(!AimDamagePrepare(client, rejected) && !rejected.prepared, "changed client timestamp rejects");
    badNt = nt; badNt.OptionalHeader.SizeOfImage ^= 1; Put(memory, 0x100, badNt);
    Check(!AimDamagePrepare(client, rejected), "changed client image size rejects");
    badNt = nt; badNt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386; Put(memory, 0x100, badNt);
    Check(!AimDamagePrepare(client, rejected), "non-AMD64 client rejects");
    badNt = nt; badNt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC; Put(memory, 0x100, badNt);
    Check(!AimDamagePrepare(client, rejected), "non-PE32+ client rejects");
    badNt = nt; badNt.Signature ^= 1; Put(memory, 0x100, badNt);
    Check(!AimDamagePrepare(client, rejected), "invalid NT signature rejects");
    Put(memory, 0x100, nt);
    auto badDos = dos; badDos.e_magic ^= 1; Put(memory, 0, badDos);
    Check(!AimDamagePrepare(client, rejected), "invalid DOS signature rejects");
    for (LONG invalid : {LONG{0}, LONG{-1}, LONG{0x100001}}) {
        badDos = dos; badDos.e_lfanew = invalid; Put(memory, 0, badDos);
        Check(!AimDamagePrepare(client, rejected), "invalid NT header displacement rejects");
    }
    Put(memory, 0, dos);
    Check(!AimDamagePrepare(0, rejected) && !rejected.prepared, "missing module cannot prepare");
    Reset();
    auto saved = Predict();
    Check(saved.valid && Near(saved.damage, 80) && saved.healthDamage == 80 && Current(saved) &&
        saved.itemServices == Address(itemServices.data()), "runtime copies direct-hit damage and validates unchanged source");
    Check(!AimDamageRead({}, source, target, volume, weapon, path).valid, "unprepared runtime rejects prediction");
    Check(!AimDamageStillCurrent({}, source, target, volume, weapon, saved), "unprepared runtime rejects retained snapshot");
    path.valid = false; Check(!Predict().valid, "unavailable path cannot predict damage");
    path.valid = true; path.clear = false; Check(!Predict().valid, "covered path cannot predict direct damage");
    path.clear = true; path.hitgroupKnown = false; Check(!Predict().valid, "clear segment without native hitgroup stays unknown");
    Reset(); targetValid = false; Check(!Predict().valid && !Current(saved), "invalid target rejects prediction and retained state");
    Reset(); volumeValid = false; Check(!Predict().valid && !Current(saved), "invalid authored geometry rejects prediction and retained state");
    for (auto global : {cs2_offsets::globals::dwLocalPlayerPawn, cs2_offsets::globals::dwLocalPlayerController,
        cs2_offsets::globals::dwEntityList}) {
        Reset(); Put(memory, global, std::uintptr_t{0});
        Check(!Predict().valid && !Current(saved), "changed local source rejects prediction and stale snapshot");
    }
    Reset(); weapon.weapon = 0; Check(!Predict().valid, "missing weapon rejects");
    Reset(); weapon.data = 0; Check(!Predict().valid, "missing weapon data rejects");
    Reset(); Put(weaponMemory.data(), aim_weapon_offsets::Data, Address(otherServices.data()));
    Check(!Predict().valid && !Current(saved), "changed weapon data ownership rejects");
    Reset(); Put(data.data(), aim_weapon_offsets::Range, 4999.0f);
    Check(!Predict().valid && !Current(saved), "weapon range changes invalidate the sampled weapon");
    Reset(); Put(enemy.data(), off::ItemServices, std::uintptr_t{0}); Check(!Predict().valid, "missing item service rejects");
    Reset(); Put(itemServices.data(), 0, client + off::ItemServicesVtable + 8); Check(!Predict().valid, "unknown item service table rejects");
    Reset(); Put(itemServices.data(), off::ServicePawn, source.pawn); Check(!Predict().valid, "foreign item service owner rejects");
    Reset(); Put(itemServices.data(), off::HasHelmet, std::uint8_t{2}); Check(!Predict().valid, "malformed helmet boolean rejects");
    for (std::uint8_t immune : {std::uint8_t{1}, std::uint8_t{2}}) {
        Reset(); Put(enemy.data(), off::GunGameImmunity, immune);
        Check(!Predict().valid, "immune or malformed immunity state rejects");
    }
    Reset(); Put(enemy.data(), cs2_offsets::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
    Check(!Predict().valid, "team change invalidates target snapshot");
    for (std::uint8_t team : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{4}}) {
        Reset(); target.enemy.team = team; Put(enemy.data(), cs2_offsets::C_BaseEntity::m_iTeamNum, team);
        Check(!Predict().valid, "unsupported teams never select guessed damage scales");
    }
    for (int armor : {-1, 10001}) {
        Reset(); Put(enemy.data(), off::ArmorValue, armor); Check(!Predict().valid, "invalid target armor rejects");
    }
    for (unsigned index = 0; index < wrappers.size(); ++index) {
        Reset(); Put(memory, wrappers[index] + off::CvarValuePointer, std::uintptr_t{0});
        if (index == 0 || index == 1 || index == 4) Check(!Predict().valid, "required missing CT or headshot-only cvar rejects");
        else Check(Predict().valid, "unused opposing-team cvar does not block valid CT prediction");
    }
    for (unsigned index : {0u, 1u}) {
        Reset(); Put(cvars[index].data(), off::CvarValue, std::numeric_limits<float>::quiet_NaN());
        Check(!Predict().valid, "nonfinite active team damage cvar rejects");
    }
    Reset(); Put(cvars[4].data(), off::CvarValue, std::uint8_t{2}); Check(!Predict().valid, "malformed headshot-only cvar rejects");
    for (auto* pawnMemory : {local.data(), enemy.data()}) {
        for (float expiry : {101.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            Reset(); Put(pawnMemory, off::HealthShotBoostExpiration, expiry);
            Check(!Predict().valid, "active or unavailable healthshot modifier rejects either participant");
        }
        Reset(); Put(pawnMemory, off::HealthShotBoostExpiration, 100.0f);
        Check(Predict().valid, "healthshot expiration equal to current predicted time is inactive");
        Reset(); saved = Predict(); Put(pawnMemory, off::HealthShotBoostExpiration, 101.0f);
        Check(!Current(saved), "new healthshot modifier invalidates retained damage");
    }
    for (float time : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        Reset(); weapon.currentTime = time;
        Check(!Predict().valid, "unknown command time cannot determine healthshot modifier status");
    }
    Reset(); Put(cvars[4].data(), off::CvarValue, std::uint8_t{1});
    Check(Predict().valid && Predict().healthDamage == 0, "runtime body damage is zero under headshot-only mode");
    path.hitgroup = 1;
    Check(Predict().valid && Predict().healthDamage == 320, "runtime headshot-only head damage remains available");
    for (std::uint8_t team : {std::uint8_t{2}, std::uint8_t{3}}) {
        Reset(); target.enemy.team = team; Put(enemy.data(), cs2_offsets::C_BaseEntity::m_iTeamNum, team);
        Put(cvars[0].data(), off::CvarValue, .5f); Put(cvars[1].data(), off::CvarValue, .25f);
        Put(cvars[2].data(), off::CvarValue, 2.0f); Put(cvars[3].data(), off::CvarValue, 1.5f);
        Check(Predict().valid && Near(Predict().damage, team == 3 ? 20.0f : 120.0f),
            "runtime body scale uses victim team without cross-team contamination");
        path.hitgroup = 1;
        Check(Predict().valid && Near(Predict().damage, team == 3 ? 160.0f : 640.0f),
            "runtime head scale uses victim team independently of body scale");
    }
    Reset(); Put(enemy.data(), off::ArmorValue, 100);
    Check(Predict().healthDamage == 40, "runtime armor reduces direct body damage");
    path.hitgroup = 1;
    Check(Predict().healthDamage == 320, "runtime absent helmet preserves head damage");
    Put(itemServices.data(), off::HasHelmet, std::uint8_t{1});
    Check(Predict().healthDamage == 160, "runtime helmet protects head");
    Reset(); saved = Predict(); Put(enemy.data(), off::ArmorValue, 1);
    Check(!Current(saved), "armor change invalidates retained damage");
    Reset(); saved = Predict(); Put(itemServices.data(), off::HasHelmet, std::uint8_t{1});
    Check(!Current(saved), "helmet change invalidates retained damage even on saved body shot");
    Reset(); saved = Predict(); Put(cvars[0].data(), off::CvarValue, 2.0f);
    Check(!Current(saved), "head scale change invalidates retained prediction");
    Reset(); saved = Predict(); Put(cvars[1].data(), off::CvarValue, .5f);
    Check(!Current(saved), "body scale change invalidates retained prediction");
    Reset(); saved = Predict(); Put(cvars[4].data(), off::CvarValue, std::uint8_t{1});
    Check(!Current(saved), "headshot-only change invalidates retained prediction");
    Reset(); saved = Predict(); otherServices = itemServices;
    Put(enemy.data(), off::ItemServices, Address(otherServices.data()));
    Check(Predict().valid && !Current(saved), "replacement service with equal values invalidates previous service identity");
    for (auto offset : {off::HeadshotMultiplier, off::ArmorRatio, off::RangeModifier}) {
        Reset(); saved = Predict(); Put(data.data(), offset, .25f);
        Check(!Current(saved), "changed weapon damage scalar invalidates retained prediction");
    }
    Reset(); saved = Predict(); Put(data.data(), off::Damage, 99);
    Check(!Current(saved), "changed base damage invalidates retained prediction");
    Reset(); mutateOnRecheck = true;
    Check(!Predict().valid && targetValidCalls == 2, "source changing between read and revalidation cannot publish stale prediction");
    Reset(); saved = Predict(); saved.valid = false;
    Check(!Current(saved), "invalid snapshots cannot become current through matching inputs");
    AimDamageTestRuntime(false);
    Check(!Predict().valid, "production module ownership rejects synthetic client without test override");
    AimDamageTestRuntime(true);
    VirtualFree(memory, 0, MEM_RELEASE); memory = nullptr;
}
}

bool SilentAimTargetValid(const AimSource&, const AimTarget&) noexcept {
    ++targetValidCalls;
    if (mutateOnRecheck && targetValidCalls == 2) Put(enemy.data(), off::ArmorValue, 1);
    return targetValid;
}
bool SilentAimTargetVolumeValid(const AimSource&, const AimTarget&, const AimTargetVolume&) noexcept { return volumeValid; }

int main() {
    MathChecks();
    RuntimeChecks();
    std::printf("aim damage checks=%u failures=%u; synthetic client memory only\n", checks, failures);
    return failures ? 1 : 0;
}
