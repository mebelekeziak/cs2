#include "grenade_prediction_runtime.h"
#include "grenade_prediction_tracker.h"
#include "grenade_prediction_offsets.h"
#include "cs2_offsets.h"
#include "aim_weapon_offsets.h"
#include "aim_shoot_origin_offsets.h"
#include "third_person_offsets.h"
#include "movement_jumpbug_offsets.h"
#include "movement_memory.h"
#include "client_frame.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdio>

namespace {
namespace off = grenade_prediction_offsets;
namespace ent = cs2_offsets;
namespace cam = third_person_offsets;
namespace weapon = aim_weapon_offsets;
using LaunchFn = bool (*)(void*,void*,EspVec3*,EspVec3*,bool);
using TimeFn = float* (*)(float*,int);
using VelocityFn = EspVec3* (*)(void*,EspVec3*);
using ShapeFn = void (*)(void*,const float*);
using ResultFn = void (*)(void*);
using FilterFn = void (*)(void*,void*,std::uint64_t,std::uint8_t,std::uint8_t);
using TraceFn = bool (*)(void*,void*,const void*,const void*,void*,void*);
std::uintptr_t client=0;
LaunchFn launch=nullptr;
TimeFn nativeTime=nullptr;
VelocityFn nativeVelocity=nullptr;
GrenadePredictionLogCallback logSink=nullptr;
ShapeFn shape=nullptr;
ResultFn result=nullptr;
FilterFn filter=nullptr;
TraceFn trace=nullptr;
bool prepared=false;
std::atomic<bool> active{false};
std::atomic<const char*> status{"grenade prediction not initialized"};
SRWLOCK ownerLock=SRWLOCK_INIT, publicationLock=SRWLOCK_INIT;
struct Entity {
    std::uintptr_t address=0, identity=0;
    std::uint32_t handle=UINT32_MAX;
    bool operator==(const Entity&) const = default;
};
struct Source {
    std::uintptr_t system=0;
    Entity pawn{}, controller{};
    std::uint8_t team=0;
    bool operator==(const Source&) const = default;
};
Source publishedSource{};
ULONGLONG capturedAt=0;
std::vector<GrenadePredictionVisual> published;
GrenadePredictionTracker tracker;
Source trackedSource{};
std::uint64_t frameSerial=0;
struct PreparedThrow {
    std::uint32_t weapon=UINT32_MAX;
    GrenadePath path;
    ULONGLONG sampledAt=0;
    std::uint64_t frame=0;
    float releaseTime=0;
    bool transferred=false, committed=false, capturedReleased=false;
};
PreparedThrow preparedThrow;
ULONGLONG reportedAt=0;
std::size_t candidateCursor=0;
std::uint64_t budgetFrequency=0;
#ifdef CS2_GRENADE_PREDICTION_CHECKS
bool testRuntime=false;
std::uint64_t (*testBudgetNowUs)()=nullptr;
#endif
bool PrepareBudgetClock() noexcept {
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0 ||
        static_cast<std::uint64_t>(frequency.QuadPart)>UINT64_MAX/1000000) return false;
    budgetFrequency=static_cast<std::uint64_t>(frequency.QuadPart); return true;
}
std::uint64_t BudgetNowUs() noexcept {
#ifdef CS2_GRENADE_PREDICTION_CHECKS
    if (testRuntime && testBudgetNowUs) return testBudgetNowUs();
#endif
    LARGE_INTEGER counter{};
    if (!budgetFrequency || !QueryPerformanceCounter(&counter) || counter.QuadPart<=0) return 0;
    const auto value=static_cast<std::uint64_t>(counter.QuadPart);
    const auto seconds=value/budgetFrequency;
    if (seconds>=UINT64_MAX/1000000) return 0;
    return seconds*1000000+(value%budgetFrequency)*1000000/budgetFrequency;
}
std::uint64_t BudgetDeadline() noexcept {
    const auto now=BudgetNowUs();
    return now && now<=UINT64_MAX-6000 ? now+6000 : 0;
}
bool BudgetExpired(std::uint64_t deadline) noexcept {
    const auto now=BudgetNowUs();
    return !deadline || !now || now>deadline;
}
template<class T> bool Read(std::uintptr_t at,T& value) noexcept { return MovementCopy(at,&value,sizeof(value)); }
template<std::size_t N> bool Match(std::uintptr_t at,const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(at,bytes,N) && !std::memcmp(bytes,expected,N);
}
bool Current() noexcept {
#ifdef CS2_GRENADE_PREDICTION_CHECKS
    if (testRuntime) return client!=0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"))==client;
}
bool Finite(EspVec3 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x)<1000000 && std::abs(p.y)<1000000 && std::abs(p.z)<1000000;
}
bool Resolve(std::uintptr_t system,std::uint32_t handle,Entity& out) noexcept {
    if (handle==UINT32_MAX) return false;
    const auto index=handle&ent::entity_layout::IndexMask;
    std::uintptr_t bucket=0,back=0;
    std::uint32_t actual=UINT32_MAX;
    if (!Read(system+ent::entity_layout::TableOffset+(index>>9)*sizeof(bucket),bucket) || !bucket) return false;
    out={0,bucket+(index&511)*ent::entity_layout::IdentityStride,handle};
    return Read(out.identity,out.address) && out.address &&
        Read(out.identity+ent::entity_layout::HandleOffset,actual) && actual==handle &&
        Read(out.address+ent::CEntityInstance::m_pEntity,back) && back==out.identity;
}
bool Identify(std::uintptr_t system,std::uintptr_t address,Entity& out) noexcept {
    std::uintptr_t identity=0;
    std::uint32_t handle=UINT32_MAX;
    return address && Read(address+ent::CEntityInstance::m_pEntity,identity) && identity &&
        Read(identity+ent::entity_layout::HandleOffset,handle) && Resolve(system,handle,out) && out.address==address;
}
bool Same(std::uintptr_t system,const Entity& entity) noexcept {
    Entity other;
    return Resolve(system,entity.handle,other) && other==entity;
}
bool Thrower(std::uintptr_t system,std::uintptr_t projectile,Entity& out) noexcept {
    // The installed native grenade getter uses m_hThrower, then m_hOwnerEntity.
    // Match its pawn-only association without retaining or calling an entity.
    for (const auto member : {off::Thrower,off::Owner}) {
        std::uint32_t handle=UINT32_MAX; std::uintptr_t table=0;
        if (Read(projectile+member,handle) && Resolve(system,handle,out) &&
            Read(out.address,table) && table==client+aim_shoot_origin_offsets::PawnVtable) return true;
    }
    return false;
}
bool Local(Source& s) noexcept {
    std::uintptr_t pawn=0,controller=0;
    std::uint32_t linked=UINT32_MAX;
    int health=0; std::uint8_t life=1;
    return Current() && Read(client+ent::globals::dwGameEntitySystem,s.system) && s.system &&
        Read(client+ent::globals::dwLocalPlayerPawn,pawn) && Identify(s.system,pawn,s.pawn) &&
        Read(client+ent::globals::dwLocalPlayerController,controller) && Identify(s.system,controller,s.controller) &&
        Read(controller+ent::CCSPlayerController::m_hPlayerPawn,linked) && linked==s.pawn.handle &&
        Read(pawn+ent::C_BaseEntity::m_iHealth,health) && health>0 &&
        Read(pawn+ent::C_BaseEntity::m_lifeState,life) && life==0 &&
        Read(pawn+ent::C_BaseEntity::m_iTeamNum,s.team) && (s.team==2 || s.team==3);
}
void Clear() noexcept {
    AcquireSRWLockExclusive(&publicationLock);
    published.clear(); capturedAt=0; publishedSource={};
    ReleaseSRWLockExclusive(&publicationLock);
}
void ResetOwner() noexcept {
    tracker.Clear(); preparedThrow={}; trackedSource={}; frameSerial=0;
    candidateCursor=0;
}
bool Clock(const Entity& entity,float& time) noexcept {
    int group=0;
    if (!Read(entity.identity+off::IdentityWorldGroup,group)) return false;
    // m_worldGroupId is a bit pattern, not a signed split-screen index.
    // The native getter accepts every nonzero value, including 0xFFFFFFFF.
    __try { if (nativeTime(&time,group)!=&time) return false; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::isfinite(time) && time>=0;
}
bool Velocity(const Entity& entity,EspVec3& value) noexcept {
    const float nan=std::numeric_limits<float>::quiet_NaN();
    value={nan,nan,nan};
    if (!nativeVelocity) return false;
    __try { if (nativeVelocity(reinterpret_cast<void*>(entity.address),&value)!=&value) return false; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return Finite(value);
}
bool Ended(const Entity& entity,GrenadeKind kind,bool& ended) noexcept {
    int tick=0; std::uint8_t effect=0;
    if (!Read(entity.address+off::ExplodeTick,tick) ||
        !Read(entity.address+off::ExplodeBegan,effect) || effect>1) return false;
    ended=tick!=0 || effect!=0;
    if (kind==GrenadeKind::Smoke) {
        int smokeTick=0; std::uint8_t smoke=0;
        if (!Read(entity.address+off::SmokeTickBegin,smokeTick) ||
            !Read(entity.address+off::SmokeDidEffect,smoke) || smoke>1) return false;
        ended=ended || smokeTick!=0 || smoke!=0;
    }
    // Successful Molotov ignition retires this projectile. The complete scan
    // and full-handle revalidation remove it without waiting for Inferno end.
    return true;
}
bool Launch(const Entity& held,const Entity& pawn,GrenadeState& s) noexcept {
    const float nan=std::numeric_limits<float>::quiet_NaN();
    s.position=s.velocity={nan,nan,nan};
    __try { launch(reinterpret_cast<void*>(held.address),reinterpret_cast<void*>(pawn.address),&s.position,&s.velocity,true); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    // Native bool describes stashed-source selection, not query success.
    return Finite(s.position) && Finite(s.velocity);
}
struct SweepContext {
    Source source{};
    Entity ignored{};
    unsigned* remaining=nullptr;
    ULONGLONG deadline=0;
};
template<class T> void Put(unsigned char* bytes,std::size_t offset,T value) noexcept { std::memcpy(bytes+offset,&value,sizeof(value)); }
bool Sweep(void* opaque,const EspVec3& start,const EspVec3& end,GrenadeTrace& hit) noexcept {
    auto& c=*static_cast<SweepContext*>(opaque);
    std::uintptr_t currentSystem=0,currentPawn=0;
    if (!active.load(std::memory_order_acquire) || !Current() || !*c.remaining || BudgetExpired(c.deadline) ||
        !Read(client+ent::globals::dwGameEntitySystem,currentSystem) || currentSystem!=c.source.system ||
        !Read(client+ent::globals::dwLocalPlayerPawn,currentPawn) || currentPawn!=c.source.pawn.address ||
        !Same(c.source.system,c.source.pawn) || !Same(c.source.system,c.ignored)) return false;
    --*c.remaining;
    std::uintptr_t manager=0;
    if (!Read(client+cam::TraceManager,manager) || !manager) return false;
    alignas(16) unsigned char shapeBytes[cam::ShapeSize]{};
    alignas(16) unsigned char resultBytes[cam::TraceSize]{};
    alignas(8) unsigned char filterBytes[cam::FilterSize]{};
    const float hull[]{-off::HullExtent,-off::HullExtent,-off::HullExtent,off::HullExtent,off::HullExtent,off::HullExtent};
    Put(filterBytes,0,client+cam::FilterVtable);
    for (std::size_t at : {0x20,0x24,0x28,0x2C}) Put(filterBytes,at,UINT32_MAX);
    Put(filterBytes,0x34,static_cast<std::uint16_t>(UINT16_MAX));
    filterBytes[0x37]=0x0F; filterBytes[0x38]=off::GrenadeLayer; filterBytes[0x39]=0x49;
    __try {
        filter(filterBytes,reinterpret_cast<void*>(c.ignored.address),off::GrenadeMask,off::GrenadeLayer,0x0F);
        Put(filterBytes,0x18,off::GrenadeInteractsAs);
        filterBytes[0x39]=off::GrenadeFlags39;
        shape(shapeBytes,hull); result(resultBytes);
        trace(reinterpret_cast<void*>(manager),shapeBytes,&start,&end,filterBytes,resultBytes);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    std::memcpy(&hit.fraction,resultBytes+cam::TraceFraction,sizeof(hit.fraction));
    std::memcpy(&hit.normal,resultBytes+movement_jumpbug_offsets::TraceNormal,sizeof(hit.normal));
    hit.startSolid=resultBytes[movement_jumpbug_offsets::TraceSolid]!=0;
    return true;
}
// Installed physics constants and timer/cvar readers are kept in the native
// contract header. The core receives copies and owns no engine references.
bool Physics(GrenadeKind kind,std::uintptr_t projectile,GrenadePhysics& p) noexcept {
    float gravity=0,scale=off::InitialGravityScale,elasticity=off::InitialElasticity;
    if (!MovementReadCvar(client+movement_jumpbug_offsets::Gravity,gravity) || !std::isfinite(gravity) || gravity<0 || gravity>10000)
        return false;
    if (!Read(client+movement_offsets::TickIntervalConstant,p.step) || !std::isfinite(p.step) || p.step<0.0001f || p.step>0.1f)
        return false;
    if (projectile && (!Read(projectile+off::ActualGravityScale,scale) || !Read(projectile+off::Elasticity,elasticity))) return false;
    if (!std::isfinite(scale) || scale<0 || scale>10 || !std::isfinite(elasticity) || elasticity<0 || elasticity>10) return false;
    p.gravity=gravity*scale;
    p.elasticity=std::clamp(elasticity,0.0f,off::MaximumElasticity);
    p.elasticityLimit=off::MaximumElasticity;
    p.overbounce=off::Overbounce; p.clipEpsilon=off::ClipEpsilon;
    p.groundNormalZ=off::GroundNormalZ; p.softGroundNormalZ=off::SoftGroundNormalZ;
    p.stopSpeed=off::StopSpeed;
    p.fastSpeed=std::sqrt(off::FastFloorSpeedSquared);
    p.fastNormalDot=off::FastFloorDotThreshold; p.fastDampingBias=1.5f;
    p.nativeStepGravity=true;
    p.collisionSkin=0.03125f; // Query-only numerical contact separation.
    switch (kind) {
    case GrenadeKind::HE: case GrenadeKind::Flash:
        p.fuseTime=kind==GrenadeKind::HE?off::HighExplosiveFuse:off::FlashFuse;
        p.thinkInterval=0.2f; p.strictFuse=true; break;
    case GrenadeKind::Smoke:
        p.minRestTime=off::SmokeMinimumAge; p.restSpeed=off::SmokeRestSpeed; break;
    case GrenadeKind::Decoy:
        p.firstThinkTime=off::DecoyFirstThink; p.thinkInterval=off::DecoyThinkInterval; p.restSpeed=off::DecoyRestSpeed; break;
    case GrenadeKind::Molotov: case GrenadeKind::Incendiary: {
        float slope=0;
        if (!MovementReadCvar(client+off::MolotovFuse,p.fuseTime) || !std::isfinite(p.fuseTime) || p.fuseTime<0 || p.fuseTime>30 ||
            !MovementReadCvar(client+off::MolotovMaxSlope,slope) || !std::isfinite(slope) || slope<0 || slope>90) return false;
        p.fireNormalZ=std::cos(slope*0.01745329251994329577f);
        p.strictFuse=true;
        break;
    }
    }
    return true;
}
bool Held(const Source& source,GrenadeState& state,Entity& entity) noexcept {
    std::uintptr_t services=0,table=0,owner=0;
    std::uint32_t handle=UINT32_MAX;
    if (!Read(source.pawn.address+weapon::WeaponServices,services) || !services ||
        !Read(services,table) || table!=client+weapon::ServicesVtable ||
        !Read(services+weapon::ServicePawn,owner) || owner!=source.pawn.address ||
        !Read(services+weapon::ActiveWeapon,handle) || !Resolve(source.system,handle,entity) ||
        !Read(entity.address,table)) return false;
    bool found=false;
    for (const auto& entry:off::WeaponTables) if (table==client+entry.rva) { state.kind=static_cast<GrenadeKind>(entry.kind); found=true; break; }
    if (!found) return false;
    std::uint8_t held=0,pin=0; float throwTime=0;
    if (!Read(entity.address+off::Held,held) || held!=1 || !Read(entity.address+off::PinPulled,pin) || pin>1 ||
        !Read(entity.address+off::ThrowTime,throwTime) || !std::isfinite(throwTime) || (!pin && throwTime<=0)) return false;
    return Launch(entity,source.pawn,state) && Same(source.system,entity);
}
struct ProjectileCandidate {
    Entity entity{};
    GrenadeState state{};
    bool enemy=false;
    float spawn=0;
};
struct FrameCounts {
    unsigned projectiles=0, local=0, enemy=0, ownership=0, state=0;
    unsigned input=0, clock=0, physics=0, trace=0, budget=0, transferred=0;
};
void HandoffPrepared(const std::vector<ProjectileCandidate>& observed,FrameCounts& counts) {
    if (!active.load(std::memory_order_acquire)) return;
    // Handoff needs a newly observed local projectile, not a usable physics
    // clock/scene snapshot. Those optional inputs must not discard a forecast
    // that was already drawn while holding this grenade.
    for (const auto& candidate:observed) {
        std::uint64_t firstFrame=0;
        if (candidate.enemy || preparedThrow.transferred || !preparedThrow.committed || !preparedThrow.sampledAt ||
            preparedThrow.path.kind!=candidate.state.kind || !tracker.FirstObservedFrame(candidate.entity.handle,firstFrame) ||
            firstFrame<preparedThrow.frame || (firstFrame==preparedThrow.frame && !preparedThrow.capturedReleased)) continue;
        auto path=preparedThrow.path;
        if (tracker.SetPath(candidate.entity.handle,std::move(path),true)) {
            preparedThrow.transferred=true; ++counts.transferred;
            if (logSink) {
                char message[224]{};
                std::snprintf(message,sizeof(message),
                    "GRENADE_PATH_TRANSFER revision=3 weapon=0x%x projectile=0x%x release_time=%.6f prepared_frame=%llu observed_frame=%llu",
                    preparedThrow.weapon,candidate.entity.handle,preparedThrow.releaseTime,
                    static_cast<unsigned long long>(preparedThrow.frame),static_cast<unsigned long long>(firstFrame));
                logSink(message);
            }
        }
    }
}
void OwnerFrame(void*,int) noexcept {
    if (!TryAcquireSRWLockExclusive(&ownerLock)) return;
    const auto settings=GrenadeGetSettings();
    Source source;
    const auto now=GetTickCount64();
    if (!active.load(std::memory_order_acquire) || (!settings.prediction && !settings.enemyPrediction) || !Local(source)) {
        ResetOwner(); Clear(); ReleaseSRWLockExclusive(&ownerLock); return;
    }
    if (capturedAt && publishedSource==source && now>=capturedAt && now-capturedAt<40) {
        ReleaseSRWLockExclusive(&ownerLock); return;
    }
    // A prediction-clock correction is not a grenade lifecycle transition.
    // Retained paths are owned by actual full handles and end markers.
    if (trackedSource!=source) ResetOwner();
    trackedSource=source;
    ++frameSerial;
    if (!settings.prediction) preparedThrow={};
    tracker.BeginFrame();
    FrameCounts counts;
    std::vector<ProjectileCandidate> candidates;
    std::vector<ProjectileCandidate> observed;
    candidates.reserve(GrenadePredictionTracker::MaxEntries);
    observed.reserve(GrenadePredictionTracker::MaxEntries);
    bool scanComplete=true;
    // Discovery and lifecycle checks are independent of the native trace budget.
    // Every live identity is visited even when no new simulation can run.
    for (unsigned b=0;b<ent::entity_layout::BucketCount;++b) {
        std::uintptr_t bucket=0;
        if (!Read(source.system+ent::entity_layout::TableOffset+b*sizeof(bucket),bucket)) { scanComplete=false; continue; }
        if (!bucket) continue;
        std::array<unsigned char,512*ent::entity_layout::IdentityStride> ids{};
        if (!MovementCopy(bucket,ids.data(),ids.size())) { scanComplete=false; continue; }
        for (unsigned i=0;i<512;++i) {
            std::uintptr_t address=0,table=0;
            std::uint32_t handle=UINT32_MAX;
            std::memcpy(&address,ids.data()+i*ent::entity_layout::IdentityStride,sizeof(address));
            std::memcpy(&handle,ids.data()+i*ent::entity_layout::IdentityStride+ent::entity_layout::HandleOffset,sizeof(handle));
            if (!address) continue;
            if (!Read(address,table)) {
                std::uint64_t firstFrame=0;
                if (tracker.FirstObservedFrame(handle,firstFrame)) scanComplete=false;
                continue;
            }
            GrenadeKind kind=GrenadeKind::HE; bool found=false;
            for (const auto& entry:off::ProjectileTables) if (table==client+entry.rva) {
                kind=static_cast<GrenadeKind>(entry.kind); found=true; break;
            }
            if (!found) continue;
            ++counts.projectiles;
            ProjectileCandidate candidate;
            if ((handle&ent::entity_layout::IndexMask)!=b*512+i || !Resolve(source.system,handle,candidate.entity) ||
                candidate.entity.address!=address) { scanComplete=false; ++counts.state; continue; }
            if (kind==GrenadeKind::Molotov) {
                std::uint8_t incendiary=0;
                if (!Read(address+off::IncendiaryFlag,incendiary) || incendiary>1) { scanComplete=false; ++counts.state; continue; }
                if (incendiary) kind=GrenadeKind::Incendiary;
            }
            bool ended=false;
            if (!Ended(candidate.entity,kind,ended)) { scanComplete=false; ++counts.state; continue; }
            if (ended) { tracker.End(handle); continue; }
            if (!tracker.MarkLive(handle,kind,frameSerial)) { ++counts.budget; continue; }
            GrenadeKind savedKind{}; bool savedEnemy=false;
            const bool known=tracker.Lookup(handle,savedKind,savedEnemy);
            if (known && savedKind!=kind) { tracker.End(handle); ++counts.state; continue; }
            Entity thrower;
            std::uint8_t team=0;
            if (Thrower(source.system,address,thrower) &&
                Read(thrower.address+ent::C_BaseEntity::m_iTeamNum,team) && (team==2 || team==3)) {
                if (thrower==source.pawn) candidate.enemy=false;
                else if (team!=source.team) candidate.enemy=true;
                else { tracker.End(handle); continue; }
                if (known && savedEnemy!=candidate.enemy) { tracker.End(handle); continue; }
            } else if (known) candidate.enemy=savedEnemy;
            else { ++counts.ownership; continue; }
            if (!(candidate.enemy?settings.enemyPrediction:settings.prediction)) { tracker.End(handle); continue; }
            candidate.state.kind=kind;
            if (!tracker.Observe(handle,kind,candidate.enemy,frameSerial)) { ++counts.budget; continue; }
            if (candidate.enemy) ++counts.enemy; else ++counts.local;
            observed.push_back(candidate);
            if (tracker.Complete(handle)) continue;
            std::uintptr_t scene=0;
            std::uint8_t dormant=1;
            float time=0;
            if (!Read(address+ent::C_BaseEntity::m_pGameSceneNode,scene) || !scene ||
                !Read(scene+ent::CGameSceneNode::m_bDormant,dormant) || dormant ||
                !Read(scene+ent::CGameSceneNode::m_vecAbsOrigin,candidate.state.position) || !Finite(candidate.state.position)) {
                ++counts.input; continue;
            }
            if (!Read(address+off::ProjectileSpawnTime,candidate.spawn) || !std::isfinite(candidate.spawn) || candidate.spawn<0 ||
                !Clock(candidate.entity,time) || time<candidate.spawn || time-candidate.spawn>120) { ++counts.clock; continue; }
            candidate.state.age=time-candidate.spawn;
            candidates.push_back(candidate);
        }
    }
    Source beforeQueries;
    if (!active.load(std::memory_order_acquire) || !Local(beforeQueries) || beforeQueries!=source) {
        ResetOwner(); Clear(); ReleaseSRWLockExclusive(&ownerLock); return;
    }
    // Save the full prepared forecast to the new local projectile, not to a
    // still-flying earlier grenade of the same type. All retained data is copied.
    if (preparedThrow.sampledAt && !preparedThrow.committed) {
        Entity previous; std::uint8_t pin=1; float throwTime=0;
        preparedThrow.committed=Resolve(source.system,preparedThrow.weapon,previous) &&
            Read(previous.address+off::PinPulled,pin) && pin==0 &&
            Read(previous.address+off::ThrowTime,throwTime) && std::isfinite(throwTime) && throwTime>0;
        if (preparedThrow.committed) preparedThrow.releaseTime=throwTime;
    }
    HandoffPrepared(observed,counts);
    GrenadeState heldState; Entity held;
    bool haveHeld=settings.prediction && Held(source,heldState,held);
    std::uint8_t heldPin=0; float heldThrowTime=0;
    if (haveHeld) {
        haveHeld=Read(held.address+off::PinPulled,heldPin) && heldPin<=1 &&
            Read(held.address+off::ThrowTime,heldThrowTime) && std::isfinite(heldThrowTime);
    }
    if (haveHeld && preparedThrow.transferred && preparedThrow.weapon==held.handle) {
        // A new positive throw time distinguishes quick throws on the same
        // inventory entity, even if no intervening pin-held frame was sampled.
        haveHeld=heldPin==1 || (heldThrowTime>0 && heldThrowTime!=preparedThrow.releaseTime);
    }
    unsigned traceBudget=4096;
    const auto deadline=BudgetDeadline();
    // New thrown predictions have priority. Completed paths need no further
    // native simulation and cannot starve subsequent enemy projectiles.
    for (std::size_t offset=0;offset<candidates.size();++offset) {
        const auto& candidate=candidates[(candidateCursor+offset)%candidates.size()];
        if (tracker.Complete(candidate.entity.handle)) continue;
        if (!active.load(std::memory_order_acquire)) break;
        if (!traceBudget || BudgetExpired(deadline)) { ++counts.budget; continue; }
        GrenadePhysics physics;
        if (!Physics(candidate.state.kind,candidate.entity.address,physics)) { ++counts.physics; continue; }
        if (physics.fuseTime>=0 && candidate.state.age>physics.fuseTime+std::max(physics.thinkInterval,physics.step)) {
            ++counts.clock; continue;
        }
        auto state=candidate.state;
        if (!Same(source.system,candidate.entity) || !Velocity(candidate.entity,state.velocity)) { ++counts.input; continue; }
        SweepContext sweep{source,candidate.entity,&traceBudget,deadline};
        auto path=GrenadeSimulate(state,physics,Sweep,&sweep);
        if (path.end==GrenadeEnd::TraceFailure || path.end==GrenadeEnd::Invalid) ++counts.trace;
        else if (active.load(std::memory_order_acquire) && Same(source.system,candidate.entity))
            tracker.SetPath(candidate.entity.handle,std::move(path));
    }
    if (!candidates.empty()) candidateCursor=(candidateCursor+1)%candidates.size();
    bool showPrepared=false;
    if (haveHeld && active.load(std::memory_order_acquire)) {
        const bool sameCommitted=preparedThrow.weapon==held.handle && preparedThrow.committed && heldPin==0 &&
            heldThrowTime==preparedThrow.releaseTime;
        if (sameCommitted) showPrepared=!preparedThrow.transferred;
        GrenadePhysics physics;
        if (!sameCommitted && Physics(heldState.kind,0,physics) && traceBudget && !BudgetExpired(deadline)) {
            SweepContext sweep{source,source.pawn,&traceBudget,deadline};
            auto path=GrenadeSimulate(heldState,physics,Sweep,&sweep);
            if (active.load(std::memory_order_acquire) && path.end!=GrenadeEnd::Invalid && path.end!=GrenadeEnd::TraceFailure &&
                Same(source.system,held)) {
                const bool committed=heldPin==0 && heldThrowTime>0;
                preparedThrow={held.handle,std::move(path),now,frameSerial,committed?heldThrowTime:0,
                    false,committed,committed}; showPrepared=true;
            }
        }
        if (!showPrepared && preparedThrow.weapon==held.handle && preparedThrow.path.kind==heldState.kind &&
            now>=preparedThrow.sampledAt && now-preparedThrow.sampledAt<=150) showPrepared=true;
    } else if (settings.prediction && !preparedThrow.transferred && preparedThrow.sampledAt) {
        // Cover the native release animation until the corresponding projectile
        // is visible. A cancelled/switch-away preview is not a committed throw.
        Entity previous;
        std::uint8_t pin=1; float throwTime=0;
        showPrepared=Resolve(source.system,preparedThrow.weapon,previous) &&
            Read(previous.address+off::PinPulled,pin) && pin==0 &&
            Read(previous.address+off::ThrowTime,throwTime) && std::isfinite(throwTime) &&
            throwTime>0 && throwTime==preparedThrow.releaseTime;
    }
    // A newly captured released preview can meet its projectile in this same
    // owner frame. Merge it before drawing instead of showing two paths.
    HandoffPrepared(observed,counts);
    // Native queries may dispatch callbacks. Recheck lifecycle after them,
    // including complete paths that did not need a new trace this frame.
    for (const auto& candidate:observed) {
        bool ended=false;
        if (!Same(source.system,candidate.entity)) tracker.End(candidate.entity.handle);
        else if (Ended(candidate.entity,candidate.state.kind,ended)) {
            if (ended) tracker.End(candidate.entity.handle);
        } else scanComplete=false;
    }
    Source after;
    if (active.load(std::memory_order_acquire) && Local(after) && source==after && scanComplete) {
        tracker.EndFrame();
        auto next=tracker.Capture(settings.prediction,settings.enemyPrediction);
        if (showPrepared && !preparedThrow.transferred) next.insert(next.begin(),{preparedThrow.path,false});
        const auto retainedPaths=next.size();
        AcquireSRWLockExclusive(&publicationLock);
        published=std::move(next); publishedSource=source; capturedAt=now;
        ReleaseSRWLockExclusive(&publicationLock);
        status.store("grenade paths retained until detonation/deployment",std::memory_order_release);
        if (logSink && (!reportedAt || now-reportedAt>=2000)) {
            char message[448]{};
            std::snprintf(message,sizeof(message),
                "GRENADE_PREDICTION revision=3 projectiles=%u local=%u enemy=%u retained=%zu ownership_reject=%u state_reject=%u "
                "input_reject=%u clock_reject=%u physics_reject=%u trace_reject=%u budget_deferred=%u prepared_transfers=%u",
                counts.projectiles,counts.local,counts.enemy,retainedPaths,counts.ownership,counts.state,
                counts.input,counts.clock,counts.physics,counts.trace,counts.budget,counts.transferred);
            logSink(message); reportedAt=now;
        }
    } else if (!active.load(std::memory_order_acquire) || !Local(after) || source!=after) {
        ResetOwner(); Clear();
    }
    ReleaseSRWLockExclusive(&ownerLock);
}
}

bool GrenadePredictionPrepare(GrenadePredictionLogCallback logger) noexcept {
    logSink=logger;
    if (prepared) return true;
    client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!PrepareBudgetClock() || !Current() || !Read(client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(client+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage!=ent::ClientImageSize || !Match(client+off::Launch,off::LaunchBytes) ||
        !Match(client+off::GetAbsVelocity,off::GetAbsVelocityBytes) ||
        !Match(client+weapon::CurrentTime,weapon::CurrentTimeBytes) || !Match(client+cam::InitializeShape,cam::InitializeShapeBytes) ||
        !Match(client+cam::InitializeTrace,cam::InitializeTraceBytes) || !Match(client+cam::PopulateFilter,cam::PopulateFilterBytes) ||
        !Match(client+cam::TraceShape,cam::TraceShapeBytes) || !ClientFrameSubscribe(OwnerFrame,logger)) {
        status.store("grenade prediction native contract unavailable",std::memory_order_release); return false;
    }
    launch=reinterpret_cast<LaunchFn>(client+off::Launch);
    nativeTime=reinterpret_cast<TimeFn>(client+weapon::CurrentTime);
    nativeVelocity=reinterpret_cast<VelocityFn>(client+off::GetAbsVelocity);
    shape=reinterpret_cast<ShapeFn>(client+cam::InitializeShape);
    result=reinterpret_cast<ResultFn>(client+cam::InitializeTrace);
    filter=reinterpret_cast<FilterFn>(client+cam::PopulateFilter);
    trace=reinterpret_cast<TraceFn>(client+cam::TraceShape);
    prepared=true;
    status.store("grenade prediction ready",std::memory_order_release);
    if (logger) logger("GRENADE_PREDICTION_READY revision=3 per-throw release tokens; observed full-handle handoff independent of physics clock; retained to actual end");
    return true;
}
void GrenadePredictionActivate() noexcept { if (prepared) active.store(true,std::memory_order_release); }
void GrenadePredictionStop() noexcept {
    active.store(false,std::memory_order_release);
    // A native trace may pump a stop on this same frame owner. Disarm now;
    // its publication check rejects the in-flight result without self-deadlock.
    if (!ClientFrameInCallback()) AcquireSRWLockExclusive(&ownerLock);
    ResetOwner();
    Clear();
    status.store("grenade prediction stopped and resident",std::memory_order_release);
    if (!ClientFrameInCallback()) ReleaseSRWLockExclusive(&ownerLock);
}
const char* GrenadePredictionStatus() noexcept { return status.load(std::memory_order_acquire); }
namespace {
std::vector<GrenadePredictionVisual> Capture() {
    std::vector<GrenadePredictionVisual> paths;
    Source source;
    if (!active.load(std::memory_order_acquire) || !Local(source)) return paths;
    AcquireSRWLockShared(&publicationLock);
    const auto now=GetTickCount64();
    if (capturedAt && now>=capturedAt && now-capturedAt<=150 && source==publishedSource) paths=published;
    ReleaseSRWLockShared(&publicationLock);
    return paths;
}
}
void GrenadePredictionRender(const EspMatrix4x4& matrix,bool valid,float width,float height) {
    if (!valid || !active.load(std::memory_order_acquire)) return;
    const auto paths=Capture();
    GrenadePredictionDraw(paths,matrix,width,height,GrenadeGetSettings());
}
#ifdef CS2_GRENADE_PREDICTION_CHECKS
void GrenadePredictionTestConfigure(std::uintptr_t base,const GrenadePredictionTestNative& native) noexcept {
    GrenadePredictionStop(); client=base; testRuntime=true; prepared=true;
    launch=native.launch; nativeTime=native.time; shape=native.shape;
    result=native.result; filter=native.filter; trace=native.trace;
    nativeVelocity=native.velocity; testBudgetNowUs=native.budgetNowUs; reportedAt=0;
    PrepareBudgetClock();
    GrenadePredictionActivate();
}
void GrenadePredictionTestFrame() noexcept { capturedAt=0; OwnerFrame(nullptr,8); }
std::vector<GrenadePredictionVisual> GrenadePredictionTestCapture() { return Capture(); }
#endif
