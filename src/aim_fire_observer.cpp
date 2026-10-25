#include "aim_fire_observer.h"
#include "aim_fire_observer_offsets.h"
#include "aim_weapon_offsets.h"
#include "aim_spread_offsets.h"
#include "silent_aim_offsets.h"
#include "cs2_offsets.h"
#include "shot_log_settings.h"
#include "movement_memory.h"
#include "movement_offsets.h"
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off = aim_fire_observer_offsets;
namespace ent = cs2_offsets;
namespace weaponOff = aim_weapon_offsets;
using Native = void* (*)(void*, void*, int);
using NativeSpread = float (*)(void*);
using NativeInaccuracy = float (*)(void*, float*, float*);
Native original = nullptr;
NativeSpread originalSpread = nullptr;
NativeInaccuracy originalInaccuracy = nullptr;
std::uintptr_t client = 0;
AimFireObserverLog logger = nullptr;
std::atomic<bool> active{false};
std::atomic<unsigned> inFlight{0};
bool prepared = false, stopped = false;
SRWLOCK queueLock = SRWLOCK_INIT;
constexpr unsigned QueueCapacity = 64;
std::array<AimFireObservation, QueueCapacity> queue{};
unsigned queueHead = 0, queueCount = 0;
std::array<AimNativeAccuracyObservation, QueueCapacity> accuracyQueue{};
unsigned accuracyHead = 0, accuracyCount = 0;
std::atomic<unsigned long long> calls{0}, selected{0}, captured{0}, rejected{0}, dropped{0};
std::atomic<unsigned long long> accuracyCalls{0}, accuracySelected{0}, accuracyCaptured{0}, accuracyRejected{0}, accuracyDropped{0};
std::atomic<const char*> status{"native shot observer not initialized"};
ULONGLONG reportTime = 0;
#ifdef CS2_AIM_FIRE_OBSERVER_CHECKS
bool testing = false;
#endif
static_assert(sizeof(AimNativeBuiltShot) == off::NativeShotSize);
static_assert(offsetof(AimNativeBuiltShot, origin) == 0x14);
static_assert(offsetof(AimNativeBuiltShot, effective) == 0x20);
static_assert(offsetof(AimNativeBuiltShot, punch) == 0x2c);
static_assert(offsetof(AimNativeBuiltShot, nativeFlag38) == 0x38);
static_assert(offsetof(AimNativeBuiltShot, originKind) == 0x3c);
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(address, bytes, N) && !std::memcmp(bytes, expected, N);
}
bool Current() noexcept {
#ifdef CS2_AIM_FIRE_OBSERVER_CHECKS
    if (testing) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
struct Source {
    std::uintptr_t system = 0, pawn = 0, controller = 0, weapon = 0;
    std::uintptr_t pawnIdentity = 0, controllerIdentity = 0, weaponIdentity = 0, services = 0, command = 0;
    std::uint32_t pawnHandle = UINT32_MAX, controllerHandle = UINT32_MAX, weaponHandle = UINT32_MAX;
    int sequence = 0;
    bool operator==(const Source&) const = default;
};
bool Entity(std::uintptr_t system, std::uintptr_t object, std::uintptr_t& identity, std::uint32_t& handle) noexcept {
    std::uintptr_t resolved = 0, bucket = 0;
    if (!object || !Read(object + ent::CEntityInstance::m_pEntity, identity) || !identity ||
        !Read(identity, resolved) || resolved != object ||
        !Read(identity + ent::entity_layout::HandleOffset, handle) || handle == UINT32_MAX) return false;
    const auto index = handle & ent::entity_layout::IndexMask;
    return Read(system + ent::entity_layout::TableOffset +
        (index >> ent::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) && bucket &&
        identity == bucket + (index & ent::entity_layout::BucketMask) * ent::entity_layout::IdentityStride;
}
bool ReadSource(void* weapon, Source& source) noexcept {
    std::uintptr_t table = 0, owner = 0;
    std::uint32_t pawn = UINT32_MAX, controller = UINT32_MAX, activeWeapon = UINT32_MAX;
    int health = 0;
    unsigned char life = 1, local = 0;
    source.weapon = reinterpret_cast<std::uintptr_t>(weapon);
    if (!Current() || !Read(client + ent::globals::dwGameEntitySystem, source.system) || !source.system ||
        !Read(client + ent::globals::dwLocalPlayerPawn, source.pawn) ||
        !Read(client + ent::globals::dwLocalPlayerController, source.controller) ||
        !Entity(source.system, source.pawn, source.pawnIdentity, source.pawnHandle) ||
        !Entity(source.system, source.controller, source.controllerIdentity, source.controllerHandle) ||
        !Entity(source.system, source.weapon, source.weaponIdentity, source.weaponHandle) ||
        !Read(source.pawn + ent::C_BaseEntity::m_iHealth, health) || health <= 0 ||
        !Read(source.pawn + ent::C_BaseEntity::m_lifeState, life) || life != 0 ||
        !Read(source.controller + ent::CBasePlayerController::m_bIsLocalPlayerController, local) || local != 1 ||
        !Read(source.controller + ent::CCSPlayerController::m_hPlayerPawn, pawn) || pawn != source.pawnHandle ||
        !Read(source.pawn + ent::C_BasePlayerPawn::m_hController, controller) || controller != source.controllerHandle ||
        !Read(source.weapon + off::OwnerHandle, pawn) || pawn != source.pawnHandle ||
        !Read(source.pawn + weaponOff::WeaponServices, source.services) || !source.services ||
        !Read(source.services, table) || table != client + weaponOff::ServicesVtable ||
        !Read(source.services + weaponOff::ServicePawn, owner) || owner != source.pawn ||
        !Read(source.services + weaponOff::ActiveWeapon, activeWeapon) || activeWeapon != source.weaponHandle ||
        !Read(source.weapon, table)) return false;
    bool supported = false;
    for (const auto& entry : weaponOff::Tables) supported |= table == client + entry.rva;
    return supported && Read(source.controller + off::CurrentCommand, source.command) && source.command &&
        Read(source.command + movement_offsets::command::Sequence, source.sequence) && source.sequence > 0;
}
bool Angles(const AimAngles& value) noexcept {
    return std::isfinite(value.pitch) && std::isfinite(value.yaw) && std::isfinite(value.roll) &&
        std::abs(value.pitch) <= 10000 && std::abs(value.yaw) <= 10000 && std::abs(value.roll) <= 10000;
}
bool Shot(const AimNativeBuiltShot& shot) noexcept {
    return shot.tick >= 0 && std::isfinite(shot.fraction) && shot.fraction >= 0 && shot.fraction < 1 &&
        shot.renderTick >= 0 && std::isfinite(shot.renderFraction) &&
        shot.renderFraction >= 0 && shot.renderFraction < 1 &&
        std::isfinite(shot.origin.x) && std::isfinite(shot.origin.y) && std::isfinite(shot.origin.z) &&
        std::abs(shot.origin.x) <= 1000000 && std::abs(shot.origin.y) <= 1000000 && std::abs(shot.origin.z) <= 1000000 &&
        Angles(shot.effective) && Angles(shot.punch);
}
void History(const Source& source, AimFireObservation& sample) noexcept {
    namespace history = silent_aim_offsets;
    std::uintptr_t repeated = 0, entry = 0, angles = 0;
    std::uint32_t presence = 0, anglePresence = 0;
    int capacity = 0, allocated = 0;
    if (!Read(source.command + history::command::Attack1Index, sample.historyIndex) ||
        !Read(source.command + history::command::HistorySize, sample.historyCount)) return;
    if (sample.historyIndex < 0 || sample.historyCount <= 0 || sample.historyCount > 256 ||
        sample.historyIndex >= sample.historyCount ||
        !Read(source.command + history::command::HistoryCapacity, capacity) || capacity < sample.historyCount || capacity > 4096 ||
        !Read(source.command + history::command::HistoryRep, repeated) || !repeated ||
        !Read(repeated + history::repeated::Allocated, allocated) || allocated < sample.historyCount || allocated > 4096 ||
        !Read(repeated + history::repeated::Entries + sample.historyIndex * sizeof(std::uintptr_t), entry) || !entry ||
        !Read(entry + history::history::Presence, presence) ||
        (presence & aim_spread_offsets::HistoryTimePresent) != aim_spread_offsets::HistoryTimePresent ||
        !(presence & history::history::AnglesPresent) || !Read(entry + history::history::Angles, angles) || !angles ||
        !Read(angles + history::angle::Presence, anglePresence) ||
        (anglePresence & history::angle::AllPresent) != history::angle::AllPresent ||
        !Read(angles + history::angle::Pitch, sample.historyAngles) || !Angles(sample.historyAngles) ||
        !Read(entry + aim_spread_offsets::HistoryTick, sample.historyTick) || sample.historyTick < 0 ||
        !Read(entry + aim_spread_offsets::HistoryFraction, sample.historyFraction) ||
        !std::isfinite(sample.historyFraction) || sample.historyFraction < 0 || sample.historyFraction >= 1) return;
    sample.historyKnown = true;
}
bool Poll(AimFireObservation& sample) noexcept {
    AcquireSRWLockExclusive(&queueLock);
    const bool available = queueCount != 0;
    if (available) {
        sample = queue[queueHead]; queueHead = (queueHead + 1) % QueueCapacity; --queueCount;
    }
    ReleaseSRWLockExclusive(&queueLock);
    return available;
}
bool PollAccuracy(AimNativeAccuracyObservation& sample) noexcept {
    AcquireSRWLockExclusive(&queueLock);
    const bool available = accuracyCount != 0;
    if (available) {
        sample = accuracyQueue[accuracyHead]; accuracyHead = (accuracyHead + 1) % QueueCapacity; --accuracyCount;
    }
    ReleaseSRWLockExclusive(&queueLock);
    return available;
}
struct Invocation {
    Invocation() noexcept { inFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~Invocation() { inFlight.fetch_sub(1, std::memory_order_release); }
};
void* Observe(void* weapon, void* output, int mode, std::uintptr_t caller) noexcept {
    Invocation invocation;
    calls.fetch_add(1, std::memory_order_relaxed);
    Source before{};
    AimFireObservation sample{};
    const bool wants = active.load(std::memory_order_acquire) && ShotLogGetSettings().enabled &&
        mode == 0 && caller == client + off::BuilderReturn;
    const bool valid = wants && ReadSource(weapon, before);
    if (wants) selected.fetch_add(1, std::memory_order_relaxed);
    if (valid) {
        sample.controller = before.controllerHandle; sample.pawn = before.pawnHandle; sample.weapon = before.weaponHandle;
        sample.sequence = before.sequence; sample.mode = mode; History(before, sample);
    }
    // Native recursion needs no observer lock/TLS. Each invocation owns its
    // snapshots on its own stack, and native is always forwarded exactly once.
    void* result = original ? original(weapon, output, mode) : nullptr;
    if (!wants) return result;
    Source after{};
    if (!valid || !active.load(std::memory_order_acquire) || result != output || !output ||
        !ReadSource(weapon, after) || !(after == before) ||
        !Read(reinterpret_cast<std::uintptr_t>(output), sample.built) || !Shot(sample.built)) {
        rejected.fetch_add(1, std::memory_order_relaxed); return result;
    }
    sample.receiptMs = GetTickCount64();
    AcquireSRWLockExclusive(&queueLock);
    if (active.load(std::memory_order_acquire) && ShotLogGetSettings().enabled) {
        if (queueCount < QueueCapacity) {
            queue[(queueHead + queueCount) % QueueCapacity] = sample; ++queueCount;
            captured.fetch_add(1, std::memory_order_relaxed);
        } else dropped.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&queueLock);
    return result;
}
__declspec(noinline) void* Hook(void* weapon, void* output, int mode) noexcept {
    return Observe(weapon, output, mode, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
// The final accuracy getters have no attack-selector argument. Only the exact
// ordinary-fire return address and current local command provide provenance.
bool AccuracySource(void* weapon, std::uintptr_t caller, std::ptrdiff_t expectedReturn,
    bool& wants, Source& source) noexcept {
    accuracyCalls.fetch_add(1, std::memory_order_relaxed);
    wants = active.load(std::memory_order_acquire) && ShotLogGetSettings().enabled && caller == client + expectedReturn;
    if (wants) accuracySelected.fetch_add(1, std::memory_order_relaxed);
    return wants && ReadSource(weapon, source);
}
void CaptureAccuracy(void* weapon, const Source& before, bool wants, bool valid,
    AimNativeAccuracyComponent component, float value, bool firstOutput, bool secondOutput) noexcept {
    if (!wants) return;
    Source after{};
    if (!valid || !active.load(std::memory_order_acquire) || !ReadSource(weapon, after) || !(after == before)) {
        accuracyRejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    AimNativeAccuracyObservation sample{};
    sample.receiptMs = GetTickCount64(); sample.controller = before.controllerHandle;
    sample.pawn = before.pawnHandle; sample.weapon = before.weaponHandle; sample.sequence = before.sequence;
    sample.component = component; sample.valueBits = std::bit_cast<std::uint32_t>(value);
    sample.value = std::bit_cast<float>(sample.valueBits);
    sample.firstOutputProvided = firstOutput; sample.secondOutputProvided = secondOutput;
    sample.movementStateKnown = Read(before.pawn + movement_offsets::pawn::Flags, sample.movementFlags) &&
        Read(before.pawn + movement_offsets::pawn::Velocity, sample.velocity) &&
        std::isfinite(sample.velocity.x) && std::isfinite(sample.velocity.y) && std::isfinite(sample.velocity.z);
    if (!sample.movementStateKnown) { sample.movementFlags = 0; sample.velocity = {}; }
    AcquireSRWLockExclusive(&queueLock);
    if (active.load(std::memory_order_acquire) && ShotLogGetSettings().enabled) {
        if (accuracyCount < QueueCapacity) {
            accuracyQueue[(accuracyHead + accuracyCount) % QueueCapacity] = sample; ++accuracyCount;
            accuracyCaptured.fetch_add(1, std::memory_order_relaxed);
        } else accuracyDropped.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&queueLock);
}
float ObserveSpread(void* weapon, std::uintptr_t caller) noexcept {
    Invocation invocation;
    bool wants = false; Source before{};
    const bool valid = AccuracySource(weapon, caller, off::SpreadReturn, wants, before);
    const float value = originalSpread ? originalSpread(weapon) : 0.0f;
    CaptureAccuracy(weapon, before, wants, valid, AimNativeAccuracyComponent::Spread, value, false, false);
    return value;
}
float ObserveInaccuracy(void* weapon, float* first, float* second, std::uintptr_t caller) noexcept {
    Invocation invocation;
    bool wants = false; Source before{};
    const bool valid = AccuracySource(weapon, caller, off::InaccuracyReturn, wants, before);
    const float value = originalInaccuracy ? originalInaccuracy(weapon, first, second) : 0.0f;
    CaptureAccuracy(weapon, before, wants, valid, AimNativeAccuracyComponent::Inaccuracy, value, first != nullptr, second != nullptr);
    return value;
}
__declspec(noinline) float HookSpread(void* weapon) noexcept {
    return ObserveSpread(weapon, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
__declspec(noinline) float HookInaccuracy(void* weapon, float* first, float* second) noexcept {
    return ObserveInaccuracy(weapon, first, second, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}

bool AimFireObserverPrepare(AimFireObserverLog log) noexcept {
    logger = log;
    if (prepared) return true;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != ent::ClientImageSize || !Match(client + off::Builder, off::BuilderBytes) ||
        !Match(client + off::BuilderCall, off::BuilderCallBytes) ||
        !Match(client + off::Spread, off::SpreadBytes) || !Match(client + off::Inaccuracy, off::InaccuracyBytes) ||
        !Match(client + off::SpreadCall, off::SpreadCallBytes) || !Match(client + off::InaccuracyCall, off::InaccuracyCallBytes)) {
        status.store("native shot observer contract unavailable"); return false;
    }
    // If any later installation step fails, active remains false and already
    // queued resident hooks forward their own valid originals without capture.
    active.store(false, std::memory_order_release);
    auto* target = reinterpret_cast<void*>(client + off::Builder);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("native shot observer hook unavailable"); return false;
    }
    target = reinterpret_cast<void*>(client + off::Spread);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookSpread), reinterpret_cast<void**>(&originalSpread)) != MH_OK ||
        !originalSpread || MH_QueueEnableHook(target) != MH_OK) {
        status.store("native spread observer hook unavailable"); return false;
    }
    target = reinterpret_cast<void*>(client + off::Inaccuracy);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookInaccuracy), reinterpret_cast<void**>(&originalInaccuracy)) != MH_OK ||
        !originalInaccuracy || MH_QueueEnableHook(target) != MH_OK) {
        status.store("native inaccuracy observer hook unavailable"); return false;
    }
    prepared = true;
    status.store("native shot observer ready");
    if (logger) logger("AIM_FIRE_OBSERVER_BUILD_VERIFIED revision=2 native invoked shot builder and ordinary-fire accuracy getters; bounded copied local command/origin/effective/punch and actual float returns; client input evidence only, not server fire; no extra native calls or gameplay writes");
    return true;
}
void AimFireObserverActivate() noexcept {
    AcquireSRWLockExclusive(&queueLock);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&queueLock);
}
void AimFireObserverStop() noexcept {
    AcquireSRWLockExclusive(&queueLock);
    stopped = true; active.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&queueLock);
    // The original and post-native copy are both inside the callback barrier.
    // Hooks/code stay resident; subsequent calls only forward native.
    while (inFlight.load(std::memory_order_acquire)) Sleep(1);
    status.store("native shot observer stopped and resident");
}
const char* AimFireObserverStatus() noexcept { return status.load(std::memory_order_acquire); }
void AimFireObserverFlushTelemetry() noexcept {
    if (!logger) return;
    AimFireObservation sample{};
    for (unsigned count = 0; count < QueueCapacity && Poll(sample); ++count) {
        const auto& shot = sample.built;
        char text[1500]{};
        std::snprintf(text, sizeof(text),
            "AIM_NATIVE_BUILT_SHOT receipt_ms=%llu controller=0x%08x pawn=0x%08x weapon=0x%08x sequence=%d mode=%d history_index=%d history_count=%d history_known=%u history_time=%d/%.9f history_angles=(%.9f,%.9f,%.9f) shot_time=%d/%.9f render_time=%d/%.9f origin=(%.9f,%.9f,%.9f) effective=(%.9f,%.9f,%.9f) shot_punch=(%.9f,%.9f,%.9f) native_flag38=%u native_flag39=%u origin_kind=%d; built_client_shot=1 server_fire=0 observer_only=1",
            static_cast<unsigned long long>(sample.receiptMs), sample.controller, sample.pawn, sample.weapon,
            sample.sequence, sample.mode, sample.historyIndex, sample.historyCount, sample.historyKnown,
            sample.historyTick, sample.historyFraction, sample.historyAngles.pitch, sample.historyAngles.yaw, sample.historyAngles.roll,
            shot.tick, shot.fraction, shot.renderTick, shot.renderFraction, shot.origin.x, shot.origin.y, shot.origin.z,
            shot.effective.pitch, shot.effective.yaw, shot.effective.roll, shot.punch.pitch, shot.punch.yaw, shot.punch.roll,
            shot.nativeFlag38, shot.nativeFlag39, shot.originKind);
        if (ShotLogGetSettings().enabled) logger(text);
    }
    AimNativeAccuracyObservation accuracy{};
    for (unsigned count = 0; count < QueueCapacity && PollAccuracy(accuracy); ++count) {
        char text[896]{};
        const bool finite = (accuracy.valueBits & 0x7f800000u) != 0x7f800000u;
        std::snprintf(text, sizeof(text),
            "AIM_NATIVE_ACCURACY receipt_ms=%llu controller=0x%08x pawn=0x%08x weapon=0x%08x sequence=%d component=%s value=%.9g value_bits=0x%08x value_finite=%u first_output_provided=%u second_output_provided=%u revision=3 movement_known=%u flags=0x%x velocity=(%.9g,%.9g,%.9g) attack_selector_known=0 attack_selector=-1; native_client_input=1 server_fire=0 observer_only=1",
            static_cast<unsigned long long>(accuracy.receiptMs), accuracy.controller, accuracy.pawn, accuracy.weapon, accuracy.sequence,
            accuracy.component == AimNativeAccuracyComponent::Spread ? "spread" : "inaccuracy", accuracy.value, accuracy.valueBits,
            finite, accuracy.firstOutputProvided, accuracy.secondOutputProvided,
            accuracy.movementStateKnown, accuracy.movementFlags, accuracy.velocity.x, accuracy.velocity.y, accuracy.velocity.z);
        if (ShotLogGetSettings().enabled) logger(text);
    }
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    char text[480]{};
    std::snprintf(text, sizeof(text), "AIM_FIRE_OBSERVER calls=%llu selected=%llu captured=%llu rejected=%llu dropped=%llu active=%u status=\"%s\"; native client construction only, not observed server receipt",
        calls.load(), selected.load(), captured.load(), rejected.load(), dropped.load(), active.load(), AimFireObserverStatus());
    logger(text);
    std::snprintf(text, sizeof(text), "AIM_NATIVE_ACCURACY_OBSERVER calls=%llu selected=%llu captured=%llu rejected=%llu dropped=%llu active=%u; actual ordinary-fire getter returns, attack selector unknown, not server receipt",
        accuracyCalls.load(), accuracySelected.load(), accuracyCaptured.load(), accuracyRejected.load(), accuracyDropped.load(), active.load());
    logger(text);
}

#ifdef CS2_AIM_FIRE_OBSERVER_CHECKS
void AimFireObserverTestConfigure(std::uintptr_t base, AimFireObserverNative native, AimFireObserverLog log) noexcept {
    client = base; original = native; logger = log; testing = true; prepared = true; stopped = false;
    queueHead = queueCount = 0; calls = 0; selected = 0; captured = 0; rejected = 0; dropped = 0; reportTime = 0;
    accuracyHead = accuracyCount = 0; accuracyCalls = 0; accuracySelected = 0; accuracyCaptured = 0; accuracyRejected = 0; accuracyDropped = 0;
    originalSpread = nullptr; originalInaccuracy = nullptr;
    active.store(true, std::memory_order_release); status.store("native shot observer test ready");
}
void* AimFireObserverTestCall(void* weapon, void* output, int mode, std::uintptr_t caller) noexcept {
    return Observe(weapon, output, mode, caller);
}
bool AimFireObserverTestPoll(AimFireObservation& sample) noexcept { return Poll(sample); }
void AimFireObserverTestConfigureAccuracy(AimFireObserverSpread spread, AimFireObserverInaccuracy inaccuracy) noexcept {
    originalSpread = spread; originalInaccuracy = inaccuracy;
}
float AimFireObserverTestSpreadCall(void* weapon, std::uintptr_t caller) noexcept { return ObserveSpread(weapon, caller); }
float AimFireObserverTestInaccuracyCall(void* weapon, float* first, float* second, std::uintptr_t caller) noexcept {
    return ObserveInaccuracy(weapon, first, second, caller);
}
bool AimFireObserverTestPollAccuracy(AimNativeAccuracyObservation& sample) noexcept { return PollAccuracy(sample); }
#endif
