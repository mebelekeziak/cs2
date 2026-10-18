#include "shot_events.h"
#include "shot_events_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cstring>
#include <cstdio>
#include <limits>

namespace {
namespace off = shot_events_offsets;
namespace ent = cs2_offsets;
int failures = 0, checks = 0, adds = 0, finds = 0;
void Expect(bool result, const char* name) { ++checks; failures += !result; std::printf("%s %s\n", result ? "PASS" : "FAIL", name); }
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
template<class T> std::uintptr_t Address(std::vector<T>& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
template<class T> void Put(std::uintptr_t address, const T& value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
struct Key { std::uint32_t hash; int token; const char* name; };
struct Event {
    std::uintptr_t vtable = 0;
    const char* name = "weapon_fire";
    std::map<std::string, int> integer;
    std::map<std::string, float> number;
    std::map<std::string, std::string> text;
};
const char* Name(void* pointer) { return static_cast<Event*>(pointer)->name; }
bool Empty(void* pointer, const Key* key) {
    auto& event = *static_cast<Event*>(pointer);
    return !event.integer.count(key->name) && !event.number.count(key->name) && !event.text.count(key->name);
}
int Integer(void* pointer, const Key* key, int fallback) {
    auto& fields = static_cast<Event*>(pointer)->integer;
    const auto found = fields.find(key->name); return found == fields.end() ? fallback : found->second;
}
float Number(void* pointer, const Key* key, float fallback) {
    auto& fields = static_cast<Event*>(pointer)->number;
    const auto found = fields.find(key->name); return found == fields.end() ? fallback : found->second;
}
const char* String(void* pointer, const Key* key, const char* fallback) {
    auto& fields = static_cast<Event*>(pointer)->text;
    const auto found = fields.find(key->name); return found == fields.end() ? fallback : found->second.c_str();
}
bool Boolean(void* pointer, const Key* key, bool fallback) { return Integer(pointer, key, fallback) != 0; }
std::uint32_t* Handle(void* pointer, std::uint32_t* output, const Key* key, std::uint32_t fallback) {
    Expect(key->token == -1 && key->hash == ShotEventsTestHash(key->name), "event getter receives the verified 16-byte hashed key and uncached token");
    *output = static_cast<std::uint32_t>(Integer(pointer, key, static_cast<int>(fallback))); return output;
}
struct Manager { std::uintptr_t vtable = 0; std::set<std::string> names; std::string unavailable; };
bool Find(void* pointer, void*, const char* name) { ++finds; return static_cast<Manager*>(pointer)->names.count(name) != 0; }
int Add(void* pointer, void* listener, const char* name, bool server) {
    ++adds;
    Expect(listener && !server, "native event interests use an externally owned client listener");
    auto& manager = *static_cast<Manager*>(pointer);
    if (manager.unavailable == name) return -1;
    const int index = static_cast<int>(manager.names.size()); manager.names.insert(name); return index;
}
std::vector<ShotNativeEvent> observed;
void Receive(const ShotNativeEvent& value) { observed.push_back(value); }
struct Actor {
    std::array<unsigned char, 0x1000> controller{};
    std::array<unsigned char, 0x1800> pawn{};
};
struct Fixture {
    std::vector<unsigned char> image = std::vector<unsigned char>(ent::ClientImageSize);
    std::vector<unsigned char> bucket = std::vector<unsigned char>(512 * ent::entity_layout::IdentityStride);
    std::array<unsigned char, 0x2200> system{};
    std::array<std::uintptr_t, 14> eventTable{};
    std::array<std::uintptr_t, 9> managerTable{};
    Manager manager{};
    Actor local{}, victim{};
    std::array<char, 32> controllerName{}, pawnName{};
    Fixture() {
        observed.clear(); adds = finds = 0;
        eventTable[off::NameSlot / 8] = reinterpret_cast<std::uintptr_t>(&Name);
        eventTable[off::EmptySlot / 8] = reinterpret_cast<std::uintptr_t>(&Empty);
        eventTable[off::IntegerSlot / 8] = reinterpret_cast<std::uintptr_t>(&Integer);
        eventTable[off::FloatSlot / 8] = reinterpret_cast<std::uintptr_t>(&Number);
        eventTable[off::StringSlot / 8] = reinterpret_cast<std::uintptr_t>(&String);
        eventTable[off::BooleanSlot / 8] = reinterpret_cast<std::uintptr_t>(&Boolean);
        eventTable[off::HandleSlot / 8] = reinterpret_cast<std::uintptr_t>(&Handle);
        managerTable[off::AddListenerSlot / 8] = reinterpret_cast<std::uintptr_t>(&Add);
        managerTable[off::FindListenerSlot / 8] = reinterpret_cast<std::uintptr_t>(&Find);
        manager.vtable = Address(managerTable);
        Put(Address(image) + off::Manager, Address(manager));
        Put(Address(image) + ent::globals::dwEntityList, Address(system));
        Put(Address(system) + ent::entity_layout::TableOffset, Address(bucket));
        strcpy_s(controllerName.data(), controllerName.size(), "cs_player_controller");
        strcpy_s(pawnName.data(), pawnName.size(), "c_cs_player_for_precache");
        Configure(local, 1, "local"); Configure(victim, 3, "target\nname");
        Put(Address(image) + ent::globals::dwLocalPlayerPawn, Address(local.pawn));
        Put(Address(image) + ent::globals::dwLocalPlayerController, Address(local.controller));
        ShotEventsTestConfigure(Address(image), &Receive);
    }
    void Configure(Actor& actor, unsigned index, const char* name) {
        const auto owner = Address(bucket) + index * ent::entity_layout::IdentityStride;
        const auto pawn = owner + ent::entity_layout::IdentityStride;
        const std::uint32_t ownerHandle = 0x8000 | index, pawnHandle = 0x8000 | (index + 1);
        Put(owner, Address(actor.controller)); Put(owner + ent::entity_layout::HandleOffset, ownerHandle);
        Put(owner + ent::CEntityIdentity::m_designerName, Address(controllerName));
        Put(pawn, Address(actor.pawn)); Put(pawn + ent::entity_layout::HandleOffset, pawnHandle);
        Put(pawn + ent::CEntityIdentity::m_designerName, Address(pawnName));
        Put(Address(actor.controller) + ent::CEntityInstance::m_pEntity, owner);
        Put(Address(actor.pawn) + ent::CEntityInstance::m_pEntity, pawn);
        Put(Address(actor.controller) + ent::CCSPlayerController::m_hPlayerPawn, pawnHandle);
        Put(Address(actor.pawn) + ent::C_BasePlayerPawn::m_hController, ownerHandle);
        strcpy_s(reinterpret_cast<char*>(Address(actor.controller) + ent::CBasePlayerController::m_iszPlayerName), 64, name);
    }
    Event Fire(const char* name = "weapon_fire") {
        Event event; event.vtable = Address(eventTable); event.name = name;
        event.integer["userid_pawn"] = 0x8002; event.text["weapon"] = "ak47"; return event;
    }
};
void HashChecks() {
    for (const auto& value : std::array<std::pair<const char*, std::uint32_t>, 12>{{
        {"userid",0x3E7B804B},{"userid_pawn",0x79261AD5},{"attacker_pawn",0xA8F40039},
        {"weapon",0xEBDE2B13},{"silenced",0x6EE4828D},{"dmg_health",0x6CDFDF9D},
        {"dmg_armor",0x20BD9A4E},{"health",0x9AA70A58},{"hitgroup",0x103E09F1},
        {"x",0xF38610FF},{"y",0x3225AF29},{"z",0x2BF4F9C5}}})
        Expect(ShotEventsTestHash(value.first) == value.second, "field key hash agrees with independently decoded native literals");
    Expect(ShotEventsTestHash("USERID_PAWN") == ShotEventsTestHash("userid_pawn"), "native hash lowercases ASCII names");
}
void RegistrationChecks() {
    Fixture fixture;
    Expect(!ShotEventsRegistered(), "verified contracts do not claim owner-thread registration");
    ShotEventsPump();
    Expect(ShotEventsRegistered() && fixture.manager.names.size() == 11 && adds == 11,
        "owner pump registers every interest, accepting native result zero as success");
    ShotEventsPump(); Expect(adds == 11 && finds == 11, "repeated owner pumps do not repeat native membership searches within one second");
    fixture.manager.names.clear(); ShotEventsTestTime(1001); ShotEventsPump();
    Expect(adds == 22 && finds == 22 && ShotEventsRegistered(), "same-manager reset is detected at the bounded membership refresh");
    Manager replacement; replacement.vtable = Address(fixture.managerTable);
    Put(Address(fixture.image) + off::Manager, Address(replacement)); ShotEventsPump();
    Expect(adds == 33 && finds == 33 && replacement.names.size() == 11 && ShotEventsRegistered(),
        "replacement native manager registers immediately without waiting for the refresh interval");
    ShotEventsStop("test"); ShotEventsActivate(); ShotEventsPump();
    Expect(!ShotEventsRegistered() && adds == 33, "cooperative stop leaves registered code inert and cannot reactivate it");
    Fixture partial; partial.manager.unavailable = "bullet_impact"; ShotEventsPump();
    Expect(!ShotEventsRegistered() && partial.manager.names.size() == 10 &&
        !std::strcmp(ShotEventsStatus(), "some native event interests unavailable"), "partial event interests do not report ready");
}
void CopyChecks() {
    Fixture fixture;
    auto event = fixture.Fire(); event.integer["silenced"] = 1;
    ShotEventsTestDeliver(&event);
    Expect(observed.size() == 1 && observed[0].kind == ShotNativeEventKind::WeaponFire &&
        observed[0].actor.known && observed[0].actor.local && observed[0].actor.pawn == 0x8002 &&
        observed[0].actor.controller == 0x8001 && !std::strcmp(observed[0].weapon, "ak47") &&
        observed[0].silenced && observed[0].silencedKnown, "fire delivery copies full local identities and weapon fields");
    const auto firstId = observed[0].deliveryId;
    ShotEventsTestDeliver(&event);
    Expect(observed.size() == 2 && observed[1].deliveryId > firstId,
        "two native deliveries remain distinct even with identical payload and timestamp");
    event.text["weapon"] = "overwritten";
    Expect(!std::strcmp(observed[0].weapon, "ak47"), "copied observations retain no event-owned string storage");
    auto hurt = fixture.Fire("player_hurt"); hurt.integer["userid_pawn"] = 0x8004;
    hurt.integer["attacker_pawn"] = 0x8002; hurt.integer["dmg_health"] = 42;
    hurt.integer["dmg_armor"] = 3; hurt.integer["health"] = 58; hurt.integer["armor"] = 97; hurt.integer["hitgroup"] = 2;
    ShotEventsTestDeliver(&hurt);
    const auto& hit = observed.back();
    Expect(hit.actor.local && hit.victim.known && !hit.victim.local && hit.victim.pawn == 0x8004 &&
        hit.damageKnown && hit.damage == 42 && hit.remainingHp == 58 && hit.damageArmor == 3 && hit.hitgroup == 2 &&
        !std::strcmp(hit.victim.name, "target name"), "hurt delivery preserves native damage and sanitizes copied display names");
    Put(Address(fixture.bucket) + 4 * ent::entity_layout::IdentityStride + ent::entity_layout::HandleOffset, std::uint32_t{0x10004});
    ShotEventsTestDeliver(&hurt);
    Expect(observed.back().victim.known && observed.back().victim.pawn == 0x8004 &&
        observed.back().victim.controller == UINT32_MAX && !observed.back().victim.name[0],
        "despawned or recycled victim keeps event pawn handle without adopting current controller/name");
    auto legacy = fixture.Fire(); legacy.integer.erase("userid_pawn"); legacy.integer["userid"] = 1;
    ShotEventsTestDeliver(&legacy);
    Expect(!observed.back().actor.known && !observed.back().actor.local, "numeric userid alone does not invent a current pawn identity");
    auto impact = fixture.Fire("bullet_impact"); impact.number = {{"x",1.0f},{"y",2.0f},{"z",3.0f}};
    ShotEventsTestDeliver(&impact);
    Expect(observed.back().positionKnown && observed.back().position.z == 3, "impact XYZ fields are copied with their native float ABI");
    impact.number["z"] = std::numeric_limits<float>::quiet_NaN(); ShotEventsTestDeliver(&impact);
    Expect(!observed.back().positionKnown, "nonfinite impact coordinates are reported unknown");
    hurt.integer.erase("dmg_health"); ShotEventsTestDeliver(&hurt);
    Expect(!observed.back().damageKnown && observed.back().damage == -1, "missing damage does not become an invented zero-damage hit");
    for (const auto* name : {"round_prestart", "cs_game_disconnected", "player_disconnect", "round_start", "game_newmap"}) {
        auto reset = fixture.Fire(name); reset.integer.clear(); ShotEventsTestDeliver(&reset);
        Expect(!observed.back().actor.known && !observed.back().victim.known, "session/reset event does not guess a pawn from a player slot");
    }
    const auto count = observed.size();
    auto unknown = fixture.Fire("unobserved_event"); ShotEventsTestDeliver(&unknown);
    Expect(observed.size() == count, "unregistered event names do not become shot observations");
    fixture.eventTable[off::HandleSlot / 8] = 0; ShotEventsTestDeliver(&event);
    Expect(observed.size() == count, "missing native getter rejects the copied event without invoking it");
    fixture.eventTable[off::HandleSlot / 8] = reinterpret_cast<std::uintptr_t>(&Handle);
    ShotEventsStop("stop copies"); ShotEventsTestDeliver(&event);
    Expect(observed.size() == count, "stopped resident listener forwards native lifetime without delivering more values");
}
}
int main() {
    HashChecks(); RegistrationChecks(); CopyChecks();
    std::printf("Shot event checks: %d; failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
