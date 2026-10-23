#include "player_chams.h"
#include "player_chams_offsets.h"
#include "player_chams_materials.h"
#include "cs2_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

extern "C" bool PlayerChamsPvsFixture(void*,void*,void*);
extern "C" unsigned char PlayerChamsPvsFixtureReturn;

namespace {
namespace off=player_chams_offsets;
namespace ent=cs2_offsets;
using Packet=std::array<unsigned char,0x70>;
unsigned checks=0,failures=0,calls=0,materialCalls=0,pvsCalls=0;
std::array<Packet,3> packets{},overflow{},oldOverflow{};
std::array<unsigned char,0x120> object{};
std::array<unsigned char,0x20> descriptor{},args{};
std::array<unsigned char,0x100> view{};
std::array<unsigned char,0x30> membership{};
std::array<unsigned char,0x490> skeleton{},localSkeleton{};
std::array<unsigned char,0x1600> pawn{},localPawn{};
std::array<unsigned char,0xA00> controller{},localController{};
std::array<unsigned char,0x70*512> bucket{};
std::array<unsigned char,0x220> system{};
std::uintptr_t client=0;
std::uintptr_t scene=0x10000000;
constexpr std::uintptr_t material=0x22220000,mode=0x33330000;
WorldDrawList list{};
HANDLE entered=nullptr,resume=nullptr;
bool materialsReady=true,replaceIdentity=false,emitDraw=true,nativePvs=false;
bool lastOccluded=false;
PlayerChamsStyle lastStyle=PlayerChamsStyle::Flat;
unsigned localTransition=0;
void Check(bool value,const char* message) {++checks;if (!value){++failures;std::printf("FAIL %s\n",message);}}
std::uintptr_t Address(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
template<class T> void Put(void* p,std::size_t at,T value){std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value));}
template<class T> T Get(const void* p,std::size_t at){T value{};std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
Packet Baseline(std::uintptr_t owner){Packet p{};p.fill(0xA5);Put(p.data(),0x18,owner);Put(p.data(),0x20,std::uintptr_t{0x44440000});Put(p.data(),0x28,std::uintptr_t{0x55550000});return p;}
bool Replaced(const Packet& p){return Get<std::uintptr_t>(p.data(),0x20)==material;}
void Bind(unsigned index,void* entity,std::uint32_t flags=0){
    auto* identity=bucket.data()+index*0x70;
    Put(identity,0,Address(entity));Put(identity,0x10,std::uint32_t{(4U<<15)|index});Put(identity,0x30,flags);
    Put(entity,0x10,Address(identity));
}
void Original(void*,void*,void*,void* output){
    ++calls;if (entered){SetEvent(entered);WaitForSingleObject(resume,INFINITE);}
    if (!emitDraw) return;
    packets[0]=Baseline(Address(object.data()));static_cast<WorldDrawList*>(output)->fixedCount=1;
}
bool OriginalPvs(void* data,void* bits){
    ++pvsCalls;Check(data==membership.data() && bits==view.data(),"native PVS receives original two arguments");
    if (entered){SetEvent(entered);WaitForSingleObject(resume,INFINITE);}
    if (replaceIdentity) Put(bucket.data()+2*0x70,0x10,std::uint32_t{(5U<<15)|2});
    return nativePvs;
}
void Reset(){
    for(auto& p:packets)p=Baseline(Address(object.data()));for(auto& p:overflow)p=Baseline(Address(object.data()));
    list={};list.fixed=Address(packets.data());list.fixedCapacity=3;list.overflow=Address(overflow.data());list.overflowCapacity=3;
}
void Setup(){
    Reset();object.fill(0);descriptor.fill(0);args.fill(0);view.fill(0);membership.fill(0);skeleton.fill(0);localSkeleton.fill(0);
    pawn.fill(0);localPawn.fill(0);controller.fill(0);localController.fill(0);bucket.fill(0);system.fill(0);
    calls=materialCalls=pvsCalls=0;materialsReady=true;replaceIdentity=false;emitDraw=true;nativePvs=false;localTransition=0;lastOccluded=false;
    Bind(1,controller.data());Bind(2,pawn.data());Bind(3,localController.data());Bind(4,localPawn.data());
    Put(system.data(),0x10,Address(bucket.data()));
    Put(reinterpret_cast<void*>(client),ent::globals::dwGameEntitySystem,Address(system.data()));
    Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerController,Address(localController.data()));
    Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,Address(localPawn.data()));
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(4U<<15)|2});
    Put(localController.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(4U<<15)|4});
    Put(localController.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});
    Put(pawn.data(),ent::C_BasePlayerPawn::m_hController,std::uint32_t{(4U<<15)|1});
    Put(localPawn.data(),ent::C_BasePlayerPawn::m_hController,std::uint32_t{(4U<<15)|3});
    Put(pawn.data(),ent::C_BaseEntity::m_pGameSceneNode,Address(skeleton.data()));
    Put(localPawn.data(),ent::C_BaseEntity::m_pGameSceneNode,Address(localSkeleton.data()));
    Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{3});
    Put(localPawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{2});
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,int{100});
    Put(skeleton.data(),0,client+silent_aim_hitbox_offsets::PlayerSkeletonVtable);
    Put(object.data(),off::OwnerHandle,std::uint32_t{(4U<<15)|2});
    Put(object.data(),off::ObjectDescriptor,Address(descriptor.data()));
    Put(object.data(),off::ObjectPvsData,Address(membership.data()));
    Put(object.data(),off::SkeletonInterface,Address(skeleton.data())+off::SkeletonInterfaceMember);
    Put(descriptor.data(),0,scene+off::DescriptorVtable);Put(args.data(),off::ViewInArgs,Address(view.data()));
    PlayerChamsSettings settings{};settings.enabled=true;settings.color[0]=1;settings.color[1]=0.5f;settings.color[2]=0;settings.color[3]=0.25f;
    PlayerChamsSetSettings(settings);PlayerChamsTestConfigure(client,scene,Original,OriginalPvs);
}
void Run(){PlayerChamsTestGenerate(descriptor.data(),object.data(),args.data(),&list);}
bool RunPvs(){return PlayerChamsTestPvs(membership.data(),view.data(),object.data(),scene+off::PvsReturn);}
}
bool PlayerChamsMaterialsInitialize(PlayerChamsLogCallback) noexcept {return true;}
void PlayerChamsMaterialsOwnerFrame() noexcept {}
bool PlayerChamsMaterialsResolve(PlayerChamsStyle style,std::uintptr_t,std::uintptr_t& outputMaterial,std::uintptr_t& outputMode,bool occluded) noexcept {
    ++materialCalls;outputMaterial=material;outputMode=mode;
    lastOccluded=occluded;
    lastStyle=style;
    if(replaceIdentity)Put(bucket.data()+2*0x70,0x10,std::uint32_t{(5U<<15)|2});
    if(localTransition==1)Put(localPawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{3});
    if(localTransition==2)Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    if(localTransition==3)Put(localController.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(5U<<15)|4});
    return materialsReady;
}
void PlayerChamsMaterialsStop() noexcept {}
const char* PlayerChamsMaterialsStatus() noexcept {return "Fixture materials unavailable.";}

int main(){
    client=Address(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!client)return 2;
    Check(PlayerChamsOwnerHandle(UINT32_MAX,1)==UINT32_MAX,"invalid owner remains invalid");
    Check(PlayerChamsOwnerHandle((4U<<15)|2,0)==((4U<<15)|2),"ordinary serialized owner preserved");
    Check(PlayerChamsOwnerHandle((4U<<15)|2,1)==((3U<<15)|2),"native serial correction applied");
    Check(PlayerChamsOwnerHandle(2,1)==0xFFFF8002U,"native serial underflow wraps unsigned");
    Check(PlayerChamsOwnerHandle((4U<<15)|2,0xFE)==((4U<<15)|2),"unrelated flags preserve owner");
    Reset();auto before=list;before.fixedCount=1;list.fixedCount=2;
    float color[4]{1,0.5f,0,0.25f};std::size_t changed=0;
    Check(PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed)&&changed==1,"only new player packet changed");
    Check(!Replaced(packets[0])&&Replaced(packets[1]),"prior packet left untouched");
    Check(Get<std::uintptr_t>(packets[1].data(),0x28)==mode,"matching material mode tuple");
    Check(Get<std::uint32_t>(packets[1].data(),0x50)==0x400080FFU,"RGBA order and rounded opacity");
    const auto baseline=Baseline(Address(object.data()));
    for(unsigned i=0;i<0x70;++i)if(!((i>=0x20&&i<0x30)||(i>=0x50&&i<0x54)))Check(packets[1][i]==baseline[i],"mesh skinning streams flags and packet neighbors preserved");
    Reset();before=list;list.fixedCount=2;packets[1]=Baseline(Address(object.data())+1);
    Check(PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed)&&changed==1&&!Replaced(packets[1]),"foreign object packet excluded");
    Reset();before=list;list.fixedCount=1;Put(packets[0].data(),0x28,std::uintptr_t{0});
    Check(PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed)&&changed==0,"unrenderable packet excluded");
    Reset();before=list;list.fixedCount=3;list.overflowCount=2;
    Check(PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed)&&changed==5,"fixed and overflow new packet ranges");
    Reset();before=list;before.fixedCount=3;before.overflowCount=1;before.overflow=Address(oldOverflow.data());list.fixedCount=3;list.overflowCount=2;
    Check(PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed)&&changed==1&&!Replaced(overflow[0]),"overflow reallocation uses new storage");
    Reset();before=list;list.fixedCount=4;
    Check(!PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed),"packet count over capacity rejected");
    Reset();before=list;before.fixedCount=1;
    Check(!PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed),"count rewind rejected");
    Reset();before=list;list.fixed=UINTPTR_MAX;list.fixedCount=1;
    Check(!PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed),"wrapped storage rejected");
    Reset();before=list;list.overflowCount=1;
    Check(!PlayerChamsApplyNewDraws(before,list,Address(object.data()),material,mode,color,changed),"overflow before fixed capacity rejected");
    Setup();Run();Check(calls==1&&Replaced(packets[0]),"enemy player native hook replaces packet");
    Check(lastOccluded&&materialCalls==1,"through walls defaults to one depth-disabled material lookup");
    Check(list.fixedCount==1&&Get<std::uint32_t>(packets[0].data(),0x58)==0xA5A5A5A5U,"through walls keeps one draw and preserves native ordering key");
    Setup();auto depthSettings=PlayerChamsGetSettings();depthSettings.throughWalls=false;PlayerChamsSetSettings(depthSettings);Run();Check(Replaced(packets[0])&&!lastOccluded&&materialCalls==1&&list.fixedCount==1,"through walls off uses one native depth-enabled material");
    Setup();auto settings=PlayerChamsGetSettings();settings.enabled=false;PlayerChamsSetSettings(settings);Run();Check(calls==1&&!Replaced(packets[0]),"disabled original regenerates baseline");
    Setup();Put(descriptor.data(),0,std::uintptr_t{0});Run();Check(!Replaced(packets[0]),"foreign descriptor passthrough");
    Setup();Put(args.data(),0x10,std::uintptr_t{0});Run();Check(!Replaced(packets[0]),"missing view passthrough");
    Setup();Put(object.data(),off::OwnerHandle,std::uint32_t{(3U<<15)|2});Run();Check(!Replaced(packets[0]),"stale serialized scene owner rejected");
    Setup();Put(bucket.data()+2*0x70,0x30,std::uint32_t{1});Put(object.data(),off::OwnerHandle,std::uint32_t{(3U<<15)|2});Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(3U<<15)|2});Run();Check(Replaced(packets[0]),"corrected identity serial accepted");
    Setup();Put(object.data(),off::SkeletonInterface,Address(localSkeleton.data())+0x130);Run();Check(!Replaced(packets[0]),"other skeleton association rejected");
    Setup();Put(pawn.data(),0x10,Address(bucket.data()));Run();Check(!Replaced(packets[0]),"entity identity backreference required");
    Setup();Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(5U<<15)|2});Run();Check(!Replaced(packets[0]),"reciprocal full serialized pawn ownership required");
    Setup();Put(skeleton.data(),ent::CGameSceneNode::m_bDormant,std::uint8_t{1});Run();Check(!Replaced(packets[0]),"dormant player skipped");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_iHealth,int{0});Run();Check(!Replaced(packets[0]),"dead player skipped");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1});Run();Check(!Replaced(packets[0]),"life state required");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{2});Run();Check(!Replaced(packets[0]),"teammates disabled by default");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{2});settings=PlayerChamsGetSettings();settings.teammates=true;PlayerChamsSetSettings(settings);Run();Check(Replaced(packets[0]),"teammate opt in");
    Setup();Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});Run();Check(!Replaced(packets[0]),"inconsistent local controller claim rejected");
    Setup();Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerController,Address(controller.data()));Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,Address(pawn.data()));Run();Check(!Replaced(packets[0]),"local player disabled by default");
    Setup();Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerController,Address(controller.data()));Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,Address(pawn.data()));settings=PlayerChamsGetSettings();settings.localPlayer=true;PlayerChamsSetSettings(settings);Run();Check(Replaced(packets[0]),"local player opt in validates exact local pair");
    Setup();Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});Run();Check(!Replaced(packets[0]),"disconnected missing local player skipped");
    Setup();materialsReady=false;Run();Check(!Replaced(packets[0]),"unavailable material preserves baseline");
    Setup();emitDraw=false;Run();Check(calls==1 && !materialCalls && list.fixedCount==0 && !Replaced(packets[0]),"empty native generation does not invent geometry or resolve a material");
    emitDraw=true;Run();Check(calls==2 && materialCalls==1 && Replaced(packets[0]),"native submission resumes without retaining an empty generation");
    Setup();replaceIdentity=true;Run();Check(!Replaced(packets[0]),"identity revalidated after native material resolve");
    Setup();localTransition=1;Run();Check(!Replaced(packets[0]),"local team revalidated after native material resolve");
    Setup();localTransition=2;Run();Check(!Replaced(packets[0]),"local pawn revalidated after native material resolve");
    Setup();localTransition=3;Run();Check(!Replaced(packets[0]),"local serialized link revalidated after native material resolve");
    Setup();settings=PlayerChamsGetSettings();settings.style=PlayerChamsStyle::Iridescent;settings.iridescentColor[0]=0;settings.iridescentColor[1]=1;settings.iridescentColor[2]=0;settings.iridescentColor[3]=1;PlayerChamsSetSettings(settings);Run();Check(Replaced(packets[0])&&Get<std::uint32_t>(packets[0].data(),0x50)==0xFF00FF00U,"iridescent independent color applied");
    for (const bool throughWalls : {false,true}) {
        Setup();settings=PlayerChamsGetSettings();settings.style=PlayerChamsStyle::Fresnel;
        settings.fresnelColor[0]=0;settings.fresnelColor[1]=0.5f;settings.fresnelColor[2]=1;settings.fresnelColor[3]=0.25f;
        settings.throughWalls=throughWalls;PlayerChamsSetSettings(settings);Run();
        Check(Replaced(packets[0]) && lastStyle==PlayerChamsStyle::Fresnel && lastOccluded==throughWalls &&
            Get<std::uint32_t>(packets[0].data(),0x50)==0x40FF8000U,
            "fresnel independent RGBA and selected depth variant reach the native packet");
    }
    Setup();Check(RunPvs() && pvsCalls==1,"unseen eligible enemy passes PVS on its first native query");
    Run();Check(Replaced(packets[0]),"PVS-admitted player uses existing chams packet path");
    Setup();nativePvs=true;Put(object.data(),off::OwnerHandle,UINT32_MAX);
    Check(RunPvs() && pvsCalls==1,"native visible result preserved even for an ineligible object");
    Setup();Check(!PlayerChamsTestPvs(membership.data(),view.data(),reinterpret_cast<void*>(1),scene+0x3D84E) && pvsCalls==1,
        "other native PVS caller forwards without reading captured object");
    Setup();Put(descriptor.data(),0,std::uintptr_t{0});Check(!RunPvs(),"world and foreign descriptors retain native PVS");
    Setup();Put(object.data(),off::ObjectPvsData,Address(membership.data())+8);Check(!RunPvs(),"PVS membership must belong to captured object");
    Setup();Put(object.data(),off::OwnerHandle,std::uint32_t{(3U<<15)|2});Check(!RunPvs(),"PVS rejects stale serialized scene owner");
    Setup();Put(object.data(),off::SkeletonInterface,Address(localSkeleton.data())+off::SkeletonInterfaceMember);Check(!RunPvs(),"PVS rejects mismatched current skeleton");
    Setup();Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(5U<<15)|2});Check(!RunPvs(),"PVS requires reciprocal current pawn ownership");
    Setup();Put(skeleton.data(),ent::CGameSceneNode::m_bDormant,std::uint8_t{1});Check(!RunPvs(),"PVS preserves dormant-player exclusion");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_iHealth,int{0});Check(!RunPvs(),"PVS preserves dead-player exclusion");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1});Check(!RunPvs(),"PVS preserves life-state exclusion");
    Setup();Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});Check(!RunPvs(),"PVS preserves missing-local-source exclusion");
    Setup();settings=PlayerChamsGetSettings();settings.enabled=false;PlayerChamsSetSettings(settings);Check(!RunPvs(),"disabled chams preserve native PVS");
    settings.enabled=true;settings.throughWalls=false;PlayerChamsSetSettings(settings);Check(!RunPvs(),"depth-tested chams preserve native PVS");
    settings.throughWalls=true;PlayerChamsSetSettings(settings);Check(RunPvs(),"enabling through walls takes effect without a visibility-history delay");
    Setup();Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{2});Check(!RunPvs(),"unselected teammates preserve native PVS");
    settings=PlayerChamsGetSettings();settings.teammates=true;PlayerChamsSetSettings(settings);Check(RunPvs(),"selected teammates pass native PVS rejection");
    Setup();Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});Check(!RunPvs(),"PVS rejects inconsistent local-controller claim");
    Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerController,Address(controller.data()));Put(reinterpret_cast<void*>(client),ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    Check(!RunPvs(),"unselected local player preserves native PVS");
    settings=PlayerChamsGetSettings();settings.localPlayer=true;PlayerChamsSetSettings(settings);Check(RunPvs(),"selected exact local pair passes PVS rejection");
    Setup();replaceIdentity=true;Check(!RunPvs(),"PVS validates current identity after original returns");
    replaceIdentity=false;Put(object.data(),off::OwnerHandle,std::uint32_t{(5U<<15)|2});Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{(5U<<15)|2});
    Check(RunPvs(),"replacement pawn serial is eligible immediately without a first-seen cache");
    scene=Address(&PlayerChamsPvsFixtureReturn)-off::PvsReturn;Setup();
    Check(PlayerChamsPvsFixture(membership.data(),view.data(),object.data()) && pvsCalls==1,
        "production MASM bridge captures real caller RSI and return address with intact native arguments");
    scene=0x10000000;Setup();
    Check(!PlayerChamsPvsFixture(membership.data(),view.data(),object.data()) && pvsCalls==1,
        "production MASM bridge preserves false result at a different caller");
    Setup();entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool pendingPvs=true;
    std::thread pvsCallback([&]{pendingPvs=RunPvs();});Check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"pending native PVS entered");
    PlayerChamsStop("test PVS stop");SetEvent(resume);pvsCallback.join();Check(!pendingPvs,"cooperative stop disarms pending PVS result override");
    CloseHandle(entered);CloseHandle(resume);entered=resume=nullptr;
    Check(!RunPvs(),"stopped resident PVS callback preserves native rejection");
    Setup();entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread callback([]{Run();});Check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"pending native generation entered");
    PlayerChamsStop("test stop");SetEvent(resume);callback.join();Check(!Replaced(packets[0]),"cooperative stop disarms pending original callback mutation");
    CloseHandle(entered);CloseHandle(resume);entered=resume=nullptr;
    PlayerChamsActivate();list.fixedCount=0;Run();Check(!Replaced(packets[0])&&calls==2,"stopped resident cannot reactivate and preserves original");
    VirtualFree(reinterpret_cast<void*>(client),0,MEM_RELEASE);
    std::printf("%u player chams checks, %u failures\n",checks,failures);return failures?1:0;
}
