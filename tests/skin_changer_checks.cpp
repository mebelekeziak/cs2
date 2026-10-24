#include "skin_changer.h"
#include "skin_settings.h"
#include "skin_catalog.h"
#include "skin_offsets.h"
#include "model_swap_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

// Production links the existing verified native model loader. Synthetic
// callbacks inject a loader and never enter this stub or any installed game.
bool ModelSwapPreloadResource(const char*) noexcept { return false; }

namespace {
namespace off = skin_offsets;
namespace ent = cs2_offsets;
unsigned checks = 0, failures = 0;
void Check(bool value, const char* message) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", message); } }
std::uintptr_t Address(const void* value) { return reinterpret_cast<std::uintptr_t>(value); }
template<class T> void Put(void* value, std::size_t offset, const T& input) { std::memcpy(static_cast<unsigned char*>(value) + offset, &input, sizeof(input)); }
template<class T> T Get(const void* value, std::size_t offset) { T output{}; std::memcpy(&output, static_cast<const unsigned char*>(value) + offset, sizeof(output)); return output; }
unsigned char* image = nullptr;
std::array<unsigned char, 0x2400> pawn{};
std::array<unsigned char, 0x1000> controller{};
std::array<unsigned char, 0x2100> weapon{}, knife{};
std::array<unsigned char, 0x100> services{}, system{}, owner{};
std::array<unsigned char, 0x70 * 512> bucket{};
std::array<std::uint32_t, 2> handles{};
std::array<std::array<unsigned char, 0x48 * 16>, 3> attributes{};
std::array<unsigned char, 0x300> scene{};
std::array<unsigned char, 0x20> model{};
std::array<char, 260> modelName{};
std::uintptr_t binding = 0;
constexpr std::uint32_t pawnHandle = 0x10003, controllerHandle = 0x18002, weaponHandle = 0x10004, knifeHandle = 0x10005;
constexpr const char* nativeKnife = "weapons/models/knife/knife_default_ct/weapon_knife_default_ct.vmdl";
struct Mock {
    unsigned frames = 0, sets = 0, removes = 0, refreshes = 0, models = 0, loads = 0;
    DWORD thread = 0;
    int loadMutation = 0;
    bool loadReady = true, failModel = false, errorModel = false, nested = false, inNested = false;
} mock;
void* Item(unsigned index) {
    return index == 0 ? weapon.data() + off::Item : index == 1 ? knife.data() + off::Item : pawn.data() + off::GlovesItem;
}
void Name(const char* path) {
    modelName.fill(0); std::snprintf(modelName.data(), modelName.size(), "%s", path);
    binding = Address(model.data()); Put(model.data(), model_swap_offsets::ResourceName, Address(modelName.data()));
    Put(scene.data(), model_swap_offsets::ModelBinding, Address(&binding));
    Put(knife.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(scene.data()));
}
void Identity(void* entity, unsigned index, std::uint32_t handle) {
    Put(bucket.data() + index * 0x70, 0, Address(entity));
    Put(bucket.data() + index * 0x70, 0x10, handle);
    Put(entity, ent::CEntityInstance::m_pEntity, Address(bucket.data() + index * 0x70));
}
int AttributePosition(void* item, std::uint16_t id) {
    const int count = Get<int>(item, off::AttributesCount);
    const auto data = Get<std::uintptr_t>(item, off::AttributesData);
    for (int index = 0; index < count; ++index)
        if (Get<std::uint16_t>(reinterpret_cast<void*>(data + index * off::AttributeStride), off::AttributeIndex) == id) return index;
    return -1;
}
void AttributeBits(void* item, std::uint16_t id, std::uint32_t bits) {
    int index = AttributePosition(item, id);
    const bool introduced = index < 0;
    if (introduced) { index = Get<int>(item, off::AttributesCount); Put(item, off::AttributesCount, index + 1); }
    const auto data = Get<std::uintptr_t>(item, off::AttributesData);
    Put(reinterpret_cast<void*>(data + index * off::AttributeStride), off::AttributeIndex, id);
    Put(reinterpret_cast<void*>(data + index * off::AttributeStride), off::AttributeValue, bits);
    if (introduced) Put(reinterpret_cast<void*>(data + index * off::AttributeStride), off::AttributeInitialValue, bits);
}
std::uint32_t Bits(void* item, std::uint16_t id) {
    const int index = AttributePosition(item, id);
    return index < 0 ? UINT32_MAX : Get<std::uint32_t>(reinterpret_cast<void*>(Get<std::uintptr_t>(item, off::AttributesData) + index * off::AttributeStride), off::AttributeValue);
}
void Set(void* item, const char* name, float input) {
    ++mock.sets; mock.thread = GetCurrentThreadId();
    constexpr const char* names[]{"set item texture prefab", "set item texture seed", "set item texture wear", "kill eater", "kill eater score type"};
    for (std::size_t index = 0; index < std::size(names); ++index)
        if (!std::strcmp(name, names[index])) { AttributeBits(item, off::AttributeIds[index], std::bit_cast<std::uint32_t>(input)); return; }
    Check(false, "runtime uses verified native attribute names");
}
void Remove(void* item, std::uint16_t id) {
    ++mock.removes; mock.thread = GetCurrentThreadId();
    const int index = AttributePosition(item, id);
    if (index < 0) return;
    const int count = Get<int>(item, off::AttributesCount);
    const auto data = Get<std::uintptr_t>(item, off::AttributesData);
    std::memmove(reinterpret_cast<void*>(data + index * off::AttributeStride), reinterpret_cast<void*>(data + (index + 1) * off::AttributeStride), (count - index - 1) * off::AttributeStride);
    Put(item, off::AttributesCount, count - 1);
}
void Refresh(void*) { ++mock.refreshes; mock.thread = GetCurrentThreadId(); }
void Model(void* entity, const char* path) {
    ++mock.models; Check(entity == knife.data(), "model setter only receives owned knife");
    if (mock.errorModel && std::strcmp(path, nativeKnife)) Name("models/error.vmdl");
    else if (!mock.failModel) Name(path);
}
void Native(void* frameOwner, int stage) {
    ++mock.frames;
    if (mock.nested && !mock.inNested) { mock.inNested = true; SkinChangerTestFrame(frameOwner, stage); mock.inNested = false; }
}
bool Load(const char*) {
    ++mock.loads;
    if (mock.loadMutation == 1) SkinResetSettings();
    if (mock.loadMutation == 2) SkinChangerStop("stop during load");
    if (mock.loadMutation == 3) Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
    if (mock.loadMutation == 4) Put(bucket.data() + 5 * 0x70, 0x10, std::uint32_t{0x20005});
    if (mock.loadMutation == 5) Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0);
    return mock.loadReady;
}
void Reset() {
    pawn.fill(0); controller.fill(0); weapon.fill(0); knife.fill(0); services.fill(0); system.fill(0); owner.fill(0); bucket.fill(0);
    for (auto& value : attributes) value.fill(0);
    scene.fill(0); model.fill(0); mock = {};
    Put(image, ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
    Put(image, ent::globals::dwLocalPlayerController, Address(controller.data()));
    Put(image, ent::globals::dwGameEntitySystem, Address(system.data()));
    Put(system.data(), ent::entity_layout::TableOffset, Address(bucket.data()));
    Identity(pawn.data(), 3, pawnHandle); Identity(controller.data(), 2, controllerHandle);
    Identity(weapon.data(), 4, weaponHandle); Identity(knife.data(), 5, knifeHandle);
    Put(pawn.data(), ent::C_BasePlayerPawn::m_hController, controllerHandle);
    Put(pawn.data(), ent::C_BasePlayerPawn::m_hDefaultController, UINT32_MAX);
    Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{3});
    Put(controller.data(), ent::CBasePlayerController::m_bIsLocalPlayerController, std::uint8_t{1});
    Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle);
    Put(weapon.data(), off::OwnerHandle, pawnHandle); Put(knife.data(), off::OwnerHandle, pawnHandle);
    Put(pawn.data(), off::WeaponServices, Address(services.data()));
    handles = {weaponHandle, knifeHandle};
    Put(services.data(), off::WeaponHandlesCount, 2); Put(services.data(), off::WeaponHandlesData, Address(handles.data()));
    Put(owner.data(), 0, Address(image) + model_swap_offsets::FrameVtable);
    for (unsigned index = 0; index < attributes.size(); ++index) {
        Put(Item(index), off::AttributesData, Address(attributes[index].data()));
        AttributeBits(Item(index), 200, 12345); // unrelated engine-owned attribute
        Put(Item(index), off::DefinitionIndex, static_cast<std::uint16_t>(index == 0 ? 7 : index == 1 ? 42 : 5028));
        Put(Item(index), off::Quality, 4);
        Put(Item(index), off::ItemIdHigh, std::uint32_t{987});
    }
    AttributeBits(Item(0), off::AttributeIds[0], std::bit_cast<std::uint32_t>(12.0f));
    Name(nativeKnife);
    SkinChangerTestConfigure(Address(image), Native, Set, Remove, Refresh, Model, Load);
    SkinChangerActivate();
}
SkinItemSettings Selection(int definition, int paint) {
    SkinItemSettings value; value.enabled = true; value.definitionIndex = definition; value.paintKit = paint; value.seed = 123; value.wear = 0.05f;
    return value;
}
void Select(bool models = false) {
    SkinSettings settings; settings.enabled = true;
    *SkinEnsureWeaponSettings(settings.teams[1], 7) = Selection(7, 180);
    *SkinEnsureWeaponSettings(settings.teams[0], 7) = Selection(7, 302);
    settings.teams[1].weapons[0].statTrak = 42;
    SkinSetCustomName(settings.teams[1].weapons[0], "Owner frame");
    if (models) {
        settings.teams[1].knife = Selection(515, 0); settings.teams[0].knife = Selection(500, 0);
        settings.teams[1].gloves = Selection(5030, 0); settings.teams[0].gloves = Selection(5031, 0);
    }
    SkinSetSettings(settings);
}
void Frame(int stage = model_swap_offsets::ApplyStage) { SkinChangerTestFrame(owner.data(), stage); }
int Definition(unsigned index) { return Get<std::uint16_t>(Item(index), off::DefinitionIndex); }
}
int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, ent::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    Check(skin_catalog::supports_paint(7, 180) && skin_catalog::supports_paint(7, 302), "test paints belong to installed AK-47 schema");
    Reset(); Frame(); Check(mock.frames == 1 && !mock.sets, "disabled forwards owner without item writes");
    Select(); Frame(7); Check(!mock.sets, "other frame stage cannot customize");
    Frame(); Check(mock.sets == 5 && mock.refreshes == 1 && mock.thread == GetCurrentThreadId(), "selected weapon mutates only on native owner");
    Check(Bits(Item(0), off::AttributeIds[0]) == std::bit_cast<std::uint32_t>(180.0f), "paint reaches native float setter");
    Check(Bits(Item(0), off::AttributeIds[3]) == 42, "StatTrak native integer payload preserves raw bits");
    Check(!std::strcmp(reinterpret_cast<char*>(Item(0)) + off::CustomName, "Owner frame"), "custom item name applies");
    for (int repeat = 0; repeat < 50; ++repeat) Frame();
    Check(mock.sets == 5 && mock.refreshes == 1, "stable configuration avoids per-frame regenerations");
    Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2}); Frame();
    Check(Bits(Item(0), off::AttributeIds[0]) == std::bit_cast<std::uint32_t>(302.0f), "team switch independently applies T paint");
    Check(AttributePosition(Item(0), off::AttributeIds[3]) < 0, "T loadout can independently disable StatTrak");
    SkinResetSettings(); Frame();
    Check(Bits(Item(0), off::AttributeIds[0]) == std::bit_cast<std::uint32_t>(12.0f) && Get<std::uint32_t>(Item(0), off::ItemIdHigh) == 987, "disable restores original fields and original paint");
    Check(AttributePosition(Item(0), off::AttributeIds[1]) < 0 && Bits(Item(0), 200) == 12345, "restore removes only introduced attrs and preserves unrelated attrs");

    Reset(); Select(true); Frame();
    Check(Definition(1) == 515 && Definition(2) == 5030 && mock.models == 1, "CT independent knife and gloves definitions apply");
    Check(Get<bool>(pawn.data(), off::GloveReapply) && Get<bool>(pawn.data(), off::GloveChanged), "gloves schedule native attachment rebuild");
    Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2}); Frame();
    Check(Definition(1) == 500 && Definition(2) == 5031, "T independently selects different knife and gloves");
    SkinChangerStop("test"); Check(Definition(1) == 500, "stop requesting thread never calls item setter");
    Frame(); Check(Definition(1) == 42 && Definition(2) == 5028 && !std::strcmp(modelName.data(), nativeKnife), "resident owner callback restores item and model originals");
    const auto setsAfterStop = mock.sets;
    SkinChangerActivate(); Select(true); Frame(); Check(mock.sets == setsAfterStop, "stopped feature cannot reactivate");

    Reset(); Select(); Frame();
    AttributeBits(Item(0), off::AttributeIds[0], std::bit_cast<std::uint32_t>(44.0f)); SkinResetSettings(); Frame();
    Check(Bits(Item(0), off::AttributeIds[0]) == std::bit_cast<std::uint32_t>(44.0f), "native superseding item state is never overwritten during restore");
    Reset(); Select(); Frame(); const auto appliedSets = mock.sets;
    Put(bucket.data() + 4 * 0x70, 0x10, std::uint32_t{0x20004}); SkinChangerStop("serial reuse"); Frame();
    Check(mock.sets == appliedSets, "reused identity serial cannot restore stale entity");
    Reset(); Select(); Put(weapon.data(), off::OwnerHandle, std::uint32_t{0x10006}); Frame();
    Check(!mock.sets, "foreign ownership never receives changes");
    Reset(); Select(); Put(controller.data(), ent::CBasePlayerController::m_bIsLocalPlayerController, std::uint8_t{0}); Frame();
    Check(!mock.sets, "unverified local controller cannot authorize writes");
    Reset(); Select(); Frame(); Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); Frame();
    Check(Get<std::uint32_t>(Item(0), off::ItemIdHigh) == 987, "death restores originals on same exact local pawn");
    Reset(); Select(); mock.nested = true; Frame();
    Check(mock.frames == 2 && mock.refreshes == 1, "nested native callbacks forward without repeated skin work");
    for (int mutation = 1; mutation <= 5; ++mutation) {
        Reset(); Select(true); mock.loadMutation = mutation; Frame();
        Check(!mock.models && Definition(1) == 42, "blocking preload rejects stale settings/team/life/identity/stop");
    }
    Reset(); Select(true); mock.loadReady = false; Frame();
    Check(Definition(1) == 42 && !mock.models, "failed model preload preserves original knife");
    for (int repeat = 0; repeat < 50; ++repeat) Frame();
    Check(mock.loads == 1, "failed blocking preload is suppressed until intent changes");
    auto retry = SkinGetSettings(); retry.enabled = false; SkinSetSettings(retry); Frame();
    retry.enabled = true; SkinSetSettings(retry); mock.loadReady = true; Frame();
    Check(mock.loads == 2 && Definition(1) == 515, "explicit toggle retries failed selection");
    Reset(); Select(true); mock.failModel = true; Frame();
    mock.failModel = false; SkinResetSettings(); Frame();
    Check(Definition(1) == 42 && !std::strcmp(modelName.data(), nativeKnife), "failed model application cooperatively rolls back item fields");
    Reset(); Select(true); mock.errorModel = true; Frame();
    Check(!std::strcmp(modelName.data(), "models/error.vmdl"), "synthetic setter exposes unexpected engine error model");
    SkinResetSettings(); Frame();
    Check(Definition(1) == 42 && !std::strcmp(modelName.data(), nativeKnife), "failed assignment error resource restores original model and item");
    Reset(); Select(true); mock.errorModel = true; Frame();
    mock.failModel = true; SkinResetSettings(); Frame();
    Check(std::strstr(SkinRuntimeStatus().data(), "pending") != nullptr, "failed rollback remains pending while callback code stays resident");
    mock.failModel = false; Frame();
    Check(Definition(1) == 42 && !std::strcmp(modelName.data(), nativeKnife), "owner callback retries incomplete model restoration");
    Reset(); Select(true); Frame();
    Name("weapons/models/knife/native_replacement.vmdl"); SkinResetSettings(); Frame();
    Check(!std::strcmp(modelName.data(), "weapons/models/knife/native_replacement.vmdl"), "restoration preserves an independent native knife model replacement");

    Reset();
    AttributeBits(Item(0), off::AttributeIds[3], 13);
    const auto statPosition = AttributePosition(Item(0), off::AttributeIds[3]);
    Put(attributes[0].data() + statPosition * off::AttributeStride, off::AttributeInitialValue, std::uint32_t{7});
    Select(); Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2}); Frame();
    Check(AttributePosition(Item(0), off::AttributeIds[3]) < 0, "disabling StatTrak removes existing native attribute");
    SkinResetSettings(); Frame();
    const auto restoredStat = AttributePosition(Item(0), off::AttributeIds[3]);
    Check(restoredStat >= 0 && Bits(Item(0), off::AttributeIds[3]) == 13 &&
        Get<std::uint32_t>(attributes[0].data() + restoredStat * off::AttributeStride, off::AttributeInitialValue) == 7,
        "restoring removed attribute preserves separate initial and current values");
    Reset(); Select();
    const auto paintPosition = AttributePosition(Item(0), off::AttributeIds[0]);
    Put(attributes[0].data() + paintPosition * off::AttributeStride, off::AttributeSetBonus, std::uint8_t{1}); Frame();
    Check(!mock.sets && Get<std::uint32_t>(Item(0), off::ItemIdHigh) == 987, "set-bonus variant of owned attribute is rejected without mutations");
    Reset(); Select();
    auto invalidPaint = SkinGetSettings(); invalidPaint.teams[1].weapons[0].paintKit = 1000000; SkinSetSettings(invalidPaint); Frame();
    Check(!mock.sets && !mock.refreshes, "unknown or incompatible paint kit cannot reach native setter");
    Reset(); Select(true); Frame();
    const auto beforeDisconnect = mock.sets;
    Put(image, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0}); SkinChangerStop("disconnect"); Frame();
    Check(mock.sets == beforeDisconnect, "disconnect invalidates ownership without stale entity writes");

    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("skin_changer_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
