#include "zoom.h"
#include "zoom_offsets.h"
#include "zoom_settings.h"
#include "viewmodel.h"
#include "viewmodel_settings.h"
#include "viewmodel_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <algorithm>
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
    bool viewmodel=false;
    bool nestedBuilder=false, insideBuilder=false;
    float viewmodelBase=68;
    float aspect=4.0f/3.0f;
    unsigned char viewFlags=0;
    int parameterMutation=0;
    HANDLE entered=nullptr,resume=nullptr;
} mock;
float ReferenceProjection(float fov,float aspect,unsigned char flags) {
    if (flags&2) return fov;
    constexpr double halfRadians=3.14159265358979323846/360.0;
    return static_cast<float>(std::atan(std::tan(fov*halfRadians)*(aspect*.75))/halfRadians);
}
void NativeParameters(void* input,void* xyz,float* fov) {
    mock.forwarded &= input==pawn.data();
    if (xyz) std::memset(xyz,0,3*sizeof(float));
    if (fov) *fov=mock.viewmodelBase;
    if (mock.parameterMutation) Put(services.data(),off::ActiveWeapon,std::uint32_t{0x18006});
}
struct MouseCall {
    int slot=0;
    float dx=0,dy=0,scale=0,deltaTime=0;
    float* yaw=nullptr;
    float* pitch=nullptr;
    int* mouseDx=nullptr;
    int* mouseDy=nullptr;
};
struct MouseMock {
    MouseCall normal{},alternate{};
    int normalCalls=0,alternateCalls=0;
    bool nestedAlternate=false,nestedView=false;
    float nestedYaw=19,nestedPitch=-23;
    HANDLE entered=nullptr,resume=nullptr;
} mouseMock;
void NativeMouseAlternate(int slot,float dx,float dy,float scale,float deltaTime,float* yaw,float* pitch) {
    ++mouseMock.alternateCalls;
    mouseMock.alternate={slot,dx,dy,scale,deltaTime,yaw,pitch};
    if (yaw) *yaw=123.25f+dx*scale;
    if (pitch) *pitch=-42.5f+dy*scale;
    if (mouseMock.entered) { SetEvent(mouseMock.entered); WaitForSingleObject(mouseMock.resume,INFINITE); }
}
void NativeMouse(int slot,float dx,float dy,float scale,float* yaw,float* pitch,int* mouseDx,int* mouseDy) {
    ++mouseMock.normalCalls;
    mouseMock.normal={slot,dx,dy,scale,0,yaw,pitch,mouseDx,mouseDy};
    if (yaw) *yaw=123.25f+dx*scale;
    if (pitch) *pitch=-42.5f+dy*scale;
    if (mouseDx) *mouseDx=-123456;
    if (mouseDy) *mouseDy=654321;
    if (mouseMock.nestedAlternate)
        ZoomTestMouseAlternate(0,-3.75f,8.125f,0.25f,0.03125f,&mouseMock.nestedYaw,&mouseMock.nestedPitch);
    if (mouseMock.nestedView) ZoomTestView(mode.data(),nestedView.data());
    if (mouseMock.entered) { SetEvent(mouseMock.entered); WaitForSingleObject(mouseMock.resume,INFINITE); }
}
struct SourceSnapshot {
    decltype(pawn) pawnBytes;
    decltype(weapon) weaponBytes;
    decltype(controller) controllerBytes;
    decltype(services) servicesBytes;
    decltype(system) systemBytes;
    decltype(pawnIdentity) pawnIdentityBytes;
    decltype(bucket) bucketBytes;
    decltype(mode) modeBytes;
    std::uintptr_t localPawn,localController,entitySystem;
    bool operator==(const SourceSnapshot&) const = default;
};
SourceSnapshot SnapshotSource() {
    return {pawn,weapon,controller,services,system,pawnIdentity,bucket,mode,
        Get<std::uintptr_t>(clientMemory,ent::globals::dwLocalPlayerPawn),
        Get<std::uintptr_t>(clientMemory,ent::globals::dwLocalPlayerController),
        Get<std::uintptr_t>(clientMemory,ent::globals::dwGameEntitySystem)};
}
float Baseline(void* input) {
    ++mock.baselineCalls; mock.forwarded &= input==controller.data();
    if (mock.mutation==6) Put(services.data(),off::ActiveWeapon,std::uint32_t{0x18006});
    return mock.baseline;
}
void Native(void* input,void* output) {
    ++mock.viewCalls; mock.forwarded &= input==mode.data();
    if (output) {
        std::memset(output,0xA5,view.size()); Put(output,off::ViewFov,mock.native);
        if (mock.viewmodel) {
            Put(output,viewmodel_offsets::ViewmodelFov,ReferenceProjection(std::max(5.0f,mock.viewmodelBase-(90.0f-mock.native)),mock.aspect,mock.viewFlags));
            Put(output,viewmodel_offsets::ViewAspect,mock.aspect);Put(output,viewmodel_offsets::ViewFlags,mock.viewFlags);
        }
    }
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
void NativeBuilder(void* input,void* output) {
    // Actual engine order: slot16 invokes Zoom's callback, THEN the enclosing
    // slot15 builder replaces viewmodel FOV and applies final aspect conversion.
    ZoomTestView(input,output);
    float xyz[3]{}, base=0;
    ViewmodelTestParameters(pawn.data(),xyz,&base);
    if (output) {
        Put(output,viewmodel_offsets::ViewmodelFov,ReferenceProjection(std::max(5.0f,base-(90.0f-mock.native)),mock.aspect,mock.viewFlags));
        Put(output,viewmodel_offsets::ViewAspect,mock.aspect);Put(output,viewmodel_offsets::ViewFlags,mock.viewFlags);
    }
    if (mock.nestedBuilder && !mock.insideBuilder) {
        mock.insideBuilder=true;ViewmodelTestView(input,nestedView.data());mock.insideBuilder=false;
    }
}
void Reset() {
    mock=Mock{};mouseMock=MouseMock{};pawn.fill(0);weapon.fill(0);controller.fill(0);services.fill(0);system.fill(0);
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
    ZoomTestConfigureMouse(&NativeMouse,&NativeMouseAlternate);
    ViewmodelResetSettings();ViewmodelTestConfigure(client,&NativeParameters);
    ViewmodelTestConfigureView(&NativeBuilder);
}
void Draw() {
    if (mock.viewmodel) ViewmodelTestView(mode.data(),view.data());
    else ZoomTestView(mode.data(),view.data());
}
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
void SensitivityMath() {
    float value=-1;
    Check(ZoomBlendSensitivity(1,0.4f,0,value) && value==1,"zero sensitivity strength keeps exact unscoped multiplier");
    Check(ZoomBlendSensitivity(1,0.4f,100,value) && value==0.4f,"full sensitivity strength keeps exact native multiplier");
    Check(ZoomBlendSensitivity(1,0.4f,25,value) && Near(value,0.85f),"quarter sensitivity strength blends toward native");
    Check(ZoomBlendSensitivity(2,0.5f,50,value) && Near(value,1.25f),"custom unscoped sensitivity blends linearly");
    Check(ZoomBlendSensitivity(1,1.5f,50,value) && Near(value,1.25f),"native sensitivity above baseline stays meaningful");
    Check(ZoomBlendSensitivity(1,0,100,value) && value==0,"native zero sensitivity preserved");
    Check(ZoomBlendSensitivity(1,0.4f,-1,value) && value==1,"sensitivity strength clamps below zero");
    Check(ZoomBlendSensitivity(1,0.4f,200,value) && value==0.4f,"sensitivity strength clamps above native");
    Check(ZoomBlendSensitivity(1,1,50,value) && value==1,"unscoped native boundary remains unchanged");
    for (float bad:{-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Check(!ZoomBlendSensitivity(bad,0.4f,50,value),"invalid sensitivity baseline rejected");
        Check(!ZoomBlendSensitivity(1,bad,50,value),"invalid native sensitivity rejected");
    }
    Check(!ZoomBlendSensitivity(0,0.4f,50,value),"zero unscoped baseline rejected");
    Check(!ZoomBlendSensitivity(1,0.4f,std::numeric_limits<float>::quiet_NaN(),value),"nonfinite sensitivity strength rejected");
    Check(!ZoomBlendSensitivity(1,0.4f,std::numeric_limits<float>::infinity(),value),"infinite sensitivity strength rejected");
    float previous=1;
    for (int strength=0;strength<=100;++strength) {
        Check(ZoomBlendSensitivity(1,0.4f,static_cast<float>(strength),value) && value<=previous && value>=0.4f,
            "sensitivity slider smoothly moves from unscoped to native without overshoot");
        previous=value;
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
void Mouse(bool alternate,float nativeScale,float expectedScale,int slot=0) {
    constexpr float dx=-13.625f,dy=7.375f,deltaTime=0.015625f;
    float yaw=-9,pitch=11;
    int mouseDx=17,mouseDy=-29;
    const auto before=SnapshotSource();
    const auto settingsBefore=ZoomGetSettings();
    const int callsBefore=alternate?mouseMock.alternateCalls:mouseMock.normalCalls;
    if (alternate) ZoomTestMouseAlternate(slot,dx,dy,nativeScale,deltaTime,&yaw,&pitch);
    else ZoomTestMouse(slot,dx,dy,nativeScale,&yaw,&pitch,&mouseDx,&mouseDy);
    const auto& received=alternate?mouseMock.alternate:mouseMock.normal;
    Check((alternate?mouseMock.alternateCalls:mouseMock.normalCalls)==callsBefore+1,
        "mouse callback forwards exactly once through its matching conversion");
    Check(received.slot==slot && received.dx==dx && received.dy==dy &&
        received.yaw==&yaw && received.pitch==&pitch,
        "mouse slot, original deltas and output pointer arguments forwarded exactly");
    Check(std::memcmp(&received.scale,&expectedScale,sizeof(float))==0,
        "mouse conversion receives selected scale with exact native fallback");
    if (alternate) {
        Check(received.deltaTime==deltaTime,"alternate mouse delta time forwarded exactly");
        Check(mouseDx==17 && mouseDy==-29,"alternate conversion does not invent integer mouse outputs");
    } else {
        Check(received.mouseDx==&mouseDx && received.mouseDy==&mouseDy && mouseDx==-123456 && mouseDy==654321,
            "normal mouse integer pointers forwarded and original owns integer outputs");
    }
    const auto sameOutput=[](float actual,float expected) {
        return (std::isnan(actual) && std::isnan(expected)) || actual==expected;
    };
    Check(sameOutput(yaw,123.25f+dx*expectedScale) && sameOutput(pitch,-42.5f+dy*expectedScale),
        "original conversion controls final yaw and pitch outputs without postprocessing");
    Check(SnapshotSource()==before && ZoomGetSettings()==settingsBefore,
        "mouse sensitivity leaves all entity, source and settings bytes unchanged");
    Check(mock.baselineCalls==0,"mouse sensitivity does not invoke the camera FOV getter");
}
void MouseRuntime() {
    for (bool alternate:{false,true}) {
        for (auto table:off::ScopedWeaponVtables) for (int level:{1,2}) for (float strength:{0.0f,50.0f,100.0f}) {
            Reset();Put(weapon.data(),0,client+table);Put(weapon.data(),off::ZoomLevel,level);
            ZoomSetSettings({true,level==1?0.0f:200.0f,level==2?0.0f:200.0f,strength});
            // Distinct native scales for both levels prove the incoming native
            // factor is used rather than a weapon-class or FOV approximation.
            const float native=level==1?0.375f:0.125f;
            Mouse(alternate,native,strength==0?1.0f:strength==100?native:(1+native)*0.5f);
        }
        for (float first:{0.0f,100.0f,200.0f}) for (float second:{0.0f,100.0f,200.0f}) {
            Reset();ZoomSetSettings({true,first,second,50});Mouse(alternate,0.25f,0.625f);
        }
        Reset();ZoomSetSettings({false,0,0,0});Mouse(alternate,0.375f,0.375f);
        for (int invalid=0;invalid<15;++invalid) {
            Reset();ZoomSetSettings({true,0,200,0});
            switch (invalid) {
            case 0: Put(pawn.data(),off::Scoped,std::uint8_t{0});break;
            case 1: Put(weapon.data(),off::ZoomLevel,0);break;
            case 2: Put(weapon.data(),off::ZoomLevel,3);break;
            case 3: Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);break;
            case 4: Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1});break;
            case 5: Put(bucket.data()+5*0x70,0x10,std::uint32_t{0x20005});break;
            case 6: Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003});break;
            case 7: Put(weapon.data(),0,client+off::ScopedWeaponVtables[0]+8);break;
            case 8: Put(weapon.data(),ent::CEntityInstance::m_pEntity,Address(pawnIdentity.data()));break;
            case 9: Put(pawn.data(),off::WeaponServices,std::uintptr_t{0});break;
            case 10: Put(services.data(),off::ActiveWeapon,UINT32_MAX);break;
            case 11: Put(clientMemory,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});break;
            case 12: Put(clientMemory,ent::globals::dwLocalPlayerController,std::uintptr_t{0});break;
            case 13: Put(clientMemory,ent::globals::dwGameEntitySystem,std::uintptr_t{0});break;
            case 14: Put(pawnIdentity.data(),0x10,std::uint32_t{0x20003});break;
            }
            Mouse(alternate,0.375f,0.375f);
        }
        for (int slot:{-1,1,2}) {
            Reset();ZoomSetSettings({true,0,200,0});Mouse(alternate,0.375f,0.375f,slot);
        }
        for (float native:{-1.0f,std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            Reset();ZoomSetSettings({true,0,200,0});Mouse(alternate,native,native);
        }
        Reset();ZoomSetSettings({true,0,200,50});Mouse(alternate,0,0.5f);
        Reset();ZoomSetSettings({true,0,200,50});Mouse(alternate,1.5f,1.25f);
        Reset();ZoomSetSettings({true,0,200,0});ZoomStop("mouse stopped");Mouse(alternate,0.375f,0.375f);
        ZoomActivate();Mouse(alternate,0.375f,0.375f);
        Reset();ZoomSetSettings({true,0,200,0});
        if (alternate) ZoomTestMouseAlternate(0,-2,3,0.25f,0.03125f,nullptr,nullptr);
        else ZoomTestMouse(0,-2,3,0.25f,nullptr,nullptr,nullptr,nullptr);
        const auto& received=alternate?mouseMock.alternate:mouseMock.normal;
        Check(received.scale==1 && received.yaw==nullptr && received.pitch==nullptr &&
            received.mouseDx==nullptr && received.mouseDy==nullptr,"null native output pointers are forwarded untouched");
    }
    Reset();ZoomSetSettings({true,0,200,0});mouseMock.nestedAlternate=true;
    Mouse(false,0.375f,1);
    Check(mouseMock.alternateCalls==1 && mouseMock.alternate.scale==0.25f &&
        mouseMock.alternate.dx==-3.75f && mouseMock.alternate.dy==8.125f && mouseMock.alternate.deltaTime==0.03125f &&
        mouseMock.alternate.yaw==&mouseMock.nestedYaw && mouseMock.alternate.pitch==&mouseMock.nestedPitch &&
        mouseMock.nestedYaw==123.25f-3.75f*0.25f && mouseMock.nestedPitch==-42.5f+8.125f*0.25f,
        "nested normal-to-alternate conversion retains native scale, arguments and outputs");
    Reset();ZoomSetSettings({true,0,200,0});mouseMock.nestedView=true;
    Mouse(false,0.375f,1);
    Check(mock.viewCalls==1 && Get<float>(nestedView.data(),off::ViewFov)==40 && mock.baselineCalls==0,
        "view nested inside mouse conversion forwards native FOV without borrowing outer scope");
}
void MouseStopBarrier() {
    for (bool alternate:{false,true}) {
        Reset();ZoomSetSettings({true,0,200,0});
        mouseMock.entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        mouseMock.resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<bool> stopEntered{false},stopReturned{false};
        std::thread input([alternate] {
            float yaw=0,pitch=0;int dx=0,dy=0;
            if (alternate) ZoomTestMouseAlternate(0,2,3,0.25f,0.015625f,&yaw,&pitch);
            else ZoomTestMouse(0,2,3,0.25f,&yaw,&pitch,&dx,&dy);
        });
        Check(WaitForSingleObject(mouseMock.entered,3000)==WAIT_OBJECT_0,"original mouse conversion entered before stop");
        std::thread stopper([&] { stopEntered=true;ZoomStop("mouse barrier");stopReturned=true; });
        while (!stopEntered.load()) SwitchToThread();
        Sleep(20);Check(!stopReturned.load(),"cooperative stop waits while original mouse conversion owns its output buffers");
        SetEvent(mouseMock.resume);input.join();stopper.join();
        Check(stopReturned.load(),"mouse callback drain releases cooperative stop");
        CloseHandle(mouseMock.entered);CloseHandle(mouseMock.resume);mouseMock.entered=mouseMock.resume=nullptr;
        Mouse(alternate,0.375f,0.375f);
    }
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
void ScopedViewmodel() {
    // Model the verified lossy minimum clamp; reversing its delta gives the
    // wrong answer for a 15-degree second zoom, so assert the true base FOV.
    for (const auto table:off::ScopedWeaponVtables) for (int level:{1,2})
        for (bool worldOverride:{false,true}) for (bool custom:{false,true}) {
            Reset();mock.viewmodel=true;mock.native=level==1?40.0f:15.0f;
            Put(weapon.data(),0,client+table);Put(weapon.data(),off::ZoomLevel,level);
            ZoomSetSettings({worldOverride,0,200,100,true});
            if (custom) ViewmodelSetSettings({true,2,3,4,95});
            const auto before=SnapshotSource();
            Draw();
            const float selected=custom?95.0f:68.0f;
            Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),selected),
                "scope removal keeps actual base viewmodel FOV at both levels and every supported class");
            float world=mock.native;
            if (worldOverride) ZoomComputeFov(90,mock.native,level==1?0.0f:200.0f,world);
            Check(Near(Get<float>(view.data(),off::ViewFov),world),"viewmodel zoom removal preserves independent world magnification");
            auto actual=view;Put(actual.data(),off::ViewFov,mock.native);
            Put(actual.data(),viewmodel_offsets::ViewmodelFov,std::max(5.0f,mock.viewmodelBase-(90.0f-mock.native)));
            View expected{};expected.fill(0xA5);Put(expected.data(),off::ViewFov,mock.native);
            Put(expected.data(),viewmodel_offsets::ViewmodelFov,std::max(5.0f,mock.viewmodelBase-(90.0f-mock.native)));
            Put(expected.data(),viewmodel_offsets::ViewAspect,mock.aspect);Put(expected.data(),viewmodel_offsets::ViewFlags,mock.viewFlags);
            Check(actual==expected && SnapshotSource()==before,"only current world/viewmodel projection bytes change; source and opaque bytes preserved");
            Check(mock.baselineCalls==(worldOverride?1:0),"scope removal alone does not invoke the world FOV baseline");
        }
    for (int mutation:{0,1,2,3,4,5,6,7}) {
        Reset();mock.viewmodel=true;mock.native=15;
        ZoomSetSettings({true,0,0,100,true});
        if (mutation==0) ZoomSetSettings({false,100,100,100,false});
        else if (mutation<=5) mock.mutation=mutation;
        else if (mutation==6) mock.parameterMutation=1;
        else ViewmodelStop();
        Draw();
        Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),5),
            "disabled, stale, switched, nonprimary or stopped viewmodel source retains native scoped projection");
    }
    Reset();mock.viewmodel=true;mock.native=15;mock.viewmodelBase=std::numeric_limits<float>::quiet_NaN();
    ZoomSetSettings({false,100,100,100,true});Draw();
    Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),5),"invalid native base cannot overwrite current viewmodel projection");
    for (float aspect:{1.0f,4.0f/3.0f,16.0f/9.0f,21.0f/9.0f}) for (unsigned char flags:std::array<unsigned char,3>{0,2,4}) {
        Reset();mock.viewmodel=true;mock.native=15;mock.aspect=aspect;mock.viewFlags=flags;
        ZoomSetSettings({false,100,100,100,true});Draw();
        Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),ReferenceProjection(68,aspect,flags)),
            "unzoomed viewmodel preserves native aspect projection and exact skip flag across display ratios");
    }
    for (float aspect:{0.0f,-1.0f,std::numeric_limits<float>::infinity()}) {
        Reset();mock.viewmodel=true;mock.aspect=aspect;
        ZoomSetSettings({false,100,100,100,true});Draw();
        Check(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov)==ReferenceProjection(18,aspect,0),
            "invalid resolved aspect retains native viewmodel output");
    }
    Reset();mock.viewmodel=true;mock.native=15;mock.nestedBuilder=true;
    ViewmodelSetSettings({true,0,0,0,95});ZoomSetSettings({false,100,100,100,true});Draw();
    Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),95) &&
        Near(Get<float>(nestedView.data(),viewmodel_offsets::ViewmodelFov),5),
        "completed outer builder keeps selected FOV; nested builder retains native parameters/projection");
    Reset();mock.viewmodel=true;mock.native=15;
    ViewmodelSetSettings({true,0,0,0,95});ZoomSetSettings({});Draw();
    Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),20),
        "scope removal off preserves native scoped reduction of custom base FOV");
    Reset();mock.viewmodel=true;mock.native=15;
    ZoomSetSettings({false,100,100,100,true});
    mock.entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);mock.resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> stopStarted{false},stopReturned{false};
    std::thread render([] { Draw(); });
    Check(WaitForSingleObject(mock.entered,3000)==WAIT_OBJECT_0,"native builder entered before viewmodel stop");
    std::thread stopper([&] { stopStarted=true;ViewmodelStop();stopReturned=true; });
    while (!stopStarted.load()) SwitchToThread();
    Sleep(20);Check(!stopReturned.load(),"viewmodel stop drains complete builder including nested native parameter work");
    SetEvent(mock.resume);render.join();stopper.join();
    CloseHandle(mock.entered);CloseHandle(mock.resume);mock.entered=mock.resume=nullptr;
    Draw();Check(Near(Get<float>(view.data(),viewmodel_offsets::ViewmodelFov),5) && stopReturned.load(),
        "stopped builder retains native forwarding without stale projection writes");
}
}
int main() {
    clientMemory=static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!clientMemory) return 2;
    client=Address(clientMemory);Math();SensitivityMath();Runtime();StopBarrier();MouseRuntime();MouseStopBarrier();ScopedViewmodel();
    VirtualFree(clientMemory,0,MEM_RELEASE);
    std::printf("zoom checks: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
