#include "grenade_visuals.h"
#include "grenade_area_offsets.h"
#include "cs2_offsets.h"
#include "client_frame.h"
#include "movement_memory.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {
namespace off=grenade_area_offsets;
namespace ent=cs2_offsets;
std::uintptr_t client=0, publishedSystem=0;
std::atomic<bool> active{false};
std::atomic<const char*> status{"grenade areas not initialized"};
GrenadeVisualsLogCallback logger=nullptr;
SRWLOCK stateLock=SRWLOCK_INIT;
SRWLOCK publicationLock=SRWLOCK_INIT;
std::vector<GrenadeArea> published;
ULONGLONG capturedAt=0, reportedAt=0;
bool prepared=false;
std::array<unsigned char,512*ent::entity_layout::IdentityStride> identities{};
std::array<unsigned char,off::SmokeOccupancyBytes> occupancy{};
std::array<unsigned char,off::SmokeDensityBufferBytes> density{};
#ifdef CS2_GRENADE_VISUALS_CHECKS
bool testRuntime=false;
#endif
template<class T> bool Read(std::uintptr_t at,T& out) noexcept { return MovementCopy(at,&out,sizeof(out)); }
bool CurrentClient() noexcept {
#ifdef CS2_GRENADE_VISUALS_CHECKS
    if (testRuntime) return client!=0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"))==client;
}
bool Valid(const EspVec3& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x)<1000000 && std::abs(p.y)<1000000 && std::abs(p.z)<1000000;
}
struct Identity { std::uintptr_t entity=0, slot=0; std::uint32_t handle=UINT32_MAX; };
bool Same(std::uintptr_t system,const Identity& id) noexcept {
    std::uintptr_t bucket=0, entity=0, back=0;
    std::uint32_t handle=UINT32_MAX;
    const auto index=id.handle&ent::entity_layout::IndexMask;
    return id.handle!=UINT32_MAX && Read(system+ent::entity_layout::TableOffset+
        (index>>ent::entity_layout::BucketShift)*sizeof(bucket),bucket) && bucket &&
        bucket+(index&ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride==id.slot &&
        Read(id.slot,entity) && entity==id.entity && Read(id.slot+ent::entity_layout::HandleOffset,handle) && handle==id.handle &&
        Read(id.entity+ent::CEntityInstance::m_pEntity,back) && back==id.slot;
}
bool Scene(const Identity& id,std::uintptr_t& scene) noexcept {
    std::uint8_t dormant=1;
    return Read(id.entity+ent::C_BaseEntity::m_pGameSceneNode,scene) && scene &&
        Read(scene+ent::CGameSceneNode::m_bDormant,dormant) && dormant==0;
}
unsigned MortonComponent(unsigned value,unsigned shift) noexcept {
    unsigned out=0;
    for (unsigned bit=0;bit<5;++bit) out|=((value>>(3*bit+shift))&1)<<bit;
    return out;
}
bool Smoke(const Identity& id,GrenadeArea& area) {
    int tick=0, buffer=-1;
    std::uint8_t did=0, spawned=0;
    const std::uintptr_t sim=id.entity+off::SmokeSimulation; // Embedded native object.
    std::uintptr_t owner=0;
    EspVec3 origin{};
    if (!Read(id.entity+off::SmokeTickBegin,tick) || !Read(id.entity+off::SmokeDidEffect,did) || did>1 ||
        (!tick && !did) || !Read(id.entity+off::SmokeEffectSpawned,spawned) || spawned!=1 ||
        !Read(sim+off::SmokeVoxelOwner,owner) || !owner ||
        !Read(sim+off::SmokeBuffer,buffer) || (buffer!=0 && buffer!=1) ||
        !Read(sim+off::SmokeOrigin,origin) || !Valid(origin) ||
        !MovementCopy(owner+off::SmokeOccupancy+buffer*off::SmokeOccupancyBytes,occupancy.data(),occupancy.size()) ||
        !MovementCopy(owner+off::SmokeDensity+(1-buffer)*off::SmokeDensityBufferBytes,density.data(),density.size())) return false;
    std::array<bool,1024> columns{};
    for (unsigned code=0;code<off::SmokeVoxelCount;++code) {
        if (!(occupancy[code>>3]&(1<<(code&7)))) continue;
        float value=0;
        std::memcpy(&value,density.data()+code*off::SmokeDensityStride,sizeof(value));
        if (std::isfinite(value) && value>0) columns[MortonComponent(code,0)+32*MortonComponent(code,1)]=true;
    }
    int after=-1; std::uintptr_t sameOwner=0;
    if (!Read(sim+off::SmokeVoxelOwner,sameOwner) || sameOwner!=owner ||
        !Read(sim+off::SmokeBuffer,after) || after!=buffer) return false;
    area.kind=GrenadeAreaKind::Smoke;
    area.edges=GrenadeSmokeBoundary(columns,origin,off::SmokeCellSize);
    return !area.edges.empty();
}
bool Fire(const Identity& id,GrenadeArea& area) {
    int count=0;
    std::uint8_t post=1;
    float radius=0;
    std::array<EspVec3,off::MaxFires> positions{};
    std::array<std::uint8_t,off::MaxFires> burning{};
    if (!Read(id.entity+off::FireCount,count) || count<=0 || count>static_cast<int>(off::MaxFires) ||
        !Read(id.entity+off::FirePostEffect,post) || post!=0 ||
        !Read(id.entity+off::FireHalfWidth,radius) || !std::isfinite(radius) || radius<=0 || radius>1024 ||
        !Read(id.entity+off::FirePositions,positions) || !Read(id.entity+off::FireBurning,burning)) return false;
    std::vector<GrenadeDisk> disks;
    for (int i=0;i<count;++i) {
        if (burning[i]>1) return false;
        if (!burning[i]) continue;
        if (!Valid(positions[i])) return false;
        disks.push_back({positions[i],radius});
    }
    int after=0;
    if (!Read(id.entity+off::FireCount,after) || after!=count) return false;
    area.kind=GrenadeAreaKind::Molotov;
    area.edges=GrenadeDiskBoundary(disks);
    return !area.edges.empty();
}
void OwnerFrame(void*,int) noexcept {
    if (!TryAcquireSRWLockExclusive(&stateLock)) return;
    const auto now=GetTickCount64();
    const auto settings=GrenadeGetSettings();
    std::uintptr_t system=0;
    if (!active.load(std::memory_order_acquire) || !CurrentClient() ||
        !Read(client+ent::globals::dwGameEntitySystem,system) || !system ||
        (!settings.smokeAura && !settings.molotovAura)) {
        AcquireSRWLockExclusive(&publicationLock);
        published.clear(); capturedAt=0; publishedSystem=0;
        ReleaseSRWLockExclusive(&publicationLock);
        ReleaseSRWLockExclusive(&stateLock); return;
    }
    if (capturedAt && system==publishedSystem && now-capturedAt<30) {
        ReleaseSRWLockExclusive(&stateLock); return;
    }
    std::vector<GrenadeArea> next;
    for (unsigned b=0;b<ent::entity_layout::BucketCount && next.size()<64;++b) {
        std::uintptr_t bucket=0;
        if (!Read(system+ent::entity_layout::TableOffset+b*sizeof(bucket),bucket) || !bucket ||
            !MovementCopy(bucket,identities.data(),identities.size())) continue;
        for (unsigned i=0;i<512 && next.size()<64;++i) {
            Identity id; id.slot=bucket+i*ent::entity_layout::IdentityStride;
            std::memcpy(&id.entity,identities.data()+i*ent::entity_layout::IdentityStride,sizeof(id.entity));
            std::memcpy(&id.handle,identities.data()+i*ent::entity_layout::IdentityStride+ent::entity_layout::HandleOffset,sizeof(id.handle));
            if (!id.entity || id.handle==UINT32_MAX || (id.handle&ent::entity_layout::IndexMask)!=b*512+i) continue;
            std::uintptr_t vtable=0,scene=0;
            if (!Read(id.entity,vtable)) continue;
            const bool smoke=vtable==client+off::SmokeVtable;
            const bool fire=vtable==client+off::InfernoVtable;
            if ((!smoke || !settings.smokeAura) && (!fire || !settings.molotovAura)) continue;
            if (!Same(system,id) || !Scene(id,scene)) continue;
            GrenadeArea area;
            if (!(smoke?Smoke(id,area):Fire(id,area))) continue;
            std::uintptr_t currentScene=0,currentTable=0;
            if (Same(system,id) && Scene(id,currentScene) && currentScene==scene &&
                Read(id.entity,currentTable) && currentTable==vtable) next.push_back(std::move(area));
        }
    }
    std::uintptr_t afterSystem=0;
    if (active.load(std::memory_order_acquire) && CurrentClient() &&
        Read(client+ent::globals::dwGameEntitySystem,afterSystem) && afterSystem==system) {
        AcquireSRWLockExclusive(&publicationLock);
        published=std::move(next); capturedAt=now; publishedSystem=system;
        ReleaseSRWLockExclusive(&publicationLock);
        status.store("grenade footprints sampled on native frame owner",std::memory_order_release);
        if (logger && now-reportedAt>=5000) {
            char message[192]{};
            std::size_t edges=0; for (const auto& area:published) edges+=area.edges.size();
            std::snprintf(message,sizeof(message),"GRENADE_AREAS areas=%zu edges=%zu system=%p; native smoke voxel footprint / active flame union",published.size(),edges,reinterpret_cast<void*>(system));
            logger(message); reportedAt=now;
        }
    } else {
        AcquireSRWLockExclusive(&publicationLock);
        published.clear(); capturedAt=0; publishedSystem=0;
        ReleaseSRWLockExclusive(&publicationLock);
    }
    ReleaseSRWLockExclusive(&stateLock);
}
std::vector<GrenadeArea> Capture() {
    std::vector<GrenadeArea> result;
    AcquireSRWLockShared(&publicationLock);
    std::uintptr_t system=0;
    const auto now=GetTickCount64();
    if (active.load(std::memory_order_acquire) && CurrentClient() && capturedAt && now>=capturedAt && now-capturedAt<=250 &&
        Read(client+ent::globals::dwGameEntitySystem,system) && system==publishedSystem) result=published;
    ReleaseSRWLockShared(&publicationLock);
    return result;
}
}

bool GrenadeVisualsPrepare(GrenadeVisualsLogCallback callback) noexcept {
    logger=callback;
    if (prepared) return true;
    client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!CurrentClient() || !Read(client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(client+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=off::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage!=off::ClientImageSize || !ClientFrameSubscribe(OwnerFrame,logger)) {
        status.store("grenade area native contract unavailable",std::memory_order_release); return false;
    }
    prepared=true;
    status.store("grenade areas ready",std::memory_order_release);
    if (logger) logger("GRENADE_AREAS_BUILD_VERIFIED native smoke Morton occupancy/density and active inferno cells; no entity writes");
    return true;
}
void GrenadeVisualsActivate() noexcept { if (prepared) active.store(true,std::memory_order_release); }
void GrenadeVisualsStop() noexcept {
    active.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&stateLock);
    AcquireSRWLockExclusive(&publicationLock);
    published.clear(); capturedAt=0; publishedSystem=0;
    status.store("grenade areas stopped and resident",std::memory_order_release);
    ReleaseSRWLockExclusive(&publicationLock);
    ReleaseSRWLockExclusive(&stateLock);
}
const char* GrenadeVisualsStatus() noexcept { return status.load(std::memory_order_acquire); }
void GrenadeVisualsDraw(const EspMatrix4x4& matrix,bool valid,float width,float height) {
    if (!valid || !active.load(std::memory_order_acquire)) return;
    const auto settings=GrenadeGetSettings();
    if (!settings.smokeAura && !settings.molotovAura) return;
    const auto areas=Capture();
    GrenadeDraw(areas,matrix,width,height,settings);
}
#ifdef CS2_GRENADE_VISUALS_CHECKS
bool GrenadeVisualsTestConfigure(std::uintptr_t base) noexcept {
    GrenadeVisualsStop(); client=base; prepared=true; testRuntime=true;
    GrenadeVisualsActivate(); return true;
}
void GrenadeVisualsTestFrame() noexcept { capturedAt=0; OwnerFrame(nullptr,8); }
std::vector<GrenadeArea> GrenadeVisualsTestCapture() { return Capture(); }
#endif
