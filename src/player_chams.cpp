#include "player_chams.h"
#include "player_chams_offsets.h"
#include "player_chams_materials.h"
#include "cs2_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off=player_chams_offsets;
namespace ent=cs2_offsets;
using GenerateFn=void (*)(void*,void*,void*,void*);
GenerateFn original=nullptr;
std::uintptr_t client=0,scene=0;
std::atomic<bool> active{false},stopped{false};
SRWLOCK mutationLock=SRWLOCK_INIT;
PlayerChamsLogCallback logger=nullptr;
std::atomic<const char*> status{"Player chams not initialized."};
std::atomic<unsigned long long> callbacks{0},changedPackets{0},rejections{0};
ULONGLONG lastReport=0;
unsigned long long lastCalls=0,lastChanges=0,lastRejections=0;
#ifdef CS2_PLAYER_CHAMS_CHECKS
bool testRuntime=false;
#endif

bool Copy(std::uintptr_t at,void* destination,std::size_t count) noexcept {
    if (!destination || at<0x10000 || at>0x00007fffffffffffULL-count) return false;
    __try {std::memcpy(destination,reinterpret_cast<const void*>(at),count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool Read(std::uintptr_t at,T& value) noexcept {return Copy(at,&value,sizeof(value));}
bool Write(std::uintptr_t at,const void* source,std::size_t count) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(at),source,count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ValidList(const WorldDrawList& list) noexcept {
    constexpr int limit=65536;
    constexpr std::uintptr_t max=0x00007fffffffffffULL;
    return list.fixedCapacity>=0 && list.fixedCapacity<=limit && list.fixedCount>=0 &&
        list.fixedCount<=list.fixedCapacity && list.overflowCapacity>=0 && list.overflowCapacity<=limit &&
        list.overflowCount>=0 && list.overflowCount<=list.overflowCapacity &&
        (!list.fixedCapacity || (list.fixed>=0x10000 && list.fixed<=max-list.fixedCapacity*off::PacketSize)) &&
        (!list.overflowCapacity || (list.overflow>=0x10000 && list.overflow<=max-list.overflowCapacity*off::PacketSize));
}
bool Image(std::uintptr_t base,std::uint32_t timestamp,std::uint32_t size) noexcept {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    return base && Read(base,dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<0x100000 &&
        Read(base+dos.e_lfanew,nt) && nt.Signature==IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp==timestamp && nt.OptionalHeader.SizeOfImage==size;
}
bool CurrentModules() noexcept {
#ifdef CS2_PLAYER_CHAMS_CHECKS
    if (testRuntime) return client && scene;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"))==client &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"))==scene;
}
struct Identity {
    std::uintptr_t entity=0,address=0;
    std::uint32_t raw=UINT32_MAX,flags=0,handle=UINT32_MAX;
    bool operator==(const Identity&) const=default;
};
bool Resolve(std::uintptr_t system,std::uint32_t handle,Identity& out) noexcept {
    if (!system || handle==UINT32_MAX) return false;
    const auto index=handle&ent::entity_layout::IndexMask;
    std::uintptr_t bucket=0,back=0;
    if (!Read(system+ent::entity_layout::TableOffset+(index>>ent::entity_layout::BucketShift)*sizeof(std::uintptr_t),bucket) || !bucket) return false;
    out.address=bucket+(index&ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride;
    if (!Read(out.address,out.entity) || !out.entity || !Read(out.address+ent::entity_layout::HandleOffset,out.raw) ||
        !Read(out.address+ent::CEntityIdentity::m_flags,out.flags) ||
        !Read(out.entity+ent::CEntityInstance::m_pEntity,back) || back!=out.address) return false;
    out.handle=PlayerChamsOwnerHandle(out.raw,out.flags);
    return out.handle==handle;
}
struct Source {
    std::uintptr_t system=0,skeleton=0;
    Identity pawn{},controller{};
    std::uint8_t team=0;
    bool local=false;
    bool operator==(const Source&) const=default;
};
bool ReadSource(std::uintptr_t object,Source& out) noexcept {
    std::uint32_t owner=UINT32_MAX,controller=UINT32_MAX,fallback=UINT32_MAX,link=UINT32_MAX;
    std::uintptr_t table=0,skeletonInterface=0;
    int health=0;
    std::uint8_t life=1,dormant=1,local=0;
    if (!CurrentModules() || !Read(client+ent::globals::dwGameEntitySystem,out.system) || !out.system ||
        !Read(object+off::OwnerHandle,owner) || !Resolve(out.system,owner,out.pawn) ||
        !Read(out.pawn.entity+ent::C_BasePlayerPawn::m_hController,controller) ||
        !Read(out.pawn.entity+ent::C_BasePlayerPawn::m_hDefaultController,fallback)) return false;
    if (!Resolve(out.system,controller,out.controller) && !Resolve(out.system,fallback,out.controller)) return false;
    if (!Read(out.controller.entity+ent::CCSPlayerController::m_hPlayerPawn,link) || link!=owner ||
        !Read(out.pawn.entity+ent::C_BaseEntity::m_pGameSceneNode,out.skeleton) || !out.skeleton ||
        !Read(out.skeleton,table) || (table!=client+silent_aim_hitbox_offsets::PlayerSkeletonVtable &&
            table!=client+silent_aim_hitbox_offsets::SkeletonVtable) ||
        !Read(object+off::SkeletonInterface,skeletonInterface) || skeletonInterface!=out.skeleton+off::SkeletonInterfaceMember ||
        !Read(out.skeleton+ent::CGameSceneNode::m_bDormant,dormant) || dormant ||
        !Read(out.pawn.entity+ent::C_BaseEntity::m_iHealth,health) || health<=0 ||
        !Read(out.pawn.entity+ent::C_BaseEntity::m_lifeState,life) || life ||
        !Read(out.pawn.entity+ent::C_BaseEntity::m_iTeamNum,out.team) || (out.team!=2 && out.team!=3) ||
        !Read(out.controller.entity+ent::CBasePlayerController::m_bIsLocalPlayerController,local) || local>1) return false;
    out.local=local!=0;
    return true;
}
bool Eligible(const Source& source,const PlayerChamsSettings& settings) noexcept {
    std::uintptr_t localController=0,localPawn=0;
    std::uint32_t localHandle=UINT32_MAX,link=UINT32_MAX;
    Identity local{};
    std::uint8_t team=0,isLocal=0;
    if (!Read(client+ent::globals::dwLocalPlayerController,localController) || !localController ||
        !Read(client+ent::globals::dwLocalPlayerPawn,localPawn) || !localPawn ||
        !Read(localController+ent::CBasePlayerController::m_bIsLocalPlayerController,isLocal) || isLocal!=1 ||
        !Read(localController+ent::CCSPlayerController::m_hPlayerPawn,localHandle) ||
        !Resolve(source.system,localHandle,local) || local.entity!=localPawn ||
        !Read(localPawn+ent::C_BaseEntity::m_iTeamNum,team) || (team!=2 && team!=3)) return false;
    std::uintptr_t identity=0;
    std::uint32_t raw=UINT32_MAX,flags=0;
    Identity controller{};
    if (!Read(localController+ent::CEntityInstance::m_pEntity,identity) || !identity ||
        !Read(identity+ent::entity_layout::HandleOffset,raw) || !Read(identity+ent::CEntityIdentity::m_flags,flags) ||
        !Resolve(source.system,PlayerChamsOwnerHandle(raw,flags),controller) || controller.entity!=localController ||
        !Read(localPawn+ent::C_BasePlayerPawn::m_hController,link)) return false;
    if (link!=controller.handle) {
        if (!Read(localPawn+ent::C_BasePlayerPawn::m_hDefaultController,link) || link!=controller.handle) return false;
    }
    if (source.local || source.pawn.entity==localPawn)
        return settings.localPlayer && source.local && source.controller.entity==localController && source.pawn.entity==localPawn;
    return source.team!=team || settings.teammates;
}
void HookGenerate(void* descriptor,void* objectPointer,void* argsPointer,void* listPointer) noexcept {
    WorldDrawList before{};
    const bool captured=active.load(std::memory_order_acquire) &&
        Read(reinterpret_cast<std::uintptr_t>(listPointer),before) && ValidList(before);
    original(descriptor,objectPointer,argsPointer,listPointer);
    callbacks.fetch_add(1,std::memory_order_relaxed);
    if (!active.load(std::memory_order_acquire)) return;
    const auto settings=PlayerChamsGetSettings();
    if (!settings.enabled) {status.store("Player chams disabled.",std::memory_order_release);return;}
    const auto object=reinterpret_cast<std::uintptr_t>(objectPointer);
    std::uintptr_t table=0,view=0,material=0,mode=0;
    Source source{};
    WorldDrawList after{};
    if (!captured || !Read(reinterpret_cast<std::uintptr_t>(descriptor),table) || table!=scene+off::DescriptorVtable ||
        !Read(reinterpret_cast<std::uintptr_t>(argsPointer)+off::ViewInArgs,view) || !view ||
        !ReadSource(object,source) || !Eligible(source,settings) ||
        !Read(reinterpret_cast<std::uintptr_t>(listPointer),after)) {
        rejections.fetch_add(1,std::memory_order_relaxed);return;
    }
    if (!PlayerChamsMaterialsResolve(settings.style,view,material,mode)) {
        status.store(PlayerChamsMaterialsStatus(),std::memory_order_release);return;
    }
    AcquireSRWLockShared(&mutationLock);
    Source current{};
    if (active.load(std::memory_order_acquire) && ReadSource(object,current) && current==source && Eligible(current,settings)) {
        std::size_t changed=0;
        const auto& tint=settings.style==PlayerChamsStyle::Iridescent?settings.iridescentColor:settings.color;
        if (!PlayerChamsApplyNewDraws(before,after,object,material,mode,tint,changed))
            rejections.fetch_add(1,std::memory_order_relaxed);
        if (changed) {
            changedPackets.fetch_add(changed,std::memory_order_relaxed);
            status.store("Player chams active.",std::memory_order_release);
        }
    }
    ReleaseSRWLockShared(&mutationLock);
}
}

std::uint32_t PlayerChamsOwnerHandle(std::uint32_t identity,std::uint32_t flags) noexcept {
    if (identity==UINT32_MAX) return UINT32_MAX;
    return (((identity>>15)-(flags&1U))<<15)|(identity&ent::entity_layout::IndexMask);
}
bool PlayerChamsApplyNewDraws(const WorldDrawList& before,const WorldDrawList& after,
    std::uintptr_t object,std::uintptr_t material,std::uintptr_t mode,
    const float (&color)[4],std::size_t& changed) noexcept {
    changed=0;
    if (object<0x10000 || !material || !mode || !ValidList(before) || !ValidList(after) ||
        before.fixed!=after.fixed || before.fixedCapacity!=after.fixedCapacity ||
        after.fixedCount<before.fixedCount || after.overflowCount<before.overflowCount ||
        (after.overflowCount>before.overflowCount && after.fixedCount!=after.fixedCapacity)) return false;
    std::uint32_t rgba=0;
    for (unsigned i=0;i<4;++i) {
        const float value=std::isfinite(color[i])?std::clamp(color[i],0.0f,1.0f):0.0f;
        rgba|=static_cast<std::uint32_t>(std::lround(value*255.0f))<<(i*8);
    }
    for (unsigned list=0;list<2;++list) {
        const auto storage=list?after.overflow:after.fixed;
        const int begin=list?before.overflowCount:before.fixedCount;
        const int end=list?after.overflowCount:after.fixedCount;
        for (int i=begin;i<end;++i) {
            const auto at=storage+static_cast<std::uintptr_t>(i)*off::PacketSize;
            unsigned char packet[off::PacketSize]{};
            std::uintptr_t source=0,oldMaterial=0,oldMode=0;
            if (!Copy(at,packet,sizeof(packet))) return false;
            std::memcpy(&source,packet+off::PacketObject,sizeof(source));
            std::memcpy(&oldMaterial,packet+off::PacketMaterial,sizeof(oldMaterial));
            std::memcpy(&oldMode,packet+off::PacketMode,sizeof(oldMode));
            if (source!=object || !oldMaterial || !oldMode) continue;
            std::memcpy(packet+off::PacketMaterial,&material,sizeof(material));
            std::memcpy(packet+off::PacketMode,&mode,sizeof(mode));
            std::memcpy(packet+off::PacketColor,&rgba,sizeof(rgba));
            if (!Write(at,packet,sizeof(packet))) return false;
            ++changed;
        }
    }
    return true;
}
bool PlayerChamsPrepare(PlayerChamsLogCallback callback) noexcept {
    logger=callback;
    client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    scene=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    unsigned char bytes[sizeof(off::GenerateBytes)]{};
    std::uintptr_t target=0;
    if (!Image(client,ent::ClientTimestamp,ent::ClientImageSize) || !Image(scene,off::SceneTimestamp,off::SceneImageSize) ||
        !Copy(scene+off::Generate,bytes,sizeof(bytes)) || std::memcmp(bytes,off::GenerateBytes,sizeof(bytes)) ||
        !Read(scene+off::DescriptorVtable+0x20,target) || target!=scene+off::Generate ||
        !PlayerChamsMaterialsInitialize(callback)) {
        status.store("Player chams native build verification failed.",std::memory_order_release);return false;
    }
    const auto hook=reinterpret_cast<void*>(scene+off::Generate);
    if (MH_CreateHook(hook,reinterpret_cast<void*>(&HookGenerate),reinterpret_cast<void**>(&original))!=MH_OK ||
        !original || MH_QueueEnableHook(hook)!=MH_OK) {
        status.store("Player chams hook setup failed.",std::memory_order_release);return false;
    }
    status.store("Player chams ready; starts disabled.",std::memory_order_release);
    if (logger) logger("PLAYER_CHAMS_BUILD_VERIFIED descriptor=CAnimatableSceneObjectDesc callback=0x7D7C0 owner=0xC0 skeleton_interface=0x110; serialized ownership and native per-view variants; packet changes do not prove visible appearance");
    return true;
}
void PlayerChamsActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped.load(std::memory_order_acquire) && original) active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void PlayerChamsStop(const char* reason) noexcept {
    active.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped.store(true,std::memory_order_release);
    active.store(false,std::memory_order_release);
    status.store("Player chams stopped and resident.",std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    PlayerChamsMaterialsStop();
    if (logger) {char line[384]{};std::snprintf(line,sizeof(line),"PLAYER_CHAMS_STOPPED_RESIDENT reason=%s; packet mutations drained; native code/material resources retained until process exit; no physical unload",reason?reason:"stop");logger(line);}
}
const char* PlayerChamsStatus() noexcept {return status.load(std::memory_order_acquire);}
void PlayerChamsFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now=GetTickCount64();if (lastReport && now-lastReport<2000) return;lastReport=now;
    const auto calls=callbacks.load(),changes=changedPackets.load(),rejects=rejections.load();
    if (calls==lastCalls && changes==lastChanges && rejects==lastRejections) return;
    lastCalls=calls;lastChanges=changes;lastRejections=rejects;
    char line[512]{};std::snprintf(line,sizeof(line),"PLAYER_CHAMS callbacks=%llu packets=%llu rejected=%llu status=%s; native callback counts do not establish visible rendering",calls,changes,rejects,PlayerChamsStatus());logger(line);
}
#ifdef CS2_PLAYER_CHAMS_CHECKS
void PlayerChamsTestConfigure(std::uintptr_t clientBase,std::uintptr_t sceneBase,PlayerChamsGenerateFn fn) noexcept {
    client=clientBase;scene=sceneBase;original=fn;testRuntime=true;stopped.store(false);active.store(true);
}
void PlayerChamsTestGenerate(void* descriptor,void* object,void* args,void* list) noexcept {HookGenerate(descriptor,object,args,list);}
#endif
