#include "shot_events.h"
#include "shot_events_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off = shot_events_offsets;
namespace ent = cs2_offsets;
struct Key { std::uint32_t hash = 0; std::int32_t token = -1; const char* name = nullptr; };
static_assert(sizeof(Key) == 16);
using AddFn = int (*)(void*, void*, const char*, bool);
using FindFn = bool (*)(void*, void*, const char*);
using NameFn = const char* (*)(void*);
using EmptyFn = bool (*)(void*, const Key*);
using IntFn = int (*)(void*, const Key*, int);
using FloatFn = float (*)(void*, const Key*, float);
using StringFn = const char* (*)(void*, const Key*, const char*);
using BoolFn = bool (*)(void*, const Key*, bool);
using HandleFn = std::uint32_t* (*)(void*, std::uint32_t*, const Key*, std::uint32_t);
std::uintptr_t client = 0;
ShotEventsLogCallback logger = nullptr;
ShotEventsValueCallback observer = nullptr;
std::atomic<bool> active{false};
bool prepared = false, stopped = false;
SRWLOCK callbackBarrier = SRWLOCK_INIT;
std::atomic_flag pumping = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> received{0}, delivered{0}, rejected{0}, registrationCalls{0}, deliverySequence{0};
std::atomic<unsigned> registered{0};
std::atomic<const char*> status{"not initialized"};
ULONGLONG lastSummary = 0;
std::uintptr_t checkedManager = 0; // Serialized by pumping; never dereferenced from the cache.
ULONGLONG membershipTime = 0;
bool haveMembershipTime = false;
#ifdef CS2_SHOT_EVENTS_CHECKS
bool testRuntime = false;
std::uint64_t testNow = 1;
#endif
struct EventName { const char* name; ShotNativeEventKind kind; };
constexpr EventName interests[]{
    {"weapon_fire", ShotNativeEventKind::WeaponFire},
    {"bullet_impact", ShotNativeEventKind::BulletImpact},
    {"player_hurt", ShotNativeEventKind::PlayerHurt},
    {"player_death", ShotNativeEventKind::PlayerDeath},
    {"round_start", ShotNativeEventKind::RoundStart},
    {"round_end", ShotNativeEventKind::RoundEnd},
    {"game_newmap", ShotNativeEventKind::NewMap},
    {"player_spawn", ShotNativeEventKind::PlayerSpawn},
    {"player_disconnect", ShotNativeEventKind::PlayerDisconnect},
    {"round_prestart", ShotNativeEventKind::RoundPrestart},
    {"cs_game_disconnected", ShotNativeEventKind::ClientDisconnected},
};
template<class T> bool Read(std::uintptr_t address, T& value) noexcept { return MovementCopy(address, &value, sizeof(value)); }
ULONGLONG Now() noexcept {
#ifdef CS2_SHOT_EVENTS_CHECKS
    if (testRuntime) return testNow;
#endif
    return GetTickCount64();
}
unsigned char Lower(unsigned char value) noexcept { return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value; }
std::uint32_t Hash(const char* name) noexcept {
    constexpr std::uint32_t multiplier = 0x5BD1E995;
    const auto size = std::strlen(name);
    auto hash = static_cast<std::uint32_t>(size) ^ 0x31415926u;
    const auto* data = reinterpret_cast<const unsigned char*>(name);
    std::size_t left = size;
    while (left >= 4) {
        std::uint32_t word = Lower(data[0]) | (std::uint32_t{Lower(data[1])} << 8) |
            (std::uint32_t{Lower(data[2])} << 16) | (std::uint32_t{Lower(data[3])} << 24);
        word *= multiplier; word ^= word >> 24; word *= multiplier;
        hash = hash * multiplier ^ word; data += 4; left -= 4;
    }
    if (left) {
        std::uint32_t tail = Lower(data[0]);
        if (left > 1) tail |= std::uint32_t{Lower(data[1])} << 8;
        if (left > 2) tail |= std::uint32_t{Lower(data[2])} << 16;
        hash = (hash ^ tail) * multiplier;
    }
    hash ^= hash >> 13; hash *= multiplier; hash ^= hash >> 15;
    return hash;
}
Key Field(const char* name) noexcept { return {Hash(name), -1, name}; }
template<std::size_t N> bool Text(std::uintptr_t address, char (&output)[N]) noexcept {
    output[0] = 0;
    if (!address) return false;
    for (std::size_t index = 0; index < N - 1; ++index) {
        unsigned char value = 0;
        if (!Read(address + index, value)) { output[0] = 0; return false; }
        if (!value) { output[index] = 0; return true; }
        output[index] = value < 0x20 || value == 0x7F ? ' ' : static_cast<char>(value);
    }
    output[N - 1] = 0;
    return true;
}
bool CurrentClient() noexcept {
#ifdef CS2_SHOT_EVENTS_CHECKS
    if (testRuntime) return true;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
bool Method(std::uintptr_t table, std::ptrdiff_t slot, std::ptrdiff_t expected, std::uintptr_t& method) noexcept {
    if (!Read(table + slot, method) || !method) return false;
#ifdef CS2_SHOT_EVENTS_CHECKS
    if (testRuntime) return true;
#endif
    return method == client + expected;
}
struct EventMethods {
    NameFn name = nullptr; EmptyFn empty = nullptr; IntFn integer = nullptr;
    FloatFn number = nullptr; StringFn string = nullptr; BoolFn boolean = nullptr; HandleFn handle = nullptr;
};
bool Methods(std::uintptr_t event, EventMethods& methods) noexcept {
    std::uintptr_t table = 0, address = 0;
    if (!Read(event, table) || !table) return false;
#ifdef CS2_SHOT_EVENTS_CHECKS
    if (!testRuntime)
#endif
    if (table != client + off::EventVtable) return false;
    if (!Method(table, off::NameSlot, off::Name, address)) return false;
    methods.name = reinterpret_cast<NameFn>(address);
    if (!Method(table, off::EmptySlot, off::Empty, address)) return false;
    methods.empty = reinterpret_cast<EmptyFn>(address);
    if (!Method(table, off::IntegerSlot, off::Integer, address)) return false;
    methods.integer = reinterpret_cast<IntFn>(address);
    if (!Method(table, off::FloatSlot, off::Float, address)) return false;
    methods.number = reinterpret_cast<FloatFn>(address);
    if (!Method(table, off::StringSlot, off::String, address)) return false;
    methods.string = reinterpret_cast<StringFn>(address);
    if (!Method(table, off::BooleanSlot, off::Boolean, address)) return false;
    methods.boolean = reinterpret_cast<BoolFn>(address);
    if (!Method(table, off::HandleSlot, off::Handle, address)) return false;
    methods.handle = reinterpret_cast<HandleFn>(address);
    return true;
}
bool Entity(std::uintptr_t system, std::uint32_t handle, std::uintptr_t& entity) noexcept {
    if (!system || handle == UINT32_MAX) return false;
    const auto index = handle & ent::entity_layout::IndexMask;
    std::uintptr_t bucket = 0, back = 0;
    std::uint32_t current = UINT32_MAX;
    if (!Read(system + ent::entity_layout::TableOffset +
        (index >> ent::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) || !bucket) return false;
    const auto identity = bucket + (index & ent::entity_layout::BucketMask) * ent::entity_layout::IdentityStride;
    return Read(identity + ent::entity_layout::HandleOffset, current) && current == handle &&
        Read(identity + ent::entity_layout::EntityPointerOffset, entity) && entity &&
        Read(entity + ent::CEntityInstance::m_pEntity, back) && back == identity;
}
bool Designer(std::uintptr_t entity, const char* expected) noexcept {
    std::uintptr_t identity = 0, name = 0; char text[64]{};
    return Read(entity + ent::CEntityInstance::m_pEntity, identity) && identity &&
        Read(identity + ent::CEntityIdentity::m_designerName, name) && Text(name, text) && !std::strcmp(text, expected);
}
ShotNativeActor Actor(void* event, const EventMethods& methods, const char* field) noexcept {
    ShotNativeActor result{};
    const auto key = Field(field);
    if (methods.empty(event, &key)) return result;
    methods.handle(event, &result.pawn, &key, UINT32_MAX);
    result.known = result.pawn != UINT32_MAX && (result.pawn & ent::entity_layout::IndexMask) != ent::entity_layout::IndexMask;
    if (!result.known) return result;
    std::uintptr_t system = 0, pawn = 0, controller = 0, localPawn = 0, localController = 0;
    std::uint32_t reverse = UINT32_MAX, forward = UINT32_MAX;
    // The serialized pawn remains authoritative when its current object no
    // longer exists. Names/local/controller enrichment requires the exact live
    // reciprocal pair; never fall back through a recycled player slot.
    if (!Read(client + ent::globals::dwEntityList, system) || !Entity(system, result.pawn, pawn) ||
        !Designer(pawn, "c_cs_player_for_precache") ||
        !Read(pawn + ent::C_BasePlayerPawn::m_hController, reverse) || !Entity(system, reverse, controller) ||
        !Designer(controller, "cs_player_controller") ||
        !Read(controller + ent::CCSPlayerController::m_hPlayerPawn, forward) || forward != result.pawn) return result;
    result.controller = reverse;
    Text(controller + ent::CBasePlayerController::m_iszPlayerName, result.name);
    if (Read(client + ent::globals::dwLocalPlayerPawn, localPawn) &&
        Read(client + ent::globals::dwLocalPlayerController, localController))
        result.local = pawn == localPawn && controller == localController;
    return result;
}
bool Has(void* event, const EventMethods& methods, const char* name) noexcept {
    const auto key = Field(name); return !methods.empty(event, &key);
}
int Integer(void* event, const EventMethods& methods, const char* name) noexcept {
    const auto key = Field(name); return methods.empty(event, &key) ? -1 : methods.integer(event, &key, -1);
}
void Copy(void* event) noexcept {
    ++received;
    if (!CurrentClient()) { ++rejected; status.store("client changed"); return; }
    EventMethods methods{};
    if (!Methods(reinterpret_cast<std::uintptr_t>(event), methods)) { ++rejected; status.store("event contract unavailable"); return; }
    char name[64]{};
    if (!Text(reinterpret_cast<std::uintptr_t>(methods.name(event)), name)) { ++rejected; return; }
    const EventName* selected = nullptr;
    for (const auto& interest : interests) if (!std::strcmp(name, interest.name)) { selected = &interest; break; }
    if (!selected) return;
    ShotNativeEvent value{};
    value.kind = selected->kind; value.receiptMs = Now();
    value.deliveryId = deliverySequence.fetch_add(1, std::memory_order_relaxed) + 1;
    switch (value.kind) {
    case ShotNativeEventKind::PlayerHurt: case ShotNativeEventKind::PlayerDeath:
        value.actor = Actor(event, methods, "attacker_pawn");
        value.victim = Actor(event, methods, "userid_pawn"); break;
    case ShotNativeEventKind::WeaponFire: case ShotNativeEventKind::BulletImpact:
    case ShotNativeEventKind::PlayerSpawn:
        value.actor = Actor(event, methods, "userid_pawn"); break;
    default: break;
    }
    if (Has(event, methods, "weapon")) {
        const auto key = Field("weapon");
        Text(reinterpret_cast<std::uintptr_t>(methods.string(event, &key, "")), value.weapon);
    }
    if (value.kind == ShotNativeEventKind::BulletImpact && Has(event, methods, "x") &&
        Has(event, methods, "y") && Has(event, methods, "z")) {
        const auto x = Field("x"), y = Field("y"), z = Field("z");
        value.position = {methods.number(event, &x, 0), methods.number(event, &y, 0), methods.number(event, &z, 0)};
        value.positionKnown = std::isfinite(value.position.x) && std::isfinite(value.position.y) && std::isfinite(value.position.z);
    }
    if (value.kind == ShotNativeEventKind::PlayerHurt) {
        value.damage = Integer(event, methods, "dmg_health"); value.damageArmor = Integer(event, methods, "dmg_armor");
        value.remainingHp = Integer(event, methods, "health"); value.remainingArmor = Integer(event, methods, "armor");
        value.hitgroup = Integer(event, methods, "hitgroup"); value.damageKnown = value.damage >= 0;
    }
    if (Has(event, methods, "silenced")) {
        const auto key = Field("silenced"); value.silenced = methods.boolean(event, &key, false); value.silencedKnown = true;
    }
    // The native dispatcher retains event ownership and continues afterward.
    // The observer must copy/use this value synchronously and perform no I/O.
    if (observer) { observer(value); ++delivered; }
}
void ListenerFire(void*, void* event) noexcept {
    if (!active.load(std::memory_order_acquire)) return;
    AcquireSRWLockShared(&callbackBarrier);
    if (active.load(std::memory_order_acquire)) Copy(event);
    ReleaseSRWLockShared(&callbackBarrier);
}
// The verified manager stores an externally owned listener pointer, invokes
// only byte slot +8, and frees its own wrapper without deleting the listener.
// Plain static tables avoid a CRT constructor/destructor under loader lock.
using ListenerMethod = void (*)(void*, void*) noexcept;
constexpr ListenerMethod listenerVtable[]{nullptr, &ListenerFire};
struct Listener { const ListenerMethod* vtable; };
Listener listener{listenerVtable}; // Retained until normal process exit.
template<std::size_t N> bool Bytes(std::ptrdiff_t rva, const unsigned char (&expected)[N]) noexcept {
    std::array<unsigned char, N> current{};
    return MovementCopy(client + rva, current.data(), current.size()) && !std::memcmp(current.data(), expected, N);
}
}

bool ShotEventsPrepare(ShotEventsLogCallback output, ShotEventsValueCallback callback) noexcept {
    logger = output; observer = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t add = 0, find = 0, dispatch = 0;
    const auto eventTable = client + off::EventVtable;
    EventMethods eventMethods{};
    prepared = client && Read(client, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 &&
        Read(client + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp == ent::ClientTimestamp &&
        nt.OptionalHeader.SizeOfImage == ent::ClientImageSize &&
        Method(client + off::ManagerVtable, off::AddListenerSlot, off::AddListener, add) &&
        Method(client + off::ManagerVtable, off::FindListenerSlot, off::FindListener, find) &&
        Method(client + off::ManagerVtable, off::FireClientSlot, off::FireClient, dispatch) &&
        Bytes(off::AddListener, off::AddListenerBytes) && Bytes(off::FindListener, off::FindListenerBytes) && Bytes(off::FireClient, off::FireClientBytes) &&
        Methods(reinterpret_cast<std::uintptr_t>(&eventTable), eventMethods);
    status.store(prepared ? "awaiting engine-owner registration" : "installed event contract unavailable");
    if (logger) logger(prepared ? "SHOT_EVENTS_BUILD_VERIFIED native client listener; registration deferred to engine owner; copied deliveries have no native shot ID/tick" :
        "SHOT_EVENTS_UNAVAILABLE installed event contract verification failed");
    return prepared;
}
void ShotEventsActivate() noexcept {
    AcquireSRWLockExclusive(&callbackBarrier);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&callbackBarrier);
}
void ShotEventsPump() noexcept {
    if (!active.load(std::memory_order_acquire) || pumping.test_and_set(std::memory_order_acquire)) return;
    if (!CurrentClient()) { status.store("client changed"); active.store(false); pumping.clear(std::memory_order_release); return; }
    std::uintptr_t manager = 0, table = 0, add = 0, find = 0;
    bool valid = Read(client + off::Manager, manager) && manager && Read(manager, table) && table;
#ifdef CS2_SHOT_EVENTS_CHECKS
    if (!testRuntime)
#endif
    valid = valid && table == client + off::ManagerVtable;
    valid = valid && Method(table, off::AddListenerSlot, off::AddListener, add) && Method(table, off::FindListenerSlot, off::FindListener, find);
    const auto now = Now();
    if (valid && checkedManager == manager && haveMembershipTime && now >= membershipTime && now - membershipTime < 1000) {
        pumping.clear(std::memory_order_release);
        return;
    }
    checkedManager = valid ? manager : 0;
    membershipTime = now; haveMembershipTime = valid;
    unsigned count = 0;
    if (valid) {
        // No adapter lock is held across manager calls: native dispatch holds
        // its own manager mutex while invoking us, so the reverse order could
        // deadlock a concurrently delivered event during registration.
        for (const auto& interest : interests) {
            bool found = reinterpret_cast<FindFn>(find)(reinterpret_cast<void*>(manager), &listener, interest.name);
            if (!found && active.load(std::memory_order_acquire)) {
                ++registrationCalls;
                found = reinterpret_cast<AddFn>(add)(reinterpret_cast<void*>(manager), &listener, interest.name, false) >= 0;
            }
            count += found;
        }
    }
    registered.store(count, std::memory_order_release);
    status.store(!valid ? "native event manager unavailable" : count == std::size(interests) ? "native events registered" : "some native event interests unavailable");
    pumping.clear(std::memory_order_release);
}
void ShotEventsStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&callbackBarrier);
    stopped = true; active.store(false, std::memory_order_release); status.store("stopped and resident");
    ReleaseSRWLockExclusive(&callbackBarrier);
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "SHOT_EVENTS_STOPPED_RESIDENT reason=%s; copied callbacks drained; native listener remains registered and inert until normal process exit", reason ? reason : "stop");
        logger(message);
    }
}
void ShotEventsFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (lastSummary && now - lastSummary < 2000) return;
    lastSummary = now;
    char message[512]{};
    std::snprintf(message, sizeof(message), "SHOT_EVENTS_DIAGNOSTICS active=%u registered=%u/%zu registration_calls=%llu received=%llu copied=%llu rejected=%llu status=\"%s\"; event receipts are not shot IDs or server timestamps",
        active.load(), registered.load(), std::size(interests), registrationCalls.load(), received.load(), delivered.load(), rejected.load(), status.load());
    logger(message);
}
bool ShotEventsRegistered() noexcept {
    return active.load(std::memory_order_acquire) && registered.load(std::memory_order_acquire) == std::size(interests);
}
const char* ShotEventsStatus() noexcept { return status.load(std::memory_order_acquire); }
#ifdef CS2_SHOT_EVENTS_CHECKS
void ShotEventsTestConfigure(std::uintptr_t base, ShotEventsValueCallback callback) noexcept {
    testRuntime = true; client = base; observer = callback; logger = nullptr;
    prepared = true; stopped = false; active.store(true); received = delivered = rejected = registrationCalls = deliverySequence = 0; registered = 0;
    checkedManager = 0; membershipTime = 0; haveMembershipTime = false; testNow = 1;
}
void ShotEventsTestDeliver(void* event) noexcept { ListenerFire(&listener, event); }
std::uint32_t ShotEventsTestHash(const char* name) noexcept { return Hash(name); }
void ShotEventsTestTime(std::uint64_t now) noexcept { testNow = now; }
#endif
