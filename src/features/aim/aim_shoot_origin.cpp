#include "aim_shoot_origin.h"
#include "aim_shoot_origin_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = aim_shoot_origin_offsets;
namespace ent = cs2_offsets;
#ifdef CS2_AIM_SHOOT_ORIGIN_CHECKS
bool testRuntime = false;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&bytes)[N]) noexcept {
    unsigned char copy[N]{};
    return MovementCopy(at, copy, N) && std::memcmp(copy, bytes, N) == 0;
}
bool Current(const AimShootOriginContext& context) noexcept {
#ifdef CS2_AIM_SHOOT_ORIGIN_CHECKS
    if (testRuntime) return context.client != 0;
#endif
    return context.client && context.client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Source(const AimShootOriginContext& context, const AimSource& source, AimShootOriginSnapshot& out) noexcept {
    std::uintptr_t pawn = 0, controller = 0, system = 0, table = 0, target = 0, owner = 0;
    std::uintptr_t controllerIdentity = 0;
    std::uint32_t linked = UINT32_MAX, reverse = UINT32_MAX;
    int health = 0;
    std::uint8_t life = 1, dormant = 1;
    return Current(context) && source.pawn && source.controller && source.system &&
        Read(context.client + ent::globals::dwLocalPlayerPawn, pawn) && pawn == source.pawn &&
        Read(context.client + ent::globals::dwLocalPlayerController, controller) && controller == source.controller &&
        Read(context.client + ent::globals::dwGameEntitySystem, system) && system == source.system &&
        Read(pawn + ent::CEntityInstance::m_pEntity, out.identity) && out.identity &&
        Read(out.identity, target) && target == pawn &&
        Read(out.identity + ent::entity_layout::HandleOffset, out.handle) && out.handle != UINT32_MAX &&
        Read(controller + ent::CEntityInstance::m_pEntity, controllerIdentity) && controllerIdentity &&
        Read(controllerIdentity, target) && target == controller &&
        Read(controllerIdentity + ent::entity_layout::HandleOffset, out.controllerHandle) && out.controllerHandle != UINT32_MAX &&
        Read(controller + ent::CCSPlayerController::m_hPlayerPawn, linked) && linked == out.handle &&
        Read(pawn + ent::C_BasePlayerPawn::m_hController, reverse) && reverse == out.controllerHandle &&
        Read(pawn + ent::C_BaseEntity::m_iHealth, health) && health > 0 &&
        Read(pawn + ent::C_BaseEntity::m_lifeState, life) && life == 0 &&
        Read(pawn + ent::C_BaseEntity::m_pGameSceneNode, out.scene) && out.scene &&
        Read(out.scene + ent::CGameSceneNode::m_bDormant, dormant) && dormant == 0 &&
        Read(pawn + off::CameraServices, out.camera) && Read(pawn + off::ObserverServices, out.observer) &&
        Read(pawn, table) && table == context.client + off::PawnVtable &&
        Read(table + off::PawnGetterSlot, target) && target == context.client + off::PawnGetter &&
        Read(table + off::ViewOffsetSlot, target) && target == context.client + off::ViewOffsetGetter &&
        Read(pawn + off::WeaponServices, out.services) && out.services &&
        Read(out.services, table) && table == context.client + off::ServicesVtable &&
        Read(table + off::GetterSlot, target) && target == context.client + off::Getter &&
        Read(out.services + off::ServicePawn, owner) && owner == pawn;
}
bool SameSource(const AimShootOriginSnapshot& a, const AimShootOriginSnapshot& b) noexcept {
    return a.services == b.services && a.identity == b.identity && a.scene == b.scene &&
        a.handle == b.handle && a.controllerHandle == b.controllerHandle &&
        a.camera == b.camera && a.observer == b.observer;
}
bool Position(const AimVector& position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
        std::fabs(position.x) <= 1000000 && std::fabs(position.y) <= 1000000 && std::fabs(position.z) <= 1000000;
}
bool Query(const AimShootOriginContext& context, std::uintptr_t services, AimVector& position) noexcept {
    if (!context.getter) return false;
    // Poison all components: partial writes and a returned foreign pointer fail.
    position = {NAN, NAN, NAN};
    __try {
        return context.getter(reinterpret_cast<void*>(services), &position) == &position && Position(position);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool AimShootOriginPrepare(std::uintptr_t client, AimShootOriginContext& context) noexcept {
    context = {};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t getter = 0, pawnGetter = 0, offsetGetter = 0;
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != ent::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != ent::ClientImageSize ||
        !Match(client + off::Getter, off::GetterBytes) || !Match(client + off::PawnGetter, off::PawnGetterBytes) ||
        !Match(client + off::ViewOffsetGetter, off::ViewOffsetGetterBytes) || !Match(client + off::BaseEye, off::BaseEyeBytes) ||
        !Read(client + off::ServicesVtable + off::GetterSlot, getter) || getter != client + off::Getter ||
        !Read(client + off::PawnVtable + off::PawnGetterSlot, pawnGetter) || pawnGetter != client + off::PawnGetter ||
        !Read(client + off::PawnVtable + off::ViewOffsetSlot, offsetGetter) || offsetGetter != client + off::ViewOffsetGetter) return false;
    context = {client, reinterpret_cast<AimShootOriginFn>(getter)};
    return true;
}

bool AimShootOriginRead(const AimShootOriginContext& context, const AimSource& source,
    AimShootOriginSnapshot& out) noexcept {
    out = {};
    AimShootOriginSnapshot before{}, after{};
    if (!Source(context, source, before)) { out.reason = "shooting-origin source unsupported or stale"; return false; }
    if (!Query(context, before.services, before.position)) { out.reason = "native shooting-origin query failed"; return false; }
    if (!Source(context, source, after) || !SameSource(before, after)) {
        out.reason = "shooting-origin source changed during query"; return false;
    }
    before.valid = true;
    before.reason = "current native shooting origin";
    out = before;
    return true;
}

bool AimShootOriginStillCurrent(const AimShootOriginContext& context, const AimSource& source,
    const AimShootOriginSnapshot& snapshot) noexcept {
    AimShootOriginSnapshot current{};
    return snapshot.valid && AimShootOriginRead(context, source, current) && SameSource(snapshot, current) &&
        snapshot.position.x == current.position.x && snapshot.position.y == current.position.y && snapshot.position.z == current.position.z;
}
#ifdef CS2_AIM_SHOOT_ORIGIN_CHECKS
void AimShootOriginTestRuntime(bool enabled) noexcept { testRuntime = enabled; }
#endif
