#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = aim_weapon_offsets;
namespace ent = cs2_offsets;
#ifdef CS2_AIM_WEAPON_CHECKS
bool testRuntime = false;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&bytes)[N]) noexcept {
    unsigned char copy[N]{};
    return MovementCopy(at,copy,N) && std::memcmp(copy,bytes,N)==0;
}
bool Current(const AimWeaponContext& context) noexcept {
#ifdef CS2_AIM_WEAPON_CHECKS
    if (testRuntime) return context.client != 0;
#endif
    return context.client && context.client==reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Source(const AimWeaponContext& context, const AimSource& source, AimWeaponSnapshot& out) noexcept {
    std::uintptr_t pawn=0, controller=0, system=0, identity=0, resolved=0, bucket=0, table=0, servicePawn=0;
    std::uint32_t handle=UINT32_MAX, linked=UINT32_MAX;
    int health=0; std::uint8_t life=1;
    if (!Current(context) || !source.pawn || !source.controller || !source.system ||
        !Read(context.client+ent::globals::dwLocalPlayerPawn,pawn) || pawn!=source.pawn ||
        !Read(context.client+ent::globals::dwLocalPlayerController,controller) || controller!=source.controller ||
        !Read(context.client+ent::globals::dwGameEntitySystem,system) || system!=source.system ||
        !Read(pawn+ent::CEntityInstance::m_pEntity,identity) || !identity || !Read(identity,resolved) || resolved!=pawn ||
        !Read(identity+ent::entity_layout::HandleOffset,handle) || handle==UINT32_MAX ||
        !Read(controller+ent::CCSPlayerController::m_hPlayerPawn,linked) || linked!=handle ||
        !Read(pawn+ent::C_BaseEntity::m_iHealth,health) || health<=0 ||
        !Read(pawn+ent::C_BaseEntity::m_lifeState,life) || life!=0 ||
        !Read(pawn+off::WeaponServices,out.services) || !out.services ||
        !Read(out.services,table) || table!=context.client+off::ServicesVtable ||
        !Read(out.services+off::ServicePawn,servicePawn) || servicePawn!=pawn ||
        !Read(out.services+off::ActiveWeapon,out.handle) || out.handle==UINT32_MAX) return false;
    const auto index=out.handle & ent::entity_layout::IndexMask;
    if (!Read(system+ent::entity_layout::TableOffset+(index >> ent::entity_layout::BucketShift)*sizeof(std::uintptr_t),bucket) || !bucket) return false;
    out.identity=bucket+(index & ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride;
    if (!Read(out.identity+ent::entity_layout::HandleOffset,handle) || handle!=out.handle ||
        !Read(out.identity,out.weapon) || !out.weapon || !Read(out.weapon+ent::CEntityInstance::m_pEntity,identity) || identity!=out.identity ||
        !Read(out.weapon,table) || !Read(out.weapon+off::Data,out.data) || !out.data) return false;
    for (const auto& entry:off::Tables) {
        if (table==context.client+entry.rva) {
            std::uintptr_t spread=0, inaccuracy=0;
            if (!Read(table+off::SpreadSlot,spread) || spread!=context.client+off::Spread ||
                !Read(table+off::InaccuracySlot,inaccuracy) || inaccuracy!=context.client+off::Inaccuracy) return false;
            out.name=entry.name; out.negev=entry.negev;
            return true;
        }
    }
    return false;
}
bool DebugBool(const AimWeaponContext& context, std::ptrdiff_t wrapper, bool& value) noexcept {
    std::uintptr_t storage=0; std::uint8_t byte=0;
    if (!Read(context.client+wrapper+off::CvarValuePointer,storage) || !storage ||
        !Read(storage+off::CvarValue,byte) || byte>1) return false;
    value=byte!=0; return true;
}
bool CallModel(const AimWeaponContext& context, std::uintptr_t weapon, int scene,
    std::uintptr_t punch, AimWeaponSnapshot& out, int& cooldown) noexcept {
    if (!context.spread || !context.inaccuracy || !context.cooldown || !context.time || !context.punch) return false;
    __try {
        out.spread=context.spread(reinterpret_cast<void*>(weapon));
        out.inaccuracy=context.inaccuracy(reinterpret_cast<void*>(weapon),nullptr,nullptr);
        cooldown=context.cooldown(reinterpret_cast<void*>(weapon));
        if (context.time(&out.currentTime,scene)!=&out.currentTime ||
            context.punch(reinterpret_cast<void*>(punch),&out.aimPunch,true)!=&out.aimPunch) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool AimWeaponPrepare(std::uintptr_t client, AimWeaponContext& context) noexcept {
    context={};
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!client || !Read(client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(client+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage!=ent::ClientImageSize ||
        !Match(client+off::Spread,off::SpreadBytes) || !Match(client+off::Inaccuracy,off::InaccuracyBytes) ||
        !Match(client+off::PrimaryCooldown,off::PrimaryCooldownBytes) || !Match(client+off::CurrentTime,off::CurrentTimeBytes) ||
        !Match(client+off::AimPunch,off::AimPunchBytes)) return false;
    for (const auto& entry:off::Tables) {
        std::uintptr_t spread=0,inaccuracy=0;
        if (!Read(client+entry.rva+off::SpreadSlot,spread) || spread!=client+off::Spread ||
            !Read(client+entry.rva+off::InaccuracySlot,inaccuracy) || inaccuracy!=client+off::Inaccuracy) return false;
    }
    context={client,reinterpret_cast<AimWeaponSpreadFn>(client+off::Spread),
        reinterpret_cast<AimWeaponInaccuracyFn>(client+off::Inaccuracy),
        reinterpret_cast<AimWeaponCooldownFn>(client+off::PrimaryCooldown),
        reinterpret_cast<AimWeaponTimeFn>(client+off::CurrentTime),
        reinterpret_cast<AimWeaponPunchFn>(client+off::AimPunch)};
    return true;
}

bool AimWeaponRead(const AimWeaponContext& context, const AimSource& source, AimWeaponSnapshot& out) noexcept {
    out={};
    if (!Source(context,source,out)) { out.reason="active weapon/source unsupported or stale"; return false; }
    std::uintptr_t punch=0, table=0, owner=0, rules=0;
    std::uint8_t reload=0,burst=0,fullAuto=0,revolver=0,freeze=0,defusing=0,wait=0;
    int bullets=0,remaining=0,scene=-1,playerState=-1,shots=0;
    std::uint32_t flags=0;
    if (!Read(out.weapon+off::Mode,out.mode) || out.mode<0 || out.mode>1 ||
        !Read(out.weapon+off::Clip,out.clip) || out.clip<0 || out.clip>1000 ||
        !Read(out.weapon+off::RecoilIndex,out.recoilIndex) || !std::isfinite(out.recoilIndex) || out.recoilIndex<0 ||
        !Read(out.weapon+off::InReload,reload) || reload>1 ||
        !Read(out.weapon+off::BurstMode,burst) || burst>1 ||
        !Read(out.weapon+off::BurstRemaining,remaining) || remaining<0 ||
        !Read(out.data+off::NumBullets,bullets) || bullets!=1 ||
        !Read(out.data+off::FullAuto,fullAuto) || fullAuto>1 ||
        !Read(out.data+off::IsRevolver,revolver) || revolver!=0 ||
        !Read(out.data+off::Range,out.range) || !std::isfinite(out.range) || out.range<=0 || out.range>1000000 ||
        !Read(out.services+off::NextAttack,out.nextAttack) || !std::isfinite(out.nextAttack) ||
        !Read(out.identity+off::IdentityScene,scene) || scene<0 ||
        !Read(source.pawn+off::PunchServices,punch) || !punch || !Read(punch,table) || table!=context.client+off::PunchVtable ||
        !Read(punch+off::ServicePawn,owner) || owner!=source.pawn ||
        !Read(context.client+off::GameRules,rules) || !rules || !Read(rules+off::FreezePeriod,freeze) || freeze>1 ||
        !Read(source.pawn+off::Defusing,defusing) || defusing>1 ||
        !Read(source.pawn+off::WaitForNoAttack,wait) || wait>1 ||
        !Read(source.pawn+off::PlayerState,playerState) ||
        !Read(source.pawn+off::ShotsFired,shots) || shots<0 ||
        !Read(source.pawn+off::Flags,flags) ||
        !DebugBool(context,off::DebugMaxRadius,out.forceMaxRadius) ||
        !DebugBool(context,off::DebugFixedAngle,out.fixedSpreadAngle)) {
        out.reason="weapon data/state unavailable or unsupported"; return false;
    }
    out.fullAuto=fullAuto!=0;
    out.reloading=reload!=0;
    out.punchServices=punch;
    if (burst || remaining) { out.reason="burst-fire model unsupported"; return false; }
    int cooldown=0;
    if (!CallModel(context,out.weapon,scene,punch,out,cooldown) || cooldown<0 || cooldown>1 ||
        !std::isfinite(out.spread) || out.spread<0 || out.spread>1 ||
        !std::isfinite(out.inaccuracy) || out.inaccuracy<0 || out.inaccuracy>1 ||
        !std::isfinite(out.currentTime) || out.currentTime<0 ||
        !std::isfinite(out.aimPunch.pitch) || !std::isfinite(out.aimPunch.yaw) || !std::isfinite(out.aimPunch.roll) ||
        !AimWeaponStillCurrent(context,source,out)) {
        out.reason="native weapon model unavailable or source changed"; return false;
    }
    out.reason=out.clip==0 ? "empty clip" : reload ? "reloading" : freeze || (flags&32) ? "frozen" :
        defusing ? "defusing" : playerState!=0 ? "player state blocks firing" : wait ? "awaiting attack release" :
        !out.fullAuto && shots ? "semi-auto awaiting release" : out.nextAttack>out.currentTime ? "weapon-service cooldown" :
        !cooldown ? "primary cooldown" : "ready";
    out.ready=std::strcmp(out.reason,"ready")==0;
    return true;
}

bool AimWeaponReadSpread(const AimWeaponContext& context, const AimSource& source,
    AimWeaponSnapshot& out) noexcept {
    out = {};
    AimWeaponSnapshot before{}, after{};
    if (!context.spread || !context.inaccuracy || !Source(context, source, before) ||
        !Read(before.weapon + off::Mode, before.mode) || before.mode < 0 || before.mode > 1) return false;
    // These native getters read the current accuracy model without updating it.
    // Keep them on the existing command/prediction owner, never in Present.
    __try {
        before.spread = context.spread(reinterpret_cast<void*>(before.weapon));
        before.inaccuracy = context.inaccuracy(reinterpret_cast<void*>(before.weapon), nullptr, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(before.spread) || before.spread < 0 || before.spread > 1 ||
        !std::isfinite(before.inaccuracy) || before.inaccuracy < 0 || before.inaccuracy > 1 ||
        !Source(context, source, after) || !Read(after.weapon + off::Mode, after.mode) ||
        before.weapon != after.weapon || before.identity != after.identity || before.handle != after.handle ||
        before.services != after.services || before.data != after.data || before.mode != after.mode) return false;
    out = before;
    return true;
}

bool AimWeaponStillCurrent(const AimWeaponContext& context, const AimSource& source,
    const AimWeaponSnapshot& snapshot) noexcept {
    AimWeaponSnapshot current{}; int mode=-1,clip=-1,remaining=-1,bullets=0; float recoil=-1,nextAttack=0,range=0;
    std::uintptr_t punch=0,table=0,owner=0; std::uint8_t reload=0,burst=0,fullAuto=0,revolver=0;
    return Source(context,source,current) && current.weapon==snapshot.weapon && current.identity==snapshot.identity &&
        current.handle==snapshot.handle && current.services==snapshot.services && current.data==snapshot.data &&
        Read(current.weapon+off::Mode,mode) && mode==snapshot.mode &&
        Read(current.weapon+off::Clip,clip) && clip==snapshot.clip &&
        Read(current.weapon+off::RecoilIndex,recoil) && recoil==snapshot.recoilIndex &&
        Read(current.weapon+off::InReload,reload) && reload==static_cast<std::uint8_t>(snapshot.reloading) &&
        Read(current.weapon+off::BurstMode,burst) && burst==0 && Read(current.weapon+off::BurstRemaining,remaining) && remaining==0 &&
        Read(current.data+off::NumBullets,bullets) && bullets==1 && Read(current.data+off::IsRevolver,revolver) && revolver==0 &&
        Read(current.data+off::FullAuto,fullAuto) && fullAuto==static_cast<std::uint8_t>(snapshot.fullAuto) &&
        Read(current.data+off::Range,range) && range==snapshot.range &&
        Read(current.services+off::NextAttack,nextAttack) && nextAttack==snapshot.nextAttack &&
        Read(source.pawn+off::PunchServices,punch) && punch==snapshot.punchServices &&
        Read(punch,table) && table==context.client+off::PunchVtable && Read(punch+off::ServicePawn,owner) && owner==source.pawn;
}
#ifdef CS2_AIM_WEAPON_CHECKS
void AimWeaponTestRuntime(bool enabled) noexcept { testRuntime=enabled; }
#endif
