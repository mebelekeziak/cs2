#include "movement_jumpbug.h"
#include "movement_jumpbug_offsets.h"
#include "movement_memory.h"
#include "cs2_offsets.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = movement_jumpbug_offsets;
namespace ent = cs2_offsets;
std::uintptr_t client = 0;
using FilterFn = void* (*)(void*, void*, std::uint64_t, int);
using ResultFn = void (*)(void*);
using TraceFn = void (*)(void*, void*, const void*, const void*, const void*, void*);
#ifdef CS2_JUMPBUG_CHECKS
std::uintptr_t testClient = 0;
FilterFn testFilter = nullptr;
ResultFn testResult = nullptr;
TraceFn testTrace = nullptr;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&want)[N]) noexcept {
    unsigned char got[N]{};
    return MovementCopy(at, got, N) && !std::memcmp(got, want, N);
}
bool Current(std::uintptr_t module) noexcept {
#ifdef CS2_JUMPBUG_CHECKS
    if (testClient) return module == testClient;
#endif
    return module && module == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Finite(const std::array<float, 3>& vector) noexcept {
    for (float value : vector) if (!std::isfinite(value)) return false;
    return true;
}
bool Cvar(std::ptrdiff_t wrapper, float& value) noexcept {
    std::uintptr_t pointer = 0;
    return Read(client + wrapper + movement_offsets::CvarValuePointer, pointer) && pointer &&
        Read(pointer + movement_offsets::CvarValue, value) && std::isfinite(value);
}
struct Snapshot {
    std::uintptr_t scene = 0, collision = 0, identity = 0;
    std::uint32_t serial = 0, flags = 0;
    std::uint64_t mask = 0;
    float gravityScale = 0;
    std::array<float, 3> origin{}, mins{}, maxs{}, velocity{};
};
bool Capture(std::uintptr_t pawn, std::uintptr_t services, Snapshot& value) noexcept {
    std::uintptr_t local = 0, back = 0, owner = 0;
    int health = 0;
    unsigned char moveType = 0;
    if (!pawn || !services || !Current(client) ||
        !Read(client + ent::globals::dwLocalPlayerPawn, local) || local != pawn ||
        !Read(pawn + movement_offsets::pawn::MovementServices, back) || back != services ||
        !Read(services + movement_offsets::services::Pawn, owner) || owner != pawn ||
        !Read(pawn + ent::C_BaseEntity::m_iHealth, health) || health <= 0 ||
        !Read(pawn + movement_offsets::pawn::ActualMoveType, moveType) || moveType != movement_offsets::Walk ||
        !Read(pawn + movement_offsets::pawn::Flags, value.flags) || (value.flags & movement_offsets::OnGround) ||
        !Read(pawn + ent::CEntityInstance::m_pEntity, value.identity) || !value.identity ||
        !Read(value.identity + ent::entity_layout::HandleOffset, value.serial) || value.serial == UINT32_MAX ||
        !Read(pawn + ent::C_BaseEntity::m_pGameSceneNode, value.scene) || !value.scene ||
        !Read(pawn + ent::C_BaseEntity::m_pCollision, value.collision) || !value.collision ||
        !Read(value.scene + ent::CGameSceneNode::m_vecAbsOrigin, value.origin) ||
        !Read(value.collision + ent::CCollisionProperty::m_vecMins, value.mins) ||
        !Read(value.collision + ent::CCollisionProperty::m_vecMaxs, value.maxs) ||
        !Read(pawn + movement_offsets::pawn::Velocity, value.velocity) || !Finite(value.velocity) ||
        !Read(pawn + off::ActualGravityScale, value.gravityScale) ||
        !Read(pawn + off::CollisionMask, value.mask) || !std::isfinite(value.gravityScale) || value.gravityScale < 0 ||
        !Finite(value.origin) || !Finite(value.mins) || !Finite(value.maxs)) return false;
    for (unsigned axis = 0; axis < 3; ++axis) if (value.mins[axis] >= value.maxs[axis]) return false;
    if (value.flags & 0x10) value.mask |= 0x20; // Native movement-mask getter.
    return true;
}
bool Same(const Snapshot& a, const Snapshot& b) noexcept {
    return a.scene == b.scene && a.collision == b.collision && a.identity == b.identity &&
        a.serial == b.serial && a.flags == b.flags && a.mask == b.mask && a.gravityScale == b.gravityScale &&
        a.origin == b.origin && a.mins == b.mins && a.maxs == b.maxs && a.velocity == b.velocity;
}
bool Trace(std::uintptr_t pawn, std::uintptr_t services, const Snapshot& source,
    const std::array<float, 3>& end, MovementJumpbugResult& output) noexcept {
    auto filterFn = reinterpret_cast<FilterFn>(client + off::InitializeFilter);
    auto resultFn = reinterpret_cast<ResultFn>(client + off::InitializeTrace);
    auto traceFn = reinterpret_cast<TraceFn>(client + off::TraceHull);
#ifdef CS2_JUMPBUG_CHECKS
    if (testClient) { filterFn = testFilter; resultFn = testResult; traceFn = testTrace; }
#endif
    if (!filterFn || !resultFn || !traceFn) return false;
    alignas(16) unsigned char result[off::TraceSize]{};
    alignas(8) unsigned char filter[off::FilterSize]{};
    const float bounds[6]{source.mins[0], source.mins[1], source.mins[2],
        source.maxs[0], source.maxs[1], source.maxs[2]};
    __try {
        resultFn(result);
        filterFn(filter, reinterpret_cast<void*>(pawn), source.mask, 11);
        traceFn(reinterpret_cast<void*>(services + off::TraceCache), result,
            source.origin.data(), end.data(), bounds, filter);
        if (result[off::TraceSolid]) return false;
        std::memcpy(&output.fraction, result + off::TraceFraction, sizeof(output.fraction));
        std::memcpy(output.normal.data(), result + off::TraceNormal, sizeof(output.normal));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool MovementJumpbugPrepare(std::uintptr_t module) noexcept {
    client = 0;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!Current(module) || !Read(module, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 || !Read(module + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != ent::ClientTimestamp || nt.OptionalHeader.SizeOfImage != ent::ClientImageSize ||
        !Match(module + off::InitializeFilter, off::InitializeFilterBytes) ||
        !Match(module + off::InitializeTrace, off::InitializeTraceBytes) ||
        !Match(module + off::TraceHull, off::TraceHullBytes)) return false;
    client = module;
    return true;
}

MovementJumpbugResult MovementProbeJumpbug(std::uintptr_t pawn, std::uintptr_t services,
    const std::array<float, 3>& velocity, bool holdingDuck) noexcept {
    MovementJumpbugResult output{};
    if (holdingDuck || !Finite(velocity) || velocity[2] > 0) return output;
    Snapshot before{}, after{};
    float gravity = 0, normal = 0, interval = 0;
    if (!Capture(pawn, services, before) || before.velocity != velocity || !Cvar(off::Gravity, gravity) || gravity < 0 ||
        !Cvar(off::StandableNormal, normal) || normal < 0 || normal > 1 ||
        !Read(client + movement_offsets::TickIntervalConstant, interval) ||
        !std::isfinite(interval) || interval <= 0 || interval > 0.1f) return output;
    auto adjusted = velocity;
    adjusted[2] -= before.gravityScale * gravity * interval * 0.5f;
    std::array<float, 3> end{};
    for (unsigned axis = 0; axis < 3; ++axis) end[axis] = before.origin[axis] + adjusted[axis] * interval;
    const auto unextendedEnd = end;
    end[2] -= 2.0f;
    if (!Finite(end) || !Trace(pawn, services, before, end, output)) return {};
    float gravityAfter = 0, normalAfter = 0, intervalAfter = 0;
    if (!Capture(pawn, services, after) || !Same(before, after) ||
        !Cvar(off::Gravity, gravityAfter) || gravity != gravityAfter ||
        !Cvar(off::StandableNormal, normalAfter) || normal != normalAfter ||
        !Read(client + movement_offsets::TickIntervalConstant, intervalAfter) || interval != intervalAfter ||
        !std::isfinite(output.fraction) || output.fraction < 0 || output.fraction > 1 || !Finite(output.normal)) return {};
    output.valid = true;
    // Steep surfaces remain native movement; only near-flat landing schedules assistance.
    if (!(output.fraction > 0 && output.fraction < 1) ||
        output.normal[2] < normal || output.normal[2] < 0.985f) return output;
    // The extension only finds a nearby plane. Its trace fraction is not a
    // fraction of command time: project that plane onto the unextended segment.
    // This also rejects surfaces reachable only by the extra two-unit probe.
    double probeDot = 0, commandDot = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        probeDot += static_cast<double>(output.normal[axis]) *
            (static_cast<double>(end[axis]) - before.origin[axis]);
        commandDot += static_cast<double>(output.normal[axis]) *
            (static_cast<double>(unextendedEnd[axis]) - before.origin[axis]);
    }
    if (!std::isfinite(probeDot) || !std::isfinite(commandDot) || probeDot >= 0 || commandDot >= 0)
        return output;
    const double when = static_cast<double>(output.fraction) * probeDot / commandDot;
    if (!std::isfinite(when) || when <= 0 || when >= 1) return output;
    const float commandFraction = static_cast<float>(when);
    if (!(commandFraction > 0 && commandFraction < 1)) return output;
    output.fraction = commandFraction;
    output.landing = true;
    return output;
}

#ifdef CS2_JUMPBUG_CHECKS
void MovementJumpbugTestConfigure(std::uintptr_t module, JumpbugFilterFn filter,
    JumpbugResultFn result, JumpbugTraceFn trace) noexcept {
    client = 0; testClient = module; testFilter = filter; testResult = result; testTrace = trace;
}
#endif
