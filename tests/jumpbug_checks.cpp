#include "movement_jumpbug.h"
#include "movement_jumpbug_offsets.h"
#include "movement_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = movement_jumpbug_offsets;
unsigned checks = 0, failures = 0, calls = 0;
void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
template<class T> void Put(void* at, std::size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(at) + offset, &value, sizeof(value)); }
template<class T> T Get(const void* at, std::size_t offset = 0) { T value{}; std::memcpy(&value, static_cast<const unsigned char*>(at) + offset, sizeof(value)); return value; }
unsigned char* memory = nullptr;
std::array<unsigned char, 0x2000> pawn{}, services{};
std::array<unsigned char, 0x200> node{}, collision{}, identity{}, gravity{}, standable{};
float fraction = 0.25f, normalZ = 1, normalX = 0, expectedEndX = 11.5625f;
bool solid = false, change = false, fault = false;
std::uint64_t expectedMask = 0x103;
void* Filter(void* result, void* skip, std::uint64_t mask, int layer) {
    Check(skip == pawn.data() && mask == expectedMask && layer == 11, "native movement filter mask/group");
    Check(reinterpret_cast<std::uintptr_t>(result) % 8 == 0, "filter aligned");
    return result;
}
void Result(void* result) { std::memset(result, 0, off::TraceSize); Put(result, off::TraceFraction, 1.0f); }
void Trace(void* cache, void* result, const void* start, const void* end, const void* bounds, void*) {
    ++calls;
    Check(cache == services.data() + off::TraceCache, "native bbox cache receiver");
    Check(reinterpret_cast<std::uintptr_t>(result) % 16 == 0, "trace aligned");
    Check(Get<float>(result, off::TraceFraction) == 1, "trace initialized");
    const auto from = Get<std::array<float, 3>>(start), to = Get<std::array<float, 3>>(end);
    Check(from == std::array<float, 3>{10, 20, 30}, "copied feet origin");
    Check(std::fabs(to[0] - expectedEndX) < 1e-6f && to[1] == 20 && std::fabs(to[2] - 26.33984375f) < 1e-6f,
        "half actual gravity and two-unit probe extension");
    const auto hull = Get<std::array<float, 6>>(bounds);
    Check(hull == std::array<float, 6>{-16, -16, 0, 16, 16, 72}, "copied pawn hull");
    Put(result, off::TraceFraction, fraction);
    Put(result, off::TraceNormal, std::array<float, 3>{normalX, 0, normalZ});
    Put(result, off::TraceSolid, static_cast<unsigned char>(solid));
    if (change) Put(node.data(), cs2_offsets::CGameSceneNode::m_vecAbsOrigin, std::array<float, 3>{11, 20, 30});
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
}
void Reset() {
    fraction = 0.25f; normalZ = 1; normalX = 0; expectedEndX = 11.5625f; solid = change = fault = false;
    expectedMask = 0x103;
    Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, reinterpret_cast<std::uintptr_t>(pawn.data()));
    Put(pawn.data(), movement_offsets::pawn::MovementServices, reinterpret_cast<std::uintptr_t>(services.data()));
    Put(services.data(), movement_offsets::services::Pawn, reinterpret_cast<std::uintptr_t>(pawn.data()));
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), movement_offsets::pawn::ActualMoveType, static_cast<unsigned char>(2));
    Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{0});
    Put(pawn.data(), movement_offsets::pawn::Velocity, std::array<float, 3>{100, 0, -100});
    Put(pawn.data(), cs2_offsets::CEntityInstance::m_pEntity, reinterpret_cast<std::uintptr_t>(identity.data()));
    Put(identity.data(), cs2_offsets::entity_layout::HandleOffset, std::uint32_t{7});
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_pGameSceneNode, reinterpret_cast<std::uintptr_t>(node.data()));
    Put(pawn.data(), cs2_offsets::C_BaseEntity::m_pCollision, reinterpret_cast<std::uintptr_t>(collision.data()));
    Put(node.data(), cs2_offsets::CGameSceneNode::m_vecAbsOrigin, std::array<float, 3>{10, 20, 30});
    Put(collision.data(), cs2_offsets::CCollisionProperty::m_vecMins, std::array<float, 3>{-16, -16, 0});
    Put(collision.data(), cs2_offsets::CCollisionProperty::m_vecMaxs, std::array<float, 3>{16, 16, 72});
    Put(pawn.data(), off::ActualGravityScale, 1.0f);
    Put(pawn.data(), off::CollisionMask, std::uint64_t{0x103});
    Put(memory, off::Gravity + 8, reinterpret_cast<std::uintptr_t>(gravity.data())); Put(gravity.data(), 0x58, 800.0f);
    Put(memory, off::StandableNormal + 8, reinterpret_cast<std::uintptr_t>(standable.data())); Put(standable.data(), 0x58, 0.7f);
    Put(memory, movement_offsets::TickIntervalConstant, 1.0f / 64.0f);
}
MovementJumpbugResult Probe(std::array<float, 3> velocity = {100, 0, -100}, bool duck = false) {
    return MovementProbeJumpbug(reinterpret_cast<std::uintptr_t>(pawn.data()), reinterpret_cast<std::uintptr_t>(services.data()), velocity, duck);
}
}
int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, cs2_offsets::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    const auto module = reinterpret_cast<std::uintptr_t>(memory);
    MovementJumpbugTestConfigure(module, Filter, Result, Trace);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE; nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp = cs2_offsets::ClientTimestamp; nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = cs2_offsets::ClientImageSize;
    Put(memory, 0, dos); Put(memory, 0x100, nt);
    std::memcpy(memory + off::InitializeFilter, off::InitializeFilterBytes, sizeof(off::InitializeFilterBytes));
    std::memcpy(memory + off::InitializeTrace, off::InitializeTraceBytes, sizeof(off::InitializeTraceBytes));
    std::memcpy(memory + off::TraceHull, off::TraceHullBytes, sizeof(off::TraceHullBytes));
    Check(MovementJumpbugPrepare(module), "matching native build prepares");
    for (auto at : {off::InitializeFilter, off::InitializeTrace, off::TraceHull}) {
        memory[at] ^= 1; Check(!MovementJumpbugPrepare(module), "changed native entry rejects"); memory[at] ^= 1;
    }
    Check(MovementJumpbugPrepare(module), "reprepare"); Reset();
    auto result = Probe(); Check(result.valid && result.landing && std::fabs(result.fraction - 937.0f / 1700.0f) < 1e-6f,
        "flat landing maps extended trace fraction to command time");
    Reset(); expectedMask = 0x123; Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{0x10});
    Check(Probe().landing, "native mask bit16 adds20");
    for (float value : {0.0f, 1.0f}) { Reset(); fraction = value; result = Probe(); Check(result.valid && !result.landing, "endpoint is not landing"); }
    for (float value : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) { Reset(); fraction = value; Check(!Probe().valid, "invalid fraction rejects"); }
    Reset(); fraction = 0.0001f; result = Probe();
    Check(result.landing && std::fabs(result.fraction - fraction * 937.0f / 425.0f) < 1e-8f && result.fraction < 0.001f,
        "early valid command fraction is not shifted by arbitrary clamp");
    Reset(); fraction = 0.999f * 425.0f / 937.0f; result = Probe();
    Check(result.landing && std::fabs(result.fraction - 0.999f) < 1e-6f && result.fraction > 0.99f,
        "late valid command fraction is not shifted by arbitrary clamp");
    Reset(); fraction = 0.5f; result = Probe();
    Check(result.valid && !result.landing, "extension-only contact beyond command rejects");
    Reset(); normalX = 0.1f; normalZ = std::sqrt(0.99f); result = Probe();
    const double obliqueExpected = 0.25 * (static_cast<double>(normalX) * 1.5625 - static_cast<double>(normalZ) * 3.66015625) /
        (static_cast<double>(normalX) * 1.5625 - static_cast<double>(normalZ) * 1.66015625);
    Check(result.landing && std::fabs(result.fraction - obliqueExpected) < 1e-7 &&
        std::fabs(result.fraction - 937.0f / 1700.0f) > 0.01f,
        "oblique near-flat plane uses full normal dot product");
    Reset(); normalX = 0.1f; normalZ = std::sqrt(0.99f); expectedEndX = 41.25f;
    Put(pawn.data(), movement_offsets::pawn::Velocity, std::array<float, 3>{2000, 0, -100});
    result = Probe({2000, 0, -100}); Check(result.valid && !result.landing, "unextended trajectory moving away from plane rejects");
    Reset(); normalZ = 0.98f; Check(!Probe().landing, "steep slope rejects assistance");
    Reset(); Put(standable.data(), 0x58, 1.0f); normalZ = 0.99f; Check(!Probe().landing, "live standable normal gates landing");
    Reset(); solid = true; Check(!Probe().valid, "start-solid rejects");
    Reset(); change = true; Check(!Probe().valid, "origin change during native callbacks rejects");
    Reset(); fault = true; Check(!Probe().valid, "native fault rejects");
    Reset(); const auto before = calls;
    Check(!Probe({100, 0, 1}).valid && !Probe({100, 0, -100}, true).valid && calls == before, "rising and physical duck skip native trace");
    Reset(); Put(pawn.data(), movement_offsets::pawn::Flags, std::uint32_t{1}); Check(!Probe().valid, "grounded skips");
    Reset(); Put(pawn.data(), movement_offsets::pawn::ActualMoveType, static_cast<unsigned char>(8)); Check(!Probe().valid, "noclip skips");
    Reset(); Put(pawn.data(), off::ActualGravityScale, std::numeric_limits<float>::quiet_NaN()); Check(!Probe().valid, "invalid actual gravity rejects");
    Reset(); Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0}); Check(!Probe().valid, "stale pawn rejects");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("jumpbug: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
