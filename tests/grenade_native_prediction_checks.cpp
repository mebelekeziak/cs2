#include "grenade_native_prediction.h"
#include "grenade_native_offsets.h"
#include "grenade_prediction_offsets.h"
#include "aim_weapon_offsets.h"
#include "movement_memory.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
namespace off=grenade_native_offsets;
int checks=0,failures=0;
void Check(bool ok,const char* label) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n",label); } }
std::uintptr_t At(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template<class T> void Put(void* p,std::size_t at,T value) { std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value)); }
struct Vector { int count=0,pad=0; std::uintptr_t data=0; int capacity=0,flags=0; };
enum class Fault { None, Create, Simulate, Retired, Reused, WrongTable, Negative, Oversize, Capacity, NullData, Nonfinite, Empty, BadEndpoint, ChangedGlobals, Remove };
struct Fixture {
    std::vector<unsigned char> client=std::vector<unsigned char>(0x29a0000);
    std::array<unsigned char,0x1100> object{};
    std::array<unsigned char,0x70> identity{};
    std::array<unsigned char,off::ClockSnapshotSize> globals{},otherGlobals{};
    std::array<unsigned char,0x60> fuseData{};
    std::array<Vector,2> segments{};
    std::array<EspVec3,3> first{{{1,2,3},{4,5,6},{7,8,9}}};
    std::array<EspVec3,2> second{{{7,8,9},{11,12,13}}};
    EspVec3 endpoint{14,15,16};
    int creates=0,simulates=0,removes=0,type=-1;
    bool args=true; Fault fault=Fault::None;
    GrenadeState state{}; GrenadeNativeApi api{};
    void Reset();
}* f=nullptr;
void* Create(int type,void* weapon,float duration) {
    ++f->creates; f->type=type; f->args &= At(weapon)==0x123456 && duration==0;
    return f->fault==Fault::Create?nullptr:f->object.data();
}
void Simulate(void* object,const EspVec3* p,const EspVec3* v) {
    ++f->simulates; f->args &= object==f->object.data() && p->x==f->state.position.x &&
        v->x==f->state.velocity.x && v->y==f->state.velocity.y && v->z==f->state.velocity.z;
    for (auto at:off::ClockSlots) Put(f->globals.data(),at,std::uint32_t{0xdeadbeef});
    if (f->fault==Fault::Simulate) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    if (f->fault==Fault::Retired) Put(f->identity.data(),0,std::uintptr_t{0});
    if (f->fault==Fault::Reused) Put(f->identity.data(),0x10,std::uint32_t{99});
    if (f->fault==Fault::ChangedGlobals) Put(f->client.data(),off::Globals,At(f->otherGlobals.data()));
}
void Remove(void* object) {
    ++f->removes; f->args &= object==f->object.data();
    if (f->fault==Fault::Remove) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
}
const EspVec3* Position(void*) { return &f->endpoint; }
void Fixture::Reset() {
    f=this; object.fill(0); identity.fill(0); globals.fill(0x23); otherGlobals.fill(0x56);
    endpoint={14,15,16}; creates=simulates=removes=0; type=-1; args=true; fault=Fault::None;
    first={{{1,2,3},{4,5,6},{7,8,9}}}; second={{{7,8,9},{11,12,13}}};
    state={GrenadeKind::HE,{1,2,3},{3500,-2400,1600},0};
    Put(client.data(),off::Globals,At(globals.data()));
    Put(client.data(),grenade_prediction_offsets::MolotovFuse+aim_weapon_offsets::CvarValuePointer,At(fuseData.data()));
    Put(fuseData.data(),aim_weapon_offsets::CvarValue,float{2});
    Put(object.data(),0,At(client.data())+off::TracerVtable); Put(object.data(),0x10,At(identity.data()));
    Put(identity.data(),0,At(object.data())); Put(identity.data(),0x10,std::uint32_t{7});
    segments={Vector{3,0,At(first.data()),3,0},Vector{2,0,At(second.data()),2,0}};
    Put(object.data(),off::Segments,Vector{2,0,At(segments.data()),2,0});
    api={At(client.data()),Create,Simulate,Remove,Position,MovementCopy};
}
}
int main() {
    Fixture data; GrenadePath path;
    for (unsigned k=0;k<6;++k) {
        data.Reset(); data.state.kind=static_cast<GrenadeKind>(k); const auto clock=data.globals;
        Check(GrenadeNativePredict(data.api,0x123456,data.state,path),"all six kinds support native prediction");
        Check(data.type==off::KindTypes[k] && data.args && data.creates==1 && data.simulates==1 && data.removes==1,
            "factory ABI, aggressive launch vectors and exactly-once cleanup");
        Check(data.globals==clock,"native clock changes are restored");
        Check(path.end==GrenadeEnd::NativePreview && path.kind==data.state.kind && path.nodes.size()==6 &&
            path.nodes[3].impact && !path.nodes[0].impact && path.finalPosition.x==14 && path.nodes.back().position.z==16,
            "native segment boundaries and exact endpoint are copied");
        data.first[0].x=999;
        Check(path.nodes.front().position.x==1,"published vectors own their storage after native retirement");
    }
    for (auto fault:{Fault::Create,Fault::Simulate,Fault::Retired,Fault::Reused,Fault::WrongTable,Fault::Negative,
        Fault::Oversize,Fault::Capacity,Fault::NullData,Fault::Nonfinite,Fault::Empty,Fault::BadEndpoint,Fault::ChangedGlobals,Fault::Remove}) {
        data.Reset(); data.fault=fault; const auto clock=data.globals; const auto other=data.otherGlobals;
        if (fault==Fault::WrongTable) Put(data.object.data(),0,std::uintptr_t{1});
        if (fault==Fault::Negative) data.segments[0].count=-1;
        if (fault==Fault::Oversize) data.segments[0].count=data.segments[0].capacity=4096;
        if (fault==Fault::Capacity) data.segments[0].capacity=1;
        if (fault==Fault::NullData) data.segments[0].data=0;
        if (fault==Fault::Nonfinite) data.first[1].y=std::numeric_limits<float>::quiet_NaN();
        if (fault==Fault::Empty) data.segments[0].count=data.segments[1].count=0;
        if (fault==Fault::BadEndpoint) data.endpoint.z=std::numeric_limits<float>::infinity();
        path.nodes.push_back({{999,999,999},0,false});
        Check(!GrenadeNativePredict(data.api,0x123456,data.state,path) && path.nodes.empty() && !path.Terminal(),
            "native failure or malformed output never publishes an endpoint");
        const bool cannotOwn=fault==Fault::Create || fault==Fault::Retired || fault==Fault::Reused || fault==Fault::WrongTable;
        Check(data.removes==(cannotOwn?0:1),"cleanup only removes the exact owned live identity");
        Check(fault==Fault::ChangedGlobals?data.otherGlobals==other:data.globals==clock,
            "fault paths restore clock slots without writing another lifecycle's globals");
    }
    data.Reset(); data.state.velocity.x=std::numeric_limits<float>::quiet_NaN();
    Check(!GrenadeNativePredict(data.api,0x123456,data.state,path) && data.creates==0,"invalid launch never creates a tracer");
    data.Reset(); data.state.age=1;
    Check(!GrenadeNativePredict(data.api,0x123456,data.state,path) && data.creates==0,"native full-throw API rejects unsupported partial-flight input");
    data.Reset(); data.api.remove=nullptr;
    Check(!GrenadeNativePredict(data.api,0x123456,data.state,path) && data.creates==0,"missing cleanup contract prevents allocation");
    for (float fuse:{-1.0f,31.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        data.Reset(); data.state.kind=GrenadeKind::Molotov;
        Put(data.fuseData.data(),aim_weapon_offsets::CvarValue,fuse);
        Check(!GrenadeNativePredict(data.api,0x123456,data.state,path) && data.creates==0,
            "unbounded or invalid native fire horizon is rejected before simulation");
    }
    std::printf("Native grenade prediction: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
