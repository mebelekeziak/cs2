#include "player_reader.h"
#include "cs2_offsets.h"
#include "esp_item_offsets.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

void PlayerReaderTestConfigure(std::uintptr_t module) noexcept;

namespace {
namespace ent = cs2_offsets;
namespace item = esp_item_offsets;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
std::uintptr_t Address(const void* object) { return reinterpret_cast<std::uintptr_t>(object); }
template<class T> void Put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}
constexpr std::uint32_t Handle(unsigned index) { return (4u << 15) | index; }
struct Fixture {
    std::vector<unsigned char> module = std::vector<unsigned char>(ent::ClientImageSize);
    std::array<unsigned char, 0x2200> system{};
    std::array<unsigned char, 512 * ent::entity_layout::IdentityStride> bucket{};
    std::array<unsigned char, 0xA00> controller{};
    std::array<unsigned char, 0x3800> pawn{};
    std::array<unsigned char, 0x2000> rifle{}, c4{}, planted{};
    std::array<unsigned char, 0x120> playerScene{}, c4Scene{}, plantedScene{};
    std::array<unsigned char, 0x100> collision{}, services{};
    char controllerName[64] = "cs_player_controller";
    char pawnName[64] = "c_cs_player_for_precache";
    void Bind(unsigned index, void* object, const char* designer, void* scene, std::ptrdiff_t vtable = 0) {
        auto* slot = bucket.data() + index * ent::entity_layout::IdentityStride;
        Put(slot, 0, Address(object));
        Put(slot, ent::entity_layout::HandleOffset, Handle(index));
        Put(slot, ent::CEntityIdentity::m_designerName, Address(designer));
        Put(object, ent::CEntityInstance::m_pEntity, Address(slot));
        Put(object, ent::C_BaseEntity::m_pGameSceneNode, Address(scene));
        Put(object, 0, Address(module.data()) + vtable);
        if (scene) Put(scene, ent::CGameSceneNode::m_flAbsScale, 1.0f);
    }
    Fixture() {
        Put(module.data(), ent::globals::dwEntityList, Address(system.data()));
        Put(module.data(), ent::globals::dwLocalPlayerController, Address(controller.data()));
        Put(module.data(), ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
        Put(system.data(), ent::entity_layout::TableOffset, Address(bucket.data()));
        Bind(1, controller.data(), controllerName, nullptr);
        Bind(2, pawn.data(), pawnName, playerScene.data());
        Bind(3, rifle.data(), nullptr, nullptr);
        Bind(4, c4.data(), nullptr, c4Scene.data(), item::C4Vtable);
        Bind(5, planted.data(), nullptr, plantedScene.data(), item::PlantedC4Vtable);
        Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, Handle(2));
        const char name[] = "Fixture player";
        std::memcpy(controller.data() + ent::CBasePlayerController::m_iszPlayerName, name, sizeof(name));
        Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 100);
        Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
        Put(pawn.data(), ent::C_BaseEntity::m_pCollision, Address(collision.data()));
        Put(collision.data(), ent::CCollisionProperty::m_vecMins, EspVec3{-0.1f, -0.1f, 0});
        Put(collision.data(), ent::CCollisionProperty::m_vecMaxs, EspVec3{0.1f, 0.1f, 0.8f});
        Put(pawn.data(), item::WeaponServices, Address(services.data()));
        Put(services.data(), item::ActiveWeapon, Handle(3));
        Put(rifle.data(), item::OwnerHandle, Handle(2));
        Put(rifle.data(), item::Item + item::DefinitionIndex, std::uint16_t{7});
        Put(c4.data(), item::OwnerHandle, Handle(2));
        Put(c4.data(), item::Item + item::DefinitionIndex, std::uint16_t{49});
        Put(c4Scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{10, 20, 30});
        Put(plantedScene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{40, 50, 60});
        EspMatrix4x4 matrix{};
        for (unsigned i = 0; i < 4; ++i) matrix.m[i][i] = 1;
        Put(module.data(), ent::globals::dwViewMatrix, matrix);
        PlayerReaderTestConfigure(Address(module.data()));
    }
};
}

int main() {
    Fixture fixture;
    auto frame = PlayerReaderCapture();
    Check(frame.valid && frame.playerCount == 1, "synthetic controller/pawn captured");
    Check(frame.players[0].weaponDefinition == 7, "active AK definition copied");
    Check(frame.players[0].carriesC4, "C4 carrier recognized while rifle selected");
    Check(frame.bombCount == 0, "owned and inactive planted C4 excluded from world");
    Put(fixture.pawn.data(), item::WeaponServices, std::uintptr_t{0});
    frame = PlayerReaderCapture();
    Check(frame.valid && frame.playerCount == 1 && frame.players[0].weaponDefinition == 0,
          "missing weapon services preserves player geometry");
    Check(frame.players[0].carriesC4, "carrier recognition independent of weapon services");
    Put(fixture.pawn.data(), item::WeaponServices, Address(fixture.services.data()));
    Put(fixture.services.data(), item::ActiveWeapon, Handle(3) + (1u << 15));
    frame = PlayerReaderCapture();
    Check(frame.playerCount == 1 && frame.players[0].weaponDefinition == 0,
          "active handle with matching index but stale serial rejects weapon");
    Put(fixture.services.data(), item::ActiveWeapon, Handle(3));
    Put(fixture.c4.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
    frame = PlayerReaderCapture();
    Check(frame.players[0].carriesC4 && frame.bombCount == 0,
          "inventory C4 without scene still identifies carrier");
    Put(fixture.c4.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(fixture.c4Scene.data()));
    Put(fixture.services.data(), item::ActiveWeapon, Handle(4));
    frame = PlayerReaderCapture();
    Check(frame.players[0].weaponDefinition == 49 && frame.players[0].carriesC4, "active C4 and carrier are separate inputs");
    Put(fixture.c4.data(), item::OwnerHandle, UINT32_MAX);
    frame = PlayerReaderCapture();
    Check(!frame.players[0].carriesC4, "dropping C4 clears carrier next capture");
    Check(frame.players[0].weaponDefinition == 0, "active weapon no longer owned is rejected");
    Check(frame.bombCount == 1 && frame.bombs[0].kind == EspBombKind::Dropped, "unowned C4 appears dropped");
    Check(frame.bombs[0].handle == Handle(4) && frame.bombs[0].origin.z == 30, "world identity and scene origin copied");
    Put(fixture.c4.data(), item::OwnerHandle, Handle(2));
    frame = PlayerReaderCapture();
    Check(frame.bombCount == 0 && frame.players[0].carriesC4 && frame.players[0].weaponDefinition == 49,
          "pickup clears world marker and restores carrier and held C4");
    Put(fixture.c4.data(), item::OwnerHandle, UINT32_MAX);
    Put(fixture.c4.data(), item::OwnerHandle, Handle(40));
    frame = PlayerReaderCapture();
    Check(frame.bombCount == 0 && !frame.players[0].carriesC4, "unresolved owner never means dropped or carrier");
    Put(fixture.c4.data(), item::OwnerHandle, UINT32_MAX);
    Put(fixture.c4.data(), item::C4BombPlanted, std::uint8_t{1});
    Put(fixture.planted.data(), item::BombTicking, std::uint8_t{1});
    frame = PlayerReaderCapture();
    Check(frame.bombCount == 1 && frame.bombs[0].kind == EspBombKind::Planted, "plant replaces old weapon marker");
    Check(frame.bombs[0].handle == Handle(5) && frame.bombs[0].origin.x == 40, "planted position comes from planted entity");
    Put(fixture.planted.data(), item::BombDefused, std::uint8_t{1});
    Check(PlayerReaderCapture().bombCount == 0, "defused bomb disappears");
    Put(fixture.planted.data(), item::BombDefused, std::uint8_t{0});
    Put(fixture.planted.data(), item::BombExploded, std::uint8_t{1});
    Check(PlayerReaderCapture().bombCount == 0, "exploded bomb disappears");
    Put(fixture.planted.data(), item::BombExploded, std::uint8_t{0});
    Put(fixture.planted.data(), item::BombTicking, std::uint8_t{0});
    Check(PlayerReaderCapture().bombCount == 0, "inactive bomb disappears");
    Put(fixture.planted.data(), item::BombTicking, std::uint8_t{2});
    Check(PlayerReaderCapture().bombCount == 0, "malformed boolean rejects");
    Put(fixture.planted.data(), item::BombTicking, std::uint8_t{1});
    Put(fixture.plantedScene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{1});
    Check(PlayerReaderCapture().bombCount == 0, "dormant world entity rejects");
    Put(fixture.plantedScene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{0});
    Put(fixture.plantedScene.data(), ent::CGameSceneNode::m_vecAbsOrigin,
        EspVec3{std::numeric_limits<float>::quiet_NaN(), 0, 0});
    Check(PlayerReaderCapture().bombCount == 0, "invalid world origin rejects");
    Put(fixture.plantedScene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{40, 50, 60});
    Put(fixture.planted.data(), 0, Address(fixture.module.data()) + item::C4Vtable + 8);
    Check(PlayerReaderCapture().bombCount == 0, "wrong C4 class table rejects");
    Put(fixture.planted.data(), 0, Address(fixture.module.data()) + item::PlantedC4Vtable);
    Put(fixture.planted.data(), ent::CEntityInstance::m_pEntity, std::uintptr_t{0});
    Check(PlayerReaderCapture().bombCount == 0, "stale entity back pointer rejects");
    Put(fixture.module.data(), ent::globals::dwEntityList, std::uintptr_t{0});
    frame = PlayerReaderCapture();
    Check(!frame.valid && frame.bombCount == 0 && frame.playerCount == 0, "missing system clears all snapshots");
    PlayerReaderTestConfigure(0);
    std::printf("player reader: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
