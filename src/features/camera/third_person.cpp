#include "third_person.h"
#include "third_person_settings.h"
#include "third_person_view.h"
#include "third_person_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
namespace off = third_person_offsets;
using ViewFn = void (*)(void*, void*);
using QueryFn = int (*)(void*, int);
using ShapeFn = void (*)(void*, const float*);
using ResultFn = void (*)(void*);
using FilterFn = void (*)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
using TraceFn = bool (*)(void*, void*, const void*, const void*, void*, void*);
ViewFn originalView = nullptr;
QueryFn originalQuery = nullptr;
ShapeFn initializeShape = nullptr;
ResultFn initializeTrace = nullptr;
FilterFn populateFilter = nullptr;
TraceFn traceShape = nullptr;
std::uintptr_t client = 0;
DWORD scopeTls = TLS_OUT_OF_INDEXES;
SRWLOCK viewLock = SRWLOCK_INIT;
std::atomic<bool> active{false};
bool prepared = false, stopped = false;
std::atomic<const char*> status{"third person not initialized"};
ThirdPersonLogCallback logger = nullptr;
std::atomic<unsigned long long> calls{0}, translated{0}, clipped{0}, rejected{0};
std::atomic<DWORD> lastOwner{0};
ULONGLONG reportTime = 0;
unsigned long long reportedCalls = 0;
#ifdef CS2_THIRD_PERSON_CHECKS
bool testRuntime = false;
#endif
struct Source {
    std::uintptr_t pawn = 0, identity = 0, controller = 0;
    std::uint32_t handle = UINT32_MAX;
    bool operator==(const Source&) const = default;
};
// Copied identity values only: no retained entity is ever dereferenced. A failed
// view clears publication, so later model queries fall back with that view.
SRWLOCK visibilityLock = SRWLOCK_INIT;
Source visibleSource{};
ULONGLONG visibleTime = 0;
void PublishVisibility(const Source& source) noexcept {
    AcquireSRWLockExclusive(&visibilityLock);
    visibleSource = source;
    visibleTime = source.pawn ? GetTickCount64() : 0;
    ReleaseSRWLockExclusive(&visibilityLock);
}
bool VisibleSource(const Source& source) noexcept {
    AcquireSRWLockShared(&visibilityLock);
    const bool visible = visibleSource == source;
    ReleaseSRWLockShared(&visibilityLock);
    return visible;
}
struct Scope { bool replace = false; };
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Write(std::uintptr_t address, const void* value, std::size_t bytes) noexcept {
    if (!value || address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address), value, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(at, bytes, N) && !std::memcmp(bytes, expected, N);
}
bool CurrentClient() noexcept {
#ifdef CS2_THIRD_PERSON_CHECKS
    if (testRuntime) return client != 0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
bool InputReady() noexcept {
    std::uintptr_t input = 0;
    return Read(client + off::InputPointer, input) && input == client + movement_offsets::Input;
}
bool ReadSource(Source& source) noexcept {
    namespace ent = cs2_offsets;
    std::uintptr_t resolved = 0;
    std::uint32_t linked = UINT32_MAX;
    int health = 0; std::uint8_t life = 1;
    return CurrentClient() && Read(client + ent::globals::dwLocalPlayerPawn, source.pawn) && source.pawn &&
        Read(source.pawn + ent::CEntityInstance::m_pEntity, source.identity) && source.identity &&
        Read(source.identity, resolved) && resolved == source.pawn &&
        Read(source.identity + ent::entity_layout::HandleOffset, source.handle) && source.handle != UINT32_MAX &&
        Read(client + ent::globals::dwLocalPlayerController, source.controller) && source.controller &&
        Read(source.controller + ent::CCSPlayerController::m_hPlayerPawn, linked) && linked == source.handle &&
        Read(source.pawn + ent::C_BaseEntity::m_iHealth, health) && health > 0 &&
        Read(source.pawn + ent::C_BaseEntity::m_lifeState, life) && life == 0;
}
bool PrimaryMode(void* mode) noexcept {
    std::uintptr_t vtable = 0;
    int slot = -1;
    return CurrentClient() && InputReady() && Read(reinterpret_cast<std::uintptr_t>(mode), vtable) &&
        (vtable == client + off::ModeSharedVtable || vtable == client + off::ModeNormalVtable) &&
        Read(reinterpret_cast<std::uintptr_t>(mode) + off::ModeSlot, slot) && slot == 0;
}
int Query(void* input, int slot, std::uintptr_t caller) noexcept {
    // Only the base view's final translation is suppressed. Other native
    // consumers retain third-person model visibility. Scope lives on this
    // callback's stack via dynamic TLS, never in static TLS or game memory.
    const auto* scope = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(scopeTls));
    if (scope && caller == client + off::IsThirdPersonCameraReturn &&
        reinterpret_cast<std::uintptr_t>(input) == client + movement_offsets::Input && slot == 0)
        return scope->replace ? 0 : originalQuery(input, slot);
    Source source{};
    if (active.load(std::memory_order_acquire) && ThirdPersonGetSettings().enabled && slot == 0 &&
        reinterpret_cast<std::uintptr_t>(input) == client + movement_offsets::Input && InputReady() && ReadSource(source) &&
        ((scope && scope->replace) || VisibleSource(source))) return 1;
    return originalQuery(input, slot);
}
int HookQuery(void* input, int slot) noexcept {
    return Query(input, slot, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
template<class T> void Put(unsigned char* buffer, std::size_t offset, T value) noexcept {
    std::memcpy(buffer + offset, &value, sizeof(value));
}
bool Sweep(const Source& source, const CameraVector& eye, const CameraVector& end, float& fraction) noexcept {
    std::uintptr_t manager = 0;
    if (!Read(client + off::TraceManager, manager) || !manager) return false;
    alignas(16) unsigned char shape[off::ShapeSize]{};
    alignas(16) unsigned char result[off::TraceSize]{};
    alignas(8) unsigned char filter[off::FilterSize]{};
    const float hull[]{-off::HullExtent, -off::HullExtent, -off::HullExtent,
        off::HullExtent, off::HullExtent, off::HullExtent};
    Put(filter, 0, client + off::FilterVtable);
    for (std::size_t offset : {0x20, 0x24, 0x28, 0x2C}) Put(filter, offset, UINT32_MAX);
    Put(filter, 0x34, static_cast<std::uint16_t>(UINT16_MAX));
    filter[0x37] = 0x0F; filter[0x38] = 3; filter[0x39] = 0x49;
    populateFilter(filter, reinterpret_cast<void*>(source.pawn), off::CameraMask, off::CameraLayer, 0x0F);
    std::uint64_t exclude = 0;
    std::memcpy(&exclude, filter + 0x10, sizeof(exclude));
    Put(filter, 0x10, exclude | off::CameraExclude);
    initializeShape(shape, hull);
    initializeTrace(result);
    // A false trace return can mean an unobstructed sweep. Native camera
    // code consumes the initialized fraction independently of this bool.
    traceShape(reinterpret_cast<void*>(manager), shape, &eye, &end, filter, result);
    std::memcpy(&fraction, result + off::TraceFraction, sizeof(fraction));
    return true;
}
void HookView(void* mode, void* view) noexcept {
    calls.fetch_add(1, std::memory_order_relaxed);
    Scope* previous = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(scopeTls));
    // Reentrant views are native-owned. Do not reuse outer view decisions.
    if (previous) {
        Scope native{};
        if (!TlsSetValue(scopeTls, &native)) { originalView(mode, view); return; }
        originalView(mode, view);
        TlsSetValue(scopeTls, previous);
        return;
    }
    AcquireSRWLockShared(&viewLock);
    const auto settings = ThirdPersonGetSettings();
    Source before{};
    Scope scope{active.load(std::memory_order_acquire) && settings.enabled && PrimaryMode(mode) && ReadSource(before)};
    if (scopeTls == TLS_OUT_OF_INDEXES || !TlsSetValue(scopeTls, &scope)) {
        scope.replace = false;
        active.store(false, std::memory_order_release);
    }
    originalView(mode, view);
    // The native base origin and angles already exist at this stage. Preserve
    // every angle/FOV/effect; modify just this current view's origin afterward.
    if (scope.replace) {
        Source after{};
        CameraVector eye{}, angles{}, destination{};
        const auto address = reinterpret_cast<std::uintptr_t>(view);
        bool valid = ReadSource(after) && after == before && PrimaryMode(mode) &&
            Read(address + off::ViewOrigin, eye) && Read(address + off::ViewAngles, angles) &&
            ThirdPersonDestination(eye, angles, settings, destination);
        float fraction = 1;
        if (valid && settings.collision) valid = Sweep(after, eye, destination, fraction) &&
            ThirdPersonClip(eye, destination, fraction, destination);
        // A trace may invoke native callbacks; revalidate entity/module source
        // before touching the view. Keep no engine pointers across callbacks.
        Source finalSource{};
        valid = valid && ReadSource(finalSource) && finalSource == before;
        if (valid && Write(address + off::ViewOrigin, &destination, sizeof(destination))) {
            PublishVisibility(before);
            translated.fetch_add(1, std::memory_order_relaxed);
            if (fraction < 1) clipped.fetch_add(1, std::memory_order_relaxed);
            lastOwner.store(GetCurrentThreadId(), std::memory_order_relaxed);
            status.store("third person active", std::memory_order_release);
        } else {
            PublishVisibility({});
            rejected.fetch_add(1, std::memory_order_relaxed);
            status.store("camera source/trace unavailable; native view retained", std::memory_order_release);
        }
    } else if (active.load(std::memory_order_acquire)) {
        PublishVisibility({});
        status.store(settings.enabled ? "awaiting live local camera" : "third person disabled", std::memory_order_release);
    }
    if (scopeTls != TLS_OUT_OF_INDEXES) TlsSetValue(scopeTls, nullptr);
    ReleaseSRWLockShared(&viewLock);
}
}

bool ThirdPersonPrepare(ThirdPersonLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t shared = 0, normal = 0, inputVtable = 0;
    if (!Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Match(client + off::OverrideView, off::OverrideViewBytes) || !Match(client + off::IsThirdPerson, off::IsThirdPersonBytes) ||
        !Match(client + off::ModeSlotGetter, off::ModeSlotGetterBytes) ||
        !Match(client + off::InitializeShape, off::InitializeShapeBytes) ||
        !Match(client + off::InitializeTrace, off::InitializeTraceBytes) ||
        !Match(client + off::PopulateFilter, off::PopulateFilterBytes) || !Match(client + off::TraceShape, off::TraceShapeBytes) ||
        !InputReady() || !Read(client + movement_offsets::Input, inputVtable) || inputVtable != client + movement_offsets::InputVtable ||
        !Read(client + off::ModeSharedVtable + 16 * sizeof(std::uintptr_t), shared) || shared != client + off::OverrideView ||
        !Read(client + off::ModeNormalVtable + 32 * sizeof(std::uintptr_t), normal) || normal != client + off::ModeSlotGetter) {
        status.store("third person build verification failed", std::memory_order_release); return false;
    }
    scopeTls = TlsAlloc();
    if (scopeTls == TLS_OUT_OF_INDEXES) { status.store("camera dynamic TLS unavailable", std::memory_order_release); return false; }
    initializeShape = reinterpret_cast<ShapeFn>(client + off::InitializeShape);
    initializeTrace = reinterpret_cast<ResultFn>(client + off::InitializeTrace);
    populateFilter = reinterpret_cast<FilterFn>(client + off::PopulateFilter);
    traceShape = reinterpret_cast<TraceFn>(client + off::TraceShape);
    auto* view = reinterpret_cast<void*>(client + off::OverrideView);
    auto* query = reinterpret_cast<void*>(client + off::IsThirdPerson);
    if (MH_CreateHook(view, reinterpret_cast<void*>(&HookView), reinterpret_cast<void**>(&originalView)) != MH_OK || !originalView ||
        MH_CreateHook(query, reinterpret_cast<void*>(&HookQuery), reinterpret_cast<void**>(&originalQuery)) != MH_OK || !originalQuery ||
        MH_QueueEnableHook(view) != MH_OK || MH_QueueEnableHook(query) != MH_OK) {
        status.store("third person hook preparation failed", std::memory_order_release); return false;
    }
    prepared = true;
    status.store("third person disabled", std::memory_order_release);
    if (logger) logger("THIRD_PERSON_BUILD_VERIFIED base_view/query; slot0 alive pawn; native camera hull=9; no input/cvar/command writes");
    return true;
}
void ThirdPersonActivate() noexcept {
    AcquireSRWLockExclusive(&viewLock);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&viewLock);
}
void ThirdPersonStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&viewLock);
    stopped = true;
    PublishVisibility({});
    active.store(false, std::memory_order_release);
    status.store("third person stopped and resident", std::memory_order_release);
    ReleaseSRWLockExclusive(&viewLock);
    if (logger) {
        char text[384]{};
        std::snprintf(text, sizeof(text), "THIRD_PERSON_STOPPED_RESIDENT reason=%s; view edits drained; original forwarding retained; no physical unload", reason ? reason : "stop");
        logger(text);
    }
}
const char* ThirdPersonStatus() noexcept { return status.load(std::memory_order_acquire); }
bool ThirdPersonViewActive(std::uintptr_t pawn, std::uintptr_t identity,
    std::uintptr_t controller, std::uint32_t handle) noexcept {
    if (!pawn || !identity || !controller || handle == UINT32_MAX ||
        !active.load(std::memory_order_acquire) || !ThirdPersonGetSettings().enabled) return false;
    const Source expected{pawn, identity, controller, handle};
    Source current{};
    if (!ReadSource(current) || !(current == expected)) return false;
    AcquireSRWLockShared(&visibilityLock);
    const auto now = GetTickCount64();
    const bool visible = visibleSource == expected && now >= visibleTime && now - visibleTime <= 250;
    ReleaseSRWLockShared(&visibilityLock);
    return visible && active.load(std::memory_order_acquire) && ThirdPersonGetSettings().enabled;
}
void ThirdPersonFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto count = calls.load(std::memory_order_relaxed);
    if (count == reportedCalls) return;
    reportedCalls = count;
    char text[512]{};
    std::snprintf(text, sizeof(text), "THIRD_PERSON views=%llu moved=%llu clipped=%llu rejected=%llu owner=%lu enabled=%d status=%s; counters do not prove visible output", count,
        translated.load(std::memory_order_relaxed), clipped.load(std::memory_order_relaxed), rejected.load(std::memory_order_relaxed),
        lastOwner.load(std::memory_order_relaxed), ThirdPersonGetSettings().enabled, ThirdPersonStatus());
    logger(text);
}

void ThirdPersonPollControls(bool foreground) noexcept {
    static bool wasDown = false;
    const bool down = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (foreground && active.load(std::memory_order_acquire) && down && !wasDown) {
        auto settings = ThirdPersonGetSettings();
        settings.enabled = !settings.enabled;
        ThirdPersonSetSettings(settings);
    }
    wasDown = down;
}

#ifdef CS2_THIRD_PERSON_CHECKS
void ThirdPersonTestConfigure(std::uintptr_t base, CameraViewFn view, CameraQueryFn query,
    CameraShapeFn shape, CameraResultFn result, CameraFilterFn filter, CameraTraceFn trace) noexcept {
    client = base; originalView = view; originalQuery = query;
    initializeShape = shape; initializeTrace = result; populateFilter = filter; traceShape = trace;
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    prepared = true; stopped = false; testRuntime = true;
    PublishVisibility({});
    active.store(true, std::memory_order_release);
}
void ThirdPersonTestView(void* mode, void* view) noexcept { HookView(mode, view); }
int ThirdPersonTestQuery(void* input, int slot, std::uintptr_t caller) noexcept { return Query(input, slot, caller); }
#endif
