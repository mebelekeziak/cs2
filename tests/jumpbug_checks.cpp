#include "movement_jumpbug.h"
#include "movement_jumpbug_offsets.h"
#include "movement_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = movement_jumpbug_offsets;
using Vector = std::array<float, 3>;
unsigned checks = 0, failures = 0, calls = 0, providerCalls = 0;
void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
template<class T> void Put(void* at, std::size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(at) + offset, &value, sizeof(value)); }
template<class T> T Get(const void* at, std::size_t offset = 0) { T value{}; std::memcpy(&value, static_cast<const unsigned char*>(at) + offset, sizeof(value)); return value; }
unsigned char* memory = nullptr;
std::array<unsigned char, 0x2000> pawn{}, services{};
std::array<unsigned char, 0x200> node{}, collision{}, identity{}, gravity{}, standable{}, vectors{};
Vector normal{0, 0, 1}, velocity{100, 0, -100};
double planeDistance = 0;
float ceiling = 10000, overriddenFraction = -1;
int obstructPhase = -1, faultPhase = -1, changePhase = -1;
bool changeHulls = false, changeRules = false, badNormal = false, providerFault = false, groundMissing = false;
std::uint64_t expectedMask = 0x103;
struct RecordedTrace { Vector start{}, end{}; std::array<float, 6> bounds{}; } recorded[4];
double Dot(const Vector& a, const Vector& b) {
    double result = 0;
    for (unsigned axis = 0; axis < 3; ++axis) result += static_cast<double>(a[axis]) * b[axis];
    return result;
}
double Support(const std::array<float, 6>& bounds) {
    double result = 0;
    for (unsigned axis = 0; axis < 3; ++axis) result += normal[axis] * bounds[normal[axis] >= 0 ? axis : axis + 3];
    return result;
}
void PlaneForGap(float gap) {
    const auto origin = Get<Vector>(node.data(), cs2_offsets::CGameSceneNode::m_vecAbsOrigin);
    const auto mins = Get<Vector>(collision.data(), cs2_offsets::CCollisionProperty::m_vecMins);
    const auto maxs = Get<Vector>(collision.data(), cs2_offsets::CCollisionProperty::m_vecMaxs);
    const std::array<float, 6> bounds{mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]};
    planeDistance = Dot(normal, origin) + Support(bounds) - gap * normal[2];
}
const void* Vectors() {
    ++providerCalls;
    if (providerFault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return vectors.data();
}
void* Filter(void* result, void* skip, std::uint64_t mask, int layer) {
    Check(skip == pawn.data() && mask == expectedMask && layer == 11, "native movement filter mask/group");
    Check(reinterpret_cast<std::uintptr_t>(result) % 8 == 0, "filter aligned");
    return result;
}
void Result(void* result) { std::memset(result, 0, off::TraceSize); Put(result, off::TraceFraction, 1.0f); }
void Trace(void* cache, void* result, const void* start, const void* end, const void* bounds, void*) {
    const auto phase = calls++;
    Check(cache == services.data() + off::TraceCache, "native bbox cache receiver");
    Check(reinterpret_cast<std::uintptr_t>(result) % 16 == 0, "trace aligned");
    Check(Get<float>(result, off::TraceFraction) == 1, "trace initialized");
    const auto from = Get<Vector>(start), to = Get<Vector>(end);
    const auto hull = Get<std::array<float, 6>>(bounds);
    if (phase < 4) recorded[phase] = {from, to, hull};
    // Independent swept-box versus infinite-plane fixture. It has no planner
    // timing equations and does not synthesize a success at an expected time.
    const double fromGap = Dot(normal, from) + Support(hull) - planeDistance;
    const double toGap = Dot(normal, to) + Support(hull) - planeDistance;
    float fraction = 1;
    bool solid = fromGap < 0;
    Vector hitNormal{};
    if (toGap < 0 && fromGap >= 0 && !(groundMissing && phase == 3)) {
        fraction = static_cast<float>(fromGap / (fromGap - toGap)); hitNormal = normal;
    }
    if (from[2] + hull[5] > ceiling) solid = true;
    if (to[2] + hull[5] > ceiling && to[2] > from[2]) fraction = std::min(fraction,
        (ceiling - from[2] - hull[5]) / (to[2] - from[2]));
    if (static_cast<int>(phase) == obstructPhase) fraction = 0.5f;
    if (phase == 0 && overriddenFraction != -1) fraction = overriddenFraction;
    if (badNormal && phase == 0) hitNormal = {0, 0, 2};
    Put(result, off::TraceFraction, fraction); Put(result, off::TraceNormal, hitNormal);
    Put(result, off::TraceSolid, static_cast<unsigned char>(solid));
    if (static_cast<int>(phase) == changePhase)
        Put(node.data(), cs2_offsets::CGameSceneNode::m_vecAbsOrigin, Vector{11, 20, 30});
    if (changeHulls) Put(vectors.data(), off::CrouchedMaxs, Vector{16, 16, 55});
    if (changeRules) Put(memory, off::GroundTraceDropConstant, -3.0f);
    if (static_cast<int>(phase) == faultPhase) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
}
void Reset(float gap = 1.5f) {
    calls = providerCalls = 0; normal = {0, 0, 1}; velocity = {100, 0, -100};
    ceiling = 10000; overriddenFraction = -1; obstructPhase = faultPhase = changePhase = -1;
    changeHulls = changeRules = badNormal = providerFault = groundMissing = false;
    expectedMask = 0x103;
    Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, reinterpret_cast<std::uintptr_t>(pawn.data()));
    Put(pawn.data(), movement_offsets::pawn::MovementServices, reinterpret_cast<std::uintptr_t>(services.data()));
    Put(services.data(), movement_offsets::services::Pawn, reinterpret_cast<std::uintptr_t>(pawn.data()));
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), movement_offsets::pawn::ActualMoveType, static_cast<unsigned char>(2));
    Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{0});
    Put(pawn.data(), movement_offsets::pawn::Velocity, velocity);
    Put(pawn.data(), cs2_offsets::CEntityInstance::m_pEntity, reinterpret_cast<std::uintptr_t>(identity.data()));
    Put(identity.data(), cs2_offsets::entity_layout::HandleOffset, std::uint32_t{7});
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_pGameSceneNode, reinterpret_cast<std::uintptr_t>(node.data()));
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_pCollision, reinterpret_cast<std::uintptr_t>(collision.data()));
    Put(node.data(), cs2_offsets::CGameSceneNode::m_vecAbsOrigin, Vector{10, 20, 30});
    Put(collision.data(), cs2_offsets::CCollisionProperty::m_vecMins, Vector{-16, -16, 0});
    Put(collision.data(), cs2_offsets::CCollisionProperty::m_vecMaxs, Vector{16, 16, 72});
    Put(vectors.data(), off::StandingMins, Vector{-16, -16, 0}); Put(vectors.data(), off::StandingMaxs, Vector{16, 16, 72});
    Put(vectors.data(), off::CrouchedMins, Vector{-16, -16, 0}); Put(vectors.data(), off::CrouchedMaxs, Vector{16, 16, 54});
    Put(services.data(), off::Ducked, static_cast<unsigned char>(0)); Put(services.data(), off::Ducking, static_cast<unsigned char>(0));
    Put(services.data(), off::DuckOverride, static_cast<unsigned char>(0)); Put(services.data(), off::DuckAmount, 0.0f);
    Put(pawn.data(), off::ActualGravityScale, 1.0f); Put(pawn.data(), off::CollisionMask, std::uint64_t{0x103});
    Put(memory, off::Gravity + 8, reinterpret_cast<std::uintptr_t>(gravity.data())); Put(gravity.data(), 0x58, 800.0f);
    Put(memory, off::StandableNormal + 8, reinterpret_cast<std::uintptr_t>(standable.data())); Put(standable.data(), 0x58, 0.7f);
    Put(memory, movement_offsets::TickIntervalConstant, 1.0f / 64.0f);
    Put(memory, off::DuckOriginFactor, 0.5f); Put(memory, off::UnduckOriginFactor, -0.5f);
    Put(memory, off::GroundTraceDropConstant, -2.0f); Put(memory, off::GroundTraceLiftConstant, 4.0f);
    Put(memory, off::SubtickTimeSnapConstant, 131072.0f); PlaneForGap(gap);
}
MovementJumpbugResult Probe(bool duck = false) {
    return MovementProbeJumpbug(reinterpret_cast<std::uintptr_t>(pawn.data()), reinterpret_cast<std::uintptr_t>(services.data()), velocity, duck);
}
}
int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, cs2_offsets::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    const auto module = reinterpret_cast<std::uintptr_t>(memory);
    MovementJumpbugTestConfigure(module, Filter, Result, Trace, Vectors);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE; nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp = cs2_offsets::ClientTimestamp; nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = cs2_offsets::ClientImageSize;
    Put(memory, 0, dos); Put(memory, 0x100, nt);
    std::memcpy(memory + off::InitializeFilter, off::InitializeFilterBytes, sizeof(off::InitializeFilterBytes));
    std::memcpy(memory + off::InitializeTrace, off::InitializeTraceBytes, sizeof(off::InitializeTraceBytes));
    std::memcpy(memory + off::TraceHull, off::TraceHullBytes, sizeof(off::TraceHullBytes));
    std::memcpy(memory + off::HullVectors, off::HullVectorsBytes, sizeof(off::HullVectorsBytes));
    Check(MovementJumpbugPrepare(module), "matching native build prepares");
    for (auto at : {off::InitializeFilter, off::InitializeTrace, off::TraceHull, off::HullVectors}) {
        memory[at] ^= 1; Check(!MovementJumpbugPrepare(module), "changed native entry rejects"); memory[at] ^= 1;
    }
    Check(MovementJumpbugPrepare(module), "reprepare"); Reset();
    auto result = Probe();
    Check(result.valid && result.landing && calls == 4 && providerCalls == 2, "posture plan requires all synchronous native probes and provider recheck");
    Check(result.fraction * 64 == std::floor(result.fraction * 64) && result.fraction > 0 && result.fraction < 1,
        "event survives native one-sixty-fourth tick snapping");
    volatile float rounded = result.fraction + 131072.0f;
    rounded = rounded - 131072.0f;
    Check(rounded == result.fraction, "native add/subtract float32 conversion preserves selected event");
    Check(std::fabs(result.clearance - 1) < 0.02f && result.captureMinFraction <= result.fraction && result.fraction <= result.captureMaxFraction,
        "event maximizes clearance from penetration and ground capture limit");
    Check(recorded[1].start == Vector{10, 20, 39} && recorded[1].bounds == std::array<float, 6>{-16, -16, 0, 16, 16, 54},
        "airborne duck raises origin by half copied hull size difference");
    Check(recorded[2].start == recorded[1].end && recorded[2].end == result.eventOrigin &&
        recorded[2].bounds == std::array<float, 6>{-16, -16, 0, 16, 16, 72}, "native unduck sweeps full standing hull along lowered origin path");
    Check(std::fabs(recorded[2].start[2] - recorded[2].end[2] - 9) < 1e-6 &&
        std::fabs(recorded[3].start[2] - recorded[3].end[2] - 6) < 1e-6 && recorded[3].bounds[5] == 68,
        "native standing ground probe lifts four, drops two and shortens upper hull four");
    Check(result.eventOrigin[2] > 28.5f && result.eventOrigin[2] < 30.5f && result.eventOrigin[2] > recorded[0].end[2],
        "selected standing event precedes floor penetration");
    Reset(); Put(vectors.data(), off::CrouchedMaxs, Vector{16, 16, 50});
    result = Probe(); Check(result.landing && result.duckOriginDelta[2] == 11 && recorded[1].start[2] == 41,
        "posture geometry follows native provider instead of hardcoded heights");
    Reset(); normal = {0.1f, 0, std::sqrt(0.99f)}; PlaneForGap(1.5f); result = Probe();
    Check(result.landing && std::fabs(result.clearance - 1) < 0.02f, "oblique near-flat plane uses full support and trajectory dot product");
    Reset(3.0f); result = Probe(); Check(result.landing, "capture window may open before actual contact in next command");
    Reset(5.0f); result = Probe(); Check(result.valid && !result.landing, "unreachable capture window preserves native movement");
    Reset(2.6f); velocity = {100, 0, -10000}; Put(pawn.data(), movement_offsets::pawn::Velocity, velocity);
    result = Probe(); Check(result.valid && !result.landing, "sub-grid capture window rejects instead of rounding into penetration");
    Reset(1.5f); ceiling = 108.0f; result = Probe();
    Check(result.valid && !result.landing && calls == 3, "headroom rejects unduck sweep although final standing occupancy would fit");
    for (int phase : {1, 2}) { Reset(); obstructPhase = phase; Check(!Probe().landing, "blocked crouch or unduck path rejects"); }
    Reset(); groundMissing = true; Check(!Probe().landing, "missing ground at selected event rejects ledge extrapolation");
    Reset(); normal = {0.2f, 0, std::sqrt(0.96f)}; PlaneForGap(1.5f); Check(!Probe().landing, "steep surface yields to native movement");
    Reset(); badNormal = true; Check(!Probe().landing, "unnormalized plane rejects");
    for (float value : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
        Reset(); overriddenFraction = value; Check(!Probe().valid, "invalid native fraction rejects");
    }
    for (float value : {0.0f, 1.0f}) {
        Reset(); overriddenFraction = value; result = Probe(); Check(result.valid && !result.landing, "native trace endpoint does not schedule");
    }
    Reset(); Put(standable.data(), 0x58, 1.0f); normal = {0.1f, 0, std::sqrt(0.99f)}; PlaneForGap(1.5f);
    Check(!Probe().landing, "live standable normal gates capture");
    for (int phase : {0, 1, 2, 3}) { Reset(); changePhase = phase; Check(!Probe().valid, "origin changes in any native callback invalidate"); }
    Reset(); changeHulls = true; Check(!Probe().valid, "provider data changed during trace invalidates");
    Reset(); changeRules = true; Check(!Probe().valid, "native constant drift during trace invalidates");
    for (int phase : {0, 1, 2, 3}) { Reset(); faultPhase = phase; Check(!Probe().valid, "native trace exception rejects"); }
    Reset(); providerFault = true; Check(!Probe().valid, "native provider exception rejects");
    for (auto member : {off::Ducked, off::Ducking, off::DuckOverride}) {
        Reset(); Put(services.data(), member, static_cast<unsigned char>(1)); Check(!Probe().valid && calls == 0, "incompatible native posture skips traces");
    }
    Reset(); Put(services.data(), off::DuckAmount, 0.5f); Check(!Probe().valid, "partial crouch skips");
    Reset(); Put(collision.data(), cs2_offsets::CCollisionProperty::m_vecMaxs, Vector{16, 16, 54});
    Check(!Probe().valid, "live posture hull mismatch skips");
    Reset(); Check(!Probe(true).valid && calls == 0, "physical duck retains ownership");
    Reset(); velocity[2] = 1; Check(!Probe().valid && calls == 0, "rising skips");
    Reset(); Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{1}); Check(!Probe().valid, "grounded skips");
    Reset(); Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{2}); Check(!Probe().valid, "native duck flag skips");
    Reset(); Put(pawn.data(), movement_offsets::pawn::ActualMoveType, static_cast<unsigned char>(8)); Check(!Probe().valid, "noclip skips");
    Reset(); Put(pawn.data(), off::ActualGravityScale, std::numeric_limits<float>::quiet_NaN()); Check(!Probe().valid, "invalid actual gravity rejects");
    Reset(); Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0}); Check(!Probe().valid, "stale pawn rejects");
    Reset(); expectedMask = 0x123; Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{0x10}); Check(Probe().landing, "native mask bit16 adds20");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("jumpbug: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
