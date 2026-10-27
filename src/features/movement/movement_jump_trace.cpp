#include "movement_jump_trace.h"
#include "movement_offsets.h"
#include "movement_memory.h"
#include "movement_command.h"
#include "movement_jump.h"
#include "movement_jumpbug_offsets.h"
#include "cs2_offsets.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off = movement_offsets;
namespace entity = cs2_offsets;
using JumpFn = void (*)(void*, void*);
JumpFn original = nullptr;
std::uintptr_t client = 0; // Immutable after preparation, published by enable.
std::atomic<bool> enabled{false};
std::atomic<MovementLogCallback> sink{nullptr};

template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
struct Source {
    std::uintptr_t pawn = 0, services = 0, identity = 0, system = 0;
    std::uintptr_t controller = 0, controllerIdentity = 0;
    std::uint32_t handle = UINT32_MAX, controllerHandle = UINT32_MAX;
};
bool ResolveIdentity(std::uintptr_t system, std::uintptr_t object,
                     std::uintptr_t& identity, std::uint32_t& handle) noexcept {
    std::uintptr_t bucket = 0, resolved = 0;
    if (!Read(object + entity::CEntityInstance::m_pEntity, identity) || !identity ||
        !Read(identity + entity::entity_layout::HandleOffset, handle) || handle == UINT32_MAX) return false;
    const auto index = handle & entity::entity_layout::IndexMask;
    return Read(system + entity::entity_layout::TableOffset + (index >> 9) * sizeof(std::uintptr_t), bucket) &&
        bucket && identity == bucket + (index & 511) * entity::entity_layout::IdentityStride &&
        Read(identity, resolved) && resolved == object;
}
bool ReadSource(std::uintptr_t state, Source& source) noexcept {
    std::uintptr_t local = 0, back = 0;
    std::uint32_t linkedPawn = UINT32_MAX;
    if (state < 0x10000 + off::services::ModernJumpState ||
        !Read(state + 8, source.services) || source.services != state - off::services::ModernJumpState ||
        !Read(source.services + off::services::Pawn, source.pawn) || !source.pawn ||
        !Read(client + entity::globals::dwLocalPlayerPawn, local) || local != source.pawn ||
        !Read(source.pawn + off::pawn::MovementServices, back) || back != source.services ||
        !Read(client + entity::globals::dwGameEntitySystem, source.system) || !source.system ||
        !ResolveIdentity(source.system, source.pawn, source.identity, source.handle) ||
        !Read(client + entity::globals::dwLocalPlayerController, source.controller) || !source.controller ||
        !ResolveIdentity(source.system, source.controller, source.controllerIdentity, source.controllerHandle) ||
        !Read(source.controller + entity::CCSPlayerController::m_hPlayerPawn, linkedPawn) || linkedPawn != source.handle) return false;
    return true;
}
bool SameSource(const Source& a, const Source& b) noexcept {
    return a.pawn == b.pawn && a.services == b.services && a.identity == b.identity && a.system == b.system &&
        a.controller == b.controller && a.controllerIdentity == b.controllerIdentity &&
        a.handle == b.handle && a.controllerHandle == b.controllerHandle;
}
struct Snapshot {
    std::uint32_t flags = 0;
    std::int32_t tick = 0;
    float fraction = 0;
    std::array<float, 3> velocity{}, origin{};
    MovementButtons buttons{};
    JumpStamp actual{}, usable{}, landed{};
};
bool Capture(const Source& source, std::uintptr_t moveData, Snapshot& snap) noexcept {
    if (!Read(source.pawn + off::pawn::Flags, snap.flags) ||
        !Read(moveData + off::move_data::Tick, snap.tick) ||
        !Read(moveData + off::move_data::Fraction, snap.fraction) ||
        !Read(moveData + off::move_data::Velocity, snap.velocity) ||
        !Read(moveData + movement_jumpbug_offsets::MoveOrigin, snap.origin) ||
        !Read(source.services + off::services::Buttons, snap.buttons) ||
        !Read(source.services + off::services::ModernActual, snap.actual) ||
        !Read(source.services + off::services::ModernUsable, snap.usable) ||
        !Read(source.services + off::services::ModernLanded, snap.landed) ||
        !std::isfinite(snap.fraction)) return false;
    for (float value : snap.velocity) if (!std::isfinite(value)) return false;
    for (float value : snap.origin) if (!std::isfinite(value)) return false;
    return std::isfinite(snap.actual.fraction) && std::isfinite(snap.usable.fraction) && std::isfinite(snap.landed.fraction);
}
void Hook(void* statePointer, void* movePointer) {
    const bool observe = enabled.load(std::memory_order_acquire);
    const auto callback = observe ? sink.load(std::memory_order_acquire) : nullptr;
    const auto state = reinterpret_cast<std::uintptr_t>(statePointer);
    const auto moveData = reinterpret_cast<std::uintptr_t>(movePointer);
    Source beforeSource{}; Snapshot before{};
    const bool captured = callback && ReadSource(state, beforeSource) && Capture(beforeSource, moveData, before);
    original(statePointer, movePointer); // Always forward, including stopped/unsupported observations.
    if (!captured || !enabled.load(std::memory_order_acquire)) return;
    const unsigned raw = ((before.buttons.down & off::Jump) ? 1u : 0u) |
        ((before.buttons.changed & off::Jump) ? 2u : 0u) | ((before.buttons.third & off::Jump) ? 4u : 0u);
    const bool fresh = raw >= 3;
    const bool duckEdge = ((before.buttons.changed | before.buttons.third) & off::Duck) != 0;
    if (!fresh && !duckEdge && !(before.flags & off::OnGround)) return;
    Source afterSource{}; Snapshot after{};
    if (!ReadSource(state, afterSource) || !SameSource(beforeSource, afterSource) ||
        !Capture(afterSource, moveData, after)) return;
    const bool cleared = (before.flags & off::OnGround) && !(after.flags & off::OnGround);
    const float verticalImpulse = after.velocity[2] - before.velocity[2];
    const bool takeoff = cleared && verticalImpulse > 0;
    // ModernActual records requests, including cooldown-rejected requests. It
    // must never be reported as evidence that the player actually jumped.
    const bool requestRecorded = before.actual.tick != after.actual.tick ||
        before.actual.fraction != after.actual.fraction;
    const bool bufferChanged = before.usable.tick != after.usable.tick ||
        before.usable.fraction != after.usable.fraction;
    const char* outcome = takeoff ? "takeoff" :
        requestRecorded ? "press_recorded_no_takeoff" :
        bufferChanged ? "buffer_changed_no_takeoff" :
        (before.buttons.down & off::Jump) ? "held_no_takeoff" : "no_takeoff";
    // Unchanged airborne slices emit nothing; the shared queue bounds producers.
    if (!fresh && !duckEdge && !cleared && !((before.flags & off::OnGround) && (before.buttons.down & off::Jump))) return;
    char message[1400]{};
    std::snprintf(message, sizeof(message),
        "MOVE_JUMP_EXEC handle=0x%x rawtick=%d fraction=%.6f raw_state=%u fresh=%u ground_cleared=%u "
        "outcome=%s takeoff=%u request_recorded=%u buffer_changed=%u vertical_impulse=%.3f "
        "flags=0x%x/0x%x velocity_before=%.3f,%.3f,%.3f velocity_after=%.3f,%.3f,%.3f "
        "origin_before=%.3f,%.3f,%.3f origin_after=%.3f,%.3f,%.3f duck_edge=%u "
        "buttons_before=0x%llx/0x%llx/0x%llx buttons_after=0x%llx/0x%llx/0x%llx "
        "actual_before=%d/%.6f actual_after=%d/%.6f usable_before=%d/%.6f usable_after=%d/%.6f "
        "landed_before=%d/%.6f landed_after=%d/%.6f; client jump-handler observation, request stamps alone are not takeoff or jumpbug proof",
        beforeSource.handle, before.tick, before.fraction, raw, fresh, cleared,
        outcome, takeoff, requestRecorded, bufferChanged, verticalImpulse, before.flags, after.flags,
        before.velocity[0], before.velocity[1], before.velocity[2], after.velocity[0], after.velocity[1], after.velocity[2],
        before.origin[0], before.origin[1], before.origin[2], after.origin[0], after.origin[1], after.origin[2], duckEdge,
        before.buttons.down, before.buttons.changed, before.buttons.third,
        after.buttons.down, after.buttons.changed, after.buttons.third,
        before.actual.tick, before.actual.fraction, after.actual.tick, after.actual.fraction,
        before.usable.tick, before.usable.fraction, after.usable.tick, after.usable.fraction,
        before.landed.tick, before.landed.fraction, after.landed.tick, after.landed.fraction);
    callback(message);
}
}

bool MovementJumpTracePrepare(MovementLogCallback callback) noexcept {
    enabled.store(false, std::memory_order_release);
    sink.store(callback, std::memory_order_release);
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, sizeof(off::ModernJumpBytes)> bytes{};
    if (!callback || !client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != entity::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != entity::ClientImageSize ||
        !MovementCopy(client + off::ModernJump, bytes.data(), bytes.size()) ||
        std::memcmp(bytes.data(), off::ModernJumpBytes, bytes.size())) {
        if (callback) callback("MOVE_JUMP_TRACE_UNAVAILABLE reason=installed jump build verification failed");
        return false;
    }
    const auto created = MH_CreateHook(reinterpret_cast<void*>(client + off::ModernJump),
        reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original));
    if (created != MH_OK) {
        callback("MOVE_JUMP_TRACE_UNAVAILABLE reason=jump observer hook creation failed"); return false;
    }
    const auto queued = MH_QueueEnableHook(reinterpret_cast<void*>(client + off::ModernJump));
    if (queued != MH_OK) {
        callback("MOVE_JUMP_TRACE_UNAVAILABLE reason=jump observer activation queue failed"); return false;
    }
    char message[240]{};
    std::snprintf(message, sizeof(message),
        "MOVE_JUMP_TRACE_PREPARED rva=0x%llx snapshot=move_data_velocity_and_live_pawn_flags writes=none; hook activation queued",
        static_cast<unsigned long long>(off::ModernJump));
    callback(message);
    return true;
}
void MovementJumpTraceEnable(bool active) noexcept {
    enabled.store(active, std::memory_order_release);
}
