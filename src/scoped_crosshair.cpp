#include "scoped_crosshair.h"
#include "aim_weapon.h"
#include "aim_weapon_offsets.h"
#include "zoom_offsets.h"
#include "third_person.h"
#include "zoom_settings.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace {
namespace ent = cs2_offsets;
namespace off = zoom_offsets;
namespace accuracy = aim_weapon_offsets;
AimWeaponContext context{};
SRWLOCK stateLock = SRWLOCK_INIT;
std::atomic<bool> active{false};
#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
bool testRuntime = false;
ScopedCrosshairClock testClock = nullptr;
#endif
struct Source {
    std::uintptr_t pawn=0, identity=0, controller=0, controllerIdentity=0, system=0;
    std::uintptr_t services=0, weapon=0, weaponIdentity=0, data=0, table=0;
    std::uint32_t pawnHandle=UINT32_MAX, controllerHandle=UINT32_MAX, weaponHandle=UINT32_MAX;
    int mode=0, level=0;
    bool operator==(const Source&) const = default;
};
struct Publication {
    Source source{};
    float spread=0, inaccuracy=0;
    ULONGLONG captured=0;
    bool valid=false;
    bool thirdPerson=false;
} publication;
Source suppressedSource{};
ULONGLONG suppressedTime=0;
bool suppressed=false;
ULONGLONG Now() noexcept {
#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
    if (testClock) return testClock();
#endif
    return GetTickCount64();
}
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address,&value,sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(address,bytes,N) && !std::memcmp(bytes,expected,N);
}
bool CurrentClient() noexcept {
#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
    if (testRuntime) return context.client!=0;
#endif
    return context.client && context.client==reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Resolve(std::uintptr_t system, std::uint32_t handle, std::uintptr_t& entity, std::uintptr_t& identity) noexcept {
    std::uintptr_t bucket=0, back=0;
    std::uint32_t serial=UINT32_MAX;
    const auto index=handle & ent::entity_layout::IndexMask;
    if (handle==UINT32_MAX || !Read(system+ent::entity_layout::TableOffset+
        (index >> ent::entity_layout::BucketShift)*sizeof(std::uintptr_t),bucket) || !bucket) return false;
    identity=bucket+(index & ent::entity_layout::BucketMask)*ent::entity_layout::IdentityStride;
    return Read(identity+ent::entity_layout::HandleOffset,serial) && serial==handle &&
        Read(identity,entity) && entity && Read(entity+ent::CEntityInstance::m_pEntity,back) && back==identity;
}
bool ReadSource(Source& out) noexcept {
    std::uintptr_t resolved=0, identity=0, servicesTable=0, servicePawn=0;
    std::uint32_t linked=UINT32_MAX;
    int health=0;
    std::uint8_t life=1, scoped=0;
    if (!CurrentClient() || !Read(context.client+ent::globals::dwLocalPlayerPawn,out.pawn) || !out.pawn ||
        !Read(context.client+ent::globals::dwLocalPlayerController,out.controller) || !out.controller ||
        !Read(context.client+ent::globals::dwGameEntitySystem,out.system) || !out.system ||
        !Read(out.pawn+ent::CEntityInstance::m_pEntity,out.identity) || !out.identity ||
        !Read(out.identity+ent::entity_layout::HandleOffset,out.pawnHandle) ||
        !Resolve(out.system,out.pawnHandle,resolved,identity) || resolved!=out.pawn || identity!=out.identity ||
        !Read(out.controller+ent::CEntityInstance::m_pEntity,out.controllerIdentity) || !out.controllerIdentity ||
        !Read(out.controllerIdentity+ent::entity_layout::HandleOffset,out.controllerHandle) ||
        !Resolve(out.system,out.controllerHandle,resolved,identity) || resolved!=out.controller || identity!=out.controllerIdentity ||
        !Read(out.controller+ent::CCSPlayerController::m_hPlayerPawn,linked) || linked!=out.pawnHandle ||
        !Read(out.pawn+ent::C_BaseEntity::m_iHealth,health) || health<=0 ||
        !Read(out.pawn+ent::C_BaseEntity::m_lifeState,life) || life!=0 ||
        !Read(out.pawn+off::Scoped,scoped) || scoped!=1 ||
        !Read(out.pawn+off::WeaponServices,out.services) || !out.services ||
        !Read(out.services,servicesTable) || servicesTable!=context.client+accuracy::ServicesVtable ||
        !Read(out.services+accuracy::ServicePawn,servicePawn) || servicePawn!=out.pawn ||
        !Read(out.services+off::ActiveWeapon,out.weaponHandle) ||
        !Resolve(out.system,out.weaponHandle,out.weapon,out.weaponIdentity) ||
        !Read(out.weapon,out.table) || !Read(out.weapon+accuracy::Data,out.data) || !out.data ||
        !Read(out.weapon+accuracy::Mode,out.mode) || out.mode<0 || out.mode>1) return false;
    bool supported=false;
    for (const auto table:off::ScopedWeaponVtables) supported |= out.table==context.client+table;
    return supported && Read(out.weapon+off::ZoomLevel,out.level) && (out.level==1 || out.level==2);
}
bool ThirdPerson(const Source& source) noexcept {
    return ThirdPersonViewActive(source.pawn,source.identity,source.controller,source.pawnHandle);
}
bool Matches(const Source& source, const AimWeaponSnapshot& weapon) noexcept {
    return weapon.weapon==source.weapon && weapon.identity==source.weaponIdentity && weapon.services==source.services &&
        weapon.data==source.data && weapon.handle==source.weaponHandle && weapon.mode==source.mode;
}
}

bool ScopedCrosshairPrepare() noexcept {
    AcquireSRWLockExclusive(&stateLock);
    active.store(false,std::memory_order_release);
    publication={};
    suppressed=false;
    const auto client=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    // Reuse the installed weapon-model build identity and byte/slot checks.
    // Scope fields/classes use the existing independently verified zoom contract.
    bool valid=AimWeaponPrepare(client,context) &&
        Match(client+off::OverrideView,off::OverrideViewBytes) &&
        Match(client+off::BaselineFov,off::BaselineFovBytes);
    for (const auto table:off::ScopedWeaponVtables) {
        bool found=false;
        for (const auto& entry:accuracy::Tables) found |= entry.rva==table;
        valid=valid && found;
    }
    active.store(valid,std::memory_order_release);
    ReleaseSRWLockExclusive(&stateLock);
    return valid;
}
void ScopedCrosshairCaptureCommand() noexcept {
    if (!active.load(std::memory_order_acquire) || !TryAcquireSRWLockExclusive(&stateLock)) return;
    publication={};
    Source before{}, after{};
    AimWeaponSnapshot weapon{};
    const bool sourceValid=active.load(std::memory_order_acquire) && ReadSource(before);
    const bool thirdPerson=sourceValid && ThirdPerson(before);
    if (sourceValid && (thirdPerson || ZoomGetSettings().removeScope) &&
        AimWeaponReadSpread(context,{before.system,before.controller,before.pawn},weapon) && Matches(before,weapon) &&
        std::isfinite(weapon.spread) && weapon.spread>=0 && weapon.spread<=1 &&
        std::isfinite(weapon.inaccuracy) && weapon.inaccuracy>=0 && weapon.inaccuracy<=1 &&
        ReadSource(after) && after==before && ThirdPerson(after)==thirdPerson &&
        (thirdPerson || ZoomGetSettings().removeScope) && active.load(std::memory_order_acquire)) {
        publication={after,weapon.spread,weapon.inaccuracy,Now(),true,thirdPerson};
    }
    if (!publication.valid || publication.thirdPerson || suppressedSource!=publication.source) suppressed=false;
    ReleaseSRWLockExclusive(&stateLock);
}
ScopedCrosshairDrawInput ScopedCrosshairCaptureForDraw() noexcept {
    ScopedCrosshairDrawInput output{};
    if (!active.load(std::memory_order_acquire)) return output;
    AcquireSRWLockShared(&stateLock);
    const auto now=Now();
    Source current{}, final{};
    // No native getter is called from the render thread. Current source checks
    // also prevent old values surviving unscoping, death or weapon recycling.
    if (active.load(std::memory_order_acquire) && publication.valid && now>=publication.captured &&
        now-publication.captured<=250 && ReadSource(current) && current==publication.source &&
        ThirdPerson(current)==publication.thirdPerson &&
        (publication.thirdPerson || (ZoomGetSettings().removeScope && suppressed &&
            suppressedSource==current && now>=suppressedTime && now-suppressedTime<=250)) &&
        ReadSource(final) && final==current) {
        output.visible=true;
        output.spread=publication.spread;
        output.inaccuracy=publication.inaccuracy;
        output.sourceKey=(static_cast<std::uint64_t>(current.pawnHandle)<<32)|current.weaponHandle;
        if (!output.sourceKey) output.sourceKey=1;
    }
    ReleaseSRWLockShared(&stateLock);
    return output;
}
namespace {
bool SuppressNative(std::uintptr_t pawn, std::uintptr_t weapon) noexcept {
    if (!active.load(std::memory_order_acquire) || !ZoomGetSettings().removeScope) return false;
    // Called only from the verified native scope visual-parameter callback. Suppress
    // only when we already have a usable replacement for the same scoped source.
    if (!TryAcquireSRWLockExclusive(&stateLock)) return false;
    const auto now=Now();
    Source current{}, final{};
    const bool replace=active.load(std::memory_order_acquire) && ZoomGetSettings().removeScope &&
        publication.valid && !publication.thirdPerson && now>=publication.captured &&
        now-publication.captured<=250 && ReadSource(current) && current==publication.source &&
        ((pawn && current.pawn==pawn) || (weapon && current.weapon==weapon)) &&
        !ThirdPerson(current) && ReadSource(final) && final==current;
    suppressed=replace;
    if (replace) { suppressedSource=current; suppressedTime=now; }
    ReleaseSRWLockExclusive(&stateLock);
    return replace;
}
}
bool ScopedCrosshairSuppressNativeScope(std::uintptr_t pawn) noexcept { return SuppressNative(pawn,0); }
bool ScopedCrosshairSuppressNativeIronSight(std::uintptr_t weapon) noexcept { return SuppressNative(0,weapon); }
void ScopedCrosshairStop() noexcept {
    active.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&stateLock);
    publication={};
    suppressed=false;
    ReleaseSRWLockExclusive(&stateLock);
}
#ifdef CS2_SCOPED_CROSSHAIR_CHECKS
void ScopedCrosshairTestConfigure(const AimWeaponContext& selected, ScopedCrosshairClock clock) noexcept {
    AcquireSRWLockExclusive(&stateLock);
    context=selected;
    testRuntime=true;
    testClock=clock;
    publication={};
    suppressed=false;
    active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&stateLock);
}
#endif
