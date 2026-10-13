#include "movement.h"
#include "movement_math.h"
#include "movement_jump.h"
#include "movement_steering.h"
#include "movement_command.h"
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
struct Request { int sequence = -1; float forward = 0, left = 0; MovementButtons buttons{}; };
std::array<Request, 256> requests{};
LARGE_INTEGER timingFrequency{};
std::uint64_t timedCalls = 0, timedAirCalls = 0;
double totalUs = 0, maximumUs = 0, totalAirUs = 0, maximumAirUs = 0;
ULONGLONG lastTimingReport = 0;
bool airborneWork = false;
const char* lastJumpReadState = nullptr;

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
    if (address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    const auto end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = memory.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        address = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    }
    return true;
}
template<class T> bool Cvar(std::ptrdiff_t rva, T& value) noexcept {
    return MovementReadCvar(client + rva, value);
}
bool EffectiveSpeed(std::uintptr_t pawn, float& speed) noexcept {
    // Locally verified engine helper, RCX=pawn and XMM0=float result. Invoke
    // only on the engine command owner; it includes server and weapon limits.
    __try {
        speed = reinterpret_cast<float (*)(void*)>(client + off::EffectiveMaxSpeed)(reinterpret_cast<void*>(pawn));
        return std::isfinite(speed) && speed > 0 && speed < 100000;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void Reset() noexcept {
    sourcePawn = sourceServices = sourceController = 0; sourceHandle = UINT32_MAX;
    sourceControllerHandle = UINT32_MAX;
    lastSequence = lastTick = -1;
    wasGrounded = haveGround = false; lastSpeed = 0;
    ResetJump(jumpState); ResetAirSteering(steeringState);
    lastJumpReadState = nullptr;
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
bool PrepareSteps(Patch& patch) noexcept {
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
        MovementSanitizeSubtick(step.value, patch.jump, patch.strafe);
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
    if (patch.size == patch.originalSize) return true;
    std::uintptr_t rep = 0;
    std::array<std::uintptr_t, 128> pointers{}, ordered{};
    if (!Read(patch.base + off::base::SubtickRep, rep) || !rep ||
        !Copy(rep + 8, pointers.data(), static_cast<std::size_t>(patch.size) * 8) ||
        !Writable(rep + 8, static_cast<std::size_t>(patch.size) * 8)) return false;
    // Place generated time-zero events first; preserve the original event order
    // and keep every owned pointer in the container exactly once.
    int written = 0;
    for (int i = patch.originalSize; i < patch.size; ++i) ordered[static_cast<std::size_t>(written++)] = pointers[static_cast<std::size_t>(i)];
    for (int i = 0; i < patch.originalSize; ++i) ordered[static_cast<std::size_t>(written++)] = pointers[static_cast<std::size_t>(i)];
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
    if (patch.size == patch.originalSize) return true;
    std::uintptr_t rep = 0;
    if (!Read(patch.base + off::base::SubtickRep, rep) || !rep ||
        !Copy(rep + 8, patch.originalOrder.data(), static_cast<std::size_t>(patch.size) * 8) ||
        !Writable(rep + 8, static_cast<std::size_t>(patch.size) * 8)) return false;
    patch.orderCaptured = true;
    return true;
}

void Process(void* input, void* commandPointer) noexcept {
    ++callbacks;
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
    const bool held = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (!held || (!settings.bunnyhop && !settings.autostrafe)) {
        ResetJump(jumpState); ResetAirSteering(steeringState); haveGround = false;
        strafeControlled = false;
        State(held ? "movement disabled in settings" : "ready; hold Space"); return;
    }
    // Replayed finalized commands do not need another pawn walk/physics solve.
    int candidateSequence = 0;
    if (!Read(reinterpret_cast<std::uintptr_t>(commandPointer) + off::command::Sequence, candidateSequence) || candidateSequence <= 0) return;
    if (candidateSequence == lastSequence) return;
    Pawn pawn{};
    if (!ReadPawn(pawn)) { Reset(); State("awaiting live walking pawn"); return; }
    if (pawn.address != sourcePawn || pawn.services != sourceServices || pawn.handle != sourceHandle ||
        pawn.controller != sourceController || pawn.controllerHandle != sourceControllerHandle) {
        Reset(); sourcePawn = pawn.address; sourceServices = pawn.services; sourceHandle = pawn.handle; sourceController = pawn.controller;
        sourceControllerHandle = pawn.controllerHandle;
        Log("MOVE_SOURCE pawn=%p services=%p handle=0x%x", reinterpret_cast<void*>(sourcePawn), reinterpret_cast<void*>(sourceServices), sourceHandle);
    }
    airborneWork = (pawn.flags & off::OnGround) == 0;
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
    if (sequence == lastSequence) return; // Never edit an already finalized/replayed command twice.
    const float desiredForward = patch.forward, desiredLeft = patch.left;
    if (sequence < lastSequence || tick < lastTick) {
        ResetJump(jumpState); ResetAirSteering(steeringState); haveGround = false; strafeControlled = false;
    }
    const bool grounded = (pawn.flags & off::OnGround) != 0;
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
    SteeringState nextSteeringState = steeringState;
    AirPhysics physics{};
    physics.surfaceFriction = pawn.friction;
    if (settings.autostrafe && !grounded &&
        AirSpeed(pawn, physics.wishSpeed) &&
        Read(patch.base + off::base::Angles, angles) && angles && Read(angles + 0x1C, yaw) &&
        Read(client + off::TickIntervalConstant, physics.interval) &&
        Cvar(off::AirCap, physics.wishSpeedCap) && Cvar(off::AirAccelerate, physics.acceleration)) {
        const float physicalForward = static_cast<float>((GetAsyncKeyState('W') & 0x8000) != 0) -
                                      static_cast<float>((GetAsyncKeyState('S') & 0x8000) != 0);
        const float physicalLeft = static_cast<float>((GetAsyncKeyState('A') & 0x8000) != 0) -
                                   static_cast<float>((GetAsyncKeyState('D') & 0x8000) != 0);
        strafe = PlanAirSteering(nextSteeringState, {pawn.velocity[0], pawn.velocity[1], yaw,
                                  physicalForward, physicalLeft, static_cast<std::uint32_t>(sequence)}, physics);
        if (strafe.valid) {
            patch.strafe = true; patch.forward = strafe.forward; patch.left = strafe.left;
            patch.basePresence |= off::base::ForwardPresent | off::base::LeftPresent;
            const auto prior = strafeControlled ? previousDirections : (patch.buttons.down ^ patch.buttons.changed);
            MovementEncodeStrafe(patch.buttons, patch.forward, patch.left, prior);
        }
    }
    if (!patch.jump && !patch.strafe) { State("movement physics unavailable"); return; }
    patch.commandPresence |= 1;
    if (!PrepareSteps(patch) || !SamePawn(pawn)) { ++rejected; State("stale pawn or unsupported subtick layout"); return; }
    float historicalForward = 0, historicalLeft = 0;
    MovementButtons historicalButtons{};
    if (!Read(pawn.services + off::services::Buttons, historicalButtons) ||
        !Read(pawn.services + off::services::CommandForward, historicalForward) ||
        !Read(pawn.services + off::services::CommandLeft, historicalLeft) ||
        !std::isfinite(historicalForward) || !std::isfinite(historicalLeft) ||
        !Writable(patch.base + off::base::SubtickSize, sizeof(int))) {
        ++rejected; State("historical command input unavailable"); return;
    }
    if (!(historicalButtons.down & (off::Forward | off::Back))) historicalForward = 0;
    if (!(historicalButtons.down & (off::Left | off::Right))) historicalLeft = 0;
    if (patch.strafe) {
        const auto transition = MovementAnalogTransition(patch.forward, patch.left, historicalForward, historicalLeft);
        if (!AppendStep(patch, transition)) {
            RollbackSteps(patch); ++rejected; State("could not append owned analog transition"); return;
        }
    }
    if (patch.jump && (jump.fresh || jump.released)) {
        const auto transition = MovementJumpTransition(jump.down);
        if (!AppendStep(patch, transition)) {
            RollbackSteps(patch); ++rejected; State("could not append owned jump transition"); return;
        }
    }
    if (!SamePawn(pawn)) { RollbackSteps(patch); ++rejected; State("pawn changed during command preparation"); return; }
    if (!CaptureStepOrder(patch) || !FinishStepOrder(patch)) {
        RollbackSteps(patch); ++rejected; State("subtick ordering unavailable"); return;
    }
    if (!Apply(patch)) {
        RollbackSteps(patch);
        ++rejected; enabled.store(false, std::memory_order_release); State("command write failed; movement stopped"); return;
    }
    lastSequence = sequence; lastTick = tick;
    requests[static_cast<std::size_t>(sequence) % requests.size()] = {sequence, patch.forward, patch.left, patch.buttons};
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
        if (enabled.load(std::memory_order_acquire)) Process(input, command);
        QueryPerformanceCounter(&end);
        const double us = 1000000.0 * (end.QuadPart - begin.QuadPart) / timingFrequency.QuadPart;
        ++timedCalls; totalUs += us; maximumUs = std::max(maximumUs, us);
        if (airborneWork) { ++timedAirCalls; totalAirUs += us; maximumAirUs = std::max(maximumAirUs, us); }
        const auto now = GetTickCount64();
        if (!lastTimingReport || now - lastTimingReport >= 2000) {
            lastTimingReport = now;
            Log("MOVE_PERF callbacks=%llu mean_us=%.3f max_us=%.3f air_callbacks=%llu air_mean_us=%.3f air_max_us=%.3f dropped_logs=%llu; command callback performs no file I/O",
                timedCalls, totalUs / timedCalls, maximumUs, timedAirCalls, timedAirCalls ? totalAirUs / timedAirCalls : 0,
                maximumAirUs, MovementTelemetryDropped());
            timedCalls = timedAirCalls = 0; totalUs = maximumUs = totalAirUs = maximumAirUs = 0;
        }
        ReleaseSRWLockExclusive(&commandLock);
    }
    // Original rebuilds move_crc using the modified native buttons, then normal
    // command production copies those buttons to the network protobuf itself.
    original(input, command);
}
void HookIngest(void* services, void* command) noexcept {
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
            const auto& request = requests[static_cast<std::size_t>(sequence) % requests.size()];
            if (request.sequence == sequence) {
                const bool mismatch = std::abs(forward - request.forward) > 0.0001f ||
                    std::abs(left - request.left) > 0.0001f || buttons.down != request.buttons.down;
                const auto now = GetTickCount64();
                if (!lastConsumeReport || now - lastConsumeReport >= 2000) {
                    lastConsumeReport = now;
                    Log("MOVE_CONSUMED sequence=%d mismatch=%u requested=%.5f,%.5f consumed=%.5f,%.5f requested_buttons=0x%llx consumed_buttons=0x%llx; ingestion observed, physics integration remains separate",
                        sequence, mismatch, request.forward, request.left, forward, left, request.buttons.down, buttons.down);
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
    Log("MOVE_IMPLEMENTATION revision=3 legacy_wrapper=0x2529200 reads=guarded_copy diagnostics=queued steering=analytic pending_jump=simulation_clock subticks=time_zero");
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
