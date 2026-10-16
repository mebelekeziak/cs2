#include "zoom.h"
#include "zoom_offsets.h"
#include "zoom_settings.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off = zoom_offsets;
namespace ent = cs2_offsets;
using ViewFn = void (*)(void*, void*);
using BaselineFn = float (*)(void*);
ViewFn originalView = nullptr;
BaselineFn baselineFov = nullptr;
std::uintptr_t client = 0;
DWORD scopeTls = TLS_OUT_OF_INDEXES;
SRWLOCK viewLock = SRWLOCK_INIT;
std::atomic<bool> active{false};
bool prepared = false, stopped = false;
std::atomic<const char*> status{"scoped zoom not initialized"};
ZoomLogCallback logger = nullptr;
std::atomic<unsigned long long> calls{0}, edited{0}, rejected{0};
ULONGLONG reportTime = 0;
unsigned long long reportedCalls = 0;
#ifdef CS2_ZOOM_CHECKS
bool testRuntime = false;
#endif
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char copied[N]{};
    return MovementCopy(address,copied,N) && std::memcmp(copied,bytes,N) == 0;
}
bool CurrentClient() noexcept {
#ifdef CS2_ZOOM_CHECKS
    if (testRuntime) return client != 0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
struct Source {
    std::uintptr_t pawn=0, controller=0, pawnIdentity=0, services=0, weapon=0, weaponIdentity=0;
    std::uint32_t pawnHandle=UINT32_MAX, weaponHandle=UINT32_MAX;
    int level=0;
    bool operator==(const Source&) const = default;
};
bool ReadSource(Source& out) noexcept {
    std::uintptr_t resolved=0, system=0, bucket=0, vtable=0;
    std::uint32_t linked=UINT32_MAX, serial=UINT32_MAX;
    std::uint8_t scoped=0, life=1;
    int health=0;
    if (!CurrentClient() || !Read(client+ent::globals::dwLocalPlayerPawn,out.pawn) || !out.pawn ||
        !Read(client+ent::globals::dwLocalPlayerController,out.controller) || !out.controller ||
        !Read(out.pawn+ent::CEntityInstance::m_pEntity,out.pawnIdentity) || !out.pawnIdentity ||
        !Read(out.pawnIdentity,resolved) || resolved!=out.pawn ||
        !Read(out.pawnIdentity+ent::entity_layout::HandleOffset,out.pawnHandle) || out.pawnHandle==UINT32_MAX ||
        !Read(out.controller+ent::CCSPlayerController::m_hPlayerPawn,linked) || linked!=out.pawnHandle ||
        !Read(out.pawn+ent::C_BaseEntity::m_iHealth,health) || health<=0 ||
        !Read(out.pawn+ent::C_BaseEntity::m_lifeState,life) || life!=0 ||
        !Read(out.pawn+off::Scoped,scoped) || scoped!=1 ||
        !Read(out.pawn+off::WeaponServices,out.services) || !out.services ||
        !Read(out.services+off::ActiveWeapon,out.weaponHandle) || out.weaponHandle==UINT32_MAX ||
        !Read(client+ent::globals::dwGameEntitySystem,system) || !system) return false;
    const auto index=out.weaponHandle & ent::entity_layout::IndexMask;
    if (!Read(system+ent::entity_layout::TableOffset+(index >> ent::entity_layout::BucketShift)*sizeof(std::uintptr_t),bucket) || !bucket) return false;
    const auto identity=bucket+(index & ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride;
    if (!Read(identity+ent::entity_layout::HandleOffset,serial) || serial!=out.weaponHandle ||
        !Read(identity,out.weapon) || !out.weapon || !Read(out.weapon+ent::CEntityInstance::m_pEntity,out.weaponIdentity) ||
        out.weaponIdentity!=identity || !Read(out.weapon,vtable)) return false;
    bool scopedWeapon=false;
    for (const auto table:off::ScopedWeaponVtables) scopedWeapon |= vtable==client+table;
    return scopedWeapon && Read(out.weapon+off::ZoomLevel,out.level) && (out.level==1 || out.level==2);
}
bool PrimaryMode(void* mode) noexcept {
    std::uintptr_t table=0; int slot=-1;
    const auto address=reinterpret_cast<std::uintptr_t>(mode);
    return CurrentClient() && Read(address,table) && table==client+off::ModeVtable &&
        Read(address+off::ModeSlot,slot) && slot==0;
}
bool WriteFov(std::uintptr_t address, float value) noexcept {
    if (address<0x10000 || address>0x00007fffffffffffULL-sizeof(value)) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void HookView(void* mode, void* view) noexcept {
    calls.fetch_add(1,std::memory_order_relaxed);
    // Reentrant views are forwarded without borrowing the outer source/settings.
    if (scopeTls!=TLS_OUT_OF_INDEXES && TlsGetValue(scopeTls)) { originalView(mode,view); return; }
    AcquireSRWLockShared(&viewLock);
    bool scope=false;
    const bool tlsReady=scopeTls!=TLS_OUT_OF_INDEXES && TlsSetValue(scopeTls,&scope);
    const auto settings=ZoomGetSettings();
    Source before{};
    const bool replace=tlsReady && active.load(std::memory_order_acquire) && settings.enabled &&
        PrimaryMode(mode) && ReadSource(before);
    originalView(mode,view);
    if (replace) {
        Source after{}, final{}; float native=0, output=0;
        const auto address=reinterpret_cast<std::uintptr_t>(view);
        bool valid=ReadSource(after) && after==before && PrimaryMode(mode) &&
            Read(address+off::ViewFov,native);
        // The native getter also handles desiredFOV==0 via the engine default.
        const float baseline=valid ? baselineFov(reinterpret_cast<void*>(after.controller)) : 0;
        valid=valid && ZoomComputeFov(baseline,native,before.level==1 ? settings.zoom1 : settings.zoom2,output) &&
            ReadSource(final) && final==before && PrimaryMode(mode);
        if (valid && (output==native || WriteFov(address+off::ViewFov,output))) {
            edited.fetch_add(1,std::memory_order_relaxed);
            status.store("scoped zoom active",std::memory_order_release);
        } else {
            rejected.fetch_add(1,std::memory_order_relaxed);
            status.store("zoom source/FOV unavailable; native view retained",std::memory_order_release);
        }
    } else if (active.load(std::memory_order_acquire)) {
        status.store(settings.enabled ? "awaiting scoped local weapon" : "scoped zoom disabled",std::memory_order_release);
    }
    if (tlsReady) TlsSetValue(scopeTls,nullptr);
    ReleaseSRWLockShared(&viewLock);
}
}

bool ZoomComputeFov(float baseline, float native, float strength, float& output) noexcept {
    if (!std::isfinite(baseline) || !std::isfinite(native) || !std::isfinite(strength) ||
        baseline<=0 || baseline>=179 || native<=0 || native>=179 || native>baseline) return false;
    strength=std::clamp(strength,0.0f,200.0f);
    if (strength==0) { output=baseline; return true; }
    if (strength==100) { output=native; return true; }
    constexpr double radians=3.14159265358979323846/360.0;
    const double baselineTan=std::tan(baseline*radians);
    const double magnification=baselineTan/std::tan(native*radians);
    const double result=std::atan(baselineTan/(1.0+(strength/100.0)*(magnification-1.0)))/radians;
    if (!std::isfinite(result) || result<=0 || result>=179) return false;
    output=static_cast<float>(result); return true;
}
bool ZoomPrepare(ZoomLogCallback callback) noexcept {
    logger=callback;
    client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{}; std::uintptr_t view=0;
    if (!Read(client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(client+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp!=ent::ClientTimestamp || nt.OptionalHeader.SizeOfImage!=ent::ClientImageSize ||
        !Match(client+off::OverrideView,off::OverrideViewBytes) || !Match(client+off::BaselineFov,off::BaselineFovBytes) ||
        !Read(client+off::ModeVtable+16*sizeof(std::uintptr_t),view) || view!=client+off::OverrideView) {
        status.store("scoped zoom build verification failed",std::memory_order_release); return false;
    }
    scopeTls=TlsAlloc();
    if (scopeTls==TLS_OUT_OF_INDEXES) { status.store("zoom dynamic TLS unavailable",std::memory_order_release); return false; }
    baselineFov=reinterpret_cast<BaselineFn>(client+off::BaselineFov);
    auto* target=reinterpret_cast<void*>(client+off::OverrideView);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&HookView),reinterpret_cast<void**>(&originalView))!=MH_OK ||
        !originalView || MH_QueueEnableHook(target)!=MH_OK) {
        status.store("scoped zoom hook preparation failed",std::memory_order_release); return false;
    }
    prepared=true;
    status.store("scoped zoom disabled",std::memory_order_release);
    if (logger) logger("ZOOM_BUILD_VERIFIED normal mode final view; six scoped weapon RTTI tables; full serialized handles; native baseline getter; view FOV only");
    return true;
}
void ZoomActivate() noexcept {
    AcquireSRWLockExclusive(&viewLock);
    if (prepared && !stopped) active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&viewLock);
}
void ZoomStop(const char* reason) noexcept {
    active.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&viewLock);
    stopped=true; active.store(false,std::memory_order_release);
    status.store("scoped zoom stopped and resident",std::memory_order_release);
    ReleaseSRWLockExclusive(&viewLock);
    if (logger) {
        char text[384]{};
        std::snprintf(text,sizeof(text),"ZOOM_STOPPED_RESIDENT reason=%s; view edits drained; original forwarding retained; no physical unload",reason ? reason : "stop");
        logger(text);
    }
}
const char* ZoomStatus() noexcept { return status.load(std::memory_order_acquire); }
void ZoomFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now=GetTickCount64();
    if (reportTime && now-reportTime<2000) return;
    reportTime=now;
    const auto count=calls.load(std::memory_order_relaxed);
    if (count==reportedCalls) return;
    reportedCalls=count;
    char text[384]{};
    std::snprintf(text,sizeof(text),"ZOOM views=%llu applied=%llu rejected=%llu enabled=%d status=%s; counters do not prove visible output",count,
        edited.load(std::memory_order_relaxed),rejected.load(std::memory_order_relaxed),ZoomGetSettings().enabled,ZoomStatus());
    logger(text);
}
#ifdef CS2_ZOOM_CHECKS
void ZoomTestConfigure(std::uintptr_t base, ZoomViewFn original, ZoomBaselineFn baseline) noexcept {
    client=base; originalView=original; baselineFov=baseline;
    if (scopeTls==TLS_OUT_OF_INDEXES) scopeTls=TlsAlloc();
    testRuntime=true; prepared=true; stopped=false; active.store(true,std::memory_order_release);
}
void ZoomTestView(void* mode, void* view) noexcept { HookView(mode,view); }
#endif
