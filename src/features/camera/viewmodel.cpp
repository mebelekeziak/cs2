#include "viewmodel.h"
#include "viewmodel_settings.h"
#include "viewmodel_offsets.h"
#include "zoom_settings.h"
#include "zoom_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {
namespace off = viewmodel_offsets;
namespace ent = cs2_offsets;
using ParametersFn = void (*)(void*, void*, float*);
ParametersFn original = nullptr;
using ViewFn = void (*)(void*, void*);
ViewFn originalView = nullptr;
std::uintptr_t client = 0;
DWORD callbackTls = TLS_OUT_OF_INDEXES;
DWORD viewTls = TLS_OUT_OF_INDEXES;
SRWLOCK callbackLock = SRWLOCK_INIT;
SRWLOCK viewLock = SRWLOCK_INIT;
std::atomic<bool> active{false};
std::atomic<const char*> status{"viewmodel not initialized"};
bool prepared = false, stopped = false;
ViewmodelLogCallback logger = nullptr;
std::atomic<unsigned long long> viewCalls{0}, viewEligible{0}, viewEdited{0}, viewRejected{0};
std::atomic<unsigned long long> baseRejected{0}, projectionRejected{0}, sourceRejected{0}, parameterCalls{0}, parameterEdited{0};
std::atomic<float> lastNative{0}, lastBase{0}, lastProjected{0};
ULONGLONG reportTime = 0;
#ifdef CS2_VIEWMODEL_CHECKS
bool testing = false;
#endif
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Current() noexcept {
#ifdef CS2_VIEWMODEL_CHECKS
    if (testing) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
struct Source {
    std::uintptr_t pawn = 0, identity = 0, controller = 0;
    std::uint32_t handle = UINT32_MAX;
    std::uintptr_t services = 0, weapon = 0, weaponIdentity = 0;
    std::uint32_t weaponHandle = UINT32_MAX;
    int level = 0;
    bool operator==(const Source&) const = default;
};
bool Local(Source& source, std::uintptr_t pawn) noexcept {
    std::uintptr_t resolved = 0;
    std::uint32_t linked = UINT32_MAX;
    int health = 0;
    std::uint8_t life = 1;
    return Current() && Read(client + ent::globals::dwLocalPlayerPawn, source.pawn) && source.pawn && source.pawn == pawn &&
        Read(source.pawn + ent::CEntityInstance::m_pEntity, source.identity) && source.identity &&
        Read(source.identity, resolved) && resolved == source.pawn &&
        Read(source.identity + ent::entity_layout::HandleOffset, source.handle) && source.handle != UINT32_MAX &&
        Read(client + ent::globals::dwLocalPlayerController, source.controller) && source.controller &&
        Read(source.controller + ent::CCSPlayerController::m_hPlayerPawn, linked) && linked == source.handle &&
        Read(source.pawn + ent::C_BaseEntity::m_iHealth, health) && health > 0 &&
        Read(source.pawn + ent::C_BaseEntity::m_lifeState, life) && life == 0;
}
bool ScopedLocal(Source& source, std::uintptr_t pawn) noexcept {
    namespace zoom = zoom_offsets;
    std::uintptr_t system = 0, bucket = 0, table = 0;
    std::uint32_t serial = UINT32_MAX;
    std::uint8_t scoped = 0;
    if (!Local(source, pawn) || !Read(pawn + zoom::Scoped, scoped) || scoped != 1 ||
        !Read(pawn + zoom::WeaponServices, source.services) || !source.services ||
        !Read(source.services + zoom::ActiveWeapon, source.weaponHandle) || source.weaponHandle == UINT32_MAX ||
        !Read(client + ent::globals::dwGameEntitySystem, system) || !system) return false;
    const auto index = source.weaponHandle & ent::entity_layout::IndexMask;
    if (!Read(system + ent::entity_layout::TableOffset + (index >> ent::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) || !bucket) return false;
    source.weaponIdentity = bucket + (index & ent::entity_layout::BucketMask) * ent::entity_layout::IdentityStride;
    std::uintptr_t back = 0;
    if (!Read(source.weaponIdentity + ent::entity_layout::HandleOffset, serial) || serial != source.weaponHandle ||
        !Read(source.weaponIdentity, source.weapon) || !source.weapon ||
        !Read(source.weapon + ent::CEntityInstance::m_pEntity, back) || back != source.weaponIdentity ||
        !Read(source.weapon, table)) return false;
    bool supported = false;
    for (const auto weaponTable : zoom::ScopedWeaponVtables) supported |= table == client + weaponTable;
    return supported && Read(source.weapon + zoom::ZoomLevel, source.level) && (source.level == 1 || source.level == 2);
}
bool PrimaryMode(void* mode) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(mode);
    std::uintptr_t table = 0; int slot = -1;
    return Current() && Read(address, table) && table == client + off::ModeVtable &&
        Read(address + off::ModeSlot, slot) && slot == 0;
}
bool ProjectFov(std::uintptr_t view, float base, float& output) noexcept {
    unsigned char flags = 0;
    if (!std::isfinite(base) || base <= 0 || base >= 179 || !Read(view + off::ViewFlags, flags)) return false;
    output = base;
    if (flags & off::SkipAspectMask) return true;
    float aspect = 0;
    if (!Read(view + off::ViewAspect, aspect) || !std::isfinite(aspect) || aspect <= 0) return false;
    output = std::atan(std::tan(base * 0.00872664619f) * (aspect * off::AspectScale)) * 57.29577637f * 2.0f;
    return std::isfinite(output) && output > 0 && output < 179;
}
bool Store(void* destination, const void* source, std::size_t size) noexcept {
    __try { std::memcpy(destination, source, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Parameters(void* pawn, void* xyz, float* fov) noexcept {
    parameterCalls.fetch_add(1, std::memory_order_relaxed);
    const auto* allow = viewTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<bool*>(TlsGetValue(viewTls));
    if (allow && !*allow) { original(pawn, xyz, fov); return; }
    if (callbackTls != TLS_OUT_OF_INDEXES && TlsGetValue(callbackTls)) { original(pawn, xyz, fov); return; }
    AcquireSRWLockShared(&callbackLock);
    bool inside = true;
    const bool tlsReady = callbackTls != TLS_OUT_OF_INDEXES && TlsSetValue(callbackTls, &inside);
    const auto settings = ViewmodelGetSettings();
    Source before{}, after{};
    const auto address = reinterpret_cast<std::uintptr_t>(pawn);
    const bool replace = tlsReady && active.load(std::memory_order_acquire) && settings.enabled && Local(before, address);
    original(pawn, xyz, fov);
    float position[3]{}, nativeFov = 0;
    if (replace && Local(after, address) && before == after &&
        MovementCopy(reinterpret_cast<std::uintptr_t>(xyz), position, sizeof(position)) &&
        MovementCopy(reinterpret_cast<std::uintptr_t>(fov), &nativeFov, sizeof(nativeFov)) &&
        std::isfinite(nativeFov) && std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]) &&
        MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(xyz), sizeof(position)) &&
        MovementOwnedWritable(reinterpret_cast<std::uintptr_t>(fov), sizeof(nativeFov))) {
        position[0] += settings.x; position[1] += settings.y; position[2] += settings.z;
        if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]) &&
            Store(xyz, position, sizeof(position)) && Store(fov, &settings.fov, sizeof(settings.fov)))
        {
            parameterEdited.fetch_add(1, std::memory_order_relaxed);
            status.store("viewmodel overrides active", std::memory_order_release);
        }
    }
    if (tlsReady) TlsSetValue(callbackTls, nullptr);
    ReleaseSRWLockShared(&callbackLock);
}
void FinalView(void* mode, void* view) noexcept {
    viewCalls.fetch_add(1, std::memory_order_relaxed);
    auto* outer = viewTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<bool*>(TlsGetValue(viewTls));
    if (outer) {
        const bool previous = *outer; *outer = false;
        originalView(mode, view); *outer = previous; return;
    }
    AcquireSRWLockShared(&viewLock);
    bool allowParameters = true;
    const bool tlsReady = viewTls != TLS_OUT_OF_INDEXES && TlsSetValue(viewTls, &allowParameters);
    Source before{}, after{}, final{};
    std::uintptr_t pawn = 0;
    const bool replace = tlsReady && active.load(std::memory_order_acquire) && ZoomGetSettings().removeScope &&
        PrimaryMode(mode) && Read(client + ent::globals::dwLocalPlayerPawn, pawn) && ScopedLocal(before, pawn);
    originalView(mode, view);
    if (replace) {
        viewEligible.fetch_add(1, std::memory_order_relaxed);
        const auto address = reinterpret_cast<std::uintptr_t>(view);
        float base = 0, projected = 0, native = 0;
        bool valid = ScopedLocal(after, pawn) && before == after && PrimaryMode(mode);
        if (!valid) sourceRejected.fetch_add(1, std::memory_order_relaxed);
        const bool gotBase = valid && ViewmodelUnzoomedFov(pawn, base);
        if (valid && !gotBase) baseRejected.fetch_add(1, std::memory_order_relaxed);
        const bool gotProjection = gotBase && Read(address + off::ViewmodelFov, native) && ProjectFov(address, base, projected);
        if (gotBase && !gotProjection) projectionRejected.fetch_add(1, std::memory_order_relaxed);
        valid = gotProjection && ScopedLocal(final, pawn) && before == final && PrimaryMode(mode) &&
            MovementOwnedWritable(address + off::ViewmodelFov, sizeof(projected));
        if (valid && Store(reinterpret_cast<void*>(address + off::ViewmodelFov), &projected, sizeof(projected))) {
            viewEdited.fetch_add(1, std::memory_order_relaxed);
            lastNative.store(native, std::memory_order_relaxed); lastBase.store(base, std::memory_order_relaxed);
            lastProjected.store(projected, std::memory_order_relaxed);
            status.store("viewmodel scope zoom removed after completed builder", std::memory_order_release);
        } else viewRejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (tlsReady) TlsSetValue(viewTls, nullptr);
    ReleaseSRWLockShared(&viewLock);
}
template<std::size_t N> bool Match(std::ptrdiff_t rva, const unsigned char (&wanted)[N]) noexcept {
    unsigned char actual[N]{};
    return MovementCopy(client + rva, actual, N) && std::memcmp(actual, wanted, N) == 0;
}
}

bool ViewmodelUnzoomedFov(std::uintptr_t pawn, float& fov) noexcept {
    if (callbackTls != TLS_OUT_OF_INDEXES && TlsGetValue(callbackTls)) return false;
    AcquireSRWLockShared(&callbackLock);
    bool inside = true;
    const bool tlsReady = callbackTls != TLS_OUT_OF_INDEXES && TlsSetValue(callbackTls, &inside);
    Source before{}, after{};
    float position[3]{}, nativeFov = 0;
    bool valid = tlsReady && active.load(std::memory_order_acquire) && original && Local(before, pawn);
    const auto settings = ViewmodelGetSettings();
    // The verified helper writes only these caller-owned outputs. Bypassing our
    // hook avoids recursion; apply the same selected base FOV explicitly.
    if (valid) original(reinterpret_cast<void*>(pawn), position, &nativeFov);
    valid = valid && Local(after, pawn) && before == after && std::isfinite(nativeFov) && nativeFov > 0 && nativeFov < 179;
    if (valid) fov = settings.enabled ? settings.fov : nativeFov;
    if (tlsReady) TlsSetValue(callbackTls, nullptr);
    ReleaseSRWLockShared(&callbackLock);
    return valid;
}
bool ViewmodelPrepare(ViewmodelLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!Current() || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != ent::ClientTimestamp || nt.OptionalHeader.SizeOfImage != ent::ClientImageSize ||
        !Match(off::Parameters, off::ParametersBytes) || !Match(off::FovProducer, off::FovProducerBytes) ||
        !Match(off::AspectProducer, off::AspectProducerBytes) || !Match(off::Builder, off::BuilderBytes)) {
        status.store("viewmodel contracts unavailable"); return false;
    }
    callbackTls = TlsAlloc();
    viewTls = TlsAlloc();
    std::uintptr_t builder = 0;
    if (callbackTls == TLS_OUT_OF_INDEXES || viewTls == TLS_OUT_OF_INDEXES ||
        !Read(client + off::ModeVtable + 15 * sizeof(std::uintptr_t), builder) || builder != client + off::Builder) {
        status.store("viewmodel TLS/builder unavailable"); return false;
    }
    auto* target = reinterpret_cast<void*>(client + off::Parameters);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Parameters), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("viewmodel hook unavailable"); return false;
    }
    target = reinterpret_cast<void*>(client + off::Builder);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&FinalView), reinterpret_cast<void**>(&originalView)) != MH_OK ||
        !originalView || MH_QueueEnableHook(target) != MH_OK) {
        status.store("viewmodel final builder hook unavailable"); return false;
    }
    prepared = true;
    status.store("viewmodel ready");
    if (logger) logger("VIEWMODEL_BUILD_VERIFIED revision=2 caller-owned XYZ/base FOV; slot15 completed builder AFTER slot16 and both native FOV overwrites/aspect; no persistent cvar/entity writes");
    return true;
}
void ViewmodelActivate() noexcept {
    AcquireSRWLockExclusive(&callbackLock);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&callbackLock);
}
void ViewmodelStop() noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&viewLock);
    AcquireSRWLockExclusive(&callbackLock);
    stopped = true;
    active.store(false, std::memory_order_release);
    status.store("viewmodel stopped and resident");
    ReleaseSRWLockExclusive(&callbackLock);
    ReleaseSRWLockExclusive(&viewLock);
}
const char* ViewmodelStatus() noexcept { return status.load(std::memory_order_acquire); }
void ViewmodelFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    char text[1000]{};
    const auto settings = ViewmodelGetSettings();
    std::snprintf(text, sizeof(text), "VIEWMODEL revision=2 parameters=%llu parameter_edits=%llu builders=%llu scope_eligible=%llu scope_applied=%llu rejected=%llu source_rejected=%llu base_rejected=%llu projection_rejected=%llu enabled=%d remove_scope=%d native=%.3f base=%.3f projected=%.3f status=%s; counters are not visible-result proof",
        parameterCalls.load(), parameterEdited.load(), viewCalls.load(), viewEligible.load(), viewEdited.load(), viewRejected.load(),
        sourceRejected.load(), baseRejected.load(), projectionRejected.load(), settings.enabled, ZoomGetSettings().removeScope,
        lastNative.load(), lastBase.load(), lastProjected.load(), ViewmodelStatus());
    logger(text);
}
#ifdef CS2_VIEWMODEL_CHECKS
void ViewmodelTestConfigure(std::uintptr_t base, ViewmodelParametersFn native) noexcept {
    client = base; original = native; testing = true; prepared = true; stopped = false;
    if (callbackTls == TLS_OUT_OF_INDEXES) callbackTls = TlsAlloc();
    active.store(true, std::memory_order_release);
}
void ViewmodelTestParameters(void* pawn, void* xyz, float* fov) noexcept { Parameters(pawn, xyz, fov); }
void ViewmodelTestConfigureView(ViewmodelViewFn native) noexcept {
    originalView = native;
    if (viewTls == TLS_OUT_OF_INDEXES) viewTls = TlsAlloc();
}
void ViewmodelTestView(void* mode, void* view) noexcept { FinalView(mode, view); }
#endif
