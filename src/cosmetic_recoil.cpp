#include "cosmetic_recoil.h"
#include "cosmetic_recoil_offsets.h"
#include "viewmodel_settings.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {
namespace off = cosmetic_recoil_offsets;
namespace ent = cs2_offsets;
using NativeFn = void* (*)(void*, void*);
using CameraFn = void (*)(void*, void*, void*, float*);
using AngleAddFn = void (*)(const float*, const float*, float*);
NativeFn original = nullptr;
CameraFn originalCameraBase = nullptr, originalCamera = nullptr;
AngleAddFn originalAdd = nullptr;
std::uintptr_t client = 0;
DWORD callbackTls = TLS_OUT_OF_INDEXES;
SRWLOCK callbackLock = SRWLOCK_INIT;
std::atomic<bool> active{false};
bool prepared = false, stopped = false;
std::atomic<const char*> status{"cosmetic recoil not initialized"};
CosmeticRecoilLogCallback logger = nullptr;
enum class Failure : unsigned {
    None, Module, System, PawnGlobal, PawnIdentity, PawnBucket,
    ControllerGlobal, ControllerIdentity, ControllerBucket, ControllerPawn,
    Health, Life, ServiceLink, ServiceTable, ServiceOwner, CameraLink, CameraTable, CameraOwner, Count
};
constexpr const char* FailureNames[]{"none", "module", "system", "pawn_global", "pawn_identity", "pawn_bucket",
    "controller_global", "controller_identity", "controller_bucket", "controller_pawn",
    "health", "life", "service_link", "service_table", "service_owner", "camera_link", "camera_table", "camera_owner"};
std::atomic<unsigned long long> calls{0}, cameraBaseCalls{0}, cameraCalls{0}, otherCalls{0};
std::atomic<unsigned long long> selectedCalls{0}, nestedCalls{0}, beforeRejected{0}, afterRejected{0};
std::atomic<unsigned long long> changedSources{0}, returnRejected{0}, outputRejected{0};
std::atomic<unsigned long long> cameraEdits{0}, nonzeroEdits{0};
std::atomic<unsigned long long> cameraCoreCalls{0}, cameraCoreRejected{0}, viewPunchCalls{0}, viewPunchEdits{0}, viewPunchNonzero{0};
std::atomic<float> lastViewPitch{0}, lastViewYaw{0}, lastViewRoll{0};
std::atomic<unsigned long long> sourceFailures[static_cast<unsigned>(Failure::Count)]{};
std::atomic<Failure> lastFailure{Failure::None};
std::atomic<std::uintptr_t> lastCaller{0}, lastService{0}, lastPawn{0}, lastTable{0}, lastOwner{0};
std::atomic<float> lastPitch{0}, lastYaw{0}, lastRoll{0};
ULONGLONG reportTime = 0;
unsigned long long reportedCalls = UINT64_MAX;
int reportedCamera = -1, reportedActive = -1;
#ifdef CS2_COSMETIC_RECOIL_CHECKS
bool testing = false;
#endif
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Current() noexcept {
#ifdef CS2_COSMETIC_RECOIL_CHECKS
    if (testing) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
struct Source {
    std::uintptr_t pawn = 0, pawnIdentity = 0, controller = 0, controllerIdentity = 0, system = 0, service = 0;
    std::uint32_t pawnHandle = UINT32_MAX, controllerHandle = UINT32_MAX;
    bool operator==(const Source&) const = default;
};
struct CameraSource {
    Source source;
    std::uintptr_t camera = 0, table = 0;
    bool operator==(const CameraSource&) const = default;
};
enum class ScopeKind { VisualGetter, Camera };
struct Scope {
    ScopeKind kind = ScopeKind::VisualGetter;
    CameraSource camera;
    std::uintptr_t angles = 0;
};
bool Reject(Failure reason) noexcept {
    sourceFailures[static_cast<unsigned>(reason)].fetch_add(1, std::memory_order_relaxed);
    lastFailure.store(reason, std::memory_order_relaxed);
    return false;
}
bool Entity(std::uintptr_t system, std::uintptr_t object, std::uintptr_t& identity, std::uint32_t& handle,
    Failure identityFailure, Failure bucketFailure) noexcept {
    std::uintptr_t resolved = 0, bucket = 0;
    if (!object || !Read(object + ent::CEntityInstance::m_pEntity, identity) || !identity ||
        !Read(identity, resolved) || resolved != object ||
        !Read(identity + ent::entity_layout::HandleOffset, handle) || handle == UINT32_MAX) return Reject(identityFailure);
    const auto index = handle & ent::entity_layout::IndexMask;
    return (Read(system + ent::entity_layout::TableOffset +
            (index >> ent::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) && bucket &&
        identity == bucket + (index & ent::entity_layout::BucketMask) * ent::entity_layout::IdentityStride) || Reject(bucketFailure);
}
bool ReadSource(void* service, Source& out) noexcept {
    std::uintptr_t owner = 0, table = 0;
    std::uint32_t linked = UINT32_MAX;
    int health = 0;
    unsigned char life = 1;
    if (!Current()) return Reject(Failure::Module);
    if (!Read(client + ent::globals::dwGameEntitySystem, out.system) || !out.system) return Reject(Failure::System);
    if (!Read(client + ent::globals::dwLocalPlayerPawn, out.pawn) || !out.pawn) return Reject(Failure::PawnGlobal);
    lastPawn.store(out.pawn, std::memory_order_relaxed);
    if (!Entity(out.system, out.pawn, out.pawnIdentity, out.pawnHandle, Failure::PawnIdentity, Failure::PawnBucket)) return false;
    if (!Read(client + ent::globals::dwLocalPlayerController, out.controller) || !out.controller) return Reject(Failure::ControllerGlobal);
    if (!Entity(out.system, out.controller, out.controllerIdentity, out.controllerHandle,
        Failure::ControllerIdentity, Failure::ControllerBucket)) return false;
    if (!Read(out.controller + ent::CCSPlayerController::m_hPlayerPawn, linked) || linked != out.pawnHandle) return Reject(Failure::ControllerPawn);
    if (!Read(out.pawn + ent::C_BaseEntity::m_iHealth, health) || health <= 0) return Reject(Failure::Health);
    if (!Read(out.pawn + ent::C_BaseEntity::m_lifeState, life) || life != 0) return Reject(Failure::Life);
    if (!Read(out.pawn + off::PunchServices, out.service) || out.service != reinterpret_cast<std::uintptr_t>(service)) return Reject(Failure::ServiceLink);
    const bool gotTable = Read(out.service, table);
    lastTable.store(table, std::memory_order_relaxed);
    if (!gotTable || table != client + off::PunchVtable) return Reject(Failure::ServiceTable);
    const bool gotOwner = Read(out.service + off::ServiceOwner, owner);
    lastOwner.store(owner, std::memory_order_relaxed);
    if (!gotOwner || owner != out.pawn) return Reject(Failure::ServiceOwner);
    return true;
}
bool ReadCameraSource(void* camera, CameraSource& out) noexcept {
    out.camera = reinterpret_cast<std::uintptr_t>(camera);
    std::uintptr_t owner = 0, punch = 0, currentCamera = 0;
    if (!Read(out.camera, out.table) ||
        (out.table != client + off::CameraVtable && out.table != client + off::CameraBaseVtable)) return Reject(Failure::CameraTable);
    if (!Read(out.camera + off::ServiceOwner, owner) || !owner ||
        !Read(owner + off::PunchServices, punch) || !ReadSource(reinterpret_cast<void*>(punch), out.source)) return Reject(Failure::CameraOwner);
    if (owner != out.source.pawn) return Reject(Failure::CameraOwner);
    if (!Read(out.source.pawn + off::CameraServices, currentCamera) || currentCamera != out.camera) return Reject(Failure::CameraLink);
    return true;
}
bool ClearOutput(void* output) noexcept {
    float angles[3]{};
    const auto address = reinterpret_cast<std::uintptr_t>(output);
    if (!MovementCopy(address, angles, sizeof(angles)) || !std::isfinite(angles[0]) ||
        !std::isfinite(angles[1]) || !std::isfinite(angles[2]) ||
        !MovementOwnedWritable(address, sizeof(angles))) return false;
    __try { std::memset(output, 0, sizeof(angles)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    lastPitch.store(angles[0], std::memory_order_relaxed);
    lastYaw.store(angles[1], std::memory_order_relaxed);
    lastRoll.store(angles[2], std::memory_order_relaxed);
    if (angles[0] != 0 || angles[1] != 0 || angles[2] != 0) nonzeroEdits.fetch_add(1, std::memory_order_relaxed);
    return true;
}
void* Produce(void* service, void* output, std::uintptr_t caller) noexcept {
    calls.fetch_add(1, std::memory_order_relaxed);
    const bool cameraBase = caller == client + off::CameraBaseReturn;
    const bool camera = caller == client + off::CameraReturn;
    if (cameraBase) cameraBaseCalls.fetch_add(1, std::memory_order_relaxed);
    else if (camera) cameraCalls.fetch_add(1, std::memory_order_relaxed);
    else otherCalls.fetch_add(1, std::memory_order_relaxed);
    lastCaller.store(caller, std::memory_order_relaxed);
    lastService.store(reinterpret_cast<std::uintptr_t>(service), std::memory_order_relaxed);
    // Reentrant native evaluation keeps its own output. The outer stack source
    // and settings are never borrowed by nested calls.
    auto* previous = callbackTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(callbackTls));
    if (previous && previous->kind != ScopeKind::Camera) {
        nestedCalls.fetch_add(1, std::memory_order_relaxed);
        return original(service, output);
    }
    // A selected camera core owns the shared barrier. Its expected internal
    // getter may edit .45 aim-punch without recursively acquiring that SRW lock.
    const bool ownsLock = previous == nullptr;
    if (ownsLock) AcquireSRWLockShared(&callbackLock);
    Scope entered;
    const bool tlsReady = callbackTls != TLS_OUT_OF_INDEXES && TlsSetValue(callbackTls, &entered);
    const auto settings = ViewmodelGetSettings();
    const bool selected = settings.removeCameraRecoil && (cameraBase || camera);
    Source before{};
    const bool requested = tlsReady && active.load(std::memory_order_acquire) && selected;
    if (requested) selectedCalls.fetch_add(1, std::memory_order_relaxed);
    const bool sourceValid = requested && ReadSource(service, before);
    CameraSource currentCamera{};
    const bool replace = sourceValid && (!previous || (before == previous->camera.source &&
        ReadCameraSource(reinterpret_cast<void*>(previous->camera.camera), currentCamera) && currentCamera == previous->camera));
    if (sourceValid && !replace) changedSources.fetch_add(1, std::memory_order_relaxed);
    if (requested && !replace) {
        beforeRejected.fetch_add(1, std::memory_order_relaxed);
        status.store("cosmetic recoil source unavailable before native", std::memory_order_release);
    }
    // The native getter owns interpolation/cache updates. Keep that execution,
    // then alter only its caller-owned temporary visual angles, never service,
    // pawn, input, prediction, shot or aim state.
    void* result = original(service, output);
    if (replace) {
        Source after{};
        bool applied = false;
        if (result != output) returnRejected.fetch_add(1, std::memory_order_relaxed);
        else if (!ReadSource(service, after)) afterRejected.fetch_add(1, std::memory_order_relaxed);
        else if (before != after) changedSources.fetch_add(1, std::memory_order_relaxed);
        else if (!ClearOutput(output)) outputRejected.fetch_add(1, std::memory_order_relaxed);
        else applied = true;
        if (applied) {
            cameraEdits.fetch_add(1, std::memory_order_relaxed);
            status.store("cosmetic recoil active", std::memory_order_release);
        } else status.store("cosmetic recoil source/output unavailable; native retained", std::memory_order_release);
    }
    if (tlsReady) TlsSetValue(callbackTls, previous);
    if (ownsLock) ReleaseSRWLockShared(&callbackLock);
    return result;
}
__declspec(noinline) void* Hook(void* service, void* output) noexcept {
    return Produce(service, output, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
void Camera(CameraFn native, void* camera, void* origin, void* angles, float* fov) noexcept {
    cameraCoreCalls.fetch_add(1, std::memory_order_relaxed);
    if (callbackTls == TLS_OUT_OF_INDEXES) { native(camera, origin, angles, fov); return; }
    if (auto* previous = static_cast<Scope*>(TlsGetValue(callbackTls))) {
        // A nested view owns different temporary angle buffers. Hide the outer
        // camera edit scope for its entire native execution, including getter.
        const auto kind = previous->kind;
        previous->kind = ScopeKind::VisualGetter;
        native(camera, origin, angles, fov);
        previous->kind = kind;
        return;
    }
    AcquireSRWLockShared(&callbackLock);
    Scope scope;
    scope.kind = ScopeKind::Camera;
    scope.angles = reinterpret_cast<std::uintptr_t>(angles);
    const auto settings = ViewmodelGetSettings();
    const bool requested = active.load(std::memory_order_acquire) && settings.removeCameraRecoil;
    const bool sourceValid = requested && ReadCameraSource(camera, scope.camera);
    const bool replace = sourceValid && TlsSetValue(callbackTls, &scope);
    if (!replace) {
        if (requested) cameraCoreRejected.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockShared(&callbackLock);
        native(camera, origin, angles, fov);
        return;
    }
    native(camera, origin, angles, fov);
    TlsSetValue(callbackTls, nullptr);
    ReleaseSRWLockShared(&callbackLock);
}
void HookCameraBase(void* camera, void* origin, void* angles, float* fov) noexcept {
    Camera(originalCameraBase, camera, origin, angles, fov);
}
void HookCamera(void* camera, void* origin, void* angles, float* fov) noexcept {
    Camera(originalCamera, camera, origin, angles, fov);
}
void Add(const float* left, const float* right, float* output, std::uintptr_t caller) noexcept {
    auto* scope = callbackTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(callbackTls));
    if (!scope || scope->kind != ScopeKind::Camera ||
        (caller != client + off::ViewPunchBaseReturn && caller != client + off::ViewPunchReturn)) {
        originalAdd(left, right, output); return;
    }
    viewPunchCalls.fetch_add(1, std::memory_order_relaxed);
    CameraSource current{};
    float punch[3]{};
    const bool valid = reinterpret_cast<std::uintptr_t>(left) == scope->angles &&
        reinterpret_cast<std::uintptr_t>(output) == scope->angles &&
        ReadCameraSource(reinterpret_cast<void*>(scope->camera.camera), current) && current == scope->camera &&
        MovementCopy(reinterpret_cast<std::uintptr_t>(right), punch, sizeof(punch)) &&
        std::isfinite(punch[0]) && std::isfinite(punch[1]) && std::isfinite(punch[2]);
    if (!valid) { originalAdd(left, right, output); return; }
    // The leaf is void(float[3]*left,float[3]*right,float[3]*out); callers ignore
    // return registers. Substitute only this stack-owned visual operand. Native
    // service/prediction view-punch and every other angle-add caller stay intact.
    const float zero[3]{};
    originalAdd(left, zero, output);
    viewPunchEdits.fetch_add(1, std::memory_order_relaxed);
    if (punch[0] != 0 || punch[1] != 0 || punch[2] != 0) viewPunchNonzero.fetch_add(1, std::memory_order_relaxed);
    lastViewPitch.store(punch[0], std::memory_order_relaxed);
    lastViewYaw.store(punch[1], std::memory_order_relaxed);
    lastViewRoll.store(punch[2], std::memory_order_relaxed);
}
__declspec(noinline) void HookAdd(const float* left, const float* right, float* output) noexcept {
    Add(left, right, output, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
template<std::size_t N> bool Match(std::ptrdiff_t rva, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(client + rva, bytes, N) && !std::memcmp(bytes, expected, N);
}
bool Identity() noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return Current() && Read(client, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 && Read(client + dos.e_lfanew, nt) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == ent::ClientTimestamp && nt.OptionalHeader.SizeOfImage == ent::ClientImageSize &&
        Match(off::VisualPunch, off::VisualPunchBytes) && Match(off::CameraBaseCaller, off::CameraBaseCallerBytes) &&
        Match(off::CameraCaller, off::CameraCallerBytes) &&
        Match(off::CameraBaseCore, off::CameraBaseCoreBytes) && Match(off::CameraCore, off::CameraCoreBytes) &&
        Match(off::AngleAdd, off::AngleAddBytes) && Match(off::ViewPunchBaseCaller, off::ViewPunchBaseCallerBytes) &&
        Match(off::ViewPunchCaller, off::ViewPunchCallerBytes);
}
}
bool CosmeticRecoilPrepare(CosmeticRecoilLogCallback callback) noexcept {
    logger = callback;
    if (prepared) return true;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Identity()) { status.store("cosmetic recoil contracts unavailable"); return false; }
    callbackTls = TlsAlloc();
    if (callbackTls == TLS_OUT_OF_INDEXES) { status.store("cosmetic recoil dynamic TLS unavailable"); return false; }
    auto* target = reinterpret_cast<void*>(client + off::VisualPunch);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("cosmetic recoil hook unavailable"); return false;
    }
    auto* baseTarget = reinterpret_cast<void*>(client + off::CameraBaseCore);
    auto* cameraTarget = reinterpret_cast<void*>(client + off::CameraCore);
    auto* addTarget = reinterpret_cast<void*>(client + off::AngleAdd);
    if (MH_CreateHook(baseTarget, reinterpret_cast<void*>(&HookCameraBase), reinterpret_cast<void**>(&originalCameraBase)) != MH_OK ||
        !originalCameraBase || MH_CreateHook(cameraTarget, reinterpret_cast<void*>(&HookCamera), reinterpret_cast<void**>(&originalCamera)) != MH_OK ||
        !originalCamera || MH_CreateHook(addTarget, reinterpret_cast<void*>(&HookAdd), reinterpret_cast<void**>(&originalAdd)) != MH_OK ||
        !originalAdd || MH_QueueEnableHook(baseTarget) != MH_OK || MH_QueueEnableHook(cameraTarget) != MH_OK || MH_QueueEnableHook(addTarget) != MH_OK) {
        status.store("cosmetic camera punch hooks unavailable"); return false;
    }
    prepared = true;
    status.store("cosmetic recoil ready");
    if (logger) logger("COSMETIC_RECOIL_BUILD_VERIFIED revision=3 native camera core scope removes copied view-punch plus visual aim-punch; native service/shot/prediction recoil preserved; stopped code remains resident");
    return true;
}
void CosmeticRecoilActivate() noexcept {
    AcquireSRWLockExclusive(&callbackLock);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&callbackLock);
}
void CosmeticRecoilStop() noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&callbackLock);
    stopped = true;
    active.store(false, std::memory_order_release);
    status.store("cosmetic recoil stopped and resident");
    ReleaseSRWLockExclusive(&callbackLock);
}
const char* CosmeticRecoilStatus() noexcept { return status.load(std::memory_order_acquire); }
void CosmeticRecoilFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto count = calls.load(std::memory_order_relaxed);
    const auto settings = ViewmodelGetSettings();
    const int armed = active.load();
    if (count == reportedCalls && reportedCamera == static_cast<int>(settings.removeCameraRecoil) &&
        reportedActive == armed) return;
    reportedCalls = count;
    reportedCamera = settings.removeCameraRecoil; reportedActive = armed;
    char text[1800]{};
    const auto caller = lastCaller.load(std::memory_order_relaxed);
    const auto table = lastTable.load(std::memory_order_relaxed);
    std::snprintf(text, sizeof(text),
        "COSMETIC_RECOIL revision=3 calls=%llu camera_base=%llu camera=%llu other=%llu nested=%llu selected=%llu camera_edits=%llu nonzero=%llu before_rejected=%llu after_rejected=%llu changed=%llu return_rejected=%llu output_rejected=%llu remove_camera=%d active=%d last_failure=%s last_caller_rva=0x%llx last_service=0x%llx last_pawn=0x%llx last_table_rva=0x%llx last_owner=0x%llx last_native_punch=(%.4f,%.4f,%.4f) status=\"%s\"; callback samples/counters do not prove visible output",
        count, cameraBaseCalls.load(), cameraCalls.load(), otherCalls.load(), nestedCalls.load(),
        selectedCalls.load(), cameraEdits.load(), nonzeroEdits.load(), beforeRejected.load(),
        afterRejected.load(), changedSources.load(), returnRejected.load(), outputRejected.load(),
        settings.removeCameraRecoil, armed, FailureNames[static_cast<unsigned>(lastFailure.load())],
        static_cast<unsigned long long>(caller >= client ? caller - client : caller),
        static_cast<unsigned long long>(lastService.load()), static_cast<unsigned long long>(lastPawn.load()),
        static_cast<unsigned long long>(table >= client ? table - client : table), static_cast<unsigned long long>(lastOwner.load()),
        lastPitch.load(), lastYaw.load(), lastRoll.load(), CosmeticRecoilStatus());
    logger(text);
    std::snprintf(text, sizeof(text),
        "COSMETIC_RECOIL_SOURCE module=%llu system=%llu pawn_global=%llu pawn_identity=%llu pawn_bucket=%llu controller_global=%llu controller_identity=%llu controller_bucket=%llu controller_pawn=%llu health=%llu life=%llu service_link=%llu service_table=%llu service_owner=%llu camera_link=%llu camera_table=%llu camera_owner=%llu; cumulative source rejection stages",
        sourceFailures[1].load(), sourceFailures[2].load(), sourceFailures[3].load(), sourceFailures[4].load(),
        sourceFailures[5].load(), sourceFailures[6].load(), sourceFailures[7].load(), sourceFailures[8].load(),
        sourceFailures[9].load(), sourceFailures[10].load(), sourceFailures[11].load(), sourceFailures[12].load(),
        sourceFailures[13].load(), sourceFailures[14].load(), sourceFailures[15].load(), sourceFailures[16].load(), sourceFailures[17].load());
    logger(text);
    std::snprintf(text, sizeof(text),
        "COSMETIC_CAMERA core_calls=%llu core_rejected=%llu viewpunch_calls=%llu viewpunch_edits=%llu viewpunch_nonzero=%llu last_viewpunch=(%.4f,%.4f,%.4f); copied camera-service visual impulse only; persistent state retained",
        cameraCoreCalls.load(), cameraCoreRejected.load(), viewPunchCalls.load(), viewPunchEdits.load(), viewPunchNonzero.load(),
        lastViewPitch.load(), lastViewYaw.load(), lastViewRoll.load());
    logger(text);
}
#ifdef CS2_COSMETIC_RECOIL_CHECKS
void CosmeticRecoilTestConfigure(std::uintptr_t base, CosmeticRecoilNativeFn native) noexcept {
    client = base; original = native; testing = true; prepared = native != nullptr; stopped = false;
    if (callbackTls == TLS_OUT_OF_INDEXES) callbackTls = TlsAlloc();
    active.store(prepared, std::memory_order_release);
    calls.store(0); cameraBaseCalls.store(0); cameraCalls.store(0); otherCalls.store(0);
    selectedCalls.store(0); nestedCalls.store(0); beforeRejected.store(0); afterRejected.store(0);
    changedSources.store(0); returnRejected.store(0); outputRejected.store(0);
    cameraEdits.store(0); nonzeroEdits.store(0);
    cameraCoreCalls.store(0); cameraCoreRejected.store(0); viewPunchCalls.store(0); viewPunchEdits.store(0); viewPunchNonzero.store(0);
    lastViewPitch.store(0); lastViewYaw.store(0); lastViewRoll.store(0);
    for (auto& value : sourceFailures) value.store(0);
    lastFailure.store(Failure::None); lastCaller.store(0); lastService.store(0); lastPawn.store(0); lastTable.store(0); lastOwner.store(0);
    lastPitch.store(0); lastYaw.store(0); lastRoll.store(0);
    logger = nullptr; reportTime = 0; reportedCalls = UINT64_MAX;
    reportedCamera = reportedActive = -1;
}
void* CosmeticRecoilTestCall(void* service, void* output, std::uintptr_t caller) noexcept { return Produce(service, output, caller); }
void CosmeticRecoilTestSetLogger(CosmeticRecoilLogCallback callback) noexcept {
    logger = callback; reportTime = 0; reportedCalls = UINT64_MAX;
    reportedCamera = reportedActive = -1;
}
void CosmeticRecoilTestConfigureCamera(CosmeticRecoilCameraFn base, CosmeticRecoilCameraFn normal, CosmeticRecoilAngleAddFn add) noexcept {
    originalCameraBase = base; originalCamera = normal; originalAdd = add;
}
void CosmeticRecoilTestCamera(void* camera, void* origin, void* angles, float* fov, bool base) noexcept {
    Camera(base ? originalCameraBase : originalCamera, camera, origin, angles, fov);
}
void CosmeticRecoilTestAngleAdd(const float* left, const float* right, float* output, std::uintptr_t caller) noexcept {
    Add(left, right, output, caller);
}

#endif
