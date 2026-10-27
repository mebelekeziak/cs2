#include "grenade_native_prediction.h"
#include "grenade_native_offsets.h"
#include "grenade_prediction_offsets.h"
#include "aim_weapon_offsets.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace {
namespace off = grenade_native_offsets;
template<class T> bool Read(const GrenadeNativeApi& api, std::uintptr_t at, T& out) noexcept {
    return api.copy(at, &out, sizeof(out));
}
bool Finite(EspVec3 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x)<1000000 && std::abs(p.y)<1000000 && std::abs(p.z)<1000000;
}
struct Identity { std::uintptr_t object=0, identity=0; std::uint32_t handle=UINT32_MAX; };
bool Same(const GrenadeNativeApi& api, const Identity& id) noexcept {
    std::uintptr_t table=0, identity=0, back=0; std::uint32_t handle=UINT32_MAX;
    return id.object && id.identity && id.handle!=UINT32_MAX &&
        Read(api,id.object,table) && table==api.client+off::TracerVtable &&
        Read(api,id.object+0x10,identity) && identity==id.identity &&
        Read(api,identity,back) && back==id.object &&
        Read(api,identity+0x10,handle) && handle==id.handle;
}
void* Create(const GrenadeNativeApi& api, int type, std::uintptr_t weapon) noexcept {
    __try { return api.create(type,reinterpret_cast<void*>(weapon),0.0f); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool Remove(const GrenadeNativeApi& api, std::uintptr_t object) noexcept {
    __try { api.remove(reinterpret_cast<void*>(object)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct OwnedTracer {
    const GrenadeNativeApi& api; Identity id; bool retired=false;
    bool Retire() noexcept {
        if (retired || !Same(api,id)) return false;
        retired=true; // A faulting removal is never retried through an uncertain entity.
        return Remove(api,id.object);
    }
    ~OwnedTracer() { if (!retired && Same(api,id)) Retire(); }
};
// The native routine temporarily changes these five GlobalVars slots. Its
// normal path restores them; __finally also covers an interrupted native call.
bool Simulate(const GrenadeNativeApi& api, const Identity& id, const GrenadeState& state,
    std::uintptr_t globals, const unsigned char* saved, EspVec3& endpoint) noexcept {
    bool ok=false;
    __try {
        __try {
            api.simulate(reinterpret_cast<void*>(id.object),&state.position,&state.velocity);
            if (Same(api,id)) {
                const auto* p=api.position(reinterpret_cast<void*>(id.object));
                ok=p && api.copy(reinterpret_cast<std::uintptr_t>(p),&endpoint,sizeof(endpoint));
            }
        } __finally {
            // Restore only the source used by the native call, never a new map's
            // globals. All writes are on the same engine owner as its caller.
            std::uintptr_t current=0;
            if (Read(api,api.client+off::Globals,current) && current==globals) {
                for (const auto at:off::ClockSlots)
                    std::memcpy(reinterpret_cast<void*>(globals+at),saved+at,sizeof(std::uint32_t));
            } else ok=false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    return ok && Finite(endpoint);
}
// CUtlVector<Vector> has a 24-byte descriptor; its capacity flags are not a
// point count. Only counts and pointers are consumed, without taking ownership.
struct NativeVector {
    std::int32_t count; std::uint32_t padding; std::uintptr_t data;
    std::int32_t capacity; std::uint32_t flags;
};
static_assert(sizeof(NativeVector)==24 && sizeof(EspVec3)==12);
bool Valid(const NativeVector& v, std::size_t limit) noexcept {
    return v.count>=0 && static_cast<std::size_t>(v.count)<=limit &&
        v.capacity>=v.count && (v.count==0 || v.data>=0x10000);
}
}

bool GrenadeNativePredict(const GrenadeNativeApi& api, std::uintptr_t weapon,
    const GrenadeState& state, GrenadePath& output) {
    output={};
    const auto kind=static_cast<unsigned>(state.kind);
    if (!api.client || !api.create || !api.simulate || !api.remove || !api.position || !api.copy ||
        kind>=std::size(off::KindTypes) || state.age!=0 || !Finite(state.position) || !Finite(state.velocity)) return false;
    // The native fire horizon uses this live value as its loop bound. Reject
    // nonfinite or extreme settings before entering a synchronous engine call.
    if (off::KindTypes[kind]==2) {
        std::uintptr_t storage=0; float fuse=0;
        if (!Read(api,api.client+grenade_prediction_offsets::MolotovFuse+aim_weapon_offsets::CvarValuePointer,storage) ||
            !storage || !Read(api,storage+aim_weapon_offsets::CvarValue,fuse) ||
            !std::isfinite(fuse) || fuse<0 || fuse>30) return false;
    }
    std::uintptr_t globals=0; unsigned char clock[off::ClockSnapshotSize]{};
    if (!Read(api,api.client+off::Globals,globals) || !globals || !api.copy(globals,clock,sizeof(clock))) return false;
    const auto object=reinterpret_cast<std::uintptr_t>(Create(api,off::KindTypes[kind],weapon));
    if (!object) return false;
    Identity id{object};
    if (!Read(api,object+0x10,id.identity) || !id.identity || !Read(api,id.identity+0x10,id.handle)) {
        // The native factory owns failure cleanup before returning. A returned
        // but unrecognizable object is never dispatched through guessed APIs.
        return false;
    }
    OwnedTracer owner{api,id};
    if (!Same(api,id)) return false;
    EspVec3 endpoint{};
    // Creation can invoke engine callbacks. Snapshot its current clock source
    // immediately before the synchronous simulator begins.
    if (!Read(api,api.client+off::Globals,globals) || !globals || !api.copy(globals,clock,sizeof(clock)) ||
        !Simulate(api,id,state,globals,clock,endpoint) || !Same(api,id)) return false;
    NativeVector segments{};
    if (!Read(api,object+off::Segments,segments) || !Valid(segments,128) || segments.count==0) return false;
    GrenadePath path; path.kind=state.kind;
    for (int i=0;i<segments.count;++i) {
        NativeVector points{};
        if (!Read(api,segments.data+static_cast<std::size_t>(i)*sizeof(points),points) ||
            !Valid(points,4095-path.nodes.size())) return false;
        for (int n=0;n<points.count;++n) {
            EspVec3 p{};
            if (!Read(api,points.data+static_cast<std::size_t>(n)*sizeof(p),p) || !Finite(p)) return false;
            // Native segments split at impacts. Point timestamps are not
            // exported; zero means unspecified for this backend.
            path.nodes.push_back({p,0,i>0 && n==0});
        }
    }
    if (path.nodes.empty() || !Same(api,id)) return false;
    path.nodes.push_back({endpoint,0,false});
    path.finalPosition=endpoint;
    path.end=GrenadeEnd::NativePreview;
    if (!owner.Retire()) return false;
    output=std::move(path);
    return true;
}
