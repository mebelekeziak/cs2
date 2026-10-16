#include "zoom.h"
#include "zoom_offsets.h"
#include "zoom_settings.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
namespace off=zoom_offsets;
namespace ent=cs2_offsets;
unsigned checks=0, failures=0;
void Check(bool condition,const char* label) { ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n",label); } }
template<class T> void Put(void* p,std::size_t offset,T value) { std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof(value)); }
template<class T> T Get(const void* p,std::size_t offset) { T value{}; std::memcpy(&value,static_cast<const unsigned char*>(p)+offset,sizeof(value)); return value; }
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
bool Near(float a,float b) { return std::abs(a-b)<0.0002f; }
unsigned char* clientMemory=nullptr;
std::uintptr_t client=0;
std::array<unsigned char,0x2200> pawn{},weapon{};
std::array<unsigned char,0x1000> controller{};
std::array<unsigned char,0x100> services{},system{};
std::array<unsigned char,0x70> pawnIdentity{};
std::array<unsigned char,0x70*512> bucket{};
std::array<unsigned char,0x30> mode{};
using View=std::array<unsigned char,0x600>;
View view{},nestedView{};
constexpr std::uint32_t weaponHandle=0x18005, pawnHandle=0x10003;
struct Mock {
    float baseline=90,native=40;
    int viewCalls=0,baselineCalls=0;
    bool forwarded=true,nested=false,inNested=false;
    int mutation=0;
    HANDLE entered=nullptr,resume=nullptr;
} mock;
float Baseline(void* input) {
    ++mock.baselineCalls; mock.forwarded &= input==controller.data();
    if (mock.mutation==6) Put(services.data(),off::ActiveWeapon,std::uint32_t{0x18006});
    return mock.baseline;
}
void Native(void* input,void* output) {
    ++mock.viewCalls; mock.forwarded &= input==mode.data();
    if (output) { std::memset(output,0xA5,view.size()); Put(output,off::ViewFov,mock.native); }
    if (mock.nested && !mock.inNested) {
        mock.inNested=true; ZoomTestView(mode.data(),nestedView.data()); mock.inNested=false;
    }
    if (mock.mutation==1) Put(clientMemory,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});
    if (mock.mutation==2) Put(services.data(),off::ActiveWeapon,std::uint32_t{0x18006});
    if (mock.mutation==3) Put(bucket.data()+5*0x70,0x10,std::uint32_t{0x20005});
    if (mock.mutation==4) Put(weapon.data(),off::ZoomLevel,2);
    if (mock.mutation==5) Put(mode.data(),off::ModeSlot,1);
    if (mock.entered) { SetEvent(mock.entered); WaitForSingleObject(mock.resume,INFINITE); }
}
void Reset() {
    mock=Mock{}; pawn.fill(0);weapon.fill(0);controller.fill(0);services.fill(0);system.fill(0);
    pawnIdentity.fill(0);bucket.fill(0);mode.fill(0);view.fill(0);nestedView.fill(0);
    Put(clientMemory,ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    Put(clientMemory,ent::globals::dwLocalPlayerController,Address(controller.data()));
    Put(clientMemory,ent::globals::dwGameEntitySystem,Address(system.data()));
    Put(pawn.data(),ent::CEntityInstance::m_pEntity,Address(pawnIdentity.data()));
    Put(pawnIdentity.data(),0,Address(pawn.data()));Put(pawnIdentity.data(),0x10,pawnHandle);
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle);
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,100);
    Put(pawn.data(),off::Scoped,std::uint8_t{1});
    Put(pawn.data(),off::WeaponServices,Address(services.data()));
    Put(services.data(),off::ActiveWeapon,weaponHandle);
    Put(system.data(),ent::entity_layout::TableOffset,Address(bucket.data()));
    Put(bucket.data()+5*0x70,0,Address(weapon.data()));Put(bucket.data()+5*0x70,0x10,weaponHandle);
    Put(weapon.data(),ent::CEntityInstance::m_pEntity,Address(bucket.data()+5*0x70));
    Put(weapon.data(),0,client+off::ScopedWeaponVtables[0]);Put(weapon.data(),off::ZoomLevel,1);
    Put(mode.data(),0,client+off::ModeVtable);Put(mode.data(),off::ModeSlot,0);
    ZoomSetSettings({true,0,200});ZoomTestConfigure(client,&Native,&Baseline);
}
void Draw() { ZoomTestView(mode.data(),view.data()); }
void ExpectFov(float expected,const char* label) {
    Check(Near(Get<float>(view.data(),off::ViewFov),expected),label);
    auto restored=view;Put(restored.data(),off::ViewFov,mock.native);
    View native{};native.fill(0xA5);Put(native.data(),off::ViewFov,mock.native);
    Check(restored==native,"all opaque bytes including viewmodel FOV, origin and angles preserved");
    Check(mock.forwarded,"native arguments forwarded exactly");
}
void Math() {
    float value=-1;
    Check(ZoomComputeFov(90,40,0,value) && value==90,"zero strength is exact unscoped FOV");
    Check(ZoomComputeFov(90,40,100,value) && value==40,"100 strength is exact native FOV");
    Check(ZoomComputeFov(90,40,200,value) && value<40 && value>0,"200 strength increases magnification");
    Check(ZoomComputeFov(90,40,50,value) && value>40 && value<90,"half strength lies between native and baseline");
    Check(ZoomComputeFov(110,70,0,value) && value==110,"custom baseline honored");
    Check(ZoomComputeFov(90,90,200,value) && Near(value,90),"unzoomed interpolation boundary stays unzoomed");
    for (const float bad:{0.0f,-1.0f,179.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Check(!ZoomComputeFov(bad,40,50,value),"invalid baseline rejected");
        Check(!ZoomComputeFov(90,bad,50,value),"invalid native FOV rejected");
    }
    Check(!ZoomComputeFov(60,90,0,value),"special native FOV above baseline retained");
    Check(!ZoomComputeFov(90,40,std::numeric_limits<float>::quiet_NaN(),value),"NaN strength rejected");
    Check(ZoomComputeFov(90,40,-10,value) && value==90,"negative strength clamps to zero");
    float high=0;ZoomComputeFov(90,40,200,high);
    Check(ZoomComputeFov(90,40,300,value) && value==high,"excess strength clamps to 200");
    double previous=90;
    for (int strength=0;strength<=200;++strength) {
        Check(ZoomComputeFov(90,40,static_cast<float>(strength),value) && value<=previous+0.0001,"slider monotonically increases zoom");
        const double scale=std::tan(90.0*3.141592653589793/360.0)/std::tan(value*3.141592653589793/360.0);
        const double target=1+(strength/100.0)*(1/std::tan(40.0*3.141592653589793/360.0)-1);
        Check(std::abs(scale-target)<0.000002,"tangent magnification follows selected strength"); previous=value;
    }
}
void Runtime() {
    for (int level:{1,2}) for (float strength:{0.0f,100.0f,200.0f}) {
        Reset();Put(weapon.data(),off::ZoomLevel,level);
        ZoomSetSettings({true,level==1?strength:25,level==2?strength:25});
        Draw();float expected=0;ZoomComputeFov(90,40,strength,expected);
        ExpectFov(expected,"independent zoom level setting selected");
    }
    for (auto table:off::ScopedWeaponVtables) {
        Reset();Put(weapon.data(),0,client+table);Draw();ExpectFov(90,"every verified scoped weapon class supported");
    }
    Reset();ZoomSetSettings({false,0,0});Draw();ExpectFov(40,"disabled feature forwards native");
    Check(mock.baselineCalls==0,"disabled feature never calls native baseline getter");
    for (int invalid=0;invalid<13;++invalid) {
        Reset();
        switch (invalid) {
        case 0: Put(pawn.data(),off::Scoped,std::uint8_t{0});break;
        case 1: Put(weapon.data(),off::ZoomLevel,0);break;
        case 2: Put(weapon.data(),off::ZoomLevel,3);break;
        case 3: Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);break;
        case 4: Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1});break;
        case 5: Put(bucket.data()+5*0x70,0x10,std::uint32_t{0x20005});break;
        case 6: Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003});break;
        case 7: Put(weapon.data(),0,client+off::ScopedWeaponVtables[0]+8);break;
        case 8: Put(mode.data(),off::ModeSlot,1);break;
        case 9: Put(mode.data(),0,client+off::ModeVtable+8);break;
        case 10: Put(weapon.data(),ent::CEntityInstance::m_pEntity,Address(pawnIdentity.data()));break;
        case 11: Put(pawn.data(),off::WeaponServices,std::uintptr_t{0});break;
        case 12: Put(services.data(),off::ActiveWeapon,UINT32_MAX);break;
        }
        Draw();ExpectFov(40,"invalid/stale/dead/nonlocal/unsupported source retains native view");
        Check(mock.baselineCalls==0,"invalid source avoids native getter");
    }
    for (int mutation=1;mutation<=6;++mutation) {
        Reset();mock.mutation=mutation;Draw();ExpectFov(40,"source changes during original/baseline invalidate current edit");
    }
    Reset();mock.native=std::numeric_limits<float>::quiet_NaN();Draw();
    Check(std::isnan(Get<float>(view.data(),off::ViewFov)),"invalid native FOV remains unchanged");
    Reset();mock.baseline=0;Draw();ExpectFov(40,"invalid native baseline remains unchanged");
    Reset();mock.baseline=110;Draw();ExpectFov(110,"native baseline callback result used");
    Reset();mock.nested=true;Draw();ExpectFov(90,"outer view applies with nested native callback");
    Check(Get<float>(nestedView.data(),off::ViewFov)==40 && mock.baselineCalls==1,"nested view forwards without edits or outer-source reuse");
    Reset();ZoomTestView(mode.data(),nullptr);Check(mock.viewCalls==1 && mock.baselineCalls==0,"null view forwards safely");
    Reset();ZoomStop("test");Draw();ExpectFov(40,"stopped hook forwards without FOV edits");
    ZoomActivate();Draw();ExpectFov(40,"stopped runtime cannot reactivate");
}
void StopBarrier() {
    Reset();mock.entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);mock.resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> stopEntered{false},stopReturned{false};
    std::thread render([] { Draw(); });
    Check(WaitForSingleObject(mock.entered,3000)==WAIT_OBJECT_0,"native callback entered before concurrent stop");
    std::thread stopper([&] { stopEntered=true;ZoomStop("barrier");stopReturned=true; });
    while (!stopEntered.load()) SwitchToThread();
    Sleep(20);Check(!stopReturned.load(),"stop waits for active callback and view edit");
    SetEvent(mock.resume);render.join();stopper.join();
    Check(stopReturned.load(),"stop completes after callback drain");
    CloseHandle(mock.entered);CloseHandle(mock.resume);mock.entered=mock.resume=nullptr;
    Draw();ExpectFov(40,"callbacks arriving after stop keep forwarding native");
}
}
int main() {
    clientMemory=static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!clientMemory) return 2;
    client=Address(clientMemory);Math();Runtime();StopBarrier();
    VirtualFree(clientMemory,0,MEM_RELEASE);
    std::printf("zoom checks: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
