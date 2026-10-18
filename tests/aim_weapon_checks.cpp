#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off=aim_weapon_offsets;
namespace ent=cs2_offsets;
unsigned checks=0,failures=0;
void Check(bool condition,const char* label) { ++checks; if(!condition) { ++failures; std::printf("FAIL %s\n",label); } }
template<class T> void Put(void* p,std::size_t at,T value) { std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value)); }
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
unsigned char* memory=nullptr;
std::uintptr_t client=0;
std::array<unsigned char,0x2200> pawn{},weapon{};
std::array<unsigned char,0x1000> controller{},data{};
std::array<unsigned char,0x100> services{},punch{},system{},rules{},maxStorage{},angleStorage{};
std::array<unsigned char,0x70> pawnIdentity{};
std::array<unsigned char,512*0x70> bucket{};
constexpr std::uint32_t pawnHandle=0x10003,weaponHandle=0x18005;
AimSource source{};
AimWeaponContext context{};
struct Mock {
    float spread=.02f,inaccuracy=.03f,time=10;
    int cooldown=1,calls=0,mutation=0;
    bool arguments=true;
    AimAngles punch{1,-2,.25f};
} mock;
float Spread(void* p) { ++mock.calls; mock.arguments&=p==weapon.data();return mock.spread; }
float Inaccuracy(void* p,float* move,float* air) { ++mock.calls; mock.arguments&=p==weapon.data() && !move && !air;return mock.inaccuracy; }
int Cooldown(void* p) { ++mock.calls;mock.arguments&=p==weapon.data();return mock.cooldown; }
float* Time(float* p,int scene) { ++mock.calls;mock.arguments&=scene==0;*p=mock.time;return p; }
AimAngles* Punch(void* p,AimAngles* out,bool scale) {
    ++mock.calls;mock.arguments&=p==punch.data() && scale;*out=mock.punch;
    if(mock.mutation==1) Put(services.data(),off::ActiveWeapon,weaponHandle+1);
    if(mock.mutation==2) Put(weapon.data(),off::Clip,29);
    if(mock.mutation==3) Put(weapon.data(),off::Mode,1);
    if(mock.mutation==4) Put(weapon.data(),off::RecoilIndex,2.0f);
    if(mock.mutation==5) Put(weapon.data(),off::InReload,std::uint8_t{1});
    if(mock.mutation==6) Put(services.data(),off::NextAttack,11.0f);
    if(mock.mutation==7) Put(pawn.data(),off::PunchServices,std::uintptr_t{0});
    return out;
}
void Reset(std::size_t table=0) {
    mock={};pawn.fill(0);weapon.fill(0);controller.fill(0);data.fill(0);services.fill(0);punch.fill(0);
    system.fill(0);rules.fill(0);maxStorage.fill(0);angleStorage.fill(0);pawnIdentity.fill(0);bucket.fill(0);
    Put(memory,ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    Put(memory,ent::globals::dwLocalPlayerController,Address(controller.data()));
    Put(memory,ent::globals::dwGameEntitySystem,Address(system.data()));
    Put(memory,off::GameRules,Address(rules.data()));
    Put(memory,off::DebugMaxRadius+off::CvarValuePointer,Address(maxStorage.data()));
    Put(memory,off::DebugFixedAngle+off::CvarValuePointer,Address(angleStorage.data()));
    Put(pawn.data(),ent::CEntityInstance::m_pEntity,Address(pawnIdentity.data()));
    Put(pawnIdentity.data(),0,Address(pawn.data()));Put(pawnIdentity.data(),0x10,pawnHandle);
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle);
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,100);
    Put(pawn.data(),off::WeaponServices,Address(services.data()));Put(services.data(),0,client+off::ServicesVtable);
    Put(services.data(),off::ServicePawn,Address(pawn.data()));Put(services.data(),off::ActiveWeapon,weaponHandle);
    Put(pawn.data(),off::PunchServices,Address(punch.data()));Put(punch.data(),0,client+off::PunchVtable);
    Put(punch.data(),off::ServicePawn,Address(pawn.data()));
    Put(system.data(),ent::entity_layout::TableOffset,Address(bucket.data()));
    Put(bucket.data()+5*0x70,0,Address(weapon.data()));Put(bucket.data()+5*0x70,0x10,weaponHandle);
    Put(weapon.data(),ent::CEntityInstance::m_pEntity,Address(bucket.data()+5*0x70));
    Put(weapon.data(),0,client+off::Tables[table].rva);Put(weapon.data(),off::Data,Address(data.data()));
    Put(memory,off::Tables[table].rva+off::SpreadSlot,client+off::Spread);
    Put(memory,off::Tables[table].rva+off::InaccuracySlot,client+off::Inaccuracy);
    Put(weapon.data(),off::Clip,30);Put(data.data(),off::NumBullets,1);
    Put(data.data(),off::FullAuto,std::uint8_t{1});Put(data.data(),off::Range,8192.0f);
    source={Address(system.data()),Address(controller.data()),Address(pawn.data())};
    context={client,&Spread,&Inaccuracy,&Cooldown,&Time,&Punch};
}
void Positive() {
    for(std::size_t table=0;table<std::size(off::Tables);++table) {
        Reset(table);AimWeaponSnapshot out{};
        const auto beforeWeapon=weapon;
        const auto beforeData=data;
        const auto beforeServices=services;
        const auto beforePunch=punch;
        Check(AimWeaponRead(context,source,out) && out.ready,"each verified single-projectile weapon accepted");
        Check(out.spread==mock.spread && out.inaccuracy==mock.inaccuracy && out.range==8192 &&
            out.aimPunch.pitch==1 && out.aimPunch.yaw==-2 && out.aimPunch.roll==.25f,"native current model copied");
        Check(out.handle==weaponHandle && out.weapon==Address(weapon.data()) && out.fullAuto &&
            out.negev==off::Tables[table].negev && !std::strcmp(out.name,off::Tables[table].name),"full identity and special model copied");
        Check(mock.calls==5 && mock.arguments,"native ABI arguments and null optional outputs exact");
        Check(weapon==beforeWeapon && data==beforeData && services==beforeServices && punch==beforePunch,"feature makes no direct entity writes");
        Check(AimWeaponStillCurrent(context,source,out),"unchanged source revalidates");
    }
    Reset();Put(weapon.data(),off::Mode,1);Put(maxStorage.data(),off::CvarValue,std::uint8_t{1});
    Put(angleStorage.data(),off::CvarValue,std::uint8_t{1});AimWeaponSnapshot out{};
    Check(AimWeaponRead(context,source,out) && out.mode==1 && out.forceMaxRadius && out.fixedSpreadAngle,"alternate scoped/silencer mode and native debug distribution retained");
}
void Readiness() {
    for(int mutation=0;mutation<11;++mutation) {
        Reset();AimWeaponSnapshot out{};
        switch(mutation) {
        case 0:Put(weapon.data(),off::Clip,0);break;
        case 1:Put(weapon.data(),off::InReload,std::uint8_t{1});break;
        case 2:Put(rules.data(),off::FreezePeriod,std::uint8_t{1});break;
        case 3:Put(pawn.data(),off::Flags,std::uint32_t{32});break;
        case 4:Put(pawn.data(),off::Defusing,std::uint8_t{1});break;
        case 5:Put(pawn.data(),off::PlayerState,1);break;
        case 6:Put(pawn.data(),off::WaitForNoAttack,std::uint8_t{1});break;
        case 7:Put(data.data(),off::FullAuto,std::uint8_t{0});Put(pawn.data(),off::ShotsFired,1);break;
        case 8:Put(services.data(),off::NextAttack,10.25f);break;
        case 9:mock.cooldown=0;break;
        case 10:Put(services.data(),off::NextAttack,10.0f);mock.time=9.999f;break;
        }
        Check(AimWeaponRead(context,source,out) && !out.ready && std::strcmp(out.reason,"ready"),"native readiness blocks firing while retaining probability model");
    }
    Reset();Put(services.data(),off::NextAttack,10.0f);AimWeaponSnapshot out{};
    Check(AimWeaponRead(context,source,out) && out.ready,"equal service deadline eligible");
}
void Reject() {
    for(int mutation=0;mutation<32;++mutation) {
        Reset();AimWeaponSnapshot out{};
        switch(mutation) {
        case 0:Put(memory,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});break;
        case 1:Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle+0x8000);break;
        case 2:Put(pawnIdentity.data(),0x10,pawnHandle+0x8000);break;
        case 3:Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);break;
        case 4:Put(services.data(),off::ServicePawn,std::uintptr_t{0});break;
        case 5:Put(services.data(),off::ActiveWeapon,weaponHandle+0x8000);break;
        case 6:Put(bucket.data()+5*0x70,0x10,weaponHandle+0x8000);break;
        case 7:Put(weapon.data(),ent::CEntityInstance::m_pEntity,std::uintptr_t{0});break;
        case 8:Put(weapon.data(),0,client+0x1BF65C8);break;
        case 9:Put(weapon.data(),off::Mode,2);break;
        case 10:Put(data.data(),off::NumBullets,8);break;
        case 11:Put(data.data(),off::IsRevolver,std::uint8_t{1});break;
        case 12:Put(weapon.data(),off::BurstMode,std::uint8_t{1});break;
        case 13:Put(weapon.data(),off::BurstRemaining,1);break;
        case 14:Put(punch.data(),off::ServicePawn,std::uintptr_t{0});break;
        case 15:Put(punch.data(),0,std::uintptr_t{0});break;
        case 16:Put(data.data(),off::Range,std::numeric_limits<float>::quiet_NaN());break;
        case 17:mock.inaccuracy=std::numeric_limits<float>::quiet_NaN();break;
        case 18:mock.spread=-1;break;
        case 19:mock.punch.yaw=std::numeric_limits<float>::infinity();break;
        case 20:mock.time=-1;break;
        case 21:mock.cooldown=2;break;
        case 22:mock.mutation=1;break;
        case 23:mock.mutation=2;break;
        case 24:mock.mutation=3;break;
        case 25:mock.mutation=4;break;
        case 26:Put(memory,off::Tables[0].rva+off::SpreadSlot,std::uintptr_t{0});break;
        case 27:Put(maxStorage.data(),off::CvarValue,std::uint8_t{2});break;
        case 28:context.punch=nullptr;break;
        case 29:mock.mutation=5;break;
        case 30:mock.mutation=6;break;
        case 31:mock.mutation=7;break;
        }
        Check(!AimWeaponRead(context,source,out) && !out.ready,"invalid/stale/unsupported source or native output rejected");
    }
    Reset();AimWeaponSnapshot out{};Check(AimWeaponRead(context,source,out),"snapshot before late mutation");
    Put(weapon.data(),off::Data,std::uintptr_t{0});Check(!AimWeaponStillCurrent(context,source,out),"late weapon-data change rejected");
}
}
int main() {
    memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!memory) return 2;
    client=Address(memory);AimWeaponTestRuntime(true);Positive();Readiness();Reject();AimWeaponTestRuntime(false);
    VirtualFree(memory,0,MEM_RELEASE);std::printf("aim_weapon_checks: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
