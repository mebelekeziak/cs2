#include "aim_autowall.h"
#include "aim_autowall_offsets.h"
#include "aim_damage_offsets.h"
#include "aim_occlusion_offsets.h"
#include "aim_weapon.h"
#include "third_person_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
namespace off = aim_autowall_offsets;
struct Calls {
    void* (*initialize)(void*);
    void (*destroy)(void*);
    void* (*filter)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
    void (*trace)(void*, const void*, const void*, void*, int, bool);
    bool (*step)(void*, void*, void*, int, void*);
    void (*extract)(void*, void*, float, void*);
};
#ifdef CS2_AIM_AUTOWALL_CHECKS
std::uintptr_t testClient = 0;
Calls testCalls{};
#endif
template<class T> bool Read(std::uintptr_t at, T& value) noexcept { return MovementCopy(at, &value, sizeof(value)); }
template<class T> T Get(const unsigned char* at) noexcept { T value{}; std::memcpy(&value, at, sizeof(value)); return value; }
bool CurrentClient(std::uintptr_t client) noexcept {
#ifdef CS2_AIM_AUTOWALL_CHECKS
    if (testClient) return client == testClient;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
struct BulletState {
    float damage, penetration, rangeModifier, distance;
    int remaining;
    bool stopped;
    unsigned char padding[3]{};
};
static_assert(sizeof(BulletState) == 0x18);
// Separate SEH boundaries ensure the native destructor runs after faults in tracing/iteration.
bool Run(const Calls& calls, unsigned char* data, unsigned char* filter, std::uintptr_t client,
    std::uintptr_t pawn, std::uintptr_t victim, int team, const AimVector& eye, const AimVector& delta,
    BulletState state, AimOcclusionResult& result) noexcept {
    __try {
        calls.filter(filter, reinterpret_cast<void*>(pawn), aim_occlusion_offsets::ShotMask,
            aim_occlusion_offsets::ShotLayer, aim_occlusion_offsets::ShotFlags);
        filter[0x39] |= aim_occlusion_offsets::ShotFlags39;
        const auto exclude = Get<std::uint64_t>(filter + 0x10) | aim_occlusion_offsets::ShotExclude;
        std::memcpy(filter + 0x10, &exclude, sizeof(exclude));
        calls.trace(data, &eye, &delta, filter, off::PenetrationCount, true);
        // The native producer can extend the ray to acquire an exit surface.
        // Its segmentation fractions refer to this final displacement.
        const auto direction = Get<AimVector>(data + off::DirectionVector);
        state.distance = std::sqrt(direction.x*direction.x + direction.y*direction.y + direction.z*direction.z);
        if (!std::isfinite(state.distance) || state.distance <= 0 || state.distance > 1000000) return false;
        const int hits = Get<int>(data + off::HitCount), count = Get<int>(data + off::SegmentCount);
        const int hitCapacity = Get<int>(data + off::HitCapacity), capacity = Get<int>(data + off::SegmentCapacity);
        const auto hitArray = Get<std::uintptr_t>(data + off::Hits);
        const auto segments = Get<std::uintptr_t>(data + off::Segments);
        if (hits <= 0 || hits > off::MaxHits || hitCapacity < hits || hitCapacity > off::MaxHits || !hitArray ||
            count <= 0 || count > off::MaxSegments || capacity < count || capacity > off::MaxSegments || !segments) return false;
        float previous = 0;
        for (int i = 0; i < count; ++i) {
            unsigned char segment[off::SegmentSize]{};
            if (!MovementCopy(segments + i * off::SegmentSize, segment, sizeof(segment))) return false;
            const float start = Get<float>(segment), end = Get<float>(segment + 4);
            const auto first = Get<std::uint16_t>(segment + off::SegmentFirst) & 0x7fff;
            const auto last = Get<std::uint16_t>(segment + off::SegmentLast);
            const bool solid = (segment[off::SegmentFlags] & 1) != 0;
            if (!std::isfinite(start) || !std::isfinite(end) || start < previous || end < start || end > 1 ||
                first >= hits || last >= hits || (solid && first > last)) return false;
            previous = end;
            if (solid) {
                // The native solid branch also covers flesh. Reject foreign players:
                // this feature predicts cover, not collateral damage through other pawns.
                alignas(16) unsigned char impact[third_person_offsets::TraceSize]{};
                calls.extract(data, impact, start, reinterpret_cast<void*>(hitArray + first * off::HitSize));
                const auto entity = Get<std::uintptr_t>(impact + aim_occlusion_offsets::TraceEntity);
                const auto hitbox = Get<std::uintptr_t>(impact + aim_damage_offsets::TraceHitbox);
                std::uintptr_t unknown = 0;
                if (!Read(client + aim_damage_offsets::UnknownHitboxPointer, unknown)) return false;
                if (entity && hitbox && hitbox != unknown) return false;
            }
            const float before = state.damage;
            const int remaining = state.remaining;
            if (calls.step(data, &state, segment, team, nullptr) || state.stopped ||
                !std::isfinite(state.damage) || state.damage < 1 || state.damage > before ||
                state.remaining < 0 || state.remaining > remaining) return false;
            if (solid) continue;
            alignas(16) unsigned char impact[third_person_offsets::TraceSize]{};
            calls.extract(data, impact, end, reinterpret_cast<void*>(hitArray + last * off::HitSize));
            const auto entity = Get<std::uintptr_t>(impact + aim_occlusion_offsets::TraceEntity);
            std::uintptr_t unknown = 0;
            const auto hitbox = Get<std::uintptr_t>(impact + aim_damage_offsets::TraceHitbox);
            if (!Read(client + aim_damage_offsets::UnknownHitboxPointer, unknown)) return false;
            if (entity != victim) {
                if (entity && hitbox && hitbox != unknown) return false;
                continue;
            }
            int group = 0;
            if (impact[aim_occlusion_offsets::TraceHitFlag] || !hitbox ||
                !Read(client + aim_damage_offsets::UnknownHitboxPointer, unknown) || hitbox == unknown ||
                !Read(hitbox + aim_damage_offsets::HitboxGroup, group) || group < 1 || group > 8) return false;
            result.valid = result.clear = result.hitgroupKnown = true;
            result.hitgroup = group; result.fraction = end; result.distance = end * state.distance;
            result.penetrations = off::PenetrationCount - state.remaining;
            result.penetrationDamage = state.damage;
            result.reason = "native bullet path reached target";
            return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Initialize(const Calls& calls, void* data) noexcept {
    __try { return calls.initialize(data) == data; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Destroy(const Calls& calls, void* data) noexcept {
    __try { calls.destroy(data); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool AimAutowallPrepare(std::uintptr_t client, AimAutowallContext& context) noexcept {
    context = {};
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!CurrentClient(client) || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize) return false;
    for (const auto& window : off::ContractWindows) {
        unsigned char bytes[64]{};
        for (std::size_t at = 0; at < window.size; at += sizeof(bytes)) {
            const auto size = std::min(sizeof(bytes), window.size - at);
            if (!MovementCopy(client + window.rva + at, bytes, size) || std::memcmp(bytes, window.bytes + at, size)) return false;
        }
    }
    context = {client, true}; return true;
}

AimOcclusionResult AimAutowallCheck(const AimAutowallContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon) noexcept {
    AimOcclusionResult result{};
    result.reason = "autowall contract/source unavailable";
    if (!context.prepared || !CurrentClient(context.client) || !weapon.data ||
        !SilentAimTargetVolumeValid(source, target, volume)) return result;
    const AimVector delta{volume.center.x - target.eye.x, volume.center.y - target.eye.y, volume.center.z - target.eye.z};
    const float length = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
    int damage = 0; float power = 0, modifier = 0;
    std::uintptr_t manager = 0, pawn = 0;
    if (!std::isfinite(length) || length <= 0 || !std::isfinite(weapon.range) || length > weapon.range ||
        !Read(weapon.data + aim_damage_offsets::Damage, damage) || damage <= 0 || damage > 100000 ||
        !Read(weapon.data + off::Penetration, power) || !std::isfinite(power) || power <= 0 || power > 100 ||
        !Read(weapon.data + aim_damage_offsets::RangeModifier, modifier) || !std::isfinite(modifier) || modifier <= 0 || modifier > 1 ||
        !Read(context.client + third_person_offsets::TraceManager, manager) || !manager ||
        !Read(context.client + cs2_offsets::globals::dwLocalPlayerPawn, pawn) || pawn != source.pawn) return result;
    const auto client = context.client;
    Calls calls{reinterpret_cast<decltype(Calls::initialize)>(client + off::Initialize),
        reinterpret_cast<decltype(Calls::destroy)>(client + off::Destroy),
        reinterpret_cast<decltype(Calls::filter)>(client + aim_occlusion_offsets::InitializeFilter),
        reinterpret_cast<decltype(Calls::trace)>(client + off::Trace),
        reinterpret_cast<decltype(Calls::step)>(client + off::Step),
        reinterpret_cast<decltype(Calls::extract)>(client + off::Extract)};
#ifdef CS2_AIM_AUTOWALL_CHECKS
    if (testClient) calls = testCalls;
#endif
    alignas(16) unsigned char data[off::DataSize]{};
    alignas(8) unsigned char filter[third_person_offsets::FilterSize]{};
    result.reason = "autowall native trace blocked or unavailable";
    if (!Initialize(calls, data)) return result;
    const bool reached = Run(calls, data, filter, client, source.pawn, target.enemy.pawn, target.local.team, target.eye, delta,
        {static_cast<float>(damage), power, modifier, length, off::PenetrationCount, false}, result);
    const bool destroyed = Destroy(calls, data);
    std::uintptr_t after = 0;
    if (!reached || !destroyed || result.distance > weapon.range || !CurrentClient(client) ||
        !Read(client + third_person_offsets::TraceManager, after) || after != manager ||
        !Read(client + cs2_offsets::globals::dwLocalPlayerPawn, after) || after != source.pawn ||
        !SilentAimTargetVolumeValid(source, target, volume)) {
        result.valid = result.clear = result.hitgroupKnown = false;
        result.penetrationDamage = -1;
        result.reason = "autowall blocked, failed or changed during trace";
        return result;
    }
    result.bulletBaseDamage = damage; result.bulletRangeModifier = modifier; result.bulletPenetration = power;
    return result;
}
#ifdef CS2_AIM_AUTOWALL_CHECKS
void AimAutowallTestConfigure(std::uintptr_t client, AimAutowallTestCalls calls) noexcept {
    testClient = client;
    testCalls = {calls.initialize, calls.destroy, calls.filter, calls.trace, calls.step, calls.extract};
}
#endif
