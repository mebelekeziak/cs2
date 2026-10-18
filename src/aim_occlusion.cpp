#include "aim_occlusion.h"
#include "aim_occlusion_offsets.h"
#include "silent_aim_target.h"
#include "third_person_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = aim_occlusion_offsets;
namespace shared = third_person_offsets;
using ShapeFn = void (*)(void*, const float*);
using ResultFn = void (*)(void*);
using FilterFn = void* (*)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
using TraceFn = bool (*)(void*, void*, const void*, const void*, void*, void*);
#ifdef CS2_AIM_OCCLUSION_CHECKS
std::uintptr_t testClient = 0;
ShapeFn testShape = nullptr;
ResultFn testResult = nullptr;
FilterFn testFilter = nullptr;
TraceFn testTrace = nullptr;
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept {
    return MovementCopy(at, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&expected)[N]) noexcept {
    unsigned char bytes[N]{};
    return MovementCopy(at, bytes, N) && !std::memcmp(bytes, expected, N);
}
bool CurrentClient(std::uintptr_t client) noexcept {
#ifdef CS2_AIM_OCCLUSION_CHECKS
    if (testClient) return client == testClient;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Finite(AimVector vector) noexcept {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}
struct TraceOutput { float fraction; std::uintptr_t entity; unsigned char hitFlag; };
// Native calls can invoke callbacks. Stack objects expire only after the
// synchronous call returns; faults fail closed and publish no attack decision.
bool Trace(std::uintptr_t client, std::uintptr_t manager, std::uintptr_t pawn,
    const AimVector& eye, const AimVector& end, TraceOutput& output) noexcept {
    auto shapeFn = reinterpret_cast<ShapeFn>(client + shared::InitializeShape);
    auto resultFn = reinterpret_cast<ResultFn>(client + shared::InitializeTrace);
    auto filterFn = reinterpret_cast<FilterFn>(client + off::InitializeFilter);
    auto traceFn = reinterpret_cast<TraceFn>(client + shared::TraceShape);
#ifdef CS2_AIM_OCCLUSION_CHECKS
    if (testClient) {
        shapeFn = testShape; resultFn = testResult; filterFn = testFilter; traceFn = testTrace;
    }
#endif
    if (!shapeFn || !resultFn || !filterFn || !traceFn) return false;
    alignas(16) unsigned char shape[shared::ShapeSize]{};
    alignas(16) unsigned char result[shared::TraceSize]{};
    alignas(8) unsigned char filter[shared::FilterSize]{};
    const float bounds[6]{};
    __try {
        shapeFn(shape, bounds);
        resultFn(result);
        filterFn(filter, reinterpret_cast<void*>(pawn), off::ShotMask, off::ShotLayer, off::ShotFlags);
        filter[0x39] |= off::ShotFlags39;
        std::uint64_t exclude = 0;
        std::memcpy(&exclude, filter + 0x10, sizeof(exclude));
        exclude |= off::ShotExclude;
        std::memcpy(filter + 0x10, &exclude, sizeof(exclude));
        // Native camera consumers ignore the bool: false can mean no hit.
        traceFn(reinterpret_cast<void*>(manager), shape, &eye, &end, filter, result);
        std::memcpy(&output.fraction, result + shared::TraceFraction, sizeof(output.fraction));
        std::memcpy(&output.entity, result + off::TraceEntity, sizeof(output.entity));
        output.hitFlag = result[off::TraceHitFlag];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool AimOcclusionPrepare(std::uintptr_t client, AimOcclusionContext& context) noexcept {
    context = {};
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!CurrentClient(client) || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Match(client + shared::InitializeShape, shared::InitializeShapeBytes) ||
        !Match(client + shared::InitializeTrace, shared::InitializeTraceBytes) ||
        !Match(client + shared::TraceShape, shared::TraceShapeBytes) ||
        !Match(client + off::InitializeFilter, off::InitializeFilterBytes)) return false;
    context = {client, true};
    return true;
}

AimOcclusionResult AimOcclusionCheck(const AimOcclusionContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume) noexcept {
    AimOcclusionResult result{};
    result.reason = "occlusion native contract unavailable";
    if (!context.prepared || !CurrentClient(context.client)) return result;
    result.reason = "occlusion source/geometry unavailable";
    if (!source.pawn || source.pawn != target.local.pawn || !target.enemy.pawn ||
        target.enemy.pawn == source.pawn || !volume.valid || volume.pawn != target.enemy.pawn ||
        !Finite(target.eye) || !Finite(volume.center)) return result;
    const double x = static_cast<double>(volume.center.x) - target.eye.x;
    const double y = static_cast<double>(volume.center.y) - target.eye.y;
    const double z = static_cast<double>(volume.center.z) - target.eye.z;
    if (!(x*x + y*y + z*z > 0) || !std::isfinite(x*x + y*y + z*z)) return result;
    std::uintptr_t manager = 0, livePawn = 0;
    result.reason = "occlusion trace manager/source unavailable";
    if (!Read(context.client + shared::TraceManager, manager) || !manager ||
        !Read(context.client + cs2_offsets::globals::dwLocalPlayerPawn, livePawn) || livePawn != source.pawn) return result;
    TraceOutput output{};
    result.reason = "occlusion native trace failed";
    if (!Trace(context.client, manager, source.pawn, target.eye, volume.center, output)) return result;
    std::uintptr_t managerAfter = 0;
    result.reason = "occlusion source changed during trace";
    if (!CurrentClient(context.client) || !Read(context.client + shared::TraceManager, managerAfter) ||
        managerAfter != manager || !Read(context.client + cs2_offsets::globals::dwLocalPlayerPawn, livePawn) ||
        livePawn != source.pawn) return result;
    result.reason = "occlusion invalid trace result";
    if (!std::isfinite(output.fraction) || output.fraction < 0 || output.fraction > 1) return result;
    result.valid = true;
    result.fraction = output.fraction;
    // No near-endpoint tolerance: even cover just before the capsule blocks.
    // A first hit on the selected pawn is direct reachability. Its other
    // hitboxes are native collision; authored selected-capsule chance is separate.
    result.clear = !output.hitFlag && output.fraction > 0 &&
        (output.entity == target.enemy.pawn || (output.fraction == 1 && !output.entity));
    result.reason = result.clear ? "direct line of fire clear" :
        output.hitFlag ? "line of fire blocked (native hit flag)" : "line of fire blocked";
    return result;
}

#ifdef CS2_AIM_OCCLUSION_CHECKS
void AimOcclusionTestConfigure(std::uintptr_t client, AimOcclusionShapeFn shape,
    AimOcclusionResultFn result, AimOcclusionFilterFn filter, AimOcclusionTraceFn trace) noexcept {
    testClient = client; testShape = shape; testResult = result; testFilter = filter; testTrace = trace;
}
#endif
