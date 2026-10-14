#include "world_materials.h"
#include "world_material_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
unsigned failures = 0, checks = 0;
void Check(bool value, const char* name) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", name); } }
using Packet = std::array<unsigned char,0x70>;
std::array<Packet,3> packets{}, overflow{}, oldOverflow{};
std::array<unsigned char,0x90> mesh{};
std::array<unsigned char,0x20> args{}, desc{};
std::array<unsigned char,0x60> view{};
std::uintptr_t object = 0x11110000;
WorldDrawList list{};
unsigned calls = 0;
HANDLE entered = nullptr, resume = nullptr;
std::uintptr_t Address(void* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }
template<class T> void Put(void* pointer, std::size_t at, T value) { std::memcpy(static_cast<unsigned char*>(pointer)+at,&value,sizeof(value)); }
template<class T> T Get(const void* pointer, std::size_t at) { T value{}; std::memcpy(&value,static_cast<const unsigned char*>(pointer)+at,sizeof(value)); return value; }
Packet Baseline(std::uintptr_t source) {
    Packet packet{}; packet.fill(0xA5);
    Put(packet.data(),0x18,source); Put(packet.data(),8,Address(mesh.data()));
    Put(packet.data(),0x20,std::uintptr_t{0x22220000}); Put(packet.data(),0x28,std::uintptr_t{0x33330000});
    packet[0x50]=200; packet[0x51]=100; packet[0x52]=50; packet[0x53]=71;
    return packet;
}
void Reset() {
    for (auto& packet : packets) packet = Baseline(object);
    for (auto& packet : overflow) packet = Baseline(object);
    Put(mesh.data(),0x80,int{0});
    list = {}; list.fixed=Address(packets.data()); list.fixedCapacity=3;
    list.overflow=Address(overflow.data()); list.overflowCapacity=3;
}
WorldSettings Settings() {
    WorldSettings settings{}; settings.worldMaterialEnabled=true;
    settings.worldTint[0]=0.5f; settings.worldTint[1]=0.25f; settings.worldTint[2]=0;
    return settings;
}
void Original(void*,void*,void*,void* pointer) {
    ++calls;
    if (entered) { SetEvent(entered); WaitForSingleObject(resume,INFINITE); }
    auto* drawList = static_cast<WorldDrawList*>(pointer);
    packets[0]=Baseline(object); drawList->fixedCount=1;
}
void SetupHook() {
    Reset(); args.fill(0); desc.fill(0); view.fill(0); calls=0;
    Put(desc.data(),0,std::uintptr_t{0x10000000}+world_material_offsets::AggregateVtable);
    Put(args.data(),0x10,Address(view.data())); Put(view.data(),0x48,std::uint64_t{1ULL<<24});
    WorldSetSettings(Settings()); WorldMaterialsTestConfigure(0x10000000,Original);
}
}
int main() {
    Reset(); auto before=list; list.fixedCount=2;
    before.fixedCount=1; packets[0][0x50]=17;
    auto settings=Settings(); std::size_t changed=0;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==1,"only one new owned packet changed");
    Check(packets[0][0x50]==17,"existing packet preserved");
    Check(packets[1][0x50]==100 && packets[1][0x51]==25 && packets[1][0x52]==0,"native packed tint channels");
    Check(packets[1][0x53]==71,"alpha preserved");
    Check(packets[1][0x4F]==0xA5 && packets[1][0x54]==0xA5,"neighboring fields preserved");
    Check(Get<std::uintptr_t>(packets[1].data(),0x20)==0x22220000,"tint retains material");
    Reset(); before=list; list.fixedCount=2; packets[1]=Baseline(object+1);
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==1 && packets[1][0x50]==200,"other object's packet excluded");
    Reset(); before=list; list.fixedCount=1; Put(packets[0].data(),0x20,std::uintptr_t{0});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==0,"unrenderable packets excluded");
    Reset(); before=list; list.fixedCount=3; list.overflowCount=2;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==5,"fixed and overflow appended ranges");
    Reset(); before=list; before.fixedCount=3; before.overflowCount=1; before.overflow=Address(oldOverflow.data());
    list.fixedCount=3; list.overflowCount=2;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==1 && overflow[0][0x50]==200,"overflow realloc uses current storage preserves old range");
    Reset(); before=list; list.fixedCount=4;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed),"capacity overflow rejected");
    Reset(); before=list; before.fixedCount=2; list.fixedCount=1;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed),"count rewind rejected");
    Reset(); before=list; list.fixedCount=1; list.fixed=0xFFFFFFFFFFFFFF00ULL;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed),"wrapped storage rejected");
    Reset(); before=list; list.overflowCount=1;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed),"overflow before fixed capacity rejected");
    Reset(); before=list; list.fixedCount=1; settings.worldTint[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && packets[0][0x50]==200,"invalid tint no mutation");
    settings=Settings(); settings.flatTextures=true;
    WorldFlatMaterial flat{0x44440000,0x55550000,0x66660000,0x77770000};
    Reset(); before=list; list.fixedCount=1;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==1,"native flat material applied");
    Check(Get<std::uintptr_t>(packets[0].data(),0x20)==flat.material && Get<std::uintptr_t>(packets[0].data(),0x28)==flat.mode,"material and mode tuple replaced");
    Check((packets[0][0x62]&0x10) && packets[0][0x53]==71,"native override flag and original opacity");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{1});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && Get<std::uintptr_t>(packets[0].data(),0x48)==flat.type1Stream,"type one native stream replacement");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{2});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && Get<std::uintptr_t>(packets[0].data(),0x48)==flat.type2Stream,"type two native stream replacement");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{2}); flat.type2Stream=0;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==0,"missing required stream leaves original packet");
    Reset(); before=list; list.fixedCount=1; settings.worldMaterialEnabled=false;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==0 && packets[0][0x50]==200,"disabled baseline restoration on regeneration");
    SetupHook(); WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==1 && packets[0][0x50]==100,"original precedes native world mutation");
    SetupHook(); Put(desc.data(),0,std::uintptr_t{0x99990000});
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==1 && packets[0][0x50]==200,"unknown descriptor passes through");
    SetupHook(); Put(view.data(),0x48,std::uint64_t{(1ULL<<24)|(1ULL<<32)});
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(packets[0][0x50]==200,"excluded view follows native flag gate");
    SetupHook(); entered=CreateEventW(nullptr,TRUE,FALSE,nullptr); resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread callback([] { WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list); });
    Check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"fake engine callback suspended");
    WorldMaterialsStop("standalone check"); SetEvent(resume); callback.join();
    Check(packets[0][0x50]==200,"stop disarms mutation before pending original returns");
    CloseHandle(entered); CloseHandle(resume); entered=resume=nullptr;
    list.fixedCount=0; WorldMaterialsActivate();
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==2 && packets[0][0x50]==200,"stopped resident hook original continues cannot reactivate");
    std::printf("%u world materials checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
