#include "model_swap.h"
#include "client_frame.h"
#include "model_swap_offsets.h"
#include "model_swap_settings.h"
#include "model_catalog.h"
#include "cs2_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {
namespace off = model_swap_offsets;
namespace ent = cs2_offsets;
namespace bone = silent_aim_hitbox_offsets;
using Path = std::array<char, 260>;
using SetFn = void (*)(void*, const char*);
SetFn setModel = nullptr;
std::uintptr_t client = 0;
std::uintptr_t host = 0, resourceSystem = 0, tier0 = 0;
using InitializeNameFn = bool (*)(void*, const char*);
using LoadFn = void* (*)(void*, const void*, const char*);
using PurgeFn = void (*)(void*, int);
InitializeNameFn initializeName = nullptr;
LoadFn loadResource = nullptr;
PurgeFn purgeName = nullptr;
SRWLOCK stateLock = SRWLOCK_INIT;
bool prepared = false;
std::atomic<bool> active{false}, stopped{false};
ModelSwapLogCallback logger = nullptr;
std::atomic<unsigned long long> calls{0}, assignments{0}, restores{0}, failures{0}, invalidations{0};
std::atomic<DWORD> ownerThread{0};
ULONGLONG reportTime = 0;
unsigned long long reportedCalls = 0;
#ifdef CS2_MODEL_SWAP_CHECKS
bool testRuntime = false, testAvailable = true;
ModelSwapLoadFn testLoad = nullptr;
#endif

template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&bytes)[N]) noexcept {
    unsigned char copied[N]{};
    return MovementCopy(at, copied, N) && !std::memcmp(bytes, copied, N);
}
bool CurrentClient() noexcept {
#ifdef CS2_MODEL_SWAP_CHECKS
    if (testRuntime) return client != 0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
struct Source {
    std::uintptr_t pawn=0, identity=0, controller=0, scene=0;
    std::uint32_t handle=UINT32_MAX, controllerHandle=UINT32_MAX;
    std::uint8_t team=0;
    bool operator==(const Source&) const = default;
};
struct Ownership {
    Source source{};
    Path original{}, applied{}, failed{};
    bool owns=false, restorePending=false;
} owned;

bool Resolve(std::uintptr_t system, std::uint32_t handle, std::uintptr_t entity, std::uintptr_t identity) noexcept {
    std::uintptr_t bucket=0, resolved=0, back=0;
    std::uint32_t serial=UINT32_MAX;
    const auto index = handle & ent::entity_layout::IndexMask;
    if (handle == UINT32_MAX || !Read(system+ent::entity_layout::TableOffset+
        (index >> ent::entity_layout::BucketShift)*sizeof(std::uintptr_t), bucket) || !bucket) return false;
    const auto slot = bucket+(index & ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride;
    return slot==identity && Read(slot, resolved) && resolved==entity &&
        Read(slot+ent::entity_layout::HandleOffset, serial) && serial==handle &&
        Read(entity+ent::CEntityInstance::m_pEntity, back) && back==slot;
}
bool ReadSource(Source& out, bool& alive) noexcept {
    std::uintptr_t system=0, resolved=0, controllerIdentity=0, table=0;
    std::uint32_t link=UINT32_MAX, reciprocal=UINT32_MAX, fallback=UINT32_MAX;
    int health=0;
    std::uint8_t life=1, local=0, dormant=1;
    if (!CurrentClient() || !Read(client+ent::globals::dwGameEntitySystem, system) || !system ||
        !Read(client+ent::globals::dwLocalPlayerPawn, out.pawn) || !out.pawn ||
        !Read(client+ent::globals::dwLocalPlayerController, out.controller) || !out.controller ||
        !Read(out.pawn+ent::CEntityInstance::m_pEntity, out.identity) || !out.identity ||
        !Read(out.identity, resolved) || resolved!=out.pawn ||
        !Read(out.identity+ent::entity_layout::HandleOffset, out.handle) ||
        !Resolve(system, out.handle, out.pawn, out.identity) ||
        !Read(out.controller+ent::CEntityInstance::m_pEntity, controllerIdentity) || !controllerIdentity ||
        !Read(controllerIdentity+ent::entity_layout::HandleOffset, out.controllerHandle) ||
        !Resolve(system, out.controllerHandle, out.controller, controllerIdentity) ||
        !Read(out.controller+ent::CBasePlayerController::m_bIsLocalPlayerController, local) || local!=1 ||
        !Read(out.controller+ent::CCSPlayerController::m_hPlayerPawn, link) || link!=out.handle ||
        !Read(out.pawn+ent::C_BasePlayerPawn::m_hController, reciprocal) ||
        !Read(out.pawn+ent::C_BasePlayerPawn::m_hDefaultController, fallback) ||
        (reciprocal!=out.controllerHandle && fallback!=out.controllerHandle) ||
        !Read(out.pawn+ent::C_BaseEntity::m_pGameSceneNode, out.scene) || !out.scene ||
        !Read(out.scene, table) || (table!=client+bone::PlayerSkeletonVtable && table!=client+bone::SkeletonVtable) ||
        !Read(out.scene+ent::CGameSceneNode::m_bDormant, dormant) || dormant!=0 ||
        !Read(out.pawn+ent::C_BaseEntity::m_iTeamNum, out.team) || (out.team!=2 && out.team!=3) ||
        !Read(out.pawn+ent::C_BaseEntity::m_iHealth, health) ||
        !Read(out.pawn+ent::C_BaseEntity::m_lifeState, life)) return false;
    alive = health>0 && life==0;
    return true;
}
bool BindingPath(std::uintptr_t binding, Path& result) noexcept {
    std::uintptr_t model=0, name=0;
    if (!binding || !Read(binding, model) || !model || !Read(model+off::ResourceName, name) || !name) return false;
    Path copied{};
    for (std::size_t i=0; i<copied.size(); ++i) {
        if (!Read(name+i, copied[i])) return false;
        if (!copied[i]) {
            ModelSwapSettings validated;
            if (!i || !ModelSwapSetModel(validated, {copied.data(), i})) return false;
            result=validated.model;
            return true;
        }
    }
    return false;
}
bool ModelPath(const Source& source, Path& result) noexcept {
    std::uintptr_t binding=0;
    return Read(source.scene+off::ModelBinding, binding) && BindingPath(binding,result);
}
bool Preload(const Path& selected) noexcept {
#ifdef CS2_MODEL_SWAP_CHECKS
    if (testRuntime) return !testLoad || testLoad(selected.data());
#endif
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll"))!=host ||
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"))!=resourceSystem ||
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"))!=tier0) return false;
    std::uintptr_t manager=0, table=0, method=0;
    if (!Read(host+off::ResourceSystemPointer,manager) || !manager || !Read(manager,table) ||
        table!=resourceSystem+off::ResourceSystemVtable ||
        !Read(table+off::BlockingLoadSlot,method) || method!=resourceSystem+off::BlockingLoad) return false;
    alignas(16) unsigned char name[off::ResourceNameSize]{};
    const std::uint32_t flags=off::ResourceNameFlags;
    std::memcpy(name+off::ResourceNameFlagsOffset,&flags,sizeof(flags));
    const bool initialized=initializeName(name,selected.data());
    std::uint64_t type=0;
    std::memcpy(&type,name+off::ResourceType,sizeof(type));
    const auto binding=initialized && type==off::ModelResourceType ? reinterpret_cast<std::uintptr_t>(loadResource(reinterpret_cast<void*>(manager),name,"CS2 local player model")) : 0;
    Path actual{};
    const bool ready=BindingPath(binding,actual) && actual==selected;
    // The native JIT manifest owns the binding. Only the temporary native name
    // string belongs to this call; release it through the engine's allocator.
    purgeName(name,0);
    return ready;
}
bool Available(const Path& selected) noexcept {
#ifdef CS2_MODEL_SWAP_CHECKS
    if (testRuntime) return testAvailable;
#endif
    const auto catalog=ModelCatalogSnapshot();
    if (!catalog) return false;
    const auto item=std::lower_bound(catalog->begin(),catalog->end(),selected.data(),
        [](const ModelCatalogEntry& entry,const char* path) { return entry.path<path; });
    return item!=catalog->end() && item->path==selected.data();
}
bool SameSource(const Source& source, bool requireAlive=false) noexcept {
    Source current{}; bool alive=false;
    return ReadSource(current, alive) && current==source && (!requireAlive || alive);
}
void Invalidate() noexcept {
    if (owned.source.pawn) invalidations.fetch_add(1,std::memory_order_relaxed);
    owned={};
}
void Update() noexcept {
    const auto settings=ModelSwapGetSettings();
    if (owned.failed!=settings.model) owned.failed={};
    if (!owned.owns && (stopped.load() || !active.load() || !settings.enabled || !settings.model[0])) {
        Invalidate();
        ModelSwapSetRuntimeStatus(stopped.load() ? "Stopped and resident; native model restored or retained." :
            !settings.enabled ? "Player model swap disabled." : "Select a player model.");
        return;
    }
    Source source{}; bool alive=false;
    if (!ReadSource(source,alive)) {
        Invalidate();
        ModelSwapSetRuntimeStatus(stopped.load() ? "Stopped; local pawn unavailable. Code remains resident." : "Waiting for a local player.");
        return;
    }
    if (owned.source!=source) { Invalidate(); owned.source=source; }
    Path current{};
    const bool currentReadable=ModelPath(source,current);
    if (owned.owns && owned.restorePending) {
        if (currentReadable && current!=owned.applied && current!=owned.original) {
            // A subsequent native update supersedes a failed rollback too.
            owned.owns=false; owned.restorePending=false; owned.original={}; owned.applied={};
        } else {
            if (!currentReadable || current!=owned.original) {
                if (!SameSource(source)) { Invalidate(); return; }
                setModel(reinterpret_cast<void*>(source.pawn),owned.original.data());
                if (!SameSource(source)) { Invalidate(); return; }
                Path restored{};
                if (!ModelPath(source,restored) || restored!=owned.original) {
                    if (restored[0]) owned.applied=restored;
                    ModelSwapSetRuntimeStatus("Original model restoration pending on the game thread.");
                    return;
                }
            }
            restores.fetch_add(1,std::memory_order_relaxed);
            owned.owns=false; owned.restorePending=false; owned.original={}; owned.applied={};
            ModelSwapSetRuntimeStatus(stopped.load() ? "Stopped and resident; native model restored." : "Native model restored after failed assignment. Toggle to retry.");
            return;
        }
    }
    if (!currentReadable) {
        // Keep pending restoration for this exact identity while its resource
        // is temporarily unavailable; never borrow a path from another pawn.
        ModelSwapSetRuntimeStatus("Waiting for the local model resource.");
        return;
    }
    if (owned.owns && current!=owned.applied) {
        // A native spawn/team/model update supersedes our assignment. Its new
        // path becomes the baseline, with no write to a stale prior model.
        owned.owns=false; owned.original={}; owned.applied={}; owned.failed={};
    }
    const bool replace=active.load(std::memory_order_acquire) && !stopped.load(std::memory_order_acquire) &&
        settings.enabled && settings.model[0] && alive && Available(settings.model);
    if (!replace) {
        if (owned.owns && current==owned.applied && SameSource(source)) {
            setModel(reinterpret_cast<void*>(source.pawn),owned.original.data());
            Path restored{};
            if (!SameSource(source)) { Invalidate(); return; }
            if (!ModelPath(source,restored) || restored!=owned.original) {
                owned.restorePending=true;
                if (restored[0]) owned.applied=restored;
                failures.fetch_add(1,std::memory_order_relaxed);
                ModelSwapSetRuntimeStatus("Original model restoration pending on the game thread.");
                return;
            }
            restores.fetch_add(1,std::memory_order_relaxed);
            owned.owns=false; owned.original={}; owned.applied={};
        }
        owned.failed={};
        ModelSwapSetRuntimeStatus(stopped.load() ? "Stopped and resident; native model restored or retained." :
            !settings.enabled ? "Player model swap disabled." : !alive ? "Waiting for a living local player." :
            !settings.model[0] ? "Select a player model." : "Selected model is unavailable or has incompatible dependencies.");
        return;
    }
    if (current==settings.model) {
        ModelSwapSetRuntimeStatus("Selected local player model assigned. Use third person to inspect.");
        return;
    }
    if (owned.failed==settings.model) {
        ModelSwapSetRuntimeStatus("Model assignment failed; choose another model or toggle to retry.");
        return;
    }
    if (!owned.owns) owned.original=current;
    if (!SameSource(source)) { Invalidate(); return; }
    if (!Preload(settings.model)) {
        if (!SameSource(source)) { Invalidate(); return; }
        failures.fetch_add(1,std::memory_order_relaxed);
        owned.failed=settings.model;
        ModelSwapSetRuntimeStatus("Model resource could not load; current model retained. Toggle to retry.");
        return;
    }
    if (!SameSource(source,true)) { Invalidate(); return; }
    Path beforeAssignment{};
    if (!ModelPath(source,beforeAssignment) || beforeAssignment!=current) { Invalidate(); return; }
    // Blocking resource loading can execute native callbacks. Re-check stop and
    // current UI intent after it returns before assigning anything to a pawn.
    if (!active.load(std::memory_order_acquire) || stopped.load(std::memory_order_acquire) || ModelSwapGetSettings()!=settings) return;
    setModel(reinterpret_cast<void*>(source.pawn),settings.model.data());
    if (!SameSource(source)) { Invalidate(); return; }
    Path after{};
    if (!ModelPath(source,after) || after!=settings.model) {
        // A setter that resolves an error resource must not leave that resource
        // installed. Roll back this attempt on the same verified native owner.
        if (after!=current && SameSource(source)) {
            setModel(reinterpret_cast<void*>(source.pawn),current.data());
            if (!SameSource(source)) { Invalidate(); return; }
            Path rolledBack{};
            if (!ModelPath(source,rolledBack) || rolledBack!=current) {
                owned.owns=true; owned.restorePending=true;
                owned.applied=rolledBack[0] ? rolledBack : settings.model;
            }
        }
        failures.fetch_add(1,std::memory_order_relaxed);
        owned.failed=settings.model;
        ModelSwapSetRuntimeStatus("Model assignment failed; choose another model or toggle to retry.");
        return;
    }
    owned.applied=settings.model; owned.owns=true; owned.failed={};
    assignments.fetch_add(1,std::memory_order_relaxed);
    ModelSwapSetRuntimeStatus("Selected local player model assigned. Use third person to inspect.");
}
void OwnerFrame(void* owner, int stage) noexcept {
    if (stage==off::ApplyStage) {
        std::uintptr_t table=0;
        if (CurrentClient() && Read(reinterpret_cast<std::uintptr_t>(owner),table) && table==client+off::FrameVtable) {
            AcquireSRWLockExclusive(&stateLock);
            if (active.load(std::memory_order_acquire) || stopped.load(std::memory_order_acquire)) {
                calls.fetch_add(1,std::memory_order_relaxed);
                ownerThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
                Update();
            }
            ReleaseSRWLockExclusive(&stateLock);
        }
    }
}

}

bool ModelSwapPrepare(ModelSwapLogCallback callback) noexcept {
    logger=callback;
    client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    host=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll"));
    resourceSystem=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"));
    tier0=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{}; std::uintptr_t frame=0;
    if (!Read(client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(client+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage!=ent::ClientImageSize || !Match(client+off::FrameStage,off::FrameStageBytes) ||
        !Match(client+off::SetModel,off::SetModelBytes) || !Match(client+off::ModelNameGetter,off::ModelNameGetterBytes) ||
        !Read(client+off::FrameVtable+off::FrameSlot*sizeof(std::uintptr_t),frame) || frame!=client+off::FrameStage) {
        ModelSwapSetRuntimeStatus("Player model build verification failed."); return false;
    }
    const auto identity=[](std::uintptr_t base,std::uint32_t timestamp,std::uint32_t imageSize) noexcept {
        IMAGE_DOS_HEADER d{};IMAGE_NT_HEADERS64 n{};
        return Read(base,d) && d.e_magic==IMAGE_DOS_SIGNATURE && d.e_lfanew>0 && d.e_lfanew<0x100000 &&
            Read(base+d.e_lfanew,n) && n.Signature==IMAGE_NT_SIGNATURE && n.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
            n.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && n.FileHeader.TimeDateStamp==timestamp && n.OptionalHeader.SizeOfImage==imageSize;
    };
    purgeName=reinterpret_cast<PurgeFn>(GetProcAddress(reinterpret_cast<HMODULE>(tier0),"?Purge@CBufferString@@QEAAXH@Z"));
    std::uintptr_t modelInterface=0,modelTable=0,modelResolver=0;
    if (!identity(host,off::HostTimestamp,off::HostImageSize) ||
        !identity(resourceSystem,off::ResourceSystemTimestamp,off::ResourceSystemImageSize) ||
        !identity(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"engine2.dll")),off::EngineTimestamp,off::EngineImageSize) ||
        !identity(tier0,off::Tier0Timestamp,off::Tier0ImageSize) ||
        reinterpret_cast<std::uintptr_t>(purgeName)!=tier0+off::PurgeResourceName ||
        !Match(tier0+off::PurgeResourceName,off::PurgeResourceNameBytes) ||
        !Match(host+off::InitializeResourceName,off::InitializeResourceNameBytes) ||
        !Match(host+off::HostModelResolver,off::HostModelResolverBytes) ||
        !Match(resourceSystem+off::BlockingLoad,off::BlockingLoadBytes) ||
        !Read(client+off::ModelInterface,modelInterface) || !modelInterface || !Read(modelInterface,modelTable) ||
        modelTable!=host+off::HostModelVtable || !Read(modelTable+off::ModelResolveSlot,modelResolver) || modelResolver!=host+off::HostModelResolver) {
        ModelSwapSetRuntimeStatus("Player model resource-loading build verification failed."); return false;
    }
    initializeName=reinterpret_cast<InitializeNameFn>(host+off::InitializeResourceName);
    loadResource=reinterpret_cast<LoadFn>(resourceSystem+off::BlockingLoad);
    if (logger) logger("MODEL_SWAP_CATALOG_BEGIN disk-only pack discovery; native model loading has not started");
    try {
        wchar_t path[32768]{};
        const auto length=GetModuleFileNameW(reinterpret_cast<HMODULE>(client),path,static_cast<DWORD>(std::size(path)));
        std::string error;
        if (!length || length>=std::size(path) || !ModelCatalogDiscover(std::filesystem::path(path).parent_path().parent_path().parent_path(),error)) {
            ModelSwapSetRuntimeStatus("Cannot discover the installed character pack.");
            if (logger) logger("MODEL_SWAP_CATALOG_FAILED; model swap unavailable; remaining overlay startup continues");
            if (logger && !error.empty()) logger(error.c_str());
            return false;
        }
    } catch (...) { ModelSwapSetRuntimeStatus("Cannot discover the installed character pack."); return false; }
    setModel=reinterpret_cast<SetFn>(client+off::SetModel);
    if (!ClientFrameSubscribe(&OwnerFrame, logger)) {
        ModelSwapSetRuntimeStatus("Player model hook preparation failed."); return false;
    }
    prepared=true;
    ModelSwapSetRuntimeStatus("Player model swap disabled.");
    if (logger) {
        char text[256]{};
        const auto catalog=ModelCatalogSnapshot();
        std::snprintf(text,sizeof(text),"MODEL_SWAP_BUILD_VERIFIED native frame/setter; local pawn only; compatible loose models=%zu; native resource loading",catalog ? catalog->size() : 0);
        logger(text);
    }
    return true;
}
void ModelSwapActivate() noexcept {
    AcquireSRWLockExclusive(&stateLock);
    if (prepared && !stopped.load()) active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&stateLock);
}
void ModelSwapStop(const char* reason) noexcept {
    active.store(false,std::memory_order_release);
    if (ClientFrameInCallback()) {
        // A blocking native load can pump callbacks on this thread. Such a stop
        // request must not try to acquire the lock its outer frame already owns.
        stopped.store(true,std::memory_order_release);
        ModelSwapSetRuntimeStatus("Stopped; original model restoration queued on the game thread.");
        return;
    }
    AcquireSRWLockExclusive(&stateLock);
    stopped.store(true,std::memory_order_release);
    active.store(false,std::memory_order_release);
    const bool pending=owned.owns;
    ModelSwapSetRuntimeStatus(pending ? "Stopped; original model restoration queued on the game thread." : "Stopped and resident; native model retained.");
    ReleaseSRWLockExclusive(&stateLock);
    if (logger) {
        char text[384]{};
        std::snprintf(text,sizeof(text),"MODEL_SWAP_STOPPED_RESIDENT reason=%s restore_pending=%u; new swaps disarmed; restoration uses next native frame; no physical unload",reason ? reason : "stop",pending ? 1u : 0u);
        logger(text);
    }
}
void ModelSwapFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now=GetTickCount64();
    if (reportTime && now-reportTime<2000) return;
    reportTime=now;
    const auto count=calls.load(std::memory_order_relaxed);
    if (count==reportedCalls) return;
    reportedCalls=count;
    char text[768]{};
    const auto state=ModelSwapRuntimeStatus();
    std::snprintf(text,sizeof(text),"MODEL_SWAP frames=%llu assigned=%llu restored=%llu failed=%llu invalidated=%llu owner_tid=%lu status=%s",
        count,assignments.load(),restores.load(),failures.load(),invalidations.load(),ownerThread.load(),state.data());
    logger(text);
}

#ifdef CS2_MODEL_SWAP_CHECKS
void ModelSwapTestConfigure(std::uintptr_t base, ModelSwapFrameFn frame, ModelSwapSetFn setter, ModelSwapLoadFn loader) noexcept {
    ClientFrameTestConfigure(base,frame);
    ClientFrameSubscribe(&OwnerFrame);
    client=base; setModel=setter; owned={};
    testRuntime=true; testAvailable=true; testLoad=loader; prepared=true; active.store(false); stopped.store(false);
    calls.store(0); assignments.store(0); restores.store(0); failures.store(0); invalidations.store(0);
    ModelSwapResetSettings();
}
void ModelSwapTestFrame(void* owner, int stage) noexcept { ClientFrameTestDispatch(owner,stage); }
void ModelSwapTestSetAvailable(bool value) noexcept { testAvailable=value; }
#endif
