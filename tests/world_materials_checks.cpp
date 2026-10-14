#include "world_materials.h"
#include "world_material_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
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
WorldFlatMaterial flat{0x44440000,0x55550000,0x66660000,0x77770000};
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
    WorldSettings settings{}; settings.worldMaterialEnabled=true; settings.flatTextures=true;
    settings.worldTint[0]=0.5f; settings.worldTint[1]=0.25f; settings.worldTint[2]=0;
    return settings;
}
void Original(void*,void*,void*,void* pointer) {
    ++calls;
    if (entered) { SetEvent(entered); WaitForSingleObject(resume,INFINITE); }
    auto* drawList = static_cast<WorldDrawList*>(pointer);
    packets[0]=Baseline(object); drawList->fixedCount=1;
}
void SetupHook(std::uint64_t flags = 0) {
    Reset(); args.fill(0); desc.fill(0); view.fill(0); calls=0;
    Put(desc.data(),0,std::uintptr_t{0x10000000}+world_material_offsets::AggregateVtable);
    Put(args.data(),0x10,Address(view.data())); Put(view.data(),0x48,flags);
    WorldSetSettings(Settings()); WorldMaterialsTestConfigure(0x10000000,Original,flat);
}
bool Replaced(const Packet& packet) { return Get<std::uintptr_t>(packet.data(),0x20)==flat.material; }
}
int main() {
    Reset(); auto before=list; list.fixedCount=2; before.fixedCount=1;
    auto settings=Settings(); std::size_t changed=0;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==1,"only new owned packet changed");
    Check(!Replaced(packets[0]) && Replaced(packets[1]),"older packets untouched");
    Check(Get<std::uintptr_t>(packets[1].data(),0x28)==flat.mode,"native material and sorting tuple replaced");
    Check(Get<std::uint32_t>(packets[1].data(),0x50)==Get<std::uint32_t>(Baseline(object).data(),0x50),"unused packed RGBA and opacity preserved");
    Check(packets[1][0x1F]==Baseline(object)[0x1F] && packets[1][0x30]==0xA5,"packet neighbors preserved");
    Check((packets[1][0x62]&0x10)!=0,"native override flag enabled");
    Reset(); before=list; list.fixedCount=2; packets[1]=Baseline(object+1);
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==1 && !Replaced(packets[1]),"other object's packet excluded");
    Reset(); before=list; list.fixedCount=1; Put(packets[0].data(),0x20,std::uintptr_t{0});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==0,"unrenderable packets excluded");
    Reset(); before=list; list.fixedCount=3; list.overflowCount=2;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==5,"fixed and overflow appended ranges");
    Reset(); before=list; before.fixedCount=3; before.overflowCount=1; before.overflow=Address(oldOverflow.data());
    list.fixedCount=3; list.overflowCount=2;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==1 && !Replaced(overflow[0]),"overflow realloc uses current storage preserves old range");
    Reset(); before=list; list.fixedCount=4;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed),"capacity overflow rejected");
    Reset(); before=list; before.fixedCount=2; list.fixedCount=1;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed),"count rewind rejected");
    Reset(); before=list; list.fixedCount=1; list.fixed=0xFFFFFFFFFFFFFF00ULL;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed),"wrapped storage rejected");
    Reset(); before=list; list.overflowCount=1;
    Check(!WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed),"overflow before fixed capacity rejected");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{1});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && Get<std::uintptr_t>(packets[0].data(),0x48)==flat.type1Stream,"type one native stream replacement");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{2});
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && Get<std::uintptr_t>(packets[0].data(),0x48)==flat.type2Stream,"type two native stream replacement");
    Reset(); before=list; list.fixedCount=1; Put(mesh.data(),0x80,int{2}); auto missing=flat; missing.type2Stream=0;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,missing,changed) && changed==0,"missing required stream leaves original packet");
    Reset(); before=list; list.fixedCount=1;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,{},changed) && changed==0,"missing material tuple passes baseline unchanged");
    settings.flatTextures=false;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==0,"surface tint cannot pretend unused packet color is shader tint");
    settings=Settings(); settings.worldMaterialEnabled=false;
    Check(WorldMaterialsApplyNewDraws(before,list,object,settings,flat,changed) && changed==0 && !Replaced(packets[0]),"disabled baseline restoration on regeneration");
    SetupHook(); WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==1 && Replaced(packets[0]),"native view with flag24 clear remains eligible");
    SetupHook((1ULL<<24)|(1ULL<<32)); WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==1 && Replaced(packets[0]),"debug fallback flags do not classify native view eligibility");
    SetupHook(); Put(desc.data(),0,std::uintptr_t{0x99990000});
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==1 && !Replaced(packets[0]),"unknown descriptor passes through");
    SetupHook(); Put(args.data(),0x10,std::uintptr_t{0});
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(!Replaced(packets[0]),"unreadable actual view passes baseline");
    SetupHook(); entered=CreateEventW(nullptr,TRUE,FALSE,nullptr); resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread callback([] { WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list); });
    Check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"fake engine callback suspended");
    WorldMaterialsStop("standalone check"); SetEvent(resume); callback.join();
    Check(!Replaced(packets[0]),"stop disarms mutation before pending original returns");
    CloseHandle(entered); CloseHandle(resume); entered=resume=nullptr;
    list.fixedCount=0; WorldMaterialsActivate();
    WorldMaterialsTestGenerate(desc.data(),reinterpret_cast<void*>(object),args.data(),&list);
    Check(calls==2 && !Replaced(packets[0]),"stopped resident original continues cannot reactivate");
    std::printf("%u world materials checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
