#include "skin_changer.h"
#include "skin_settings.h"
#include "skin_catalog.h"
#include "skin_offsets.h"
#include "knife_animation_offsets.h"
#include "client_frame.h"
#include "cs2_offsets.h"
#include "model_swap.h"
#include "model_swap_offsets.h"
#include "movement_memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>

namespace {
namespace off = skin_offsets;
namespace ent = cs2_offsets;
namespace model = model_swap_offsets;
namespace anim = knife_animation_offsets;
using AttributeFn = void (*)(void*, const char*, float);
using RemoveFn = void (*)(void*, std::uint16_t);
using RefreshFn = void (*)(void*);
using CompositeResetFn = void (*)(void*, bool);
using RegenerateFn = void (*)(void*, bool);
using ModelFn = void (*)(void*, const char*);
using ItemSubclassFn = void* (*)(void*);
using SubclassChangedFn = void (*)(void*);
AttributeFn setAttribute = nullptr;
RemoveFn removeAttribute = nullptr;
RefreshFn refresh = nullptr;
CompositeResetFn resetComposite = nullptr;
RegenerateFn regenerate = nullptr;
ModelFn setModel = nullptr;
ItemSubclassFn itemSubclass = nullptr;
SubclassChangedFn subclassChanged = nullptr;
std::uintptr_t client = 0;
SkinChangerLogCallback logger = nullptr;
SRWLOCK stateLock = SRWLOCK_INIT;
bool prepared = false;
std::atomic<bool> active{false}, stopped{false};
std::atomic<unsigned long long> frames{0}, assignments{0}, restorations{0}, invalidations{0}, failures{0};
std::atomic<DWORD> ownerThread{0};
std::atomic<unsigned long long> animationAssignments{0}, animationRestores{0};
ULONGLONG lastReportTime = 0;
unsigned long long lastReportedFrames = 0;
#ifdef CS2_SKIN_CHANGER_CHECKS
bool testRuntime = false;
SkinChangerLoadFn testLoad = nullptr;
#endif
using Path = std::array<char, 260>;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
template<class T> bool Write(std::uintptr_t address, const T& value) noexcept {
    if (!MovementOwnedWritable(address, sizeof(value))) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(address, bytes, N) && !std::memcmp(bytes, expected, N);
}
bool CurrentClient() noexcept {
#ifdef CS2_SKIN_CHANGER_CHECKS
    if (testRuntime) return client != 0;
#endif
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
struct Identity {
    std::uintptr_t entity = 0, slot = 0;
    std::uint32_t handle = UINT32_MAX;
    bool operator==(const Identity&) const = default;
};
struct Source {
    std::uintptr_t system = 0;
    Identity pawn{}, controller{};
    bool operator==(const Source&) const = default;
};
bool Resolve(std::uintptr_t system, std::uint32_t handle, Identity& result) noexcept {
    if (handle == UINT32_MAX) return false;
    const auto index = handle & ent::entity_layout::IndexMask;
    std::uintptr_t bucket = 0, back = 0;
    std::uint32_t serial = UINT32_MAX;
    if (!Read(system + ent::entity_layout::TableOffset +
        (index >> ent::entity_layout::BucketShift) * sizeof(std::uintptr_t), bucket) || !bucket) return false;
    result.slot = bucket + (index & ent::entity_layout::BucketMask) * ent::entity_layout::IdentityStride;
    result.handle = handle;
    return Read(result.slot, result.entity) && result.entity &&
        Read(result.slot + ent::entity_layout::HandleOffset, serial) && serial == handle &&
        Read(result.entity + ent::CEntityInstance::m_pEntity, back) && back == result.slot;
}
bool FromPointer(std::uintptr_t system, std::uintptr_t pointer, Identity& result) noexcept {
    std::uintptr_t slot = 0;
    std::uint32_t handle = UINT32_MAX;
    return pointer && Read(pointer + ent::CEntityInstance::m_pEntity, slot) && slot &&
        Read(slot + ent::entity_layout::HandleOffset, handle) && Resolve(system, handle, result) && result.entity == pointer;
}
bool ReadSource(Source& source, std::uint8_t& team, bool& alive) noexcept {
    std::uintptr_t pawn = 0, controller = 0;
    std::uint32_t pawnLink = UINT32_MAX, controllerLink = UINT32_MAX, fallbackLink = UINT32_MAX;
    std::uint8_t local = 0, life = 1;
    int health = 0;
    if (!CurrentClient() || !Read(client + ent::globals::dwGameEntitySystem, source.system) || !source.system ||
        !Read(client + ent::globals::dwLocalPlayerPawn, pawn) ||
        !Read(client + ent::globals::dwLocalPlayerController, controller) ||
        !FromPointer(source.system, pawn, source.pawn) || !FromPointer(source.system, controller, source.controller) ||
        !Read(controller + ent::CBasePlayerController::m_bIsLocalPlayerController, local) || local != 1 ||
        !Read(controller + ent::CCSPlayerController::m_hPlayerPawn, pawnLink) || pawnLink != source.pawn.handle ||
        !Read(pawn + ent::C_BasePlayerPawn::m_hController, controllerLink) ||
        !Read(pawn + ent::C_BasePlayerPawn::m_hDefaultController, fallbackLink) ||
        (controllerLink != source.controller.handle && fallbackLink != source.controller.handle) ||
        !Read(pawn + ent::C_BaseEntity::m_iTeamNum, team) || (team != 2 && team != 3) ||
        !Read(pawn + ent::C_BaseEntity::m_iHealth, health) || !Read(pawn + ent::C_BaseEntity::m_lifeState, life)) return false;
    alive = health > 0 && life == 0;
    return true;
}
bool SameSource(const Source& source) noexcept {
    Source current{}; std::uint8_t team = 0; bool alive = false;
    return ReadSource(current, team, alive) && current == source;
}
bool SameEntity(const Source& source, const Identity& identity) noexcept {
    Identity current{};
    return SameSource(source) && Resolve(source.system, identity.handle, current) && current == identity;
}

// Capture only the five attributes we own. Their native list allocation and
// unrelated attributes stay engine-owned; restoration uses native set/remove.
constexpr std::array<const char*, 5> attributeNames{
    "set item texture prefab", "set item texture seed", "set item texture wear", "kill eater", "kill eater score type"};
struct Attribute {
    bool exists = false;
    std::uint32_t bits = 0;
    std::uint32_t initialBits = 0;
    bool operator==(const Attribute&) const = default;
};
struct ItemState {
    std::uint16_t definition = 0;
    int quality = 0;
    std::uint32_t idHigh = 0;
    bool initialized = false;
    std::array<char, 161> name{};
    std::array<Attribute, 5> attributes{};
    int paint = 0, seed = 0, stat = 0;
    float wear = 0;
    bool operator==(const ItemState&) const = default;
};
bool ReadAttributes(std::uintptr_t item, std::array<Attribute, 5>& result) noexcept {
    int count = 0;
    std::uintptr_t data = 0;
    if (!Read(item + off::AttributesCount, count) || count < 0 || count > 128 ||
        !Read(item + off::AttributesData, data) || (count && !data)) return false;
    result = {};
    for (int index = 0; index < count; ++index) {
        const auto attribute = data + static_cast<std::size_t>(index) * off::AttributeStride;
        std::uint16_t id = 0;
        if (!Read(attribute + off::AttributeIndex, id)) return false;
        for (std::size_t field = 0; field < result.size(); ++field) {
            if (id != off::AttributeIds[field]) continue;
            std::uint8_t setBonus = 0;
            // Duplicates cannot be round-tripped through the native ID setter.
            // The named setter skips set-bonus entries, while native remove
            // identifies by ID. Refuse that incompatible semantic record.
            if (result[field].exists || !Read(attribute + off::AttributeSetBonus, setBonus) || setBonus ||
                !Read(attribute + off::AttributeValue, result[field].bits) ||
                !Read(attribute + off::AttributeInitialValue, result[field].initialBits)) return false;
            result[field].exists = true;
        }
    }
    return true;
}
bool Snapshot(std::uintptr_t entity, std::uintptr_t item, bool glove, ItemState& value) noexcept {
    return Read(item + off::DefinitionIndex, value.definition) && Read(item + off::Quality, value.quality) &&
        Read(item + off::ItemIdHigh, value.idHigh) && Read(item + off::Initialized, value.initialized) &&
        Read(item + off::CustomName, value.name) && ReadAttributes(item, value.attributes) &&
        (glove || (Read(entity + off::FallbackPaint, value.paint) && Read(entity + off::FallbackSeed, value.seed) &&
            Read(entity + off::FallbackWear, value.wear) && Read(entity + off::FallbackStatTrak, value.stat)));
}
bool NativeAttribute(std::uintptr_t item, std::size_t index, const Attribute& attribute) noexcept {
    __try {
        if (attribute.exists) setAttribute(reinterpret_cast<void*>(item), attributeNames[index], std::bit_cast<float>(attribute.bits));
        else removeAttribute(reinterpret_cast<void*>(item), off::AttributeIds[index]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetInitialValue(std::uintptr_t item, std::size_t index, const Attribute& attribute) noexcept {
    if (!attribute.exists) return true;
    int count = 0;
    std::uintptr_t data = 0;
    if (!Read(item + off::AttributesCount, count) || count < 0 || count > 128 ||
        !Read(item + off::AttributesData, data) || !data) return false;
    for (int position = 0; position < count; ++position) {
        const auto address = data + static_cast<std::size_t>(position) * off::AttributeStride;
        std::uint16_t id = 0;
        std::uint8_t setBonus = 0;
        if (!Read(address + off::AttributeIndex, id)) return false;
        if (id == off::AttributeIds[index]) return Read(address + off::AttributeSetBonus, setBonus) && !setBonus &&
            Write(address + off::AttributeInitialValue, attribute.initialBits);
    }
    return false;
}
bool NativeRefresh(std::uintptr_t entity) noexcept {
    __try {
#ifdef CS2_SKIN_CHANGER_CHECKS
        if (testRuntime) { refresh(reinterpret_cast<void*>(entity)); return true; }
#endif
        resetComposite(reinterpret_cast<void*>(entity + off::CompositeOwner), true);
        regenerate(reinterpret_cast<void*>(entity), false);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool NativeModel(std::uintptr_t entity, const Path& path) noexcept {
    __try { setModel(reinterpret_cast<void*>(entity), path.data()); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ModelPath(std::uintptr_t entity, Path& path) noexcept {
    std::uintptr_t scene = 0, binding = 0, resource = 0, name = 0;
    if (!Read(entity + ent::C_BaseEntity::m_pGameSceneNode, scene) || !scene ||
        !Read(scene + model::ModelBinding, binding) || !binding || !Read(binding, resource) || !resource ||
        !Read(resource + model::ResourceName, name) || !name) return false;
    path = {};
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (!Read(name + index, path[index])) return false;
        if (!path[index]) return index > 0;
    }
    return false;
}
bool LoadModel(const Path& path) noexcept {
#ifdef CS2_SKIN_CHANGER_CHECKS
    if (testRuntime) return !testLoad || testLoad(path.data());
#endif
    return ModelSwapPreloadResource(path.data());
}
struct SubclassState {
    std::uint32_t id = 0;
    std::uintptr_t data = 0;
    bool operator==(const SubclassState&) const = default;
};
bool DescriptorName(std::uintptr_t data, Path& name) noexcept {
    std::uintptr_t pointer = 0;
    if (!Read(data + anim::VDataSubclassName, pointer) || !pointer) return false;
    name = {};
    for (std::size_t index = 0; index < name.size(); ++index) {
        if (!Read(pointer + index, name[index])) return false;
        if (!name[index]) return index > 0;
    }
    return false;
}
bool ValidDescriptor(const SubclassState& value, const char* expected = nullptr) noexcept {
    std::uintptr_t table = 0;
    std::uint32_t id = 0;
    int scope = 0;
    Path name{};
    return value.id && value.data && Read(value.data, table) && table == client + anim::VDataVtable &&
        Read(value.data + anim::VDataScope, scope) && scope == anim::WeaponScope &&
        Read(value.data + anim::VDataSubclassId, id) && id == value.id && DescriptorName(value.data, name) &&
        (!expected || !std::strcmp(name.data(), expected));
}
bool ReadSubclass(std::uintptr_t entity, SubclassState& value) noexcept {
    return Read(entity + anim::SubclassId, value.id) && Read(entity + anim::VData, value.data);
}
bool NativeItemSubclass(std::uintptr_t item, SubclassState& value) noexcept {
    __try { value.data = reinterpret_cast<std::uintptr_t>(itemSubclass(reinterpret_cast<void*>(item))); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return value.data && Read(value.data + anim::VDataSubclassId, value.id);
}
bool NativeSubclassChanged(std::uintptr_t entity) noexcept {
    __try { subclassChanged(reinterpret_cast<void*>(entity)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct OwnedItem {
    Identity identity{};
    std::uintptr_t item = 0;
    bool glove = false, owns = false, pending = false, seen = false, failed = false;
    ItemState original{}, applied{};
    Path originalModel{}, appliedModel{}, restoreModel{};
    SkinItemSettings selection{};
    SkinItemSettings rejected{};
    SubclassState originalSubclass{}, appliedSubclass{};
    bool ownsSubclass = false;
};
Source trackedSource{};
std::array<OwnedItem, 65> owned{};
void InvalidateAll() noexcept {
    for (auto& value : owned) if (value.item) { ++invalidations; value = {}; }
    trackedSource = {};
}
bool ItemValid(const Source& source, const OwnedItem& value) noexcept {
    if (!SameEntity(source, value.identity)) return false;
    if (value.glove) return value.identity == source.pawn && value.item == source.pawn.entity + off::GlovesItem;
    std::uint32_t owner = UINT32_MAX;
    return value.item == value.identity.entity + off::Item &&
        Read(value.identity.entity + off::OwnerHandle, owner) && owner == source.pawn.handle;
}
bool KnifeEntityValid(const Source& source, const OwnedItem& value) noexcept {
    std::uintptr_t table = 0;
    return !value.glove && ItemValid(source, value) && Read(value.identity.entity, table) && table == client + anim::KnifeVtable;
}
bool AssignSubclass(const Source& source, OwnedItem& value, const SubclassState& target, bool restoring) noexcept {
    if (!KnifeEntityValid(source, value) || !ValidDescriptor(target)) return false;
    SubclassState before{};
    if (!ReadSubclass(value.identity.entity, before)) return false;
    if (before == target) return true;
    if (!Write(value.identity.entity + anim::SubclassId, target.id) || !NativeSubclassChanged(value.identity.entity)) return false;
    SubclassState after{};
    if (!KnifeEntityValid(source, value) || !ReadSubclass(value.identity.entity, after) || after != target) return false;
    if (restoring) ++animationRestores; else ++animationAssignments;
    if (logger) {
        Path oldName{}, newName{};
        DescriptorName(before.data, oldName); DescriptorName(after.data, newName);
        char message[1024]{};
        std::snprintf(message, sizeof(message), "SKIN_KNIFE_ANIMATION_%s handle=%08x definition=%u old_subclass=%08x new_subclass=%08x old_vdata=%p new_vdata=%p old_name=%s new_name=%s owner_tid=%lu",
            restoring ? "RESTORED" : "ASSIGNED", value.identity.handle,
            restoring ? value.original.definition : value.applied.definition, before.id, after.id,
            reinterpret_cast<void*>(before.data), reinterpret_cast<void*>(after.data), oldName.data(), newName.data(), GetCurrentThreadId());
        logger(message);
    }
    return true;
}
bool ResolveSubclass(const Source& source, const OwnedItem& value, const char* expectedName, SubclassState& target) noexcept {
    return KnifeEntityValid(source, value) && NativeItemSubclass(value.item, target) &&
        ItemValid(source, value) && ValidDescriptor(target, expectedName);
}
bool AssignOwnedSubclass(const Source& source, OwnedItem& value, const SubclassState& target, bool restoring) noexcept {
    SubclassState before{};
    const bool changed = ReadSubclass(value.identity.entity, before) && before != target;
    const bool assigned = AssignSubclass(source, value, target, restoring);
    // Native VData lifecycle can recreate animation/HUD resources. Retain any
    // resulting geometry as part of this attempt, including failed attempts,
    // so rollback cannot mistake it for an unrelated engine replacement.
    Path after{};
    if (changed && ItemValid(source, value) && ModelPath(value.identity.entity, after)) value.appliedModel = after;
    return assigned;
}
bool WriteState(const Source& source, OwnedItem& value, const ItemState& desired) noexcept {
    if (!ItemValid(source, value)) return false;
    const auto item = value.item;
    // Check all directly owned scalar storage before the first mutation.
    if (!MovementOwnedWritable(item + off::DefinitionIndex, sizeof(desired.definition)) ||
        !MovementOwnedWritable(item + off::Quality, sizeof(desired.quality)) ||
        !MovementOwnedWritable(item + off::ItemIdHigh, sizeof(desired.idHigh)) ||
        !MovementOwnedWritable(item + off::Initialized, sizeof(desired.initialized)) ||
        !MovementOwnedWritable(item + off::CustomName, sizeof(desired.name)) ||
        (!value.glove && (!MovementOwnedWritable(value.identity.entity + off::FallbackPaint, sizeof(desired.paint)) ||
            !MovementOwnedWritable(value.identity.entity + off::FallbackSeed, sizeof(desired.seed)) ||
            !MovementOwnedWritable(value.identity.entity + off::FallbackWear, sizeof(desired.wear)) ||
            !MovementOwnedWritable(value.identity.entity + off::FallbackStatTrak, sizeof(desired.stat)))) ||
        (value.glove && (!MovementOwnedWritable(source.pawn.entity + off::GloveReapply, sizeof(bool)) ||
            !MovementOwnedWritable(source.pawn.entity + off::GloveChanged, sizeof(bool))))) return false;
    if (!Write(item + off::DefinitionIndex, desired.definition) || !Write(item + off::Quality, desired.quality) ||
        !Write(item + off::ItemIdHigh, desired.idHigh) || !Write(item + off::Initialized, desired.initialized) ||
        !Write(item + off::CustomName, desired.name)) return false;
    if (!value.glove && (!Write(value.identity.entity + off::FallbackPaint, desired.paint) ||
        !Write(value.identity.entity + off::FallbackSeed, desired.seed) ||
        !Write(value.identity.entity + off::FallbackWear, desired.wear) ||
        !Write(value.identity.entity + off::FallbackStatTrak, desired.stat))) return false;
    for (std::size_t index = 0; index < desired.attributes.size(); ++index)
        if (!ItemValid(source, value) || !NativeAttribute(item, index, desired.attributes[index]) ||
            !ItemValid(source, value) || !SetInitialValue(item, index, desired.attributes[index])) return false;
    if (!ItemValid(source, value)) return false;
    if (value.glove) {
        if (!Write(source.pawn.entity + off::GloveReapply, true) || !Write(source.pawn.entity + off::GloveChanged, true)) return false;
    } else if (!NativeRefresh(value.identity.entity)) return false;
    ItemState after{};
    return ItemValid(source, value) && Snapshot(value.identity.entity, item, value.glove, after) && after == desired;
}
bool Restore(const Source& source, OwnedItem& value) noexcept {
    if (!value.owns) return true;
    if (!ItemValid(source, value)) { ++invalidations; value = {}; return false; }
    ItemState current{};
    if (!Snapshot(value.identity.entity, value.item, value.glove, current)) return false;
    if (!value.pending && current != value.applied) {
        // Native inventory/network state superseded our exact assignment.
        value.owns = false; value.originalModel = {}; value.appliedModel = {};
        value.ownsSubclass = false; value.originalSubclass = {}; value.appliedSubclass = {}; return true;
    }
    const bool wasPending = value.pending;
    value.pending = true;
    if (!WriteState(source, value, value.original)) return false;
    if (value.originalModel[0]) {
        Path currentModel{};
        if (!ModelPath(value.identity.entity, currentModel)) return false;
        if (!value.restoreModel[0]) value.restoreModel = currentModel == value.appliedModel ? value.originalModel : currentModel;
        // Set original geometry before the native subclass callback. Its HUD
        // invalidation compares weapon/HUD model bindings at this exact point.
        if (currentModel != value.restoreModel) {
            if (!ItemValid(source, value) || !NativeModel(value.identity.entity, value.restoreModel) ||
                !ItemValid(source, value) || !ModelPath(value.identity.entity, currentModel) || currentModel != value.restoreModel) return false;
        }
    }
    if (value.ownsSubclass) {
        SubclassState currentSubclass{};
        if (!KnifeEntityValid(source, value) || !ReadSubclass(value.identity.entity, currentSubclass)) return false;
        if ((wasPending || currentSubclass == value.appliedSubclass) &&
            !AssignOwnedSubclass(source, value, value.originalSubclass, true)) return false;
        value.ownsSubclass = false;
    }
    if (value.restoreModel[0]) {
        Path after{};
        if (!ItemValid(source, value) || !ModelPath(value.identity.entity, after)) return false;
        if (after != value.restoreModel && (!NativeModel(value.identity.entity, value.restoreModel) ||
            !ItemValid(source, value) || !ModelPath(value.identity.entity, after) || after != value.restoreModel)) return false;
        if (!ItemValid(source, value) || !NativeRefresh(value.identity.entity)) return false;
    }
    value.owns = value.pending = false;
    value.originalModel = {}; value.appliedModel = {}; value.restoreModel = {};
    value.originalSubclass = {}; value.appliedSubclass = {};
    ++restorations;
    return true;
}
ItemState Desired(const ItemState& original, const ItemState& current, const SkinItemSettings& selected, bool knife) noexcept {
    auto result = original;
    result.definition = static_cast<std::uint16_t>(selected.definitionIndex);
    result.quality = knife ? 3 : selected.statTrak >= 0 ? 9 : original.quality;
    result.idHigh = UINT32_MAX;
    result.initialized = true;
    result.name = {};
    std::copy(selected.customName.begin(), selected.customName.end(), result.name.begin());
    const auto attribute = [&](std::size_t index, std::uint32_t bits) noexcept {
        return Attribute{true, bits, current.attributes[index].exists ? current.attributes[index].initialBits : bits};
    };
    result.attributes[0] = attribute(0, std::bit_cast<std::uint32_t>(static_cast<float>(selected.paintKit)));
    result.attributes[1] = attribute(1, std::bit_cast<std::uint32_t>(static_cast<float>(selected.seed)));
    result.attributes[2] = attribute(2, std::bit_cast<std::uint32_t>(selected.wear));
    result.attributes[3] = selected.statTrak >= 0 ? attribute(3, static_cast<std::uint32_t>(selected.statTrak)) : Attribute{};
    result.attributes[4] = selected.statTrak >= 0 ? attribute(4, 0) : Attribute{};
    result.paint = selected.paintKit; result.seed = selected.seed; result.wear = selected.wear; result.stat = selected.statTrak;
    return result;
}
OwnedItem* Track(const Identity& identity, std::uintptr_t item, bool glove) noexcept {
    for (auto& value : owned) if (value.item == item && value.identity == identity) { value.seen = true; return &value; }
    for (auto& value : owned) if (!value.item) {
        value.identity = identity; value.item = item; value.glove = glove; value.seen = true; return &value;
    }
    return nullptr;
}
void UpdateItem(const Source& source, OwnedItem& value, const SkinTeamLoadout& loadout,
    const SkinSettings& intent, std::uint8_t team, bool enabled) noexcept {
    enabled = enabled && active.load() && !stopped.load();
    if (!ItemValid(source, value)) { ++invalidations; value = {}; return; }
    ItemState current{};
    if (!Snapshot(value.identity.entity, value.item, value.glove, current)) { ++failures; return; }
    if (value.owns && !value.pending && current != value.applied) {
        value.owns = false; value.originalModel = {}; value.appliedModel = {};
        value.ownsSubclass = false; value.originalSubclass = {}; value.appliedSubclass = {};
    }
    if (!enabled) { value.failed = false; value.rejected = {}; }
    if (value.pending) { if (!Restore(source, value)) ++failures; return; }
    const auto definition = value.owns ? value.original.definition : current.definition;
    const auto* item = skin_catalog::find_item(definition);
    const bool knife = !value.glove && item && item->kind == skin_catalog::ItemKind::Knife;
    const auto* selected = value.glove ? &loadout.gloves : knife ? &loadout.knife : SkinFindWeaponSettings(loadout, definition);
    const auto* selectedItem = selected ? skin_catalog::find_item(selected->definitionIndex) : nullptr;
    const bool compatible = selectedItem && selected && selected->enabled &&
        selectedItem->kind == (value.glove ? skin_catalog::ItemKind::Gloves : knife ? skin_catalog::ItemKind::Knife : skin_catalog::ItemKind::Weapon) &&
        (knife || value.glove || selected->definitionIndex == definition) && skin_catalog::supports_paint(selected->definitionIndex, selected->paintKit);
    if (!enabled || !compatible) { value.failed = false; value.rejected = {}; if (!Restore(source, value)) ++failures; return; }
    if (value.failed && value.rejected == *selected) return;
    value.failed = false; value.rejected = {};
    const auto reject = [&]() noexcept { value.failed = true; value.rejected = *selected; ++failures; };
    if (value.owns && value.selection == *selected) {
        if (knife) {
            SubclassState target{};
            if (!ResolveSubclass(source, value, selectedItem->internal_name, target)) { reject(); return; }
            value.appliedSubclass = target; value.ownsSubclass = true; value.pending = true;
            if (!AssignOwnedSubclass(source, value, target, false)) { reject(); return; }
            value.pending = false;
        }
        return;
    }
    if (!value.owns) {
        value.original = current; value.originalModel = {}; value.appliedModel = {}; value.restoreModel = {};
        value.originalSubclass = {}; value.appliedSubclass = {}; value.ownsSubclass = false;
        if (knife && (!KnifeEntityValid(source, value) || !ReadSubclass(value.identity.entity, value.originalSubclass) ||
            !ValidDescriptor(value.originalSubclass))) { reject(); return; }
    }
    auto desired = Desired(value.original, current, *selected, knife);
    if (value.glove) { desired.paint = current.paint; desired.seed = current.seed; desired.wear = current.wear; desired.stat = current.stat; }
    Path selectedModel{};
    SubclassState beforeSubclass{};
    if (knife && !ReadSubclass(value.identity.entity, beforeSubclass)) { reject(); return; }
    if (knife && selectedItem->model_path && selectedItem->model_path[0]) {
        std::snprintf(selectedModel.data(), selectedModel.size(), "%s", selectedItem->model_path);
        if (!value.originalModel[0] && !ModelPath(value.identity.entity, value.originalModel)) { reject(); return; }
        if (!LoadModel(selectedModel)) { reject(); return; }
        // Blocking native loading may pump callbacks, stop the feature, replace
        // the entity, or change UI intent. Never continue with that old intent.
        std::uint8_t currentTeam = 0, currentLife = 1;
        int currentHealth = 0;
        if (!active.load() || stopped.load() || SkinGetSettings() != intent || !ItemValid(source, value) ||
            !Read(source.pawn.entity + ent::C_BaseEntity::m_iTeamNum, currentTeam) || currentTeam != team ||
            !Read(source.pawn.entity + ent::C_BaseEntity::m_iHealth, currentHealth) || currentHealth <= 0 ||
            !Read(source.pawn.entity + ent::C_BaseEntity::m_lifeState, currentLife) || currentLife != 0) return;
        ItemState rechecked{};
        if (!Snapshot(value.identity.entity, value.item, value.glove, rechecked) || rechecked != current) return;
        SubclassState recheckedSubclass{};
        if (!ReadSubclass(value.identity.entity, recheckedSubclass) || recheckedSubclass != beforeSubclass) return;
    }
    if (!active.load() || stopped.load() || SkinGetSettings() != intent) return;
    value.owns = true; value.pending = true;
    value.applied = desired;
    if (!WriteState(source, value, desired)) { reject(); return; }
    if (!active.load() || stopped.load() || SkinGetSettings() != intent) return;
    SubclassState target{};
    if (knife && !ResolveSubclass(source, value, selectedItem->internal_name, target)) { reject(); return; }
    if (selectedModel[0]) {
        value.appliedModel = selectedModel;
        if (!ItemValid(source, value)) { reject(); return; }
        const bool assigned = NativeModel(value.identity.entity, selectedModel);
        Path after{};
        const bool readable = ItemValid(source, value) && ModelPath(value.identity.entity, after);
        // Retain the actual result of this exact attempted assignment, including
        // an error model, for rollback. A later native model replacement is
        // independent and must not be overwritten by pending restoration.
        if (readable) value.appliedModel = after;
        if (!assigned || !readable || after != selectedModel) { reject(); return; }
        if (!ItemValid(source, value) || !NativeRefresh(value.identity.entity)) { reject(); return; }
    }
    if (knife) {
        value.appliedSubclass = target; value.ownsSubclass = true;
        if (!AssignOwnedSubclass(source, value, target, false)) { reject(); return; }
        if (!active.load() || stopped.load() || SkinGetSettings() != intent) return;
        Path after{};
        if (selectedModel[0] && (!ModelPath(value.identity.entity, after) || after != selectedModel)) { reject(); return; }
    }
    ItemState after{};
    if (!ItemValid(source, value) || !Snapshot(value.identity.entity, value.item, value.glove, after) || after != desired) { reject(); return; }
    value.pending = false; value.selection = *selected;
    ++assignments;
}
void Update() noexcept {
    Source source{}; std::uint8_t team = 0; bool alive = false;
    if (!ReadSource(source, team, alive)) {
        InvalidateAll();
        SkinSetRuntimeStatus(stopped.load() ? "Stopped and resident; local entities unavailable." : "Waiting for a local player.");
        return;
    }
    if (trackedSource != source) { InvalidateAll(); trackedSource = source; }
    const auto settings = SkinGetSettings();
    const auto& loadout = settings.teams[team == 2 ? 0 : 1];
    const bool enabled = active.load() && !stopped.load() && settings.enabled && alive;
    for (auto& value : owned) value.seen = false;
    std::uintptr_t services = 0, handles = 0;
    int count = 0;
    if (Read(source.pawn.entity + off::WeaponServices, services) && services &&
        Read(services + off::WeaponHandlesCount, count) && count >= 0 && count <= 64 &&
        Read(services + off::WeaponHandlesData, handles) && (!count || handles)) {
        for (int index = 0; index < count; ++index) {
            std::uint32_t handle = UINT32_MAX;
            Identity identity{};
            if (!Read(handles + static_cast<std::size_t>(index) * sizeof(handle), handle) || !Resolve(source.system, handle, identity)) continue;
            if (auto* value = Track(identity, identity.entity + off::Item, false)) UpdateItem(source, *value, loadout, settings, team, enabled);
        }
    }
    if (auto* glove = Track(source.pawn, source.pawn.entity + off::GlovesItem, true)) UpdateItem(source, *glove, loadout, settings, team, enabled);
    unsigned pending = 0, changed = 0, rejected = 0;
    for (auto& value : owned) {
        if (value.item && !value.seen) {
            // A detached/dropped weapon is no longer ours to customize. Restore
            // only while its exact identity still belongs to this local pawn.
            Restore(source, value);
            if (!value.owns || !ItemValid(source, value)) value = {};
        }
        pending += value.pending ? 1u : 0u;
        changed += value.owns ? 1u : 0u;
        rejected += value.failed ? 1u : 0u;
    }
    char status[256]{};
    if (pending) std::snprintf(status, sizeof(status), "Original item restoration pending on game thread (%u).", pending);
    else if (stopped.load()) std::snprintf(status, sizeof(status), "Stopped and resident; local item changes restored or superseded.");
    else if (rejected) std::snprintf(status, sizeof(status), "%u item override(s) failed; change selection or toggle to retry.", rejected);
    else if (!settings.enabled) std::snprintf(status, sizeof(status), "Skin changer disabled.");
    else if (!alive) std::snprintf(status, sizeof(status), "Waiting for a living local player.");
    else std::snprintf(status, sizeof(status), "%s loadout active; %u local item override(s).", team == 2 ? "T" : "CT", changed);
    SkinSetRuntimeStatus(status);
}
void OwnerFrame(void* owner, int stage) noexcept {
    std::uintptr_t table = 0;
    if (stage != model::ApplyStage || !CurrentClient() || !Read(reinterpret_cast<std::uintptr_t>(owner), table) || table != client + model::FrameVtable) return;
    AcquireSRWLockExclusive(&stateLock);
    if (active.load() || stopped.load()) { ++frames; ownerThread.store(GetCurrentThreadId()); Update(); }
    ReleaseSRWLockExclusive(&stateLock);
}
}

bool SkinChangerPrepare(SkinChangerLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t scopeGetter = 0, changedMethod = 0;
    if (!Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != ent::ClientImageSize || !Match(client + off::AttributeSet, off::AttributeSetBytes) ||
        !Match(client + off::AttributeRemove, off::AttributeRemoveBytes) || !Match(client + off::CompositeReset, off::CompositeResetBytes) ||
        !Match(client + off::WeaponRegenerate, off::WeaponRegenerateBytes) ||
        !Match(client + off::GloveMarkChanged, off::GloveMarkChangedBytes) ||
        !Match(client + anim::ItemSubclass, anim::ItemSubclassBytes) ||
        !Match(client + anim::SubclassChanged, anim::SubclassChangedBytes) ||
        !Match(client + anim::ScopeGetter, anim::ScopeGetterBytes) ||
        !Match(client + anim::WeaponVDataChanged, anim::WeaponVDataChangedBytes) ||
        !Read(client + anim::KnifeVtable + anim::ScopeGetterSlot, scopeGetter) || scopeGetter != client + anim::ScopeGetter ||
        !Read(client + anim::KnifeVtable + anim::VDataChangedSlot, changedMethod) || changedMethod != client + anim::WeaponVDataChanged ||
        !Match(client + model::SetModel, model::SetModelBytes)) {
        SkinSetRuntimeStatus("Native skin/item build verification failed."); return false;
    }
    setAttribute = reinterpret_cast<AttributeFn>(client + off::AttributeSet);
    removeAttribute = reinterpret_cast<RemoveFn>(client + off::AttributeRemove);
    resetComposite = reinterpret_cast<CompositeResetFn>(client + off::CompositeReset);
    regenerate = reinterpret_cast<RegenerateFn>(client + off::WeaponRegenerate);
    setModel = reinterpret_cast<ModelFn>(client + model::SetModel);
    itemSubclass = reinterpret_cast<ItemSubclassFn>(client + anim::ItemSubclass);
    subclassChanged = reinterpret_cast<SubclassChangedFn>(client + anim::SubclassChanged);
    if (!ClientFrameSubscribe(&OwnerFrame, logger)) { SkinSetRuntimeStatus("Skin changer frame registration failed."); return false; }
    prepared = true;
    SkinSetRuntimeStatus("Skin changer disabled.");
    if (logger) logger("SKIN_CHANGER_BUILD_VERIFIED native attributes/set/remove/refresh/model/knife-subclass; shared owner frame; independent T/CT loadouts");
    return true;
}
void SkinChangerActivate() noexcept {
    AcquireSRWLockExclusive(&stateLock);
    if (prepared && !stopped.load()) active.store(true);
    ReleaseSRWLockExclusive(&stateLock);
}
void SkinChangerStop(const char* reason) noexcept {
    active.store(false);
    stopped.store(true);
    if (!ClientFrameInCallback()) { AcquireSRWLockExclusive(&stateLock); ReleaseSRWLockExclusive(&stateLock); }
    SkinSetRuntimeStatus("Stopped; original item restoration queued on game thread. Code stays resident.");
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "SKIN_CHANGER_STOPPED_RESIDENT reason=%s; restore on owner frame; no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
void SkinChangerFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (lastReportTime && now - lastReportTime < 2000) return;
    lastReportTime = now;
    const auto count = frames.load();
    if (count == lastReportedFrames) return;
    lastReportedFrames = count;
    const auto status = SkinRuntimeStatus();
    char message[768]{};
    std::snprintf(message, sizeof(message), "SKIN_CHANGER frames=%llu assigned=%llu restored=%llu invalidated=%llu failed=%llu knife_animation_assigned=%llu knife_animation_restored=%llu owner_tid=%lu status=%s",
        count, assignments.load(), restorations.load(), invalidations.load(), failures.load(), animationAssignments.load(), animationRestores.load(), ownerThread.load(), status.data());
    logger(message);
}
#ifdef CS2_SKIN_CHANGER_CHECKS
void SkinChangerTestConfigure(std::uintptr_t base, void (*frame)(void*, int),
    SkinChangerAttributeFn attribute, SkinChangerRemoveFn remove, SkinChangerRefreshFn refreshFn,
    SkinChangerModelFn modelFn, SkinChangerLoadFn load, SkinChangerItemSubclassFn itemSubclassFn,
    SkinChangerSubclassChangedFn subclassChangedFn) noexcept {
    ClientFrameTestConfigure(base, frame); ClientFrameSubscribe(&OwnerFrame);
    client = base; setAttribute = attribute; removeAttribute = remove; refresh = refreshFn; setModel = modelFn;
    itemSubclass = itemSubclassFn; subclassChanged = subclassChangedFn;
    testRuntime = true; testLoad = load; prepared = true; active.store(false); stopped.store(false);
    owned = {}; trackedSource = {}; frames.store(0); assignments.store(0); restorations.store(0); invalidations.store(0); failures.store(0);
    animationAssignments.store(0); animationRestores.store(0);
    SkinResetSettings();
}
void SkinChangerTestFrame(void* owner, int stage) noexcept { ClientFrameTestDispatch(owner, stage); }
#endif
