#include "movement_jumpbug.h"
#include "movement_jumpbug_offsets.h"
#include "movement_memory.h"
#include "cs2_offsets.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
namespace off = movement_jumpbug_offsets;
namespace ent = cs2_offsets;
std::uintptr_t client = 0;
using FilterFn = void* (*)(void*, void*, std::uint64_t, int);
using ResultFn = void (*)(void*);
using TraceFn = void (*)(void*, void*, const void*, const void*, const void*, void*);
using VectorsFn = const void* (*)();
#ifdef CS2_JUMPBUG_CHECKS
std::uintptr_t testClient = 0;
FilterFn testFilter = nullptr;
ResultFn testResult = nullptr;
TraceFn testTrace = nullptr;
VectorsFn testVectors = nullptr;
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
using Vector = std::array<float, 3>;
double Dot(const Vector& a, const Vector& b) noexcept {
    double value = 0;
    for (unsigned axis = 0; axis < 3; ++axis) value += static_cast<double>(a[axis]) * b[axis];
    return value;
}
struct Hulls {
    std::uintptr_t identity = 0;
    Vector mins{}, maxs{}, duckMins{}, duckMaxs{};
};
bool CaptureHulls(Hulls& value) noexcept {
    auto vectors = reinterpret_cast<VectorsFn>(client + off::HullVectors);
#ifdef CS2_JUMPBUG_CHECKS
    if (testClient) vectors = testVectors;
#endif
    if (!vectors) return false;
    __try { value.identity = reinterpret_cast<std::uintptr_t>(vectors()); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!value.identity || !Read(value.identity + off::StandingMins, value.mins) ||
        !Read(value.identity + off::StandingMaxs, value.maxs) ||
        !Read(value.identity + off::CrouchedMins, value.duckMins) ||
        !Read(value.identity + off::CrouchedMaxs, value.duckMaxs) ||
        !Finite(value.mins) || !Finite(value.maxs) || !Finite(value.duckMins) || !Finite(value.duckMaxs)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (value.mins[axis] >= value.maxs[axis] || value.duckMins[axis] >= value.duckMaxs[axis]) return false;
    return true;
}
bool Same(const Hulls& a, const Hulls& b) noexcept {
    return a.identity == b.identity && a.mins == b.mins && a.maxs == b.maxs && a.duckMins == b.duckMins && a.duckMaxs == b.duckMaxs;
}
struct Rules {
    float gravity = 0, normal = 0, interval = 0, duckFactor = 0, unduckFactor = 0, drop = 0, lift = 0, snap = 0;
};
bool CaptureRules(Rules& value) noexcept {
    return Cvar(off::Gravity, value.gravity) && value.gravity >= 0 &&
        Cvar(off::StandableNormal, value.normal) && value.normal >= 0 && value.normal <= 1 &&
        Read(client + movement_offsets::TickIntervalConstant, value.interval) &&
        std::isfinite(value.interval) && value.interval > 0 && value.interval <= 0.1f &&
        Read(client + off::DuckOriginFactor, value.duckFactor) && value.duckFactor == 0.5f &&
        Read(client + off::UnduckOriginFactor, value.unduckFactor) && value.unduckFactor == -0.5f &&
        Read(client + off::GroundTraceDropConstant, value.drop) && value.drop == -2.0f &&
        Read(client + off::GroundTraceLiftConstant, value.lift) && value.lift == 4.0f &&
        Read(client + off::SubtickTimeSnapConstant, value.snap) && value.snap == 131072.0f;
}
bool Same(const Rules& a, const Rules& b) noexcept {
    return a.gravity == b.gravity && a.normal == b.normal && a.interval == b.interval &&
        a.duckFactor == b.duckFactor && a.unduckFactor == b.unduckFactor && a.drop == b.drop && a.lift == b.lift && a.snap == b.snap;
}
struct Snapshot {
    std::uintptr_t scene = 0, collision = 0, identity = 0;
    std::uint32_t serial = 0, flags = 0;
    std::uint64_t mask = 0;
    float gravityScale = 0;
    float duckAmount = 0;
    unsigned char ducked = 0, ducking = 0, duckOverride = 0;
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
        !Read(pawn + movement_offsets::pawn::Flags, value.flags) || (value.flags & (movement_offsets::OnGround | 2)) ||
        !Read(pawn + ent::CEntityInstance::m_pEntity, value.identity) || !value.identity ||
        !Read(value.identity + ent::entity_layout::HandleOffset, value.serial) || value.serial == UINT32_MAX ||
        !Read(pawn + ent::C_BaseEntity::m_pGameSceneNode, value.scene) || !value.scene ||
        !Read(pawn + ent::C_BaseEntity::m_pCollision, value.collision) || !value.collision ||
        !Read(value.scene + ent::CGameSceneNode::m_vecAbsOrigin, value.origin) ||
        !Read(value.collision + ent::CCollisionProperty::m_vecMins, value.mins) ||
        !Read(value.collision + ent::CCollisionProperty::m_vecMaxs, value.maxs) ||
        !Read(pawn + movement_offsets::pawn::Velocity, value.velocity) || !Finite(value.velocity) ||
        !Read(services + off::Ducked, value.ducked) || value.ducked ||
        !Read(services + off::Ducking, value.ducking) || value.ducking ||
        !Read(services + off::DuckOverride, value.duckOverride) || value.duckOverride ||
        !Read(services + off::DuckAmount, value.duckAmount) || value.duckAmount != 0 ||
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
        a.ducked == b.ducked && a.ducking == b.ducking && a.duckOverride == b.duckOverride && a.duckAmount == b.duckAmount &&
        a.origin == b.origin && a.mins == b.mins && a.maxs == b.maxs && a.velocity == b.velocity;
}
struct TraceResult { float fraction = 0; Vector normal{}; bool solid = false; };
bool Trace(std::uintptr_t pawn, std::uintptr_t services, std::uint64_t mask,
    const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs, TraceResult& output) noexcept {
    auto filterFn = reinterpret_cast<FilterFn>(client + off::InitializeFilter);
    auto resultFn = reinterpret_cast<ResultFn>(client + off::InitializeTrace);
    auto traceFn = reinterpret_cast<TraceFn>(client + off::TraceHull);
#ifdef CS2_JUMPBUG_CHECKS
    if (testClient) { filterFn = testFilter; resultFn = testResult; traceFn = testTrace; }
#endif
    if (!filterFn || !resultFn || !traceFn || !Finite(start) || !Finite(end)) return false;
    alignas(16) unsigned char result[off::TraceSize]{};
    alignas(8) unsigned char filter[off::FilterSize]{};
    const float bounds[6]{mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]};
    __try {
        resultFn(result);
        filterFn(filter, reinterpret_cast<void*>(pawn), mask, 11);
        traceFn(reinterpret_cast<void*>(services + off::TraceCache), result,
            start.data(), end.data(), bounds, filter);
        output.solid = result[off::TraceSolid] != 0;
        std::memcpy(&output.fraction, result + off::TraceFraction, sizeof(output.fraction));
        std::memcpy(output.normal.data(), result + off::TraceNormal, sizeof(output.normal));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::isfinite(output.fraction) && output.fraction >= 0 && output.fraction <= 1 && Finite(output.normal);
}
bool Standable(const TraceResult& trace, float normal) noexcept {
    return !trace.solid && trace.fraction > 0 && trace.fraction < 1 &&
        trace.normal[2] >= std::max(normal, 0.985f) && std::fabs(Dot(trace.normal, trace.normal) - 1) < 0.01;
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
        !Match(module + off::TraceHull, off::TraceHullBytes) ||
        !Match(module + off::HullVectors, off::HullVectorsBytes)) return false;
    client = module;
    return true;
}

MovementJumpbugResult MovementProbeJumpbug(std::uintptr_t pawn, std::uintptr_t services,
    const std::array<float, 3>& velocity, bool holdingDuck) noexcept {
    MovementJumpbugResult output{};
    if (holdingDuck || !Finite(velocity) || velocity[2] > 0) return output;
    Snapshot before{}, after{};
    Hulls hulls{}, hullsAfter{};
    Rules rules{}, rulesAfter{};
    if (!Capture(pawn, services, before) || before.velocity != velocity || !CaptureRules(rules) || !CaptureHulls(hulls) ||
        before.mins != hulls.mins || before.maxs != hulls.maxs) return output;
    Vector delta{}, duckStart{}, fullEnd{}, displacement{};
    const double gravity = static_cast<double>(rules.gravity) * before.gravityScale;
    for (unsigned axis = 0; axis < 3; ++axis) {
        delta[axis] = ((hulls.maxs[axis] - hulls.mins[axis]) -
            (hulls.duckMaxs[axis] - hulls.duckMins[axis])) * rules.duckFactor;
        duckStart[axis] = before.origin[axis] + delta[axis];
        displacement[axis] = velocity[axis] * rules.interval;
    }
    // Restrict the model to the ordinary vertical posture change. Custom
    // lateral hull changes and expanding crouch hulls retain native input.
    if (!Finite(delta) || delta[0] != 0 || delta[1] != 0 || delta[2] <= -rules.drop ||
        hulls.duckMins != hulls.mins || hulls.duckMaxs[0] != hulls.maxs[0] || hulls.duckMaxs[1] != hulls.maxs[1]) return output;
    displacement[2] -= static_cast<float>(0.5 * gravity * rules.interval * rules.interval);
    for (unsigned axis = 0; axis < 3; ++axis) fullEnd[axis] = before.origin[axis] + displacement[axis];
    fullEnd[2] += rules.drop;
    TraceResult plane{};
    if (!Trace(pawn, services, before.mask, before.origin, fullEnd, hulls.mins, hulls.maxs, plane) || plane.solid) return output;
    output.valid = true; output.origin = before.origin; output.mins = before.mins; output.maxs = before.maxs;
    output.normal = plane.normal; output.crouchedMins = hulls.duckMins; output.crouchedMaxs = hulls.duckMaxs;
    output.duckOriginDelta = delta;
    if (Standable(plane, rules.normal)) {
        Vector probeDisplacement = displacement;
        probeDisplacement[2] += rules.drop;
        const double approaching = Dot(plane.normal, velocity);
        const double gap = -static_cast<double>(plane.fraction) * Dot(plane.normal, probeDisplacement);
        const double depth = -static_cast<double>(rules.drop) * plane.normal[2];
        // Ground categorization accepts the next two units below the standing
        // hull. Select an interior grid point, rather than the exact contact
        // boundary. The grid survives the native 131072 add/subtract rounding.
        constexpr unsigned eventDivisions = 64;
        const double margin = 0.25 * plane.normal[2]; // Assistance tolerance.
        double bestScore = -1;
        if (std::isfinite(gap) && gap > 0 && approaching < 0) {
            for (unsigned index = 1; index < eventDivisions; ++index) {
                const float fraction = static_cast<float>(index) / eventDivisions;
                const double time = static_cast<double>(fraction) * rules.interval;
                const double candidateGap = gap + approaching * time - 0.5 * gravity * plane.normal[2] * time * time;
                if (candidateGap < margin || candidateGap > depth - margin) continue;
                if (output.captureMinFraction == 0) output.captureMinFraction = fraction;
                output.captureMaxFraction = fraction;
                const double score = std::min(candidateGap, depth - candidateGap);
                if (score > bestScore) {
                    bestScore = score; output.fraction = fraction;
                    output.clearance = static_cast<float>(candidateGap / plane.normal[2]);
                }
            }
        }
        if (bestScore >= 0) {
            const double time = static_cast<double>(output.fraction) * rules.interval;
            Vector duckEnd{}, groundStart{}, groundEnd{}, groundMaxs = hulls.maxs;
            for (unsigned axis = 0; axis < 3; ++axis)
                output.eventOrigin[axis] = before.origin[axis] + static_cast<float>(velocity[axis] * time);
            output.eventOrigin[2] -= static_cast<float>(0.5 * gravity * time * time);
            for (unsigned axis = 0; axis < 3; ++axis) {
                duckEnd[axis] = output.eventOrigin[axis] + delta[axis];
                // Opposite native posture factor restores the standing origin.
                output.eventOrigin[axis] = duckEnd[axis] + delta[axis] * rules.unduckFactor / rules.duckFactor;
            }
            groundStart = groundEnd = output.eventOrigin;
            groundStart[2] += rules.lift; groundEnd[2] += rules.drop; groundMaxs[2] -= rules.lift;
            TraceResult crouched{}, unduck{}, ground{};
            if (groundMaxs[2] <= hulls.mins[2]) return {};
            if (!Trace(pawn, services, before.mask, duckStart, duckEnd, hulls.duckMins, hulls.duckMaxs, crouched)) return {};
            if (!crouched.solid && crouched.fraction == 1) {
                // CanUnduck tests full standing bounds along the entire lowered
                // origin path. A final point-only occupancy test misses ceilings.
                if (!Trace(pawn, services, before.mask, duckEnd, output.eventOrigin, hulls.mins, hulls.maxs, unduck)) return {};
                if (!unduck.solid && unduck.fraction == 1) {
                    if (!Trace(pawn, services, before.mask, groundStart, groundEnd, hulls.mins, groundMaxs, ground)) return {};
                    if (Standable(ground, rules.normal) && Dot(ground.normal, plane.normal) > 0.999) {
                        const double actualGap = (ground.fraction * (rules.lift - rules.drop) - rules.lift) * ground.normal[2];
                        const double tolerance = 0.125 * ground.normal[2];
                        output.landing = actualGap >= margin && actualGap <= depth - margin &&
                            std::fabs(actualGap - output.clearance * plane.normal[2]) <= tolerance;
                    }
                }
            }
        }
    }
    // Nothing escapes the synchronous probes. Any source/provider change
    // invalidates the whole plan, including otherwise unsuccessful probes.
    if (!Capture(pawn, services, after) || !Same(before, after) || !CaptureRules(rulesAfter) || !Same(rules, rulesAfter) ||
        !CaptureHulls(hullsAfter) || !Same(hulls, hullsAfter)) return {};
    return output;
}

#ifdef CS2_JUMPBUG_CHECKS
void MovementJumpbugTestConfigure(std::uintptr_t module, JumpbugFilterFn filter,
    JumpbugResultFn result, JumpbugTraceFn trace, JumpbugVectorsFn vectors) noexcept {
    client = 0; testClient = module; testFilter = filter; testResult = result; testTrace = trace; testVectors = vectors;
}
#endif
