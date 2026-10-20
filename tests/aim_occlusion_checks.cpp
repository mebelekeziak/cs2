#include "aim_occlusion.h"
#include "aim_occlusion_offsets.h"
#include "aim_damage_offsets.h"
#include "silent_aim_target.h"
#include "third_person_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = aim_occlusion_offsets;
namespace shared = third_person_offsets;
unsigned checks = 0, failures = 0, traceCalls = 0;
void Check(bool condition, const char* label) {
    ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
template<class T> void Put(void* buffer, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(buffer) + offset, &value, sizeof(value));
}
template<class T> T Get(const void* buffer, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(buffer) + offset, sizeof(value)); return value;
}
unsigned char* memory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 16> manager{}, local{}, enemy{}, blocker{};
std::array<unsigned char, 64> nativeHitbox{}, unknownHitbox{};
float fraction = 1;
std::uintptr_t hit = 0, hitbox = 0;
unsigned char hitFlag = 0;
bool returned = false, changeManager = false, changePawn = false, fault = false;
void Shape(void* output, const float* bounds) {
    Check(reinterpret_cast<std::uintptr_t>(output) % 16 == 0, "shape alignment");
    bool zero = true; for (unsigned i = 0; i < 6; ++i) zero &= bounds[i] == 0;
    Check(zero, "ray zero bounds; no camera hull");
    std::memset(output, 0, shared::ShapeSize);
}
void Result(void* output) {
    Check(reinterpret_cast<std::uintptr_t>(output) % 16 == 0, "result alignment");
    std::memset(output, 0, shared::TraceSize);
    Put(output, shared::TraceFraction, 1.0f);
}
void* Filter(void* output, void* pawn, std::uint64_t mask, std::uint8_t layer, std::uint8_t flags) {
    Check(reinterpret_cast<std::uintptr_t>(output) % 8 == 0, "filter alignment");
    Check(pawn == local.data() && mask == off::ShotMask && layer == 3 && flags == 0x0F, "native bullet filter args");
    std::memset(output, 0, shared::FilterSize);
    Put(output, 0x10, std::uint64_t{0x100});
    static_cast<unsigned char*>(output)[0x39] = 0x49;
    return output;
}
bool Trace(void* nativeManager, void* shape, const void* start, const void* end, void* filter, void* output) {
    ++traceCalls;
    Check(nativeManager == manager.data() && shape && Get<unsigned char>(shape, 0x28) == 0, "manager and ray discriminator");
    Check(Get<std::uint64_t>(filter, 0x10) == (off::ShotExclude | 0x100) &&
        Get<unsigned char>(filter, 0x39) == 0x4B, "native bullet exclusions preserve baseline");
    const auto eye = Get<AimVector>(start, 0), center = Get<AimVector>(end, 0);
    Check(eye.x == 10 && eye.y == 20 && eye.z == 30 && center.x == 100 && center.y == 20 && center.z == 30,
        "segment starts at current eye and ends at selected capsule center");
    Check(Get<float>(output, shared::TraceFraction) == 1, "result initialized before trace");
    Check(Get<std::uintptr_t>(output, aim_damage_offsets::TraceHitbox) == 0, "hitbox output initialized before every native trace");
    Put(output, shared::TraceFraction, fraction);
    Put(output, off::TraceEntity, hit);
    Put(output, off::TraceHitFlag, hitFlag);
    Put(output, aim_damage_offsets::TraceHitbox, hitbox);
    if (changeManager) Put(memory, shared::TraceManager, std::uintptr_t{0});
    if (changePawn) Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return returned;
}
void Reset() {
    fraction = 1; hit = 0; hitbox = 0; hitFlag = 0; returned = false;
    changeManager = false; changePawn = false; fault = false;
    Put(memory, shared::TraceManager, reinterpret_cast<std::uintptr_t>(manager.data()));
    Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, reinterpret_cast<std::uintptr_t>(local.data()));
    Put(memory, aim_damage_offsets::UnknownHitboxPointer, reinterpret_cast<std::uintptr_t>(unknownHitbox.data()));
}
}
int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, cs2_offsets::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    client = reinterpret_cast<std::uintptr_t>(memory);
    AimOcclusionTestConfigure(client, Shape, Result, Filter, Trace);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE; nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp = cs2_offsets::ClientTimestamp; nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = cs2_offsets::ClientImageSize;
    Put(memory, 0, dos); Put(memory, 0x100, nt);
    std::memcpy(memory + shared::InitializeShape, shared::InitializeShapeBytes, sizeof(shared::InitializeShapeBytes));
    std::memcpy(memory + shared::InitializeTrace, shared::InitializeTraceBytes, sizeof(shared::InitializeTraceBytes));
    std::memcpy(memory + shared::TraceShape, shared::TraceShapeBytes, sizeof(shared::TraceShapeBytes));
    std::memcpy(memory + off::InitializeFilter, off::InitializeFilterBytes, sizeof(off::InitializeFilterBytes));
    AimOcclusionContext context{};
    Check(AimOcclusionPrepare(client, context) && context.prepared, "matching native contract prepares");
    for (std::size_t offset : {std::size_t(shared::InitializeShape), std::size_t(shared::InitializeTrace),
        std::size_t(shared::TraceShape), std::size_t(off::InitializeFilter)}) {
        memory[offset] ^= 1;
        AimOcclusionContext rejected{client, true};
        Check(!AimOcclusionPrepare(client, rejected) && !rejected.prepared, "entry mutations fail closed");
        memory[offset] ^= 1;
    }
    nt.FileHeader.TimeDateStamp ^= 1; Put(memory, 0x100, nt);
    AimOcclusionContext rejected{};
    Check(!AimOcclusionPrepare(client, rejected), "client identity mismatch rejected");
    nt.FileHeader.TimeDateStamp ^= 1; Put(memory, 0x100, nt);
    AimSource source{}; source.pawn = reinterpret_cast<std::uintptr_t>(local.data());
    AimTarget target{}; target.local.pawn = source.pawn; target.enemy.pawn = reinterpret_cast<std::uintptr_t>(enemy.data());
    target.eye = {10,20,30};
    AimTargetVolume volume{}; volume.valid = true; volume.pawn = target.enemy.pawn; volume.center = {100,20,30};
    Reset();
    auto result = AimOcclusionCheck(context, source, target, volume);
    Check(result.valid && result.clear && result.fraction == 1, "unobstructed false-return trace clears");
    Check(!result.hitgroupKnown && result.hitgroup == 0 && result.distance == 90,
        "clear endpoint retains 90-unit travel without inventing a pawn hitgroup");
    returned = true;
    Check(AimOcclusionCheck(context, source, target, volume).clear, "bool does not replace fraction result");
    fraction = 0.4f; hit = target.enemy.pawn;
    Check(AimOcclusionCheck(context, source, target, volume).clear, "first hit selected pawn clears before endpoint");
    hitbox = reinterpret_cast<std::uintptr_t>(nativeHitbox.data());
    Put(nativeHitbox.data(), aim_damage_offsets::HitboxGroup, 3);
    result = AimOcclusionCheck(context, source, target, volume);
    Check(result.valid && result.clear && result.hitgroupKnown && result.hitgroup == 3 && std::abs(result.distance - 36) < .0001f,
        "selected pawn collision copies the native stomach group and 90-unit segment times hit fraction");
    Put(nativeHitbox.data(), aim_damage_offsets::HitboxGroup, 1);
    Check(result.hitgroup == 3, "returned hitgroup is a copied value independent of later native hitbox changes");
    result = AimOcclusionCheck(context, source, target, volume);
    Check(result.hitgroupKnown && result.hitgroup == 1, "subsequent collision samples the current native hitgroup");
    hitbox = 0;
    result = AimOcclusionCheck(context, source, target, volume);
    Check(result.valid && result.clear && !result.hitgroupKnown && result.hitgroup == 0,
        "null hitbox retains reachability but clears previous hitgroup knowledge");
    hitbox = reinterpret_cast<std::uintptr_t>(unknownHitbox.data());
    Put(unknownHitbox.data(), aim_damage_offsets::HitboxGroup, 2);
    result = AimOcclusionCheck(context, source, target, volume);
    Check(result.valid && result.clear && !result.hitgroupKnown && result.hitgroup == 0,
        "native unknown-hitbox sentinel cannot supply a damage hitgroup even if its memory is readable");
    auto unreadable = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    Check(unreadable != nullptr, "unreadable hitbox fixture allocated");
    if (unreadable) {
        hitbox = reinterpret_cast<std::uintptr_t>(unreadable);
        result = AimOcclusionCheck(context, source, target, volume);
        Check(result.valid && result.clear && !result.hitgroupKnown && result.hitgroup == 0,
            "unreadable hitbox cannot publish stale or unverified damage context");
        VirtualFree(unreadable, 0, MEM_RELEASE);
    }
    hitbox = reinterpret_cast<std::uintptr_t>(nativeHitbox.data());
    hit = reinterpret_cast<std::uintptr_t>(blocker.data());
    result = AimOcclusionCheck(context, source, target, volume);
    Check(!result.clear, "intervening entity blocks");
    Check(result.valid && !result.hitgroupKnown && result.hitgroup == 0,
        "an intervening entity's hitbox cannot become the selected pawn's predicted hitgroup");
    hit = 0;
    result = AimOcclusionCheck(context, source, target, volume);
    Check(!result.clear, "world cover blocks");
    Check(!result.hitgroupKnown && result.hitgroup == 0, "world collision cannot supply pawn hitgroup knowledge");
    fraction = std::nextafter(1.0f, 0.0f);
    Check(!AimOcclusionCheck(context, source, target, volume).clear, "cover one float before endpoint blocks");
    fraction = 1; hit = reinterpret_cast<std::uintptr_t>(blocker.data());
    Check(!AimOcclusionCheck(context, source, target, volume).clear, "non-target endpoint collision blocks");
    hit = 0;
    result = AimOcclusionCheck(context, source, target, volume);
    Check(result.clear && !result.hitgroupKnown && result.hitgroup == 0 && result.distance == 90,
        "clear no-entity segment does not inherit a readable but unrelated hitbox record");
    hit = target.enemy.pawn; hitFlag = 1;
    Check(!AimOcclusionCheck(context, source, target, volume).clear, "native hit flag beats selected pawn and full fraction");
    hitFlag = 0; fraction = 0;
    Check(!AimOcclusionCheck(context, source, target, volume).clear, "zero-length hit fraction blocked");
    for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        fraction = invalid;
        Check(!AimOcclusionCheck(context, source, target, volume).valid, "invalid fraction fails closed");
    }
    Reset(); changeManager = true;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "manager changes during callback reject");
    Reset(); changePawn = true;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "source pawn changes during callback reject");
    Reset(); fault = true;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "native callback faults fail closed");
    Reset();
    Put(memory, shared::TraceManager, std::uintptr_t{0});
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "unavailable manager rejected");
    Reset(); const auto previous = traceCalls;
    Check(!AimOcclusionCheck({}, source, target, volume).valid, "unprepared contract rejects");
    volume.valid = false;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "missing authored geometry rejects");
    volume.valid = true; volume.pawn = source.pawn;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "mismatched geometry rejects");
    volume.pawn = target.enemy.pawn; volume.center = target.eye;
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "degenerate segment rejects");
    volume.center.x = std::numeric_limits<float>::quiet_NaN();
    Check(!AimOcclusionCheck(context, source, target, volume).valid, "nonfinite endpoint rejects");
    Check(traceCalls == previous, "invalid inputs never call native trace");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("aim occlusion checks=%u failures=%u; fake native callbacks only\n", checks, failures);
    return failures ? 1 : 0;
}
