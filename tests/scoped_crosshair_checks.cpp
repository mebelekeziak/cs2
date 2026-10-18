#include "scoped_crosshair.h"
#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "zoom_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace ent=cs2_offsets;
namespace off=aim_weapon_offsets;
namespace zoom=zoom_offsets;
unsigned checks=0, failures=0;
void Check(bool condition,const char* label) { ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n",label); } }
template<class T> void Put(void* pointer,std::size_t offset,T value) {
    std::memcpy(static_cast<unsigned char*>(pointer)+offset,&value,sizeof(value));
}
std::uintptr_t Address(const void* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }
unsigned char* memory=nullptr;
std::uintptr_t client=0;
std::array<unsigned char,0x2200> pawn{}, weapon{};
std::array<unsigned char,0x1000> controller{}, data{}, otherData{};
std::array<unsigned char,0x100> services{}, entitySystem{};
std::array<unsigned char,512*0x70> bucket{};
constexpr std::uint32_t pawnHandle=0x10003,controllerHandle=0x10004,weaponHandle=0x18005;
ULONGLONG clockValue=1000;
ULONGLONG Clock() { return clockValue; }
bool thirdPerson=true, nativeSuccess=true;
unsigned nativeCalls=0, mutation=0;
float spread=.01f, inaccuracy=.02f;
HANDLE nativeEntered=nullptr, nativeRelease=nullptr;
void Identity(unsigned index,std::uint32_t handle,void* entity) {
    auto* identity=bucket.data()+index*ent::entity_layout::IdentityStride;
    Put(identity,0,Address(entity));
    Put(identity,ent::entity_layout::HandleOffset,handle);
    Put(entity,ent::CEntityInstance::m_pEntity,Address(identity));
}
void Reset(std::size_t table=0,int level=1) {
    pawn.fill(0);weapon.fill(0);controller.fill(0);services.fill(0);entitySystem.fill(0);bucket.fill(0);
    thirdPerson=nativeSuccess=true;nativeCalls=mutation=0;clockValue=1000;spread=.01f;inaccuracy=.02f;
    Put(memory,ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    Put(memory,ent::globals::dwLocalPlayerController,Address(controller.data()));
    Put(memory,ent::globals::dwGameEntitySystem,Address(entitySystem.data()));
    Put(entitySystem.data(),ent::entity_layout::TableOffset,Address(bucket.data()));
    Identity(3,pawnHandle,pawn.data());Identity(4,controllerHandle,controller.data());Identity(5,weaponHandle,weapon.data());
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle);
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,100);
    Put(pawn.data(),zoom::Scoped,std::uint8_t{1});
    Put(pawn.data(),off::WeaponServices,Address(services.data()));
    Put(services.data(),0,client+off::ServicesVtable);
    Put(services.data(),off::ServicePawn,Address(pawn.data()));
    Put(services.data(),off::ActiveWeapon,weaponHandle);
    Put(weapon.data(),0,client+zoom::ScopedWeaponVtables[table]);
    Put(weapon.data(),off::Data,Address(data.data()));
    Put(weapon.data(),off::Mode,1);
    Put(weapon.data(),zoom::ZoomLevel,level);
    ScopedCrosshairTestConfigure({client},&Clock);
}
void Positive() {
    for (std::size_t table=0;table<std::size(zoom::ScopedWeaponVtables);++table) {
        for (int level=1;level<=2;++level) {
            Reset(table,level);
            Check(!ScopedCrosshairCaptureForDraw().visible,"no crosshair before command capture");
            const auto originalPawn=pawn;const auto originalWeapon=weapon;const auto originalServices=services;
            ScopedCrosshairCaptureCommand();
            auto input=ScopedCrosshairCaptureForDraw();
            Check(input.visible && input.spread==spread && input.inaccuracy==inaccuracy && input.sourceKey,
                "each scoped class/level publishes copied native accuracy");
            Check(nativeCalls==1,"render capture makes no native accuracy calls");
            Check(pawn==originalPawn && weapon==originalWeapon && services==originalServices,"feature leaves entity bytes unchanged");
            clockValue=1250;
            Check(ScopedCrosshairCaptureForDraw().visible,"publication valid at freshness boundary");
            clockValue=1251;
            Check(!ScopedCrosshairCaptureForDraw().visible,"expired publication hidden");
        }
    }
}
void Change(unsigned which) {
    switch (which) {
    case 0:thirdPerson=false;break;
    case 1:Put(pawn.data(),zoom::Scoped,std::uint8_t{0});break;
    case 2:Put(weapon.data(),zoom::ZoomLevel,0);break;
    case 3:Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);break;
    case 4:Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1});break;
    case 5:Put(services.data(),off::ActiveWeapon,weaponHandle+1);break;
    case 6:Put(bucket.data()+5*0x70,ent::entity_layout::HandleOffset,weaponHandle+0x8000);break;
    case 7:Put(weapon.data(),off::Mode,0);break;
    case 8:Put(weapon.data(),off::Data,Address(otherData.data()));break;
    case 9:Put(bucket.data()+3*0x70,ent::entity_layout::HandleOffset,pawnHandle+0x8000);break;
    case 10:Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle+0x8000);break;
    case 11:Put(memory,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});break;
    case 12:Put(weapon.data(),0,client+0x100);break;
    case 13:Put(services.data(),off::ServicePawn,std::uintptr_t{0});break;
    case 14:Put(bucket.data()+4*0x70,ent::entity_layout::HandleOffset,controllerHandle+0x8000);break;
    case 15:Put(weapon.data(),ent::CEntityInstance::m_pEntity,std::uintptr_t{0});break;
    case 16:Put(services.data(),0,client+0x100);break;
    case 17:Put(pawn.data(),zoom::Scoped,std::uint8_t{2});break;
    case 18:Put(weapon.data(),zoom::ZoomLevel,3);break;
    case 19:Put(weapon.data(),off::Mode,2);break;
    case 20:Put(weapon.data(),off::Data,std::uintptr_t{0});break;
    case 21:Put(weapon.data(),zoom::ZoomLevel,2);break;
    }
}
void Reject() {
    for (unsigned which=0;which<22;++which) {
        Reset();ScopedCrosshairCaptureCommand();
        Check(ScopedCrosshairCaptureForDraw().visible,"fixture valid before state transition");
        Change(which);
        Check(!ScopedCrosshairCaptureForDraw().visible,"render immediately rejects changed source/camera/scope");
        // A new valid mode/data/controller identity or scope stage can publish
        // again once sampled on the command owner; its old sample stays hidden.
        if (which!=7 && which!=8 && which!=14 && which!=21) {
            ScopedCrosshairCaptureCommand();
            Check(!ScopedCrosshairCaptureForDraw().visible,"invalid command capture clears publication");
        }
    }
    Reset();ScopedCrosshairCaptureCommand();nativeSuccess=false;ScopedCrosshairCaptureCommand();
    Check(!ScopedCrosshairCaptureForDraw().visible,"failed native read clears previous publication");
    for (int which=0;which<4;++which) {
        Reset();
        if (which==0) spread=std::numeric_limits<float>::quiet_NaN();
        if (which==1) inaccuracy=-.1f;
        if (which==2) spread=1.01f;
        if (which==3) inaccuracy=std::numeric_limits<float>::infinity();
        ScopedCrosshairCaptureCommand();
        Check(!ScopedCrosshairCaptureForDraw().visible,"nonfinite/out-of-range accuracy rejected");
    }
    for (unsigned which=1;which<=3;++which) {
        Reset();mutation=which;ScopedCrosshairCaptureCommand();
        Check(!ScopedCrosshairCaptureForDraw().visible,"source/camera mutation during native read rejected");
    }
    Reset();ScopedCrosshairCaptureCommand();clockValue=999;
    Check(!ScopedCrosshairCaptureForDraw().visible,"clock reversal rejected");
    Reset();ScopedCrosshairCaptureCommand();ScopedCrosshairStop();ScopedCrosshairCaptureCommand();
    Check(!ScopedCrosshairCaptureForDraw().visible && nativeCalls==1,"stop hides and disarms publication");
    Reset();ScopedCrosshairCaptureCommand();const auto oldKey=ScopedCrosshairCaptureForDraw().sourceKey;
    Identity(3,pawnHandle+0x8000,pawn.data());Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle+0x8000);
    Check(!ScopedCrosshairCaptureForDraw().visible,"respawn serial invalidates cached values");
    ScopedCrosshairCaptureCommand();
    Check(ScopedCrosshairCaptureForDraw().visible && ScopedCrosshairCaptureForDraw().sourceKey!=oldKey,"fresh respawn changes smoothing source key");
}
DWORD WINAPI CaptureThread(void*) { ScopedCrosshairCaptureCommand();return 0; }
DWORD WINAPI StopThread(void*) { ScopedCrosshairStop();return 0; }
void StopDrains() {
    Reset();nativeEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);nativeRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE capture=CreateThread(nullptr,0,&CaptureThread,nullptr,0,nullptr);
    Check(WaitForSingleObject(nativeEntered,2000)==WAIT_OBJECT_0,"native command capture entered");
    HANDLE stop=CreateThread(nullptr,0,&StopThread,nullptr,0,nullptr);
    Check(WaitForSingleObject(stop,30)==WAIT_TIMEOUT,"cooperative stop waits for native read");
    SetEvent(nativeRelease);
    Check(WaitForSingleObject(capture,2000)==WAIT_OBJECT_0 && WaitForSingleObject(stop,2000)==WAIT_OBJECT_0,"capture and cooperative stop drain");
    Check(!ScopedCrosshairCaptureForDraw().visible,"drained command cannot republish after stop");
    CloseHandle(capture);CloseHandle(stop);CloseHandle(nativeEntered);CloseHandle(nativeRelease);
    nativeEntered=nativeRelease=nullptr;
}
}

bool AimWeaponPrepare(std::uintptr_t,AimWeaponContext&) noexcept { return false; }
bool AimWeaponReadSpread(const AimWeaponContext&,const AimSource& source,AimWeaponSnapshot& out) noexcept {
    ++nativeCalls;
    Check(source.pawn==Address(pawn.data()) && source.controller==Address(controller.data()) && source.system==Address(entitySystem.data()),"native reader receives current command source");
    if (nativeEntered) { SetEvent(nativeEntered);WaitForSingleObject(nativeRelease,2000); }
    out={};out.weapon=Address(weapon.data());out.identity=Address(bucket.data()+5*0x70);
    out.services=Address(services.data());out.data=Address(data.data());out.handle=weaponHandle;out.mode=1;
    out.spread=spread;out.inaccuracy=inaccuracy;
    if (mutation==1) Put(weapon.data(),off::Mode,0);
    if (mutation==2) Put(pawn.data(),zoom::Scoped,std::uint8_t{0});
    if (mutation==3) thirdPerson=false;
    return nativeSuccess;
}
bool ThirdPersonViewActive(std::uintptr_t p,std::uintptr_t identity,std::uintptr_t c,std::uint32_t handle) noexcept {
    return thirdPerson && p==Address(pawn.data()) && identity==Address(bucket.data()+3*0x70) && c==Address(controller.data()) &&
        (handle==pawnHandle || handle==pawnHandle+0x8000);
}
int main() {
    memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!memory) return 2;
    client=Address(memory);
    Check(!ScopedCrosshairPrepare(),"unverified installation leaves capture inactive");
    Positive();Reject();StopDrains();ScopedCrosshairStop();
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("scoped crosshair: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
