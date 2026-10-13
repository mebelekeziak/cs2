#include "movement.h"
#include "movement_math.h"
#include "movement_jump.h"
#include "movement_steering.h"
#include "movement_command.h"
#include "movement_replay.h"
#include "movement_offsets.h"
#include "movement_memory.h"
#include "movement_telemetry.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>

namespace {
namespace off = movement_offsets;
namespace entity = cs2_offsets;
using FinalizeFn = void (*)(void*, void*);
FinalizeFn original = nullptr;
FinalizeFn originalIngest = nullptr;
std::uintptr_t client = 0;
MovementLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"movement not initialized"};
SRWLOCK commandLock = SRWLOCK_INIT;
bool stopEverRequested = false; // Protected by commandLock.
DWORD owner = 0;
std::uintptr_t sourcePawn = 0, sourceServices = 0;
std::uintptr_t sourceController = 0;
std::uint32_t sourceControllerHandle = UINT32_MAX;
std::uint32_t sourceHandle = UINT32_MAX;
int lastSequence = -1, lastTick = -1;
bool wasGrounded = false, haveGround = false;
JumpState jumpState{};
SteeringState steeringState{};
bool strafeControlled = false;
std::uint64_t previousDirections = 0;
float lastSpeed = 0;
ULONGLONG lastReport = 0;
std::uint64_t callbacks = 0, modified = 0, presses = 0, strafes = 0, rejected = 0;
ULONGLONG lastConsumeReport = 0;
struct Request {
    int sequence = -1;
    float forward = 0, left = 0;
    MovementButtons buttons{};
    std::uint64_t mask = 0;
    bool strafe = false, jumpEdge = false, edgeReported = false;
};
std::array<Request, 256> requests{};
struct ReplayCache {
    MovementReplayContext context{};
    MovementReplayPlan plan{};
    std::array<MovementReplayStep, 2> events{}; // Analog then jump.
};
ReplayCache replay{};
MovementSettings inputSettings{};
bool haveInputSettings = false;
std::uint64_t replayed = 0, replayNativeRepairs = 0, replayRestoredEvents = 0;
ULONGLONG lastReplayReport = 0;
LARGE_INTEGER timingFrequency{};
std::uint64_t timedCalls = 0, timedAirCalls = 0;
double totalUs = 0, maximumUs = 0, totalAirUs = 0, maximumAirUs = 0;
ULONGLONG lastTimingReport = 0;
bool airborneWork = false;
const char* lastJumpReadState = nullptr;
enum class Stage : unsigned { Controls, Pawn, Layout, Jump, EngineSpeed, SpeedModifiers, Keys, Solver, Subticks, History, Append, Apply, Diagnostics, Count };
struct StageSample { double total = 0, maximum = 0; std::uint64_t count = 0; };
std::array<StageSample, static_cast<unsigned>(Stage::Count)> stageSamples{};
LARGE_INTEGER stageStart{};
double totalLockUs = 0, maximumLockUs = 0;

void MarkStage(Stage stage) noexcept {
    LARGE_INTEGER end{}; QueryPerformanceCounter(&end);
    const double us = 1000000.0 * (end.QuadPart - stageStart.QuadPart) / timingFrequency.QuadPart;
    auto& sample = stageSamples[static_cast<unsigned>(stage)];
    sample.total += us; sample.maximum = std::max(sample.maximum, us); ++sample.count;
    stageStart = end;
}

void Log(const char* format, ...) noexcept {
    if (!logger) return;
    char buffer[1400]{};
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    MovementTelemetryPush(buffer);
}
void State(const char* next) noexcept {
    const char* old = status.exchange(next, std::memory_order_acq_rel);
    if (strcmp(old, next) != 0) Log("MOVE_STATE status=%s", next);
}
bool Copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    return MovementCopy(address, output, bytes);
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept { return Copy(address, &output, sizeof(output)); }
bool Writable(std::uintptr_t address, std::size_t bytes) noexcept {
    return MovementOwnedWritable(address, bytes);
}
template<class T> bool Cvar(std::ptrdiff_t rva, T& value) noexcept {
    return MovementReadCvar(client + rva, value);
}
bool EffectiveSpeed(std::uintptr_t pawn, float& speed) noexcept {
    // Locally verified engine helper, RCX=pawn and XMM0=float result. Invoke
    // only on the engine command owner; it includes server and weapon limits.
    bool valid = false;
    __try {
        speed = reinterpret_cast<float (*)(void*)>(client + off::EffectiveMaxSpeed)(reinterpret_cast<void*>(pawn));
        valid = std::isfinite(speed) && speed > 0 && speed < 100000;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        valid = false;
    }
    MarkStage(Stage::EngineSpeed);
    return valid;
}
void Reset() noexcept {
    sourcePawn = sourceServices = sourceController = 0; sourceHandle = UINT32_MAX;
    sourceControllerHandle = UINT32_MAX;
    lastSequence = lastTick = -1;
    wasGrounded = haveGround = false; lastSpeed = 0;
    ResetJump(jumpState); ResetAirSteering(steeringState);
    lastJumpReadState = nullptr;
    replay = {}; haveInputSettings = false;
    for (auto& request : requests) request.sequence = -1;
    strafeControlled = false; previousDirections = 0;
}

struct Pawn {
    std::uintptr_t address = 0, services = 0, identity = 0, system = 0;
    std::uintptr_t controller = 0, controllerIdentity = 0;
    std::uint32_t handle = UINT32_MAX, flags = 0;
    std::uint32_t controllerHandle = UINT32_MAX, simulationTick = 0;
    std::array<float, 3> velocity{};
    float friction = 0, maxSpeed = 0;
};
bool SamePawn(const Pawn& pawn) noexcept {
    std::uintptr_t current = 0, back = 0, bucket = 0, resolved = 0, services = 0, system = 0;
    std::uint32_t handle = UINT32_MAX, controllerHandle = UINT32_MAX, linkedPawn = UINT32_MAX;
    std::uintptr_t controller = 0, controllerBack = 0, controllerResolved = 0;
    const auto index = pawn.handle & entity::entity_layout::IndexMask;
    return Read(client + entity::globals::dwLocalPlayerPawn, current) && current == pawn.address &&
        Read(client + entity::globals::dwGameEntitySystem, system) && system == pawn.system &&
        Read(pawn.address + entity::CEntityInstance::m_pEntity, back) && back == pawn.identity &&
        Read(system + entity::entity_layout::TableOffset + (index >> 9) * sizeof(std::uintptr_t), bucket) && bucket &&
        pawn.identity == bucket + (index & 511) * entity::entity_layout::IdentityStride &&
        Read(pawn.identity, resolved) && resolved == pawn.address &&
        Read(pawn.identity + entity::entity_layout::HandleOffset, handle) && handle == pawn.handle &&
        Read(pawn.address + off::pawn::MovementServices, services) && services == pawn.services &&
        Read(client + entity::globals::dwLocalPlayerController, controller) && controller == pawn.controller &&
        Read(controller + entity::CEntityInstance::m_pEntity, controllerBack) && controllerBack == pawn.controllerIdentity &&
        Read(controllerBack, controllerResolved) && controllerResolved == controller &&
        Read(controllerBack + entity::entity_layout::HandleOffset, controllerHandle) && controllerHandle == pawn.controllerHandle &&
        Read(controller + entity::CCSPlayerController::m_hPlayerPawn, linkedPawn) && linkedPawn == pawn.handle;
}
bool ReadPawn(Pawn& pawn) noexcept {
    int health = 0;
    std::uint8_t life = 1, moveType = 0;
    float water = 0;
    if (!Read(client + entity::globals::dwLocalPlayerPawn, pawn.address) || !pawn.address ||
        !Read(client + entity::globals::dwGameEntitySystem, pawn.system) || !pawn.system ||
        !Read(pawn.address + entity::CEntityInstance::m_pEntity, pawn.identity) || !pawn.identity ||
        !Read(pawn.identity + entity::entity_layout::HandleOffset, pawn.handle) || pawn.handle == UINT32_MAX ||
        !Read(pawn.address + off::pawn::MovementServices, pawn.services) || !pawn.services ||
        !Read(pawn.address + entity::C_BaseEntity::m_iHealth, health) || health <= 0 || health > 1000 ||
        !Read(pawn.address + entity::C_BaseEntity::m_lifeState, life) || life != 0 ||
        !Read(pawn.address + off::pawn::ActualMoveType, moveType) || moveType != off::Walk ||
        !Read(pawn.address + off::pawn::Flags, pawn.flags) || (pawn.flags & (off::Frozen | off::WaterJump)) ||
        !Read(pawn.address + off::pawn::WaterLevel, water) || !std::isfinite(water) || water < 0 || water >= 0.5f ||
        !Read(pawn.address + off::pawn::Velocity, pawn.velocity) ||
        !Read(pawn.services + off::services::Friction, pawn.friction) ||
        !Read(pawn.services + off::services::MaxSpeed, pawn.maxSpeed) ||
        !Read(client + entity::globals::dwLocalPlayerController, pawn.controller) || !pawn.controller ||
        !Read(pawn.controller + entity::CEntityInstance::m_pEntity, pawn.controllerIdentity) || !pawn.controllerIdentity ||
        !Read(pawn.controllerIdentity + entity::entity_layout::HandleOffset, pawn.controllerHandle) || pawn.controllerHandle == UINT32_MAX ||
        !Read(pawn.controller + off::controller::TickBase, pawn.simulationTick)) return false;
    for (float component : pawn.velocity) if (!std::isfinite(component) || std::abs(component) > 100000) return false;
    return SamePawn(pawn);
}
bool AirSpeed(const Pawn& pawn, float& speed) noexcept {
    if (!EffectiveSpeed(pawn.address, speed)) return false;
    float stamina = 0, staminaScale = 0, surfaceFactor = 1;
    std::uintptr_t surface = 0;
    if (!Read(pawn.services + off::services::Stamina, stamina) || !std::isfinite(stamina) ||
        !Read(client + off::StaminaScaleConstant, staminaScale) || !std::isfinite(staminaScale) || staminaScale <= 0 ||
        !Read(pawn.services + off::services::SurfaceData, surface)) return false;
    // Parameter checking uses factor 1 when no surface data exists. A present
    // but unreadable surface does not receive that fallback.
    if (surface) {
        int index = -1;
        std::uintptr_t manager = 0, rows = 0;
        if (!Read(surface + 0x10, index) || index < 0 ||
            !Read(client + off::SurfaceManager, manager) || !manager ||
            !Read(manager + 0x28, rows) || !rows ||
            !Read(rows + static_cast<std::uintptr_t>(index) * 32, surfaceFactor) ||
            !std::isfinite(surfaceFactor) || surfaceFactor < 0) return false;
    }
    if (stamina > 0) {
        const float factor = std::clamp(1.0f - stamina / staminaScale, 0.0f, 1.0f);
        speed *= factor * factor;
    }
    speed *= surfaceFactor;
    return std::isfinite(speed) && speed > 0 && speed < 100000;
}

bool ReadJumpRules(const Pawn& pawn, JumpInput& input, float& interval, float& penalty,
                   std::uint8_t& autoBhop, std::uint8_t& legacy, MovementButtons& buttons,
                   const char*& failure) noexcept {
    failure = "tick interval";
    if (!Read(client + off::TickIntervalConstant, interval) || !std::isfinite(interval) || interval <= 0 || interval >= 0.1f) return false;
    failure = "sv_jump_spam_penalty_time";
    if (!Cvar(off::JumpPenalty, penalty) || !std::isfinite(penalty) || penalty < 0 || penalty >= 10) return false;
    failure = "sv_autobunnyhopping";
    if (!Cvar(off::AutoBunnyhop, autoBhop)) return false;
    failure = "sv_legacy_jump";
    if (!Cvar(off::LegacyJump, legacy)) return false;
    failure = "sv_bhop_time_window";
    if (!Cvar(off::BhopWindow, input.bufferWindow) || !std::isfinite(input.bufferWindow) || input.bufferWindow < 0 || input.bufferWindow >= 10) return false;
    failure = "movement input buttons";
    if (!Read(pawn.services + off::services::Buttons, buttons)) return false;
    failure = "actual jump stamp";
    if (!Read(pawn.services + off::services::ModernActual, input.actual)) return false;
    failure = "usable jump stamp";
    if (!Read(pawn.services + off::services::ModernUsable, input.usable)) return false;
    failure = "landing stamp";
    if (!Read(pawn.services + off::services::ModernLanded, input.landed)) return false;
    failure = "available";
    return true;
}

// Existing command messages are borrowed; additional time-zero transitions use
// the verified game arena/container APIs. Original event order is preserved.
struct Edit {
    std::uintptr_t address = 0;
    MovementSubtick value{};
};
struct Patch {
    std::uintptr_t command = 0, base = 0;
    MovementButtons buttons{};
    std::uint32_t commandPresence = 0, basePresence = 0;
    float forward = 0, left = 0;
    bool jump = false, strafe = false;
    std::array<Edit, 128> steps{};
    int size = 0;
    int originalSize = 0;
    std::array<std::uintptr_t, 128> originalOrder{};
    bool orderCaptured = false;
    std::array<int, 2> ownedIndices{-1, -1};
};
bool Apply(const Patch& patch) noexcept {
    // Keep SEH in a function without C++ unwinding objects. The command callback
    // owns these allocations; preflight checks reject malformed/stale layouts.
    __try {
        if (patch.jump || patch.strafe) std::memcpy(reinterpret_cast<void*>(patch.command + off::command::Buttons), &patch.buttons, sizeof(patch.buttons));
        if (patch.strafe) {
            std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Forward), &patch.forward, sizeof(float));
            std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Left), &patch.left, sizeof(float));
        }
        std::memcpy(reinterpret_cast<void*>(patch.command + off::command::Presence), &patch.commandPresence, sizeof(std::uint32_t));
        std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Presence), &patch.basePresence, sizeof(std::uint32_t));
        for (int i = 0; i < patch.size; ++i) {
            // Do not rewrite vtable, arena or the protobuf's cached size.
            const auto& step = patch.steps[static_cast<std::size_t>(i)];
            std::memcpy(reinterpret_cast<void*>(step.address + 0x10), &step.value.presence, sizeof(std::uint32_t));
            std::memcpy(reinterpret_cast<void*>(step.address + 0x18), &step.value.button, sizeof(MovementSubtick) - 0x18);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool PrepareSteps(Patch& patch, const ReplayCache* saved = nullptr) noexcept {
    int capacity = 0, allocated = 0;
    std::uintptr_t rep = 0;
    if (!Read(patch.base + off::base::SubtickSize, patch.size) ||
        !Read(patch.base + off::base::SubtickCapacity, capacity) ||
        !Read(patch.base + off::base::SubtickRep, rep) || patch.size < 0 ||
        patch.size > static_cast<int>(patch.steps.size()) || capacity < patch.size || capacity > 4096) return false;
    patch.originalSize = patch.size;
    if (!Writable(patch.base + 0x18, 0x18)) return false;
    if (rep && (!Read(rep, allocated) || allocated < patch.size || allocated > capacity)) return false;
    if (!rep && (patch.size || capacity)) return false;
    if (!patch.size) return true;
    for (int i = 0; i < patch.size; ++i) {
        auto& step = patch.steps[static_cast<std::size_t>(i)];
        if (!Read(rep + 8 + static_cast<std::uintptr_t>(i) * 8, step.address) || !step.address ||
            !Read(step.address, step.value) || !Writable(step.address + 0x10, sizeof(MovementSubtick) - 0x10) ||
            (step.value.presence & ~0x7fu) || !std::isfinite(step.value.when) || step.value.when < 0 || step.value.when > 1)
            return false;
        bool owned = false;
        if (saved) {
            for (unsigned event = 0; event < saved->events.size(); ++event) {
                if (patch.ownedIndices[event] < 0 && MovementOwnsReplayStep(saved->events[event], step.address, step.value)) {
                    patch.ownedIndices[event] = i; owned = true; break;
                }
            }
        }
        if (!owned) MovementSanitizeSubtick(step.value, patch.jump, patch.strafe);
    }
    return true;
}
std::uintptr_t BorrowSubtick(std::uintptr_t base) noexcept {
    // Mirrors the producer's B65677..B656AC append path, including reuse of
    // inactive allocated messages. Allocation belongs to the engine's arena.
    __try {
        auto* repeated = reinterpret_cast<std::byte*>(base + 0x18);
        auto& size = *reinterpret_cast<int*>(repeated + 8);
        auto* rep = *reinterpret_cast<std::byte**>(repeated + 0x10);
        if (rep && size < *reinterpret_cast<int*>(rep)) {
            auto* step = *reinterpret_cast<void**>(rep + 8 + static_cast<std::size_t>(size) * 8);
            ++size;
            return reinterpret_cast<std::uintptr_t>(step);
        }
        void* arena = *reinterpret_cast<void**>(repeated);
        void* step = reinterpret_cast<void* (*)(void*)>(client + off::AllocateSubtick)(arena);
        if (!step) return 0;
        reinterpret_cast<void* (*)(void*, void*)>(client + off::AddAllocatedSubtick)(repeated, step);
        return reinterpret_cast<std::uintptr_t>(step);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
bool AppendStep(Patch& patch, const MovementSubtick& value) noexcept {
    if (patch.size >= static_cast<int>(patch.steps.size())) return false;
    auto& edit = patch.steps[static_cast<std::size_t>(patch.size)];
    edit.address = BorrowSubtick(patch.base);
    if (!edit.address || !Read(edit.address, edit.value) ||
        !Writable(edit.address + 0x10, sizeof(MovementSubtick) - 0x10)) return false;
    const auto vtable = edit.value.vtable, arena = edit.value.arena;
    const auto cachedSize = edit.value.cachedSize;
    edit.value = value;
    edit.value.vtable = vtable; edit.value.arena = arena; edit.value.cachedSize = cachedSize;
    ++patch.size;
    return true;
}
bool FinishStepOrder(const Patch& patch) noexcept {
    if (patch.size == patch.originalSize && patch.ownedIndices[0] < 0 && patch.ownedIndices[1] < 0) return true;
    std::uintptr_t rep = 0;
    std::array<std::uintptr_t, 128> pointers{}, ordered{};
    if (!Read(patch.base + off::base::SubtickRep, rep) || !rep ||
        !Copy(rep + 8, pointers.data(), static_cast<std::size_t>(patch.size) * 8) ||
        !Writable(rep + 8, static_cast<std::size_t>(patch.size) * 8)) return false;
    // Place generated time-zero events first; preserve the original event order
    // and keep every owned pointer in the container exactly once.
    int written = 0;
    for (int index : patch.ownedIndices) {
        if (index >= 0) ordered[static_cast<std::size_t>(written++)] = pointers[static_cast<std::size_t>(index)];
    }
    for (int i = 0; i < patch.size; ++i) {
        if (i != patch.ownedIndices[0] && i != patch.ownedIndices[1])
            ordered[static_cast<std::size_t>(written++)] = pointers[static_cast<std::size_t>(i)];
    }
    __try {
        std::memcpy(reinterpret_cast<void*>(rep + 8), ordered.data(), static_cast<std::size_t>(patch.size) * 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void RollbackSteps(const Patch& patch) noexcept {
    // Reused entries rely on cleared payloads. Restore pointer ordering and
    // clear generated events before making them inactive in the owned pool.
    __try {
        for (int i = patch.originalSize; i < patch.size; ++i) {
            const auto address = patch.steps[static_cast<std::size_t>(i)].address;
            if (address) {
                *reinterpret_cast<std::uint32_t*>(address + 0x10) = 0;
                std::memset(reinterpret_cast<void*>(address + 0x18), 0, sizeof(MovementSubtick) - 0x18);
            }
        }
        if (patch.orderCaptured) {
            const auto rep = *reinterpret_cast<std::uintptr_t*>(patch.base + off::base::SubtickRep);
            std::memcpy(reinterpret_cast<void*>(rep + 8), patch.originalOrder.data(), static_cast<std::size_t>(patch.size) * 8);
        }
        *reinterpret_cast<int*>(patch.base + off::base::SubtickSize) = patch.originalSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
bool CaptureStepOrder(Patch& patch) noexcept {
    if (patch.size == patch.originalSize && patch.ownedIndices[0] < 0 && patch.ownedIndices[1] < 0) return true;
    std::uintptr_t rep = 0;
    if (!Read(patch.base + off::base::SubtickRep, rep) || !rep ||
        !Copy(rep + 8, patch.originalOrder.data(), static_cast<std::size_t>(patch.size) * 8) ||
        !Writable(rep + 8, static_cast<std::size_t>(patch.size) * 8)) return false;
    patch.orderCaptured = true;
    return true;
}

MovementReplayContext ReplayContext(const Pawn& pawn, const Patch& patch, int sequence, int tick,
                                   const MovementSettings& settings) noexcept {
    MovementReplayContext context{};
    context.valid = true; context.held = true;
    context.bunnyhop = settings.bunnyhop; context.autostrafe = settings.autostrafe;
    context.sequence = sequence; context.commandTick = tick;
    context.grounded = (pawn.flags & off::OnGround) != 0;
    context.owner = owner; context.simulationTick = pawn.simulationTick;
    context.command = patch.command; context.base = patch.base;
    context.pawn = pawn.address; context.pawnIdentity = pawn.identity; context.services = pawn.services; context.system = pawn.system;
    context.controller = pawn.controller; context.controllerIdentity = pawn.controllerIdentity;
    context.pawnHandle = pawn.handle; context.controllerHandle = pawn.controllerHandle;
    return context;
}
bool PutOwnedEvent(Patch& patch, unsigned event, const MovementSubtick& value) noexcept {
    const int index = patch.ownedIndices[event];
    if (index < 0) {
        const int added = patch.size;
        if (!AppendStep(patch, value)) return false;
        patch.ownedIndices[event] = added;
    } else {
        // Exact ownership was established before sanitizing physical events.
        // Fresh historical axes can change the analog delta during a rebuild.
        auto& old = patch.steps[static_cast<std::size_t>(index)].value;
        const auto vtable = old.vtable, arena = old.arena;
        const auto cachedSize = old.cachedSize;
        old = value; old.vtable = vtable; old.arena = arena; old.cachedSize = cachedSize;
    }
    return true;
}
void RetireOwnedEvent(Patch& patch, unsigned event) noexcept {
    if (patch.ownedIndices[event] >= 0) {
        MovementSubtick neutral{}; neutral.presence = 4; // Retain only time zero.
        PutOwnedEvent(patch, event, neutral);
    }
}
bool ReadHistory(const Pawn& pawn, const Patch& patch, float& forward, float& left) noexcept {
    MovementButtons buttons{};
    if (!Read(pawn.services + off::services::Buttons, buttons) ||
        !Read(pawn.services + off::services::CommandForward, forward) ||
        !Read(pawn.services + off::services::CommandLeft, left) ||
        !std::isfinite(forward) || !std::isfinite(left) ||
        !Writable(patch.base + off::base::SubtickSize, sizeof(int))) return false;
    if (!(buttons.down & (off::Forward | off::Back))) forward = 0;
    if (!(buttons.down & (off::Left | off::Right))) left = 0;
    return true;
}
void RememberReplay(const Patch& patch, const MovementReplayContext& context,
                    const MovementReplayPlan& plan, bool repeated = false) noexcept {
    replay.context = context; replay.plan = plan;
    for (unsigned event = 0; event < replay.events.size(); ++event) {
        const int index = patch.ownedIndices[event];
        if (index < 0) replay.events[event] = {};
        else {
            const auto& step = patch.steps[static_cast<std::size_t>(index)];
            replay.events[event] = {step.address, step.value};
        }
    }
    auto& request = requests[static_cast<std::size_t>(context.sequence) % requests.size()];
    const bool reported = repeated && request.sequence == context.sequence && request.edgeReported;
    request = {context.sequence, patch.forward, patch.left, patch.buttons, MovementReplayMask(plan),
               plan.strafe, plan.jumpEdge, reported};
}
void RepairReplay(const Pawn& pawn, Patch& patch, const MovementReplayContext& context) noexcept {
    const auto plan = replay.plan; // No jump/steering planner state advances.
    const auto incoming = patch.buttons;
    const auto mask = MovementReplayMask(plan);
    patch.jump = plan.jump; patch.strafe = plan.strafe;
    patch.buttons = MovementReplayButtons(patch.buttons, plan);
    if (patch.strafe) {
        patch.forward = plan.forward; patch.left = plan.left;
        patch.basePresence |= off::base::ForwardPresent | off::base::LeftPresent;
    }
    patch.commandPresence |= 1;
    if (!PrepareSteps(patch, &replay) || !SamePawn(pawn)) {
        ++rejected; State("rebuild source or subtick layout unavailable"); return;
    }
    MarkStage(Stage::Subticks);
    float historicalForward = 0, historicalLeft = 0;
    if (!ReadHistory(pawn, patch, historicalForward, historicalLeft)) {
        ++rejected; State("rebuild historical input unavailable"); return;
    }
    MarkStage(Stage::History);
    const unsigned retained = static_cast<unsigned>(patch.ownedIndices[0] >= 0) +
                              static_cast<unsigned>(patch.ownedIndices[1] >= 0);
    if ((patch.strafe && !PutOwnedEvent(patch, 0,
            MovementAnalogTransition(plan.forward, plan.left, historicalForward, historicalLeft))) ||
        (plan.jumpEdge && !PutOwnedEvent(patch, 1, MovementJumpTransition(plan.jumpDown)))) {
        RollbackSteps(patch); ++rejected; State("could not restore owned rebuild transition"); return;
    }
    if (!SamePawn(pawn)) { RollbackSteps(patch); ++rejected; State("rebuild pawn changed during preparation"); return; }
    MarkStage(Stage::Append);
    if (!CaptureStepOrder(patch) || !FinishStepOrder(patch)) {
        RollbackSteps(patch); ++rejected; State("rebuild subtick ordering unavailable"); return;
    }
    if (!Apply(patch)) {
        RollbackSteps(patch); ++rejected; enabled.store(false, std::memory_order_release);
        State("rebuild command write failed; movement stopped"); return;
    }
    MarkStage(Stage::Apply);
    const bool repaired = (((incoming.down ^ patch.buttons.down) |
        (incoming.changed ^ patch.buttons.changed) | (incoming.third ^ patch.buttons.third)) & mask) != 0;
    const unsigned restored = static_cast<unsigned>(patch.size - patch.originalSize);
    ++replayed; replayNativeRepairs += repaired; replayRestoredEvents += restored;
    RememberReplay(patch, context, plan, true);
    const auto now = GetTickCount64();
    if (!lastReplayReport || now - lastReplayReport >= 2000) {
        lastReplayReport = now;
        Log("MOVE_REPLAY sequence=%d simtick=%u native_rebuilt=%u jump_edge=%u jump_down=%u retained=%u restored=%u subticks_before=%d subticks_after=%d incoming=0x%llx/0x%llx/0x%llx repaired=0x%llx/0x%llx/0x%llx history=%.5f,%.5f desired=%.5f,%.5f total=%llu native_repairs=%llu events_restored=%llu; cached plan reused without advancing planner",
            context.sequence, context.simulationTick, repaired, plan.jumpEdge, plan.jumpDown, retained, restored,
            patch.originalSize, patch.size, incoming.down, incoming.changed, incoming.third,
            patch.buttons.down, patch.buttons.changed, patch.buttons.third, historicalForward, historicalLeft,
            patch.forward, patch.left, replayed, replayNativeRepairs, replayRestoredEvents);
    }
}

void Process(void* input, void* commandPointer) noexcept {
    ++callbacks;
    QueryPerformanceCounter(&stageStart);
    airborneWork = false;
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) != client ||
        reinterpret_cast<std::uintptr_t>(input) != client + off::Input) {
        enabled.store(false, std::memory_order_release); Reset(); State("client/input identity changed; movement stopped"); return;
    }
    const DWORD thread = GetCurrentThreadId();
    if (owner != thread) {
        Log("MOVE_OWNER old=%lu current=%lu; discard command state", owner, thread);
        owner = thread; Reset();
    }
    DWORD foregroundPid = 0;
    const HWND window = GetForegroundWindow();
    GetWindowThreadProcessId(window, &foregroundPid);
    if (!window || foregroundPid != GetCurrentProcessId() || IsIconic(window)) {
        Reset(); State("game unfocused"); return;
    }
    // End disarms input even when rendering has stalled. Present completes its
    // own resource cleanup; mapped hooks continue forwarding original calls.
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        enabled.store(false, std::memory_order_release); Reset(); State("movement stopped by End"); return;
    }
    const auto settings = MovementGetSettings();
    if (haveInputSettings && (inputSettings.bunnyhop != settings.bunnyhop || inputSettings.autostrafe != settings.autostrafe)) {
        replay = {}; ResetJump(jumpState); ResetAirSteering(steeringState);
        strafeControlled = false; haveGround = false;
    }
    inputSettings = settings; haveInputSettings = true;
    const bool held = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (!held || (!settings.bunnyhop && !settings.autostrafe)) {
        ResetJump(jumpState); ResetAirSteering(steeringState); haveGround = false;
        strafeControlled = false; replay = {};
        State(held ? "movement disabled in settings" : "ready; hold Space"); return;
    }
    MarkStage(Stage::Controls);
    Pawn pawn{};
    if (!ReadPawn(pawn)) { Reset(); State("awaiting live walking pawn"); return; }
    if (pawn.address != sourcePawn || pawn.services != sourceServices || pawn.handle != sourceHandle ||
        pawn.controller != sourceController || pawn.controllerHandle != sourceControllerHandle) {
        Reset(); sourcePawn = pawn.address; sourceServices = pawn.services; sourceHandle = pawn.handle; sourceController = pawn.controller;
        sourceControllerHandle = pawn.controllerHandle;
        Log("MOVE_SOURCE pawn=%p services=%p handle=0x%x", reinterpret_cast<void*>(sourcePawn), reinterpret_cast<void*>(sourceServices), sourceHandle);
    }
    airborneWork = (pawn.flags & off::OnGround) == 0;
    MarkStage(Stage::Pawn);
    Patch patch{}; patch.command = reinterpret_cast<std::uintptr_t>(commandPointer);
    int sequence = 0, tick = 0;
    std::uintptr_t angles = 0;
    float yaw = 0;
    if (!Read(patch.command + off::command::Base, patch.base) || !patch.base ||
        !Read(patch.command + off::command::Sequence, sequence) || sequence <= 0 ||
        !Read(patch.base + off::base::Tick, tick) || tick < 0 ||
        !Read(patch.command + off::command::Presence, patch.commandPresence) ||
        !Read(patch.base + off::base::Presence, patch.basePresence) ||
        !Read(patch.command + off::command::Buttons, patch.buttons) ||
        !Read(patch.base + off::base::Forward, patch.forward) ||
        !Read(patch.base + off::base::Left, patch.left) ||
        !Writable(patch.command + off::command::Buttons, sizeof(patch.buttons)) ||
        !Writable(patch.command + off::command::Presence, sizeof(patch.commandPresence)) ||
        !Writable(patch.base + off::base::Presence, sizeof(patch.basePresence)) ||
        !Writable(patch.base + off::base::Forward, 2 * sizeof(float))) {
        ++rejected; State("command layout unavailable"); return;
    }
    const float desiredForward = patch.forward, desiredLeft = patch.left;
    if (sequence < lastSequence || tick < lastTick) {
        replay = {}; ResetJump(jumpState); ResetAirSteering(steeringState); haveGround = false; strafeControlled = false;
    }
    const bool grounded = (pawn.flags & off::OnGround) != 0;
    MarkStage(Stage::Layout);
    const auto context = ReplayContext(pawn, patch, sequence, tick, settings);
    if (MovementCanReplay(replay.context, context)) { RepairReplay(pawn, patch, context); return; }
    const ReplayCache rebuilt = replay;
    const bool sameRebuiltSource = MovementReplaySameSource(rebuilt.context, context) &&
        rebuilt.context.sequence == sequence && context.simulationTick >= rebuilt.context.simulationTick;
    if (replay.context.valid && replay.context.sequence == sequence && !sameRebuiltSource) {
        // A pooled command/base or a source identity changed under the same
        // sequence, or prediction rolled back. Do not reuse the old plan.
        replay = {}; ResetJump(jumpState); ResetAirSteering(steeringState);
        haveGround = false; strafeControlled = false;
    }
    // Ground/tick progression can require another plan for this sequence, but
    // must keep the pending/releasing phase and recognize still-owned events.
    replay = {};
    const float speed = std::hypot(pawn.velocity[0], pawn.velocity[1]);
    if (haveGround && grounded != wasGrounded) {
        Log("MOVE_%s sequence=%d tick=%d speed=%.3f previous_speed=%.3f vz=%.3f", grounded ? "LAND" : "TAKEOFF",
            sequence, tick, speed, lastSpeed, pawn.velocity[2]);
    }
    haveGround = true; wasGrounded = grounded; lastSpeed = speed;
    float interval = 0, penalty = 0;
    JumpState nextJumpState = jumpState;
    JumpOutput jump{};
    JumpInput jumpInput{};
    std::uint8_t autoBhop = 0, legacy = 0;
    MovementButtons engineButtons{};
    const char* jumpReadState = "disabled in settings";
    const bool jumpRulesValid = settings.bunnyhop && ReadJumpRules(pawn, jumpInput, interval, penalty, autoBhop, legacy, engineButtons, jumpReadState);
    if (!lastJumpReadState || strcmp(lastJumpReadState, jumpReadState) != 0) {
        lastJumpReadState = jumpReadState;
        Log("MOVE_JUMP_RULES valid=%u state=%s legacy_wrapper=0x%llx legacy=%u auto=%u penalty=%.6f window=%.6f interval=%.6f",
            jumpRulesValid, jumpReadState, static_cast<unsigned long long>(off::LegacyJump), legacy, autoBhop, penalty, jumpInput.bufferWindow, interval);
    }
    if (jumpRulesValid) {
        jumpInput.grounded = grounded; jumpInput.engineDown = (engineButtons.down & off::Jump) != 0;
        jumpInput.modern = legacy == 0; jumpInput.autoBunnyhop = autoBhop != 0;
        jumpInput.commandTick = tick; jumpInput.interval = interval; jumpInput.penalty = penalty;
        jumpInput.simulationTick = pawn.simulationTick;
        jump = PlanJump(nextJumpState, jumpInput);
        if (jump.valid) {
            patch.jump = true;
            MovementEncodeJump(patch.buttons, jump.down, jumpState.down);
        }
    }
    SteeringOutput strafe{};
    MarkStage(Stage::Jump);
    SteeringState nextSteeringState = steeringState;
    AirPhysics physics{};
    physics.surfaceFriction = pawn.friction;
    const bool airSpeedValid = settings.autostrafe && !grounded && AirSpeed(pawn, physics.wishSpeed);
    MarkStage(Stage::SpeedModifiers);
    if (airSpeedValid &&
        Read(patch.base + off::base::Angles, angles) && angles && Read(angles + 0x1C, yaw) &&
        Read(client + off::TickIntervalConstant, physics.interval) &&
        Cvar(off::AirCap, physics.wishSpeedCap) && Cvar(off::AirAccelerate, physics.acceleration)) {
        const float physicalForward = static_cast<float>((GetAsyncKeyState('W') & 0x8000) != 0) -
                                      static_cast<float>((GetAsyncKeyState('S') & 0x8000) != 0);
        const float physicalLeft = static_cast<float>((GetAsyncKeyState('A') & 0x8000) != 0) -
                                   static_cast<float>((GetAsyncKeyState('D') & 0x8000) != 0);
        MarkStage(Stage::Keys);
        strafe = PlanAirSteering(nextSteeringState, {pawn.velocity[0], pawn.velocity[1], yaw,
                                  physicalForward, physicalLeft, static_cast<std::uint32_t>(sequence)}, physics);
        MarkStage(Stage::Solver);
        if (strafe.valid) {
            patch.strafe = true; patch.forward = strafe.forward; patch.left = strafe.left;
            patch.basePresence |= off::base::ForwardPresent | off::base::LeftPresent;
            const auto prior = strafeControlled ? previousDirections : (patch.buttons.down ^ patch.buttons.changed);
            MovementEncodeStrafe(patch.buttons, patch.forward, patch.left, prior);
        }
    }
    if (!patch.jump && !patch.strafe) { State("movement physics unavailable"); return; }
    patch.commandPresence |= 1;
    if (!PrepareSteps(patch, sameRebuiltSource ? &rebuilt : nullptr) || !SamePawn(pawn)) { ++rejected; State("stale pawn or unsupported subtick layout"); return; }
    MarkStage(Stage::Subticks);
    float historicalForward = 0, historicalLeft = 0;
    if (!ReadHistory(pawn, patch, historicalForward, historicalLeft)) {
        ++rejected; State("historical command input unavailable"); return;
    }
    MarkStage(Stage::History);
    if (patch.strafe) {
        const auto transition = MovementAnalogTransition(patch.forward, patch.left, historicalForward, historicalLeft);
        if (!PutOwnedEvent(patch, 0, transition)) {
            RollbackSteps(patch); ++rejected; State("could not append owned analog transition"); return;
        }
    } else RetireOwnedEvent(patch, 0);
    if (patch.jump && (jump.fresh || jump.released)) {
        const auto transition = MovementJumpTransition(jump.down);
        if (!PutOwnedEvent(patch, 1, transition)) {
            RollbackSteps(patch); ++rejected; State("could not append owned jump transition"); return;
        }
    } else RetireOwnedEvent(patch, 1);
    if (!SamePawn(pawn)) { RollbackSteps(patch); ++rejected; State("pawn changed during command preparation"); return; }
    MarkStage(Stage::Append);
    if (!CaptureStepOrder(patch) || !FinishStepOrder(patch)) {
        RollbackSteps(patch); ++rejected; State("subtick ordering unavailable"); return;
    }
    if (!Apply(patch)) {
        RollbackSteps(patch);
        ++rejected; enabled.store(false, std::memory_order_release); State("command write failed; movement stopped"); return;
    }
    MarkStage(Stage::Apply);
    lastSequence = sequence; lastTick = tick;
    const MovementReplayPlan plan{patch.jump, patch.strafe, patch.jump && (jump.fresh || jump.released),
                                  jump.down, patch.forward, patch.left, patch.buttons};
    RememberReplay(patch, context, plan);
    strafeControlled = patch.strafe;
    previousDirections = patch.buttons.down & off::Directions;
    if (patch.jump) jumpState = nextJumpState;
    else ResetJump(jumpState);
    if (patch.strafe) steeringState = nextSteeringState;
    else ResetAirSteering(steeringState);
    if (jump.fresh) ++presses;
    if (jump.fresh || jump.released) {
        Log("MOVE_JUMP sequence=%d tick=%d simtick=%u ground=%u down=%u fresh=%u release=%u ack=%u phase=%u actual=%d/%.6f usable=%d/%.6f landed=%d/%.6f engine_down=%u penalty=%.6f window=%.6f legacy=%u auto=%u",
            sequence, tick, pawn.simulationTick, grounded, jump.down, jump.fresh, jump.released, jump.acknowledged, static_cast<unsigned>(jump.phase),
            jumpInput.actual.tick, jumpInput.actual.fraction, jumpInput.usable.tick, jumpInput.usable.fraction,
            jumpInput.landed.tick, jumpInput.landed.fraction, jumpInput.engineDown, penalty, jumpInput.bufferWindow, legacy, autoBhop);
    }
    ++modified; strafes += patch.strafe;
    State(patch.strafe ? "air strafing" : grounded ? "ground jump timing" : "airborne jump released");
    const auto now = GetTickCount64();
    if (!lastReport || now - lastReport >= 2000) {
        lastReport = now;
        std::uint8_t precision = 0;
        const bool rules = Cvar(off::AutoBunnyhop, autoBhop) && Cvar(off::JumpPrecision, precision);
        Log("MOVE_CMD sequence=%d tick=%d ground=%u speed=%.3f velocity=%.3f,%.3f,%.3f jump_down=%u fresh_press=%u strafe=%u analog=%.5f,%.5f intent=%.5f,%.5f yaw=%.3f wish_yaw=%.3f gain=%.4f buttons=0x%llx/0x%llx/0x%llx subticks=%d callbacks=%llu modified=%llu presses=%llu strafes=%llu rejected=%llu cap=%.4f accel=%.4f maxspeed=%.4f friction=%.4f interval=%.6f penalty=%.6f server_autobhop=%u precision=%u rules_valid=%u start=%.5f,%.5f steering=%u error=%.3f turn=%.3f jump_rules_valid=%u jump_plan_valid=%u",
            sequence, tick, grounded, speed, pawn.velocity[0], pawn.velocity[1], pawn.velocity[2], jump.down, jump.fresh,
            patch.strafe, patch.forward, patch.left, desiredForward, desiredLeft, yaw, strafe.wishYaw, strafe.accelerationStep,
            patch.buttons.down, patch.buttons.changed, patch.buttons.third, patch.size, callbacks, modified, presses, strafes, rejected,
            physics.wishSpeedCap, physics.acceleration, physics.wishSpeed, physics.surfaceFriction, physics.interval,
            penalty, autoBhop, precision, rules, historicalForward, historicalLeft, static_cast<unsigned>(strafe.mode),
            strafe.headingError, strafe.headingChange, jumpRulesValid, jump.valid);
    }
}
void HookFinalize(void* input, void* command) noexcept {
    if (enabled.load(std::memory_order_acquire)) {
        LARGE_INTEGER begin{}, end{};
        QueryPerformanceCounter(&begin);
        AcquireSRWLockExclusive(&commandLock);
        LARGE_INTEGER acquired{}; QueryPerformanceCounter(&acquired);
        const double lockUs = 1000000.0 * (acquired.QuadPart - begin.QuadPart) / timingFrequency.QuadPart;
        totalLockUs += lockUs; maximumLockUs = std::max(maximumLockUs, lockUs);
        airborneWork = false;
        if (enabled.load(std::memory_order_acquire)) Process(input, command);
        if (enabled.load(std::memory_order_acquire)) MarkStage(Stage::Diagnostics);
        QueryPerformanceCounter(&end);
        const double us = 1000000.0 * (end.QuadPart - begin.QuadPart) / timingFrequency.QuadPart;
        ++timedCalls; totalUs += us; maximumUs = std::max(maximumUs, us);
        if (airborneWork) { ++timedAirCalls; totalAirUs += us; maximumAirUs = std::max(maximumAirUs, us); }
        const auto now = GetTickCount64();
        if (!lastTimingReport || now - lastTimingReport >= 2000) {
            lastTimingReport = now;
            Log("MOVE_PERF callbacks=%llu mean_us=%.3f max_us=%.3f air_callbacks=%llu air_mean_us=%.3f air_max_us=%.3f dropped_logs=%llu lock_mean_us=%.3f lock_max_us=%.3f; command callback performs no file I/O",
                timedCalls, totalUs / timedCalls, maximumUs, timedAirCalls, timedAirCalls ? totalAirUs / timedAirCalls : 0,
                maximumAirUs, MovementTelemetryDropped(), totalLockUs / timedCalls, maximumLockUs);
            constexpr const char* stageNames[]{"controls", "pawn", "layout", "jump", "engine_speed", "speed_modifiers", "physical_keys", "solver", "subticks", "history", "append", "apply", "diagnostics"};
            static_assert(std::size(stageNames) == static_cast<unsigned>(Stage::Count));
            for (unsigned i = 0; i < stageSamples.size(); ++i) {
                const auto& sample = stageSamples[i];
                if (sample.count) Log("MOVE_STAGE name=%s calls=%llu mean_us=%.3f max_us=%.3f", stageNames[i], sample.count, sample.total / sample.count, sample.maximum);
            }
            stageSamples = {};
            timedCalls = timedAirCalls = 0; totalUs = maximumUs = totalAirUs = maximumAirUs = 0;
            totalLockUs = maximumLockUs = 0;
        }
        ReleaseSRWLockExclusive(&commandLock);
    }
    // Original rebuilds move_crc using the modified native buttons, then normal
    // command production copies those buttons to the network protobuf itself.
    original(input, command);
}
void HookIngest(void* services, void* command) noexcept {
    MovementButtons incoming{}, before{};
    const bool observedBefore = enabled.load(std::memory_order_acquire) &&
        Read(reinterpret_cast<std::uintptr_t>(command) + off::command::Buttons, incoming) &&
        Read(reinterpret_cast<std::uintptr_t>(services) + off::services::Buttons, before);
    originalIngest(services, command);
    if (!enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&commandLock);
    const auto address = reinterpret_cast<std::uintptr_t>(services);
    if (enabled.load(std::memory_order_acquire) && address == sourceServices) {
        int sequence = 0;
        float forward = 0, left = 0;
        MovementButtons buttons{};
        if (Read(reinterpret_cast<std::uintptr_t>(command) + off::command::Sequence, sequence) && sequence > 0 &&
            Read(address + off::services::CommandForward, forward) && Read(address + off::services::CommandLeft, left) &&
            Read(address + off::services::Buttons, buttons)) {
            auto& request = requests[static_cast<std::size_t>(sequence) % requests.size()];
            if (request.sequence == sequence) {
                const bool inputMismatch = observedBefore &&
                    ((((incoming.down ^ request.buttons.down) | (incoming.changed ^ request.buttons.changed) |
                       (incoming.third ^ request.buttons.third)) & request.mask) != 0);
                const bool mismatch = inputMismatch || (request.strafe &&
                    (std::abs(forward - request.forward) > 0.0001f || std::abs(left - request.left) > 0.0001f)) ||
                    ((buttons.down ^ request.buttons.down) & request.mask) != 0;
                const bool edge = request.jumpEdge && !request.edgeReported;
                const auto now = GetTickCount64();
                if (mismatch || edge || !lastConsumeReport || now - lastConsumeReport >= 2000) {
                    lastConsumeReport = now;
                    request.edgeReported = request.edgeReported || edge;
                    Log("MOVE_CONSUMED sequence=%d mismatch=%u input_mismatch=%u jump_edge=%u before_valid=%u requested=%.5f,%.5f consumed=%.5f,%.5f requested_buttons=0x%llx/0x%llx/0x%llx incoming_buttons=0x%llx/0x%llx/0x%llx before_buttons=0x%llx/0x%llx/0x%llx consumed_buttons=0x%llx/0x%llx/0x%llx; ingestion observed, physics integration remains separate",
                        sequence, mismatch, inputMismatch, edge, observedBefore, request.forward, request.left, forward, left,
                        request.buttons.down, request.buttons.changed, request.buttons.third,
                        incoming.down, incoming.changed, incoming.third, before.down, before.changed, before.third,
                        buttons.down, buttons.changed, buttons.third);
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&commandLock);
}
}

bool MovementPrepare(MovementLogCallback callback) noexcept {
    logger = callback;
    MovementTelemetrySetCallback(callback);
    QueryPerformanceFrequency(&timingFrequency);
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, sizeof(off::FinalizeBytes)> bytes{};
    std::array<unsigned char, sizeof(off::MaxSpeedBytes)> maxSpeedBytes{};
    std::array<unsigned char, sizeof(off::IngestBytes)> ingestBytes{};
    std::array<unsigned char, sizeof(off::AllocateBytes)> allocateBytes{};
    std::array<unsigned char, sizeof(off::AddAllocatedBytes)> appendBytes{};
    std::uintptr_t vtable = 0, method = 0;
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != entity::ClientTimestamp || nt.OptionalHeader.SizeOfImage != entity::ClientImageSize ||
        !Copy(client + off::Finalize, bytes.data(), bytes.size()) || memcmp(bytes.data(), off::FinalizeBytes, bytes.size()) ||
        !Copy(client + off::EffectiveMaxSpeed, maxSpeedBytes.data(), maxSpeedBytes.size()) ||
        memcmp(maxSpeedBytes.data(), off::MaxSpeedBytes, maxSpeedBytes.size()) ||
        !Copy(client + off::Ingest, ingestBytes.data(), ingestBytes.size()) || memcmp(ingestBytes.data(), off::IngestBytes, ingestBytes.size()) ||
        !Copy(client + off::AllocateSubtick, allocateBytes.data(), allocateBytes.size()) || memcmp(allocateBytes.data(), off::AllocateBytes, allocateBytes.size()) ||
        !Copy(client + off::AddAllocatedSubtick, appendBytes.data(), appendBytes.size()) || memcmp(appendBytes.data(), off::AddAllocatedBytes, appendBytes.size()) ||
        !Read(client + off::Input, vtable) || vtable != client + off::InputVtable ||
        !Read(vtable + 6 * sizeof(std::uintptr_t), method) || method != client + off::Finalize) {
        State("installed input build verification failed"); return false;
    }
    const auto result = MH_CreateHook(reinterpret_cast<void*>(method), reinterpret_cast<void*>(&HookFinalize), reinterpret_cast<void**>(&original));
    if (result != MH_OK) { Log("MOVE_HOOK_FAILED create=%s", MH_StatusToString(result)); State("command hook creation failed"); return false; }
    const auto ingestion = MH_CreateHook(reinterpret_cast<void*>(client + off::Ingest), reinterpret_cast<void*>(&HookIngest), reinterpret_cast<void**>(&originalIngest));
    if (ingestion != MH_OK) { Log("MOVE_HOOK_FAILED ingestion=%s", MH_StatusToString(ingestion)); State("ingestion hook creation failed"); return false; }
    const auto ingestQueue = MH_QueueEnableHook(reinterpret_cast<void*>(client + off::Ingest));
    if (ingestQueue != MH_OK) { State("ingestion hook queue failed"); return false; }
    const auto queue = MH_QueueEnableHook(reinterpret_cast<void*>(method));
    if (queue != MH_OK) { Log("MOVE_HOOK_FAILED queue=%s", MH_StatusToString(queue)); State("command hook queue failed"); return false; }
    Log("MOVE_BUILD_VERIFIED client=%p input=%p vtable=%p finalize=%p slot=6 timestamp=0x%lx image_size=0x%lx; match values read at command time",
        reinterpret_cast<void*>(client), reinterpret_cast<void*>(client + off::Input), reinterpret_cast<void*>(vtable),
        reinterpret_cast<void*>(method), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
    Log("MOVE_IMPLEMENTATION revision=5 legacy_wrapper=0x2529200 reads=guarded_copy writes=owned_guarded_probe diagnostics=queued steering=wasd_gain stages=enabled pending_jump=simulation_clock subticks=time_zero");
    State("awaiting command hook activation"); return true;
}
void MovementActivate() noexcept {
    AcquireSRWLockExclusive(&commandLock);
    if (!stopEverRequested) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&commandLock);
}
void MovementStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&commandLock);
    stopEverRequested = true;
    enabled.store(false, std::memory_order_release);
    Reset(); State("stopped and resident");
    Log("MOVE_STOPPED_RESIDENT reason=%s callbacks=%llu modified=%llu presses=%llu strafes=%llu rejected=%llu; command mutations drained, hook/trampoline retained",
        reason, callbacks, modified, presses, strafes, rejected);
    ReleaseSRWLockExclusive(&commandLock);
    MovementTelemetryDrain(128);
}
const char* MovementStatus() noexcept { return status.load(std::memory_order_acquire); }
void MovementFlushTelemetry() noexcept { MovementTelemetryDrain(); }
